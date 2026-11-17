#include "tracklibrary.h"
#include "albumgrouping.h"
#include "collectionfeatures.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>
#include <QUuid>
#include <algorithm>

namespace {
QString cleanName(const QString &name) { return name.simplified().left(80); }
QString newAlbumId() { return QStringLiteral("manual:") + QUuid::createUuid().toString(QUuid::WithoutBraces); }
}

QVariantList TrackListModel::albumOptions() const
{
    QVariantList rows;
    rows = m_database->valid() ? m_database->all("album") : m_albums.entries();
    auto collator = libraryCollator();
    std::sort(rows.begin(), rows.end(), [&collator](const auto &a, const auto &b) { return collator.compare(a.toMap().value("name").toString(), b.toMap().value("name").toString()) < 0; });
    return rows;
}

QVariantList TrackListModel::smartPlaylists() const
{
    int recent = 0, never = 0, rare = 0;
    const qint64 since = QDateTime::currentMSecsSinceEpoch() - 30LL * 86400000;
    if (!m_workingSetLoaded) {
        const auto counts = m_database->smartCounts(); recent = counts.value("recent").toInt(); never = counts.value("never").toInt(); rare = counts.value("rare").toInt();
    }
    for (const auto &entry : m_tracks) {
        const auto track = entry.toMap();
        const auto count = track.value("playCount").toLongLong();
        recent += track.value("addedAt").toLongLong() >= since;
        never += count == 0;
        rare += count > 0 && count < 5;
    }
    return {QVariantMap{{"id", "recent"}, {"name", tr("Recently added")}, {"description", tr("Discovered in the last 30 days, newest first.")}, {"icon", "clock"}, {"count", recent}},
        QVariantMap{{"id", "never"}, {"name", tr("Never played")}, {"description", tr("Songs that haven't yet reached 50% playback.")}, {"icon", "music"}, {"count", never}},
        QVariantMap{{"id", "rare"}, {"name", tr("Rarely played")}, {"description", tr("Rediscover songs with 1–4 qualified plays.")}, {"icon", "smart"}, {"count", rare}}};
}

bool TrackListModel::renamePlaylist(const QString &id, const QString &name)
{
    const auto before = m_playlistData;
    const QString title = name.simplified();
    if (title.isEmpty() || title.size() > 80) {
        emit importError(tr("Give your playlist a name of 1–80 characters."));
        return false;
    }
    for (const auto &entry : m_playlistData) {
        const auto item = entry.toMap();
        if (item.value("id").toString() != id && item.value("name").toString().compare(title, Qt::CaseInsensitive) == 0) {
            emit importError(tr("A playlist with that name already exists."));
            return false;
        }
    }
    for (auto &entry : m_playlistData) {
        auto item = entry.toMap();
        if (item.value("id").toString() != id) continue;
        item["name"] = title;
        entry = item;
        recordPlaylistEdit(before, tr("Rename playlist"));
        saveSettings();
        publishUpdates();
        return true;
    }
    return false;
}

bool TrackListModel::movePlaylistTrack(const QString &id, const QString &fileUrl, const QString &beforeUrl)
{
    const auto before = m_playlistData;
    for (auto &entry : m_playlistData) {
        auto item = entry.toMap();
        if (item.value("id").toString() != id) continue;
        auto urls = item.value("urls").toStringList();
        if (!urls.contains(fileUrl) || (!beforeUrl.isEmpty() && !urls.contains(beforeUrl))) return false;
        if (fileUrl == beforeUrl) return true;
        urls.removeAll(fileUrl);
        urls.insert(beforeUrl.isEmpty() ? urls.size() : urls.indexOf(beforeUrl), fileUrl);
        item["urls"] = urls;
        entry = item;
        recordPlaylistEdit(before, tr("Reorder playlist"));
        saveSettings();
        publishUpdates();
        return true;
    }
    return false;
}

QString TrackListModel::importPlaylist(const QUrl &fileUrl)
{
    QFile file(fileUrl.toLocalFile());
    if (!fileUrl.isLocalFile() || !file.open(QIODevice::ReadOnly) || file.size() > 8 * 1024 * 1024) {
        emit importError(tr("Choose a readable local M3U playlist smaller than 8 MB."));
        return {};
    }
    const QDir directory = QFileInfo(file).absoluteDir();
    QString text = QString::fromUtf8(file.readAll());
    if (text.startsWith(QChar(0xfeff))) text.remove(0, 1);
    QStringList urls;
    QList<QUrl> imports;
    int skipped = 0;
    const QStringList extensions{"mp3", "flac", "ogg", "oga", "opus", "wav", "m4a", "aac", "aif", "aiff", "wma"};
    for (QString line : text.split(QLatin1Char('\n'))) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        QUrl url;
        if (line.startsWith(QStringLiteral("file:"))) url = QUrl(line);
        else if (!QUrl(line).scheme().isEmpty()) { ++skipped; continue; }
        else url = QUrl::fromLocalFile(directory.absoluteFilePath(line));
        const QFileInfo info(url.toLocalFile());
        if (!url.isLocalFile() || !info.isFile() || !info.isReadable() || !extensions.contains(info.suffix().toLower()) || urls.size() >= 10000) {
            ++skipped;
            continue;
        }
        url = QUrl::fromLocalFile(info.canonicalFilePath());
        const QString key = url.toString();
        if (urls.contains(key)) continue;
        urls.append(key);
        if (trackForUrl(key).isEmpty()) imports.append(url);
    }
    if (urls.isEmpty()) {
        emit importError(tr("This playlist has no readable local audio files."));
        return {};
    }
    QString base = cleanName(QFileInfo(file).completeBaseName());
    if (base.isEmpty()) base = tr("Imported playlist");
    QString name = base;
    int suffix = 2;
    const auto exists = [this](const QString &title) {
        for (const auto &item : m_playlistData)
            if (item.toMap().value("name").toString().compare(title, Qt::CaseInsensitive) == 0) return true;
        return false;
    };
    while (exists(name)) name = base.left(70) + QStringLiteral(" (%1)").arg(suffix++);
    const auto before = m_playlistData;
    ++m_suppressPlaylistUndo;
    const QString id = createPlaylist(name);
    --m_suppressPlaylistUndo;
    if (id.isEmpty()) return {};
    auto playlist = m_playlistData.last().toMap();
    playlist["urls"] = urls;
    m_playlistData.last() = playlist;
    recordPlaylistEdit(before, tr("Import playlist"));
    saveSettings();
    publishUpdates();
    if (!imports.isEmpty()) addFiles(imports);
    if (skipped) emit importError(tr("Playlist imported; %1 missing, unsupported, or remote entries skipped.").arg(skipped));
    return id;
}

void TrackListModel::recordPlaylistEdit(const QVariantList &before, const QString &label)
{
    if (m_suppressPlaylistUndo || before == m_playlistData) return;
    const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_playlistEdits.append({token, before, m_playlistData});
    if (m_playlistEdits.size() > 20) m_playlistEdits.removeFirst();
    emit playlistEditRecorded(label, token);
}

bool TrackListModel::restoreQueueEdit(const QString &id, const QVariantMap &input)
{
    auto session = input;
    QStringList requested;
    for (const auto &key : {"queue", "source"}) for (const auto &entry : session.value(key).toList()) requested.append(entry.toMap().value("fileUrl").toString());
    QStringList ids;
    if (m_database->valid()) ids = m_database->matchingSongUrls(requested);
    else for (const auto &url : requested) if (!trackForUrl(url).isEmpty()) ids.append(url);
    const QSet<QString> valid(ids.cbegin(), ids.cend());
    for (const auto &key : {"queue", "source"}) {
        QVariantList kept;
        for (const auto &entry : session.value(key).toList()) if (valid.contains(entry.toMap().value("fileUrl").toString())) kept.append(entry);
        session[key] = kept;
    }
    return m_queueStore->updateSession(id, session);
}

QStringList TrackListModel::queueSearchUrls(const QString &search, const QStringList &urls) const
{
    if (search.trimmed().isEmpty()) return urls;
    QStringList matching;
    if (m_database->valid()) matching = m_database->matchingSongUrls(urls, search);
    else {
        const auto query = CollectionRules::folded(search.trimmed());
        const QSet<QString> unique(urls.cbegin(), urls.cend());
        for (const auto &url : unique) {
            const auto track = trackForUrl(url); QString text;
            for (const auto &field : {"title", "artist", "album", "albumArtist", "genre"}) text += track.value(field).toString() + ' ';
            if (CollectionRules::folded(text).contains(query)) matching.append(url);
        }
    }
    const QSet<QString> found(matching.cbegin(), matching.cend());
    QStringList result; for (const auto &url : urls) if (found.contains(url)) result.append(url);
    return result;
}

QStringList TrackListModel::recentlyPlayedUrls(int days) const
{
    const auto since = QDate::currentDate().addDays(1 - qBound(1, days, 90)).toString(Qt::ISODate);
    QSet<QString> urls;
    for (auto day = m_dailyListening.cbegin(); day != m_dailyListening.cend(); ++day) {
        if (day.key() < since) continue;
        const auto totals = day.value().toMap();
        for (auto track = totals.cbegin(); track != totals.cend(); ++track) {
            const auto counts = track.value().toMap();
            if (counts.value("ms").toLongLong() >= 10000 || counts.value("plays").toInt() > 0) urls.insert(track.key());
        }
    }
    return urls.values();
}

bool TrackListModel::undoPlaylistEdit(const QString &token)
{
    for (int i = m_playlistEdits.size() - 1; i >= 0; --i) {
        const auto edit = m_playlistEdits[i];
        if (edit.token != token) continue;
        if (m_playlistData != edit.after) { emit importError(tr("This playlist edit can no longer be undone because the playlists changed.")); return false; }
        m_playlistData = edit.before; m_playlistEdits = m_playlistEdits.mid(0, i);
        saveSettings(); publishUpdates(); return true;
    }
    return false;
}

bool TrackListModel::exportPlaylist(const QString &id, const QUrl &fileUrl)
{
    QVariantMap playlist;
    for (const auto &entry : m_playlistData)
        if (entry.toMap().value("id").toString() == id) playlist = entry.toMap();
    if (playlist.isEmpty() || !fileUrl.isLocalFile()) return false;
    const QString target = fileUrl.toLocalFile();
    // A playlist export must never overwrite a song file.
    if (!QStringList{"m3u", "m3u8"}.contains(QFileInfo(target).suffix().toLower())) {
        emit importError(tr("Use a .m3u or .m3u8 filename for the exported playlist."));
        return false;
    }
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit importError(tr("Couldn't export playlist: %1").arg(file.errorString()));
        return false;
    }
    QTextStream stream(&file); // Qt 6 writes UTF-8 by default, including .m3u exports.
    stream << "#EXTM3U\n#PLAYLIST:" << playlist.value("name").toString() << '\n';
    const QDir directory = QFileInfo(target).absoluteDir();
    for (const auto &url : playlist.value("urls").toStringList()) {
        const auto track = trackForUrl(url);
        if (!track.isEmpty()) {
            QString label = track.value("artist").toString() + QStringLiteral(" - ") + track.value("title").toString();
            label.replace(QLatin1Char('\n'), QLatin1Char(' '));
            label.replace(QLatin1Char('\r'), QLatin1Char(' '));
            stream << "#EXTINF:" << track.value("duration").toLongLong() / 1000 << ',' << label << '\n';
        }
        QString path = directory.relativeFilePath(QUrl(url).toLocalFile());
        if (path.contains(QLatin1Char('\n')) || path.contains(QLatin1Char('\r')) || path != path.trimmed())
            path = QUrl(url).toString(QUrl::FullyEncoded);
        else if (path.startsWith(QLatin1Char('#'))) path.prepend(QStringLiteral("./"));
        stream << path << '\n';
    }
    stream.flush();
    if (stream.status() != QTextStream::Ok || !file.commit()) {
        emit importError(tr("Couldn't finish writing the playlist."));
        return false;
    }
    return true;
}

QVariantMap TrackListModel::playbackSession() const
{
    const auto saved = m_queueStore->session();
    QVariantList queue;
    QHash<QString, QVariantMap> entries;
    for (const auto &entry : saved.value("queue").toList()) {
        const auto item = entry.toMap();
        const QString id = item.value("id").toString();
        const QString url = item.value("fileUrl").toString();
        if (id.isEmpty() || entries.contains(id) || trackForUrl(url).isEmpty() || !QFileInfo::exists(QUrl(url).toLocalFile())) continue;
        QVariantMap valid{{"id", id}, {"fileUrl", url}};
        entries.insert(id, valid);
        queue.append(valid);
        if (queue.size() >= 10000) break;
    }
    QVariantList source;
    QSet<QString> included;
    for (const auto &entry : saved.value("source").toList()) {
        const auto id = entry.toMap().value("id").toString();
        if (!entries.contains(id) || included.contains(id)) continue;
        source.append(entries.value(id));
        included.insert(id);
    }
    for (const auto &entry : queue) {
        const auto id = entry.toMap().value("id").toString();
        if (!included.contains(id)) source.append(entry);
    }
    QString currentId = saved.value("currentId").toString();
    qint64 position = qMax<qint64>(0, saved.value("position").toLongLong());
    if (!entries.contains(currentId)) { currentId = queue.isEmpty() ? QString() : queue.first().toMap().value("id").toString(); position = 0; }
    const auto track = trackForUrl(entries.value(currentId).value("fileUrl").toString());
    const auto duration = track.value("duration").toLongLong();
    if (duration > 0) position = qMin(position, qMax<qint64>(0, duration - 1));
    qint64 serial = qMax<qint64>(0, saved.value("serial").toLongLong());
    for (auto it = entries.cbegin(); it != entries.cend(); ++it) serial = qMax(serial, it.key().toLongLong());
    return {{"queue", queue}, {"source", source}, {"currentId", currentId}, {"position", position},
            {"shuffle", saved.value("shuffle", false)}, {"repeat", qBound(0, saved.value("repeat").toInt(), 2)},
            {"serial", serial}, {"muted", saved.value("muted", false)}};
}

void TrackListModel::savePlaybackSession(const QVariantMap &session)
{
    if (!m_libraryReady) return; // An empty model during startup must not erase the previous session.
    m_queueStore->saveSession(session);
    saveListeningState();
}

void TrackListModel::saveListeningState()
{
    m_historySaveTimer.stop();
    if (m_queueStore) m_queueStore->saveHistory({{"playCounts", m_playCounts}, {"days", m_dailyListening}, {"totals", m_listeningTotals}, {"recent", m_recentListening}});
}

QVariantList TrackListModel::savedQueues() const
{
    QVariantList result;
    if (!m_queueStore) return result;
    for (const auto &value : m_queueStore->queues()) {
        const auto queue = value.toMap(), session = queue.value("session").toMap();
        result.append(QVariantMap{{"id", queue.value("id")}, {"name", queue.value("name")},
            {"trackCount", session.value("queue").toList().size()}, {"active", queue.value("id").toString() == m_queueStore->activeId()}});
    }
    return result;
}
QString TrackListModel::createQueue(const QString &name) { return m_queueStore->createQueue(name); }
QString TrackListModel::startPlaybackQueue(const QString &name, const QVariantMap &session) { return m_queueStore->startPlaybackQueue(name, session); }
bool TrackListModel::activateQueue(const QString &id) { return m_queueStore->activate(id); }
bool TrackListModel::renameQueue(const QString &id, const QString &name) { return m_queueStore->renameQueue(id, name); }
bool TrackListModel::deleteQueue(const QString &id) { return m_queueStore->removeQueue(id); }
void TrackListModel::removeOtherQueues() { m_queueStore->removeOtherQueues(); }

QString TrackListModel::albumSignature() const
{
    QStringList members;
    for (const auto &entry : tracks()) {
        const auto track = entry.toMap();
        members.append(track.value("fileUrl").toString() + QChar(0x1f) + track.value("albumId").toString()
            + QChar(0x1f) + track.value("album").toString() + QChar(0x1f) + track.value("albumArtist").toString());
    }
    members.sort();
    return QString::fromLatin1(QCryptographicHash::hash(members.join(QChar(0)).toUtf8(), QCryptographicHash::Sha256).toHex());
}

QVariantMap TrackListModel::previewAlbumEdit(const QString &operation, const QStringList &albumIds,
    const QStringList &selectedUrls, const QString &name, const QString &artist, const QString &splitBy) const
{
    const auto error = [](const QString &message) { return QVariantMap{{"error", message}}; };
    if (operation != QStringLiteral("merge") && operation != QStringLiteral("split")) return error(tr("Choose merge or split."));
    if ((operation == QStringLiteral("merge") && albumIds.size() < 2) || (operation == QStringLiteral("split") && albumIds.size() != 1))
        return error(tr("Select at least two albums to merge, or one album to split."));
    if (name.simplified().size() > 80 || artist.simplified().size() > 80) return error(tr("Use names of up to 80 characters."));
    QVariantList tracks;
    QSet<QString> seen;
    for (const auto &id : albumIds) {
        const auto members = tracksForGroup(QStringLiteral("album"), id);
        if (members.isEmpty()) return error(tr("An album changed. Refresh your selection."));
        for (const auto &entry : members) {
            const auto url = entry.toMap().value("fileUrl").toString();
            if (!seen.contains(url)) { tracks.append(entry); seen.insert(url); }
        }
    }
    QMap<QString, QVariantList> groups;
    if (operation == QStringLiteral("merge")) {
        if (name.simplified().isEmpty()) return error(tr("Enter the merged album's name."));
        groups.insert(QStringLiteral("merged"), tracks);
    } else {
        if (tracks.size() < 2) return error(tr("This album needs at least two songs to split."));
        if (splitBy == QStringLiteral("selected") && (selectedUrls.isEmpty() || selectedUrls.size() >= tracks.size() || name.simplified().isEmpty()))
            return error(tr("Select some songs and name the new album. Leave at least one song in the original."));
        for (const auto &entry : tracks) {
            const auto track = entry.toMap();
            const auto url = track.value("fileUrl").toString();
            QString key;
            if (splitBy == QStringLiteral("selected")) key = selectedUrls.contains(url) ? QStringLiteral("selected") : QStringLiteral("remaining");
            else if (splitBy == QStringLiteral("artist")) key = track.value("artist").toString();
            else if (splitBy == QStringLiteral("folder")) key = QFileInfo(QUrl(url).toLocalFile()).absolutePath();
            else if (splitBy == QStringLiteral("track")) key = url;
            else return error(tr("Choose how to split the album."));
            groups[key].append(entry);
        }
        if (groups.size() < 2) return error(tr("All songs belong to the same group. Try selecting songs manually."));
    }
    QVariantList preview;
    for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
        const auto first = it.value().first().toMap();
        QString title = first.value("album").toString();
        QString credit = first.value("albumArtist").toString();
        if (operation == QStringLiteral("merge") || it.key() == QStringLiteral("selected")) {
            title = name.simplified();
            credit = artist.simplified().isEmpty() ? tr("Various artists") : artist.simplified();
        } else if (splitBy == QStringLiteral("artist")) credit = it.key();
        else if (splitBy == QStringLiteral("folder")) title += QStringLiteral(" · ") + QDir(it.key()).dirName();
        else if (splitBy == QStringLiteral("track")) title = first.value("title").toString();
        auto ordered = it.value();
        std::sort(ordered.begin(), ordered.end(), AlbumGrouping::trackLessThan);
        QStringList urls;
        for (const auto &entry : ordered) urls.append(entry.toMap().value("fileUrl").toString());
        preview.append(QVariantMap{{"id", newAlbumId()}, {"name", title}, {"artist", credit},
            {"urls", urls}, {"tracks", ordered}, {"trackCount", urls.size()}});
    }
    return {{"signature", albumSignature()}, {"groups", preview}, {"trackCount", tracks.size()}, {"operation", operation}};
}

bool TrackListModel::applyAlbumEdit(const QVariantMap &preview)
{
    if (preview.contains("error") || preview.value("signature").toString() != albumSignature()) {
        emit importError(tr("The albums changed while previewing. Preview your changes again."));
        return false;
    }
    const auto groups = preview.value("groups").toList();
    if (groups.isEmpty()) return false;
    QVariantMap updated = m_albumOverrides;
    QSet<QString> seen;
    for (const auto &entry : groups) {
        const auto group = entry.toMap();
        if (!group.value("id").toString().startsWith(QStringLiteral("manual:")) || group.value("name").toString().isEmpty()) return false;
        for (const auto &url : group.value("urls").toStringList()) {
            if (trackForUrl(url).isEmpty() || seen.contains(url)) return false;
            seen.insert(url);
            updated[url] = QVariantMap{{"id", group.value("id")}, {"name", group.value("name")}, {"artist", group.value("artist")}};
        }
    }
    if (seen.size() != preview.value("trackCount").toInt()) return false;
    m_albumUndo.append(m_albumOverrides);
    while (m_albumUndo.size() > 20) m_albumUndo.removeFirst();
    m_albumOverrides = updated;
    publishUpdates();
    saveSettings();
    emit albumEditsChanged();
    return true;
}

bool TrackListModel::undoAlbumEdit()
{
    if (m_albumUndo.isEmpty()) return false;
    m_albumOverrides = m_albumUndo.takeLast().toMap();
    publishUpdates();
    saveSettings();
    emit albumEditsChanged();
    return true;
}

void TrackListModel::applyAlbumOverrides()
{
    for (auto it = m_albumAliases.begin(); it != m_albumAliases.end();) {
        if (it.key().startsWith(QStringLiteral("manual:"))) it = m_albumAliases.erase(it);
        else ++it;
    }
    for (auto &entry : m_tracks) {
        auto track = entry.toMap();
        const auto override = m_albumOverrides.value(track.value("fileUrl").toString()).toMap();
        if (override.isEmpty()) continue;
        track["automaticAlbum"] = track.value("album");
        track["automaticAlbumId"] = track.value("albumId");
        track["automaticAlbumArtist"] = track.value("albumArtist");
        track["albumId"] = override.value("id");
        track["album"] = override.value("name");
        track["albumArtist"] = override.value("artist");
        track["manualAlbum"] = true;
        entry = track;
    }
}

void TrackListModel::collectListening(const QString &url, qint64 audibleMs, bool qualified, const QString &session)
{
    if ((audibleMs <= 0 && !qualified) || trackForUrl(url).isEmpty()) return;
    const auto now = QDateTime::currentDateTime();
    const QString day = now.date().toString(Qt::ISODate);
    auto daily = m_dailyListening.value(day).toMap();
    auto totals = daily.value(url).toMap();
    totals["ms"] = totals.value("ms").toLongLong() + audibleMs;
    if (qualified) totals["plays"] = totals.value("plays").toInt() + 1;
    daily[url] = totals;
    m_dailyListening[day] = daily;
    m_listeningTotals[url] = m_listeningTotals.value(url).toLongLong() + audibleMs;
    int eventRow = -1;
    if (!session.isEmpty()) {
        for (int i = 0; i < m_recentListening.size(); ++i)
            if (m_recentListening[i].toMap().value("session").toString() == session) { eventRow = i; break; }
    } else if (m_historyEventStarted && !m_recentListening.isEmpty()) eventRow = 0;
    if (eventRow < 0) {
        m_recentListening.prepend(QVariantMap{{"fileUrl", url}, {"at", now.toMSecsSinceEpoch()}, {"ms", 0LL}, {"qualified", false}, {"session", session}});
        if (session.isEmpty()) m_historyEventStarted = true;
        eventRow = 0;
    }
    auto event = m_recentListening[eventRow].toMap();
    event["ms"] = event.value("ms").toLongLong() + audibleMs;
    event["qualified"] = event.value("qualified").toBool() || qualified;
    m_recentListening[eventRow] = event;
    while (m_recentListening.size() > 500) m_recentListening.removeLast();
    const QString oldest = now.date().addDays(-90).toString(Qt::ISODate);
    for (auto it = m_dailyListening.begin(); it != m_dailyListening.end();)
        if (it.key() < oldest) it = m_dailyListening.erase(it); else ++it;
    if (!m_historySaveTimer.isActive()) m_historySaveTimer.start();
    if (qualified) emit historyChanged();
    else if (!m_dashboardTimer.isActive()) m_dashboardTimer.start();
}

QVariantMap TrackListModel::dashboard() const
{
    const QDate today = QDate::currentDate();
    QHash<QString, qint64> weeklyTime;
    QHash<QString, int> weeklyPlays;
    QVariantList days;
    qint64 weeklyMs = 0, totalMs = 0;
    int plays = 0;
    for (int offset = -6; offset <= 0; ++offset) {
        const auto date = today.addDays(offset);
        const auto daily = m_dailyListening.value(date.toString(Qt::ISODate)).toMap();
        qint64 ms = 0;
        for (auto it = daily.cbegin(); it != daily.cend(); ++it) {
            const auto value = it.value().toMap();
            ms += value.value("ms").toLongLong();
            weeklyTime[it.key()] += value.value("ms").toLongLong();
            weeklyPlays[it.key()] += value.value("plays").toInt();
            plays += value.value("plays").toInt();
        }
        weeklyMs += ms;
        days.append(QVariantMap{{"label", date.toString(QStringLiteral("ddd"))}, {"ms", ms}});
    }
    QHash<QString, QVariantMap> artists;
    for (auto it = m_listeningTotals.cbegin(); it != m_listeningTotals.cend(); ++it) {
        totalMs += it.value().toLongLong();
        const auto track = trackForUrl(it.key());
        if (track.isEmpty()) continue;
        for (const auto &creditEntry : track.value("artistCredits").toList()) {
            const auto credit = creditEntry.toMap();
            const auto id = credit.value("id").toString();
            auto row = artists.value(id, QVariantMap{{"id", id}, {"name", credit.value("name")}, {"ms", 0LL}, {"plays", 0LL}});
            row["ms"] = row.value("ms").toLongLong() + it.value().toLongLong();
            row["plays"] = row.value("plays").toLongLong() + m_playCounts.value(it.key()).toLongLong();
            artists[id] = row;
        }
    }
    QVariantList topArtists, weeklyTracks, recent;
    for (const auto &artist : artists) topArtists.append(artist);
    std::sort(topArtists.begin(), topArtists.end(), [](const QVariant &a, const QVariant &b) {
        const auto x = a.toMap(), y = b.toMap();
        if (x.value("ms").toLongLong() != y.value("ms").toLongLong()) return x.value("ms").toLongLong() > y.value("ms").toLongLong();
        return x.value("name").toString() < y.value("name").toString();
    });
    for (auto it = weeklyPlays.cbegin(); it != weeklyPlays.cend(); ++it) {
        if (it.value() <= 0) continue;
        auto track = trackForUrl(it.key());
        if (track.isEmpty()) continue;
        track["weeklyPlays"] = it.value();
        track["listenedMs"] = weeklyTime.value(it.key());
        weeklyTracks.append(track);
    }
    std::sort(weeklyTracks.begin(), weeklyTracks.end(), [](const QVariant &a, const QVariant &b) {
        const auto x = a.toMap(), y = b.toMap();
        if (x.value("weeklyPlays").toInt() != y.value("weeklyPlays").toInt()) return x.value("weeklyPlays").toInt() > y.value("weeklyPlays").toInt();
        if (x.value("listenedMs").toLongLong() != y.value("listenedMs").toLongLong()) return x.value("listenedMs").toLongLong() > y.value("listenedMs").toLongLong();
        return x.value("title").toString() < y.value("title").toString();
    });
    for (const auto &entry : m_recentListening) {
        const auto event = entry.toMap();
        auto track = trackForUrl(event.value("fileUrl").toString());
        if (track.isEmpty()) continue;
        track["listenedAt"] = event.value("at");
        track["listenedMs"] = event.value("ms");
        track["qualified"] = event.value("qualified");
        recent.append(track);
        if (recent.size() >= 12) break;
    }
    return {{"weeklyMs", weeklyMs}, {"totalMs", totalMs}, {"weeklyPlays", plays}, {"days", days},
        {"weeklyTracks", weeklyTracks.mid(0, 5)}, {"topArtists", topArtists.mid(0, 5)}, {"recent", recent}};
}
