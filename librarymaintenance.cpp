#include "librarymaintenance.h"
#include "tracklibrary.h"
#include "albumgrouping.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QtConcurrentRun>
#include <algorithm>

namespace {
constexpr qint64 MaximumBackupBytes = 256LL * 1024 * 1024;
QString localPath(const QUrl &url)
{
    const auto path = url.isLocalFile() ? url.toLocalFile() : url.scheme().isEmpty() && QDir::isAbsolutePath(url.path()) ? url.path() : QString();
    return path.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}
bool inside(const QString &path, const QString &folder)
{
    return path == folder || path.startsWith(folder.endsWith(QDir::separator()) ? folder : folder + QDir::separator());
}
QVariant remapValue(const QVariant &value, const QVariantMap &mapping)
{
    if (value.metaType().id() == QMetaType::QVariantMap) {
        QVariantMap result;
        const auto source = value.toMap();
        for (auto it = source.cbegin(); it != source.cend(); ++it)
            result[mapping.value(it.key(), it.key()).toString()] = remapValue(it.value(), mapping);
        return result;
    }
    if (value.metaType().id() == QMetaType::QStringList) {
        QStringList result;
        for (const auto &item : value.toStringList()) result.append(mapping.value(item, item).toString());
        return result;
    }
    if (value.metaType().id() == QMetaType::QVariantList) {
        QVariantList result;
        for (const auto &item : value.toList()) result.append(remapValue(item, mapping));
        return result;
    }
    if (value.metaType().id() == QMetaType::QString)
        return mapping.value(value.toString(), value);
    return value;
}
QVariantMap settingsValues(QSettings &settings)
{
    QVariantMap result;
    for (const auto &key : settings.allKeys()) result[key] = settings.value(key);
    return result;
}
QString hashFile(const QString &url, const std::shared_ptr<std::atomic_bool> &cancel)
{
    QFile file(QUrl(url).toLocalFile());
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        if (cancel && cancel->load()) return {};
        const auto bytes = file.read(1024 * 1024);
        if (bytes.isEmpty()) return {};
        hash.addData(bytes);
    }
    return QString::fromLatin1(hash.result().toHex());
}
QString validateQueues(const QVariantMap &state)
{
    const auto queues = state.value("queues").toList();
    if (queues.isEmpty() || queues.size() > 4) return QStringLiteral("The backup must contain one to four queues.");
    QSet<QString> ids;
    for (const auto &value : queues) {
        const auto queue = value.toMap();
        const auto id = queue.value("id").toString(), name = queue.value("name").toString().simplified();
        if (id.isEmpty() || ids.contains(id) || name.isEmpty() || name.size() > 80)
            return QStringLiteral("The backup contains an invalid queue.");
        ids.insert(id);
        for (const auto &field : {"queue", "source"}) {
            QSet<QString> entries;
            for (const auto &item : queue.value("session").toMap().value(field).toList()) {
                const auto entry = item.toMap();
                const auto entryId = entry.value("id").toString();
                const QUrl url(entry.value("fileUrl").toString());
                if (entryId.isEmpty() || entries.contains(entryId) || !url.isLocalFile() || url.toLocalFile().isEmpty())
                    return QStringLiteral("The backup contains an invalid queue entry.");
                entries.insert(entryId);
            }
        }
    }
    if (!ids.contains(state.value("active").toString())) return QStringLiteral("The backup's active queue is invalid.");
    return {};
}
QVariantMap readBackup(const QUrl &url)
{
    QFile file(url.toLocalFile());
    if (!url.isLocalFile() || !file.open(QIODevice::ReadOnly) || file.size() > MaximumBackupBytes)
        return {{"error", QStringLiteral("Choose a readable local Musica backup up to 256 MB.")}};
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject())
        return {{"error", QStringLiteral("This isn't a valid Musica backup.")}};
    const auto bundle = document.toVariant().toMap();
    if (bundle.value("format").toString() != "musica-backup" || bundle.value("version").toInt() != 1
        || !document.object().value("settings").isObject() || !document.object().value("tracks").isArray()
        || !document.object().value("queues").isObject() || !document.object().value("extra").isObject())
        return {{"error", QStringLiteral("The backup format is unsupported or incomplete.")}};
    const auto tracks = bundle.value("tracks").toList();
    if (tracks.size() > 250000 || bundle.value("settings").toMap().size() > 10000)
        return {{"error", QStringLiteral("The backup exceeds Musica's import limits.")}};
    QSet<QString> urls;
    for (const auto &item : tracks) {
        const auto track = item.toMap();
        const QUrl url(track.value("fileUrl").toString());
        if (!url.isLocalFile() || url.toLocalFile().isEmpty() || urls.contains(url.toString()) || track.value("duration").toLongLong() < 0)
            return {{"error", QStringLiteral("The backup contains an invalid or repeated song record.")}};
        urls.insert(url.toString());
    }
    const auto queueError = validateQueues(bundle.value("queues").toMap());
    if (!queueError.isEmpty()) return {{"error", queueError}};
    return bundle;
}
}

QObject *TrackListModel::maintenance()
{
    if (!m_maintenance) m_maintenance = new LibraryMaintenance(this);
    return m_maintenance;
}

LibraryMaintenance::LibraryMaintenance(TrackListModel *library)
    : QObject(library), m_library(library), m_cancel(std::make_shared<std::atomic_bool>(false))
{
    connect(&m_worker, &QFutureWatcher<QVariantMap>::finished, this, [this] {
        const auto result = m_worker.result();
        emit busyChanged();
        if (result.contains("error")) { setStatus(result.value("error").toString()); emit error(m_status); return; }
        if (m_operation == "duplicates") {
            m_duplicates = result.value("groups").toList();
            emit duplicateGroupsChanged();
            setStatus(tr("%1 duplicate groups · %2 unreadable or changed files skipped").arg(m_duplicates.size()).arg(result.value("skipped").toInt()));
            emit completed(m_status);
        } else if (m_operation == "trashDuplicates") {
            if (m_library->scanning() || m_library->openingFiles() || m_library->metadataBusy()) {
                emit error(tr("Wait for library or metadata changes to finish, then review the duplicates again.")); return;
            }
            const auto removed = m_library->trashTracks(result.value("urls").toStringList());
            const auto errors = removed.value("errors").toList();
            if (!errors.isEmpty()) emit error(errors.first().toMap().value("error").toString());
            const auto urls = removed.value("removed").toStringList();
            for (auto &value : m_duplicates) {
                auto group = value.toMap(); QVariantList entries;
                for (const auto &item : group.value("tracks").toList())
                    if (!urls.contains(item.toMap().value("fileUrl").toString())) entries.append(item);
                group["tracks"] = entries; value = group;
            }
            m_duplicates.erase(std::remove_if(m_duplicates.begin(), m_duplicates.end(), [](const QVariant &value) {
                return value.toMap().value("tracks").toList().size() < 2;
            }), m_duplicates.end());
            emit duplicateGroupsChanged(); setStatus(tr("Moved %1 duplicate files to Trash.").arg(urls.size())); emit completed(m_status);
        }
    });
}
LibraryMaintenance::~LibraryMaintenance()
{
    m_cancel->store(true);
    if (m_worker.isRunning()) m_worker.waitForFinished();
}
void LibraryMaintenance::setStatus(const QString &text)
{
    if (m_status == text) return;
    m_status = text; emit statusChanged();
}
bool LibraryMaintenance::available()
{
    if (busy() || m_library->scanning() || m_library->openingFiles() || m_library->metadataBusy()) {
        emit error(tr("Wait for the current library operation to finish.")); return false;
    }
    return true;
}

QVariantMap LibraryMaintenance::previewRelink(const QUrl &oldUrl, const QUrl &newUrl) const
{
    const auto oldFolder = localPath(oldUrl), newFolder = localPath(newUrl);
    if (oldFolder.isEmpty() || newFolder.isEmpty() || oldFolder == newFolder || !QFileInfo(newFolder).isDir())
        return {{"error", tr("Choose different old and new local folders. The new folder must exist.")}};
    const auto tracks = m_library->tracks();
    QVariantMap urls; QVariantList matches, skipped;
    QSet<QString> existing, targets;
    for (const auto &item : tracks) existing.insert(item.toMap().value("fileUrl").toString());
    for (const auto &item : tracks) {
        const auto track = item.toMap(); const auto oldFile = track.value("fileUrl").toString();
        const auto path = localPath(QUrl(oldFile));
        if (!inside(path, oldFolder) || QFileInfo(path).isFile()) continue;
        const auto relative = QDir(oldFolder).relativeFilePath(path);
        const QFileInfo candidate(QDir(newFolder).filePath(relative));
        const auto target = candidate.isFile() ? QUrl::fromLocalFile(candidate.canonicalFilePath()).toString() : QString();
        QString reason;
        if (!candidate.isFile() || !candidate.isReadable()) reason = tr("No readable file at the new path");
        else if (existing.contains(target) || targets.contains(target)) reason = tr("New file is already in the library");
        else if (track.value("size").toLongLong() > 0 && track.value("size").toLongLong() != candidate.size()) reason = tr("File size differs");
        if (!reason.isEmpty()) { skipped.append(QVariantMap{{"fileUrl", oldFile}, {"relative", relative}, {"reason", reason}}); continue; }
        urls[oldFile] = target;
        targets.insert(target);
        matches.append(QVariantMap{{"oldUrl", oldFile}, {"newUrl", target}, {"title", track.value("title")}, {"relative", relative},
            {"size", candidate.size()}, {"modified", candidate.lastModified().toMSecsSinceEpoch()}});
    }
    return {{"oldFolder", oldFolder}, {"newFolder", newFolder}, {"mapping", urls}, {"matches", matches}, {"skipped", skipped}};
}
bool LibraryMaintenance::applyRelink(const QVariantMap &preview)
{
    if (!available()) return false;
    const auto mapping = preview.value("mapping").toMap();
    if (mapping.isEmpty()) { emit error(tr("Preview the matching moved songs first.")); return false; }
    const auto fresh = previewRelink(QUrl::fromLocalFile(preview.value("oldFolder").toString()), QUrl::fromLocalFile(preview.value("newFolder").toString()));
    if (fresh.value("mapping").toMap() != mapping || fresh.value("matches") != preview.value("matches")) {
        emit error(tr("The library or files changed after the preview. Preview the repair again.")); return false;
    }
    m_library->saveSettings();
    const auto previous = snapshot({});
    m_library->ensureTracksLoaded();
    m_library->m_releaseTimer.stop();
    m_library->m_tracks = remapValue(m_library->m_tracks, mapping).toList();
    m_library->m_trackRows.clear();
    QSet<QString> mappedUrls;
    for (auto it = mapping.cbegin(); it != mapping.cend(); ++it) mappedUrls.insert(it.value().toString());
    for (int i = 0; i < m_library->m_tracks.size(); ++i) {
        auto track = m_library->m_tracks[i].toMap(); const auto url = track.value("fileUrl").toString();
        const QFileInfo file(QUrl(url).toLocalFile());
        if (mappedUrls.contains(url)) {
            track["size"] = file.size(); track["modified"] = file.lastModified().toMSecsSinceEpoch();
            if (track.contains("path")) track["path"] = file.absoluteFilePath();
            // Folder evidence participates in compilation grouping. Update it
            // without changing the tag-derived artist/album identity.
            const auto albumFolder = track.value("albumDirectory").toString();
            if (!albumFolder.isEmpty() && inside(albumFolder, preview.value("oldFolder").toString()))
                track["albumDirectory"] = QDir::cleanPath(QDir(preview.value("newFolder").toString()).filePath(QDir(preview.value("oldFolder").toString()).relativeFilePath(albumFolder)));
            const auto artwork = QUrl(track.value("artwork").toString());
            const auto artPath = localPath(artwork), oldFolder = preview.value("oldFolder").toString();
            if (!artPath.isEmpty() && inside(artPath, oldFolder)) {
                const auto moved = QDir(preview.value("newFolder").toString()).filePath(QDir(oldFolder).relativeFilePath(artPath));
                if (QFileInfo(moved).isFile()) track["artwork"] = QUrl::fromLocalFile(moved).toString();
            }
        }
        m_library->m_tracks[i] = track; m_library->m_trackRows[url] = i;
    }
    m_library->m_favorites = remapValue(m_library->m_favorites, mapping).toStringList();
    m_library->m_playCounts = remapValue(m_library->m_playCounts, mapping).toMap();
    m_library->m_addedAt = remapValue(m_library->m_addedAt, mapping).toMap();
    m_library->m_albumOverrides = remapValue(m_library->m_albumOverrides, mapping).toMap();
    m_library->m_albumUndo = remapValue(m_library->m_albumUndo, mapping).toList();
    m_library->m_playlistData = remapValue(m_library->m_playlistData, mapping).toList();
    m_library->m_metadataCache = remapValue(m_library->m_metadataCache, mapping).toMap();
    m_library->m_recentListening = remapValue(m_library->m_recentListening, mapping).toList();
    m_library->m_listeningTotals = remapValue(m_library->m_listeningTotals, mapping).toMap();
    m_library->m_dailyListening = remapValue(m_library->m_dailyListening, mapping).toMap();
    QVariantMap pathMapping;
    for (auto it = mapping.cbegin(); it != mapping.cend(); ++it) pathMapping[QUrl(it.key()).toLocalFile()] = QUrl(it.value().toString()).toLocalFile();
    m_library->m_files = remapValue(m_library->m_files, pathMapping).toStringList();
    const auto oldFolder = preview.value("oldFolder").toString(), newFolder = preview.value("newFolder").toString();
    for (auto &folder : m_library->m_folders) if (inside(folder, oldFolder)) folder = QDir::cleanPath(QDir(newFolder).filePath(QDir(oldFolder).relativeFilePath(folder)));
    m_library->m_folders.removeDuplicates();
    // A partially matched move keeps unmatched songs in the saved snapshot; a
    // later explicit scan should still discover any untracked files at the new root.
    if (!m_library->m_folders.contains(newFolder)) m_library->m_folders.append(newFolder);
    m_library->m_queueStore->replaceState(remapValue(m_library->m_queueStore->exportState(), mapping).toMap());
    m_library->remapCollectionUrls(mapping);
    if (mapping.contains(m_library->m_listening.url())) m_library->m_listening.begin(mapping.value(m_library->m_listening.url()).toString());
    m_library->m_audibleSessions.clear();
    m_library->publishUpdates();
    if (m_library->m_database->valid() && !m_library->m_databaseSynchronized) {
        applyBackup(previous);
        emit error(tr("The repair couldn't be saved. The previous library references were kept.")); return false;
    }
    m_library->saveSettings();
    emit m_library->foldersChanged(); emit m_library->albumEditsChanged();
    emit urlsRelinked(mapping);
    setStatus(tr("Repaired %1 moved songs.").arg(mapping.size())); emit completed(m_status);
    return true;
}

bool LibraryMaintenance::findDuplicates()
{
    if (!available()) return false;
    const auto tracks = m_library->tracks(); const auto cancel = m_cancel;
    m_operation = "duplicates"; m_duplicates.clear(); emit duplicateGroupsChanged();
    setStatus(tr("Comparing files by size and SHA-256…"));
    m_worker.setFuture(QtConcurrent::run([tracks, cancel] {
        QMap<qint64, QVariantList> sizes;
        int skipped = 0;
        for (const auto &item : tracks) {
            if (cancel->load()) return QVariantMap{};
            const auto track = item.toMap(); const QFileInfo file(QUrl(track.value("fileUrl").toString()).toLocalFile());
            if (!file.isFile() || !file.isReadable()) { ++skipped; continue; }
            auto candidate = track; candidate["size"] = file.size(); candidate["modified"] = file.lastModified().toMSecsSinceEpoch();
            sizes[file.size()].append(candidate);
        }
        QVariantList groups;
        for (auto it = sizes.cbegin(); it != sizes.cend(); ++it) {
            if (it.value().size() < 2) continue;
            QMap<QString, QVariantList> hashes;
            for (const auto &item : it.value()) {
                const auto track = item.toMap(); const auto url = track.value("fileUrl").toString();
                const auto hash = hashFile(url, cancel); const QFileInfo after(QUrl(url).toLocalFile());
                if (hash.isEmpty() || track.value("size").toLongLong() != after.size() || track.value("modified").toLongLong() != after.lastModified().toMSecsSinceEpoch()) { ++skipped; continue; }
                hashes[hash].append(track);
            }
            for (auto group = hashes.cbegin(); group != hashes.cend(); ++group)
                if (group.value().size() > 1) groups.append(QVariantMap{{"hash", group.key()}, {"size", it.key()}, {"tracks", group.value()}});
        }
        return QVariantMap{{"groups", groups}, {"skipped", skipped}};
    }));
    emit busyChanged(); return true;
}
bool LibraryMaintenance::trashDuplicates(const QString &keepUrl, const QStringList &removeUrls)
{
    if (!available() || removeUrls.isEmpty()) return false;
    QVariantMap selected;
    for (const auto &item : m_duplicates) {
        const auto group = item.toMap();
        for (const auto &track : group.value("tracks").toList())
            if (track.toMap().value("fileUrl").toString() == keepUrl) selected = group;
    }
    QSet<QString> members;
    for (const auto &item : selected.value("tracks").toList()) members.insert(item.toMap().value("fileUrl").toString());
    QStringList urls = removeUrls; urls.removeDuplicates();
    if (selected.isEmpty() || urls.contains(keepUrl)) { emit error(tr("Keep one file from a reviewed duplicate group.")); return false; }
    for (const auto &url : urls) if (!members.contains(url)) { emit error(tr("Choose files from the same duplicate group.")); return false; }
    const auto expected = selected.value("hash").toString(); const auto cancel = m_cancel;
    m_operation = "trashDuplicates"; setStatus(tr("Checking the selected duplicates before moving them to Trash…"));
    m_worker.setFuture(QtConcurrent::run([keepUrl, urls, expected, cancel] {
        QStringList all = urls; all.prepend(keepUrl);
        for (const auto &url : all) if (hashFile(url, cancel) != expected)
            return QVariantMap{{"error", QStringLiteral("A duplicate changed or could not be read. Find duplicates again; no files were removed.")}};
        return QVariantMap{{"urls", urls}};
    }));
    emit busyChanged(); return true;
}

QVariantMap LibraryMaintenance::snapshot(const QVariantMap &extraSettings) const
{
    QVariantMap aliases;
    for (auto it = m_library->m_albumAliases.cbegin(); it != m_library->m_albumAliases.cend(); ++it) aliases[it.key()] = it.value();
    return {{"format", "musica-backup"}, {"version", 1}, {"created", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {"settings", settingsValues(m_library->m_settings)}, {"tracks", m_library->tracks()},
        {"queues", m_library->m_queueStore->exportState()}, {"extra", extraSettings},
        {"library", QVariantMap{{"splitArtists", m_library->m_splitArtists}, {"artistSeparators", m_library->m_artistSeparators}, {"albumAliases", aliases}}}};
}
bool LibraryMaintenance::createBackup(const QUrl &url, const QVariantMap &extraSettings)
{
    if (!available()) return false;
    if (!url.isLocalFile() || localPath(url).isEmpty()) { emit error(tr("Choose a local backup file.")); return false; }
    // Prevent replacing a music file or any of the files currently used by the library.
    const auto path = localPath(url), settings = QFileInfo(m_library->m_settings.fileName()).absoluteFilePath();
    if (path == settings || path.startsWith(settings + ".") || !m_library->trackForUrl(QUrl::fromLocalFile(path).toString()).isEmpty()) {
        emit error(tr("Choose a separate backup file, outside the current music and settings files.")); return false;
    }
    m_library->saveSettings();
    const auto bytes = QJsonDocument::fromVariant(snapshot(extraSettings)).toJson(QJsonDocument::Compact);
    if (bytes.size() > MaximumBackupBytes) { emit error(tr("This backup exceeds the 256 MB limit.")); return false; }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        emit error(tr("Couldn't write the backup: %1").arg(file.errorString())); return false;
    }
    setStatus(tr("Backup saved. Audio files are not included.")); emit completed(m_status); return true;
}
QVariantMap LibraryMaintenance::inspectBackup(const QUrl &url) const
{
    const auto bundle = readBackup(url);
    if (bundle.contains("error")) return bundle;
    return {{"created", bundle.value("created")}, {"songs", bundle.value("tracks").toList().size()},
        {"playlists", bundle.value("settings").toMap().value("library/playlists").toList().size()},
        {"queues", bundle.value("queues").toMap().value("queues").toList().size()}, {"file", url}};
}
bool LibraryMaintenance::applyBackup(const QVariantMap &bundle)
{
    m_library->m_releaseTimer.stop(); m_library->m_tracksNotification.stop(); m_library->m_settingsTimer.stop();
    const auto settings = bundle.value("settings").toMap();
    m_library->m_settings.clear();
    for (auto it = settings.cbegin(); it != settings.cend(); ++it) m_library->m_settings.setValue(it.key(), it.value());
    m_library->m_settings.sync();
    if (m_library->m_settings.status() != QSettings::NoError) return false;
    m_library->m_folders = settings.value("library/folders").toStringList();
    m_library->m_files = settings.value("library/files").toStringList();
    m_library->m_favorites = settings.value("library/favorites").toStringList();
    m_library->m_playlistData = settings.value("library/playlists").toList();
    m_library->m_playCounts = settings.value("library/playCounts").toMap();
    m_library->m_albumOverrides = settings.value("library/albumOverrides").toMap();
    m_library->m_albumUndo = settings.value("library/albumUndo").toList();
    m_library->m_addedAt = settings.value("library/addedAt").toMap();
    m_library->m_dailyListening = settings.value("history/days").toMap();
    m_library->m_listeningTotals = settings.value("history/totals").toMap();
    m_library->m_recentListening = settings.value("history/recent").toList();
    m_library->m_metadataCache.clear(); m_library->m_trackLookup.clear(); m_library->m_albumAliases.clear();
    m_library->m_tracks = bundle.value("tracks").toList();
    m_library->m_workingSetLoaded = true; m_library->m_trackRows.clear();
    m_library->m_processed = 0; m_library->m_failed = 0;
    for (int i = 0; i < m_library->m_tracks.size(); ++i) {
        const auto track = m_library->m_tracks[i].toMap();
        const auto url = track.value("fileUrl").toString();
        m_library->m_trackRows[url] = i;
        m_library->m_addedAt[url] = track.value("addedAt");
        m_library->m_processed += track.value("probed").toBool();
        m_library->m_failed += !track.value("metadataError").toString().isEmpty();
    }
    const auto libraryState = bundle.value("library").toMap();
    m_library->m_splitArtists = libraryState.value("splitArtists").toBool();
    m_library->m_artistSeparators = libraryState.value("artistSeparators", ",;|").toString();
    const auto aliases = libraryState.value("albumAliases").toMap();
    for (auto it = aliases.cbegin(); it != aliases.cend(); ++it) m_library->m_albumAliases[it.key()] = it.value().toString();
    const auto state = bundle.value("queues").toMap(), history = state.value("history").toMap();
    if (history.contains("playCounts")) {
        m_library->m_playCounts = history.value("playCounts").toMap(); m_library->m_dailyListening = history.value("days").toMap();
        m_library->m_listeningTotals = history.value("totals").toMap(); m_library->m_recentListening = history.value("recent").toList();
    }
    m_library->m_queueStore->replaceState(state);
    m_library->m_settings.setValue("player/queueStoreInitialized", true);
    m_library->m_volume = qBound(0.0, settings.value("player/volume", 0.65).toDouble(), 1.0);
    m_library->m_listening.begin({}); m_library->m_audibleSessions.clear(); m_library->m_historyEventStarted = false;
    m_library->initializeCollectionTools();
    m_library->publishUpdates(); m_library->saveSettings();
    return !m_library->m_database->valid() || m_library->m_databaseSynchronized;
}
bool LibraryMaintenance::restoreBackup(const QUrl &url)
{
    if (!available()) return false;
    const auto bundle = readBackup(url);
    if (bundle.contains("error")) { emit error(bundle.value("error").toString()); return false; }
    m_library->saveSettings();
    const auto previous = snapshot({});
    if (!applyBackup(bundle)) {
        applyBackup(previous);
        emit error(tr("The backup couldn't be restored. The previous library was kept.")); return false;
    }
    m_duplicates.clear(); emit duplicateGroupsChanged();
    emit m_library->foldersChanged(); emit m_library->volumeChanged(); emit m_library->albumEditsChanged();
    emit restored(bundle.value("extra").toMap());
    setStatus(tr("Restored %1 songs and %2 queues.").arg(m_library->trackCount()).arg(m_library->savedQueues().size())); emit completed(m_status);
    return true;
}
