#include "tracklibrary.h"
#include "genregrouping.h"
#include "albumgrouping.h"
#include "filetags.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QDateTime>
#include <QDesktopServices>
#include <QFileInfo>
#include <QFile>
#include <QImage>
#include <QBuffer>
#include <QMediaMetaData>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>
#include <QtConcurrentRun>
#include <algorithm>

namespace {
const QStringList audioExtensions = {
    QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("ogg"),
    QStringLiteral("oga"), QStringLiteral("opus"), QStringLiteral("wav"),
    QStringLiteral("m4a"), QStringLiteral("aac"), QStringLiteral("aif"),
    QStringLiteral("aiff"), QStringLiteral("wma")
};

QString defaultMusicPath()
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    return path.isEmpty() ? QDir::home().filePath(QStringLiteral("Music")) : path;
}

QString folderArtist(const QFileInfo &info, const QStringList &folders)
{
    QString result;
    int bestRootLength = -1;
    for (const QString &folder : folders) {
        const QString relative = QDir(folder).relativeFilePath(info.absolutePath());
        if (relative == QStringLiteral("..") || relative.startsWith(QStringLiteral("../")))
            continue;
        auto parts = relative.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        static const QRegularExpression disc(QStringLiteral("^(?:cd|disc|disk)[\\s._-]*\\d+$"), QRegularExpression::CaseInsensitiveOption);
        if (!parts.isEmpty() && disc.match(parts.last()).hasMatch()) parts.removeLast();
        if (parts.size() >= 2 && folder.size() > bestRootLength) {
            result = parts[parts.size() - 2];
            bestRootLength = folder.size();
        }
    }
    return result;
}

QVariantList scanFiles(const QStringList &folders, const QStringList &files,
                       const QVariantMap &cache, const QString &artworkDirectory)
{
    QSet<QString> paths;
    QHash<QString, QString> coverKeys;
    const auto addFile = [&paths](const QString &path) {
        const QFileInfo info(path);
        if (info.isFile() && info.isReadable() && audioExtensions.contains(info.suffix().toLower()))
            paths.insert(info.canonicalFilePath());
    };
    QStringList roots = folders;
    std::sort(roots.begin(), roots.end(), [](const QString &a, const QString &b) { return a.size() < b.size(); });
    QStringList scannedRoots;
    for (const QString &folder : roots) {
        bool covered = false;
        for (const auto &root : scannedRoots)
            covered |= folder == root || folder.startsWith(root + QDir::separator());
        if (covered) continue;
        scannedRoots.append(folder);
        QDirIterator it(folder, QDir::Files | QDir::Readable | QDir::NoSymLinks,
                        QDirIterator::Subdirectories);
        while (it.hasNext())
            addFile(it.next());
    }
    for (const QString &file : files)
        addFile(file);

    QVariantList tracks;
    for (const QString &path : paths) {
        const QFileInfo info(path);
        const QString url = QUrl::fromLocalFile(path).toString();
        const qint64 modified = info.lastModified().toMSecsSinceEpoch();
        QVariantMap track = cache.value(url).toMap();
        if (track.contains(QStringLiteral("automaticAlbum"))) {
            track[QStringLiteral("album")] = track.take(QStringLiteral("automaticAlbum"));
            track[QStringLiteral("albumId")] = track.take(QStringLiteral("automaticAlbumId"));
            track[QStringLiteral("albumArtist")] = track.take(QStringLiteral("automaticAlbumArtist"));
        }
        if (track.isEmpty() || track.value(QStringLiteral("modified")).toLongLong() != modified
            || track.value(QStringLiteral("size")).toLongLong() != info.size()) {
            QString title = info.completeBaseName();
            QString artist = QStringLiteral("Unknown artist");
            const int separator = title.indexOf(QStringLiteral(" - "));
            if (separator > 0) {
                artist = title.left(separator).trimmed();
                title = title.mid(separator + 3).trimmed();
            }
            QDir albumFolder = info.dir();
            static const QRegularExpression disc(QStringLiteral("^(?:cd|disc|disk)[\\s._-]*\\d+$"), QRegularExpression::CaseInsensitiveOption);
            if (disc.match(albumFolder.dirName()).hasMatch())
                albumFolder.setPath(QFileInfo(albumFolder.absolutePath()).absolutePath());
            const QString album = folders.contains(albumFolder.absolutePath())
                ? QStringLiteral("Singles") : albumFolder.dirName();
            QString artwork;
            for (const QString &name : {QStringLiteral("cover.jpg"), QStringLiteral("cover.png"),
                                        QStringLiteral("folder.jpg"), QStringLiteral("folder.png"),
                                        QStringLiteral("Cover.jpg"), QStringLiteral("Cover.png")}) {
                const QString coverPath = info.dir().filePath(name);
                if (QFileInfo::exists(coverPath)) {
                    artwork = QUrl::fromLocalFile(coverPath).toString();
                    break;
                }
            }
            track = {{QStringLiteral("title"), title}, {QStringLiteral("artist"), artist},
                     {QStringLiteral("albumArtist"), artist},
                     {QStringLiteral("album"), album}, {QStringLiteral("genre"), QString()},
                     {QStringLiteral("duration"), 0}, {QStringLiteral("artwork"), artwork},
                     {QStringLiteral("probed"), false}};
        }
        track.insert(QStringLiteral("fileUrl"), url);
        track.insert(QStringLiteral("modified"), modified);
        track.insert(QStringLiteral("size"), info.size());
        track.insert(QStringLiteral("format"), info.suffix().toUpper());
        track.insert(QStringLiteral("folderArtist"), folderArtist(info, folders));
        if (track.value(QStringLiteral("fileTagVersion")).toInt() != FileTags::Version)
            FileTags::applyToTrack(track, FileTags::read(path, artworkDirectory));
        const QString coverPath = QUrl(track.value(QStringLiteral("artwork")).toString()).toLocalFile();
        if (!coverPath.isEmpty()) {
            auto found = coverKeys.find(coverPath);
            if (found == coverKeys.end()) {
                QFile cover(coverPath);
                QString key;
                if (cover.open(QIODevice::ReadOnly)) {
                    QCryptographicHash hash(QCryptographicHash::Sha256);
                    if (hash.addData(&cover)) key = QString::fromLatin1(hash.result().toHex());
                }
                found = coverKeys.insert(coverPath, key);
            }
            track.insert(QStringLiteral("artworkKey"), found.value());
        }
        if (track.value(QStringLiteral("metadataVersion")).toInt() != TrackListModel::MetadataCacheVersion)
            track.insert(QStringLiteral("probed"), false);
        AlbumGrouping::updateTrackOrder(track, path);
        AlbumGrouping::updateIdentity(track);
        tracks.append(track);
    }
    std::sort(tracks.begin(), tracks.end(), [](const QVariant &a, const QVariant &b) {
        const auto left = a.toMap();
        const auto right = b.toMap();
        const int order = QString::localeAwareCompare(left.value(QStringLiteral("title")).toString(),
                                                      right.value(QStringLiteral("title")).toString());
        return order == 0 ? left.value(QStringLiteral("fileUrl")).toString() < right.value(QStringLiteral("fileUrl")).toString()
                          : order < 0;
    });
    return tracks;
}
}

TrackListModel::TrackListModel(QObject *parent) : QObject(parent)
{
    initialize(false);
}

TrackListModel::TrackListModel(const QString &settingsFile, bool autoScan, QObject *parent)
    : QObject(parent), m_settings(settingsFile, QSettings::IniFormat)
{
    initialize(autoScan);
}

TrackListModel::~TrackListModel()
{
    if (m_openWatcher.isRunning()) m_openWatcher.waitForFinished();
    if (m_metadataWatcher.isRunning()) m_metadataWatcher.waitForFinished();
    if (m_batchMetadataWatcher.isRunning()) m_batchMetadataWatcher.waitForFinished();
    if (m_metadataBusy) finishMetadataOperation(m_batchOperation);
    saveSettings();
}

void TrackListModel::initialize(bool autoScan)
{
    initializeMetadataEditing();
    m_songs.setSourceModel(&m_songEntries);
    m_songs.setSortKey(QStringLiteral("title"));
    m_albums.setSourceModel(&m_albumEntries);
    m_artists.setSourceModel(&m_artistEntries);
    m_genres.setSourceModel(&m_genreEntries);
    m_playlists.setSourceModel(&m_playlistEntries);
    m_mostPlayed.setSourceModel(&m_songEntries);
    m_mostPlayed.setPlayedOnly(true);
    m_mostPlayed.setSortKey(QStringLiteral("plays"));
    m_database = std::make_unique<LibraryDatabase>(m_settings.fileName() + ".library.sqlite");
    connect(m_database.get(), &LibraryDatabase::changed, this, [this] { m_trackLookup.clear(); });
    m_dashboardTimer.setSingleShot(true); m_dashboardTimer.setInterval(1000);
    connect(&m_dashboardTimer, &QTimer::timeout, this, &TrackListModel::historyChanged);
    if (m_database->valid() && !m_settings.value("library/databaseReady").toBool()) m_database->clear();
    if (m_database->valid()) {
        m_songs.setDatabase(m_database.get(), "song"); m_mostPlayed.setDatabase(m_database.get(), "song");
        m_albums.setDatabase(m_database.get(), "album"); m_artists.setDatabase(m_database.get(), "artist"); m_playlists.setDatabase(m_database.get(), "playlist");
        m_genres.setDatabase(m_database.get(), "genre");
    }
    m_releaseTimer.setSingleShot(true); m_releaseTimer.setInterval(1000);
    connect(&m_releaseTimer, &QTimer::timeout, this, &TrackListModel::releaseWorkingSet);
    m_playCounts = m_settings.value(QStringLiteral("library/playCounts")).toMap();
    m_addedAt = m_settings.value(QStringLiteral("library/addedAt")).toMap();
    m_albumOverrides = m_settings.value(QStringLiteral("library/albumOverrides")).toMap();
    m_albumUndo = m_settings.value(QStringLiteral("library/albumUndo")).toList();
    m_dailyListening = m_settings.value(QStringLiteral("history/days")).toMap();
    m_listeningTotals = m_settings.value(QStringLiteral("history/totals")).toMap();
    m_recentListening = m_settings.value(QStringLiteral("history/recent")).toList();
    const auto queueFile = m_settings.fileName() + QStringLiteral(".queues.json");
    m_queueStore = std::make_unique<QueueStore>(queueFile, m_settings.value("player/session").toMap(), !m_settings.value("player/queueStoreInitialized").toBool());
    m_settings.setValue("player/queueStoreInitialized", true);
    const auto savedHistory = m_queueStore->history();
    if (savedHistory.contains("playCounts")) {
        m_playCounts = savedHistory.value("playCounts").toMap();
        m_dailyListening = savedHistory.value("days").toMap();
        m_listeningTotals = savedHistory.value("totals").toMap();
        m_recentListening = savedHistory.value("recent").toList();
    }
    connect(m_queueStore.get(), &QueueStore::changed, this, &TrackListModel::savedQueuesChanged);
    connect(m_queueStore.get(), &QueueStore::error, this, &TrackListModel::importError);
    m_historySaveTimer.setSingleShot(true); m_historySaveTimer.setInterval(5000);
    connect(&m_historySaveTimer, &QTimer::timeout, this, &TrackListModel::saveListeningState);
    m_playlistData = m_settings.value(QStringLiteral("library/playlists")).toList();
    m_folders = m_settings.value(QStringLiteral("library/folders"), QStringList{defaultMusicPath()}).toStringList();
    m_files = m_settings.value(QStringLiteral("library/files")).toStringList();
    m_favorites = m_settings.value(QStringLiteral("library/favorites")).toStringList();
    if (!m_database->hasSnapshot()) m_metadataCache = m_settings.value(QStringLiteral("library/metadata")).toMap();
    m_settings.remove(QStringLiteral("library/analysis")); // Retire the old audio-analysis cache.
    m_volume = qBound(0.0, m_settings.value(QStringLiteral("player/volume"), 0.65).toDouble(), 1.0);
    m_probeTimeout.setSingleShot(true);
    m_probeTimeout.setInterval(2500);
    m_nextProbe.setSingleShot(true);
    m_nextProbe.setInterval(20);
    m_tracksNotification.setSingleShot(true);
    m_tracksNotification.setInterval(350);
    m_settingsTimer.setSingleShot(true);
    m_settingsTimer.setInterval(10000);
    connect(&m_settingsTimer, &QTimer::timeout, this, &TrackListModel::saveSettings);
    connect(&m_tracksNotification, &QTimer::timeout, this, &TrackListModel::publishUpdates);
    connect(&m_coverWatcher, &QFutureWatcher<QVariantMap>::finished, this, [this] {
        completeProbe(m_coverWatcher.result(), m_coverGeneration, m_coverIndex);
    });
    connect(&m_nextProbe, &QTimer::timeout, this, &TrackListModel::probeNext);
    connect(&m_probeTimeout, &QTimer::timeout, this, &TrackListModel::finishProbe);
    connect(&m_probe, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::InvalidMedia)
            m_probeTimeout.start(0);
    });
    connect(&m_scanWatcher, &QFutureWatcher<QVariantList>::finished, this, [this] {
        if (m_rescanPending) {
            m_rescanPending = false;
            m_scanning = false;
            rescan();
            return;
        }
        m_tracks = m_scanWatcher.result(); m_workingSetLoaded = true;
        m_trackRows.clear();
        int row = 0;
        m_processed = 0;
        m_failed = 0;
        for (QVariant &entry : m_tracks) {
            QVariantMap track = entry.toMap();
            track.insert(QStringLiteral("favorite"), m_favorites.contains(track.value(QStringLiteral("fileUrl")).toString()));
            m_trackRows.insert(track.value(QStringLiteral("fileUrl")).toString(), row++);
            if (track.value(QStringLiteral("probed")).toBool()) {
                ++m_processed;
                if (!track.value(QStringLiteral("metadataError")).toString().isEmpty()) ++m_failed;
            }
            entry = track;
        }
        m_scanning = false;
        m_indexing = m_processed < m_tracks.size();
        publishUpdates();
        if (!m_libraryReady) { m_libraryReady = true; emit libraryReadyChanged(); }
        emit scanningChanged();
        m_probeIndex = -1;
        m_nextProbe.start();
        saveSettings();
    });
    connect(this, &TrackListModel::scanningChanged, this, &TrackListModel::processOpenRequests);
    connect(this, &TrackListModel::metadataBusyChanged, this, &TrackListModel::processOpenRequests);
    connect(&m_openWatcher, &QFutureWatcher<QVariantList>::finished, this, [this] {
        const auto results = m_openWatcher.result();
        ensureTracksLoaded();
        m_nextProbe.stop(); m_probeTimeout.stop(); ++m_generation; m_probe.setSource(QUrl()); m_probeIndex = -1;
        for (const auto &entry : results) {
            auto track = entry.toMap(); const auto url = track.value("fileUrl").toString();
            track["favorite"] = m_favorites.contains(url);
            if (track.value("duration").toLongLong() > 0 && track.value("fileTagVersion").toInt() == FileTags::Version) {
                track["probed"] = true; track["metadataVersion"] = MetadataCacheVersion; track["metadataError"] = QString();
            }
            const auto row = m_trackRows.value(url, -1);
            if (row >= 0) m_tracks[row] = track;
            else { m_trackRows[url] = m_tracks.size(); m_tracks.append(track); }
            m_metadataCache[url] = track;
        }
        m_processed = 0; m_failed = 0;
        for (const auto &entry : m_tracks) {
            const auto track = entry.toMap(); m_processed += track.value("probed").toBool();
            m_failed += !track.value("metadataError").toString().isEmpty();
        }
        m_indexing = m_processed < m_tracks.size();
        publishUpdates(); saveSettings();
        const auto request = m_activeOpenRequest; m_activeOpenRequest.clear();
        const auto tracks = requestedTracks(request);
        if (!tracks.isEmpty()) emit filesOpened(tracks, request.value("name").toString(), request.value("append").toBool());
        else emit importError(tr("No playable songs were found in the selected locations."));
        emit openingFilesChanged();
        if (m_rescanPending) { m_rescanPending = false; rescan(); }
        else { if (m_indexing) m_nextProbe.start(); processOpenRequests(); }
    });
    // Restore only the last known library. Opening the app must not walk music
    // folders, stat every song, read tags, or schedule background probes.
    if (m_database->hasSnapshot()) {
        m_databaseSynchronized = true;
        m_workingSetLoaded = false; m_databaseTrackCount = m_database->count("song");
        m_failed = m_database->failedCount(); m_processed = m_databaseTrackCount;
        const auto counts = m_database->playCounts();
        for (auto it = counts.cbegin(); it != counts.cend(); ++it) m_playCounts[it.key()] = qMax(m_playCounts.value(it.key()).toLongLong(), it.value().toLongLong());
        const auto artists = m_database->value("artistSeparation").toMap();
        m_splitArtists = artists.value("enabled").toBool(); m_artistSeparators = artists.value("separators", ",;|").toString();
        const auto aliases = m_database->value("albumAliases").toMap();
        for (auto it = aliases.cbegin(); it != aliases.cend(); ++it) m_albumAliases[it.key()] = it.value().toString();
        const auto shelf = m_database->ids("album", {QString(), QString(), QString(), "name"}).mid(0, 4);
        for (const auto &id : shelf) m_shelfAlbums.append(m_database->entry("album", id));
    } else if (m_settings.contains(QStringLiteral("library/snapshot"))) {
        m_tracks = m_settings.value(QStringLiteral("library/snapshot")).toList();
    } else {
        // Migrate libraries saved before snapshots existed, without filesystem I/O.
        for (auto it = m_metadataCache.cbegin(); it != m_metadataCache.cend(); ++it) {
            const QString path = QUrl(it.key()).toLocalFile();
            bool included = m_files.contains(path);
            for (const auto &folder : m_folders)
                included |= path.startsWith(QDir::cleanPath(folder) + QDir::separator());
            if (included) m_tracks.append(it.value());
        }
    }
    for (int row = 0; row < m_tracks.size(); ++row) {
        auto track = m_tracks[row].toMap();
        const QString url = track.value(QStringLiteral("fileUrl")).toString();
        track[QStringLiteral("favorite")] = m_favorites.contains(url);
        m_trackRows.insert(url, row);
        m_processed += track.value(QStringLiteral("probed")).toBool();
        m_failed += !track.value(QStringLiteral("metadataError")).toString().isEmpty();
        m_tracks[row] = track;
    }
    if (m_workingSetLoaded) publishUpdates();
    initializeCollectionTools();
    m_libraryReady = !autoScan;
    if (autoScan) rescan();
    else QTimer::singleShot(0, this, [this] { emit libraryReadyChanged(); });
}

int TrackListModel::favoriteCount() const
{
    if (!m_workingSetLoaded && m_database->valid()) return m_database->favoriteCount();
    int count = 0;
    for (const auto &entry : m_tracks)
        count += entry.toMap().value(QStringLiteral("favorite")).toBool();
    return count;
}

QVariantList TrackListModel::shelfAlbums() const { return m_shelfAlbums; }
QVariantList TrackListModel::tracks() const
{
    return m_workingSetLoaded ? m_tracks : m_database->all("song");
}
void TrackListModel::ensureTracksLoaded()
{
    m_releaseTimer.stop();
    if (m_workingSetLoaded || !m_database->valid()) return;
    m_tracks = m_database->all("song"); m_workingSetLoaded = true; m_trackRows.clear();
    for (int row = 0; row < m_tracks.size(); ++row) m_trackRows[m_tracks[row].toMap().value("fileUrl").toString()] = row;
    emit residentTracksChanged();
}
void TrackListModel::releaseWorkingSet()
{
    if (!m_database->valid() || !m_database->hasSnapshot() || !m_databaseSynchronized) return;
    if (scanning() || openingFiles() || m_metadataBusy || m_resumeIndexing || m_nextProbe.isActive() || m_tracksNotification.isActive() || m_coverWatcher.isRunning()) { m_releaseTimer.start(); return; }
    m_tracks.clear(); m_trackRows.clear(); m_metadataCache.clear(); m_addedAt.clear(); m_workingSetLoaded = false;
    emit residentTracksChanged();
}

QString TrackListModel::scanStatus() const
{
    if (m_scanning) return tr("Finding music files…");
    if (m_indexing) return tr("Reading tags: %1 / %2").arg(m_processed).arg(m_tracks.size());
    if (m_failed == 1) return tr("Library ready · 1 file without readable tags");
    if (m_failed > 1) return tr("Library ready · %1 files without readable tags").arg(m_failed);
    return tr("Library ready · scan manually for changes");
}

QVariantMap TrackListModel::trackForUrl(const QString &fileUrl) const
{
    if (!m_workingSetLoaded && m_database->valid()) {
        const auto url = QUrl(fileUrl).toString();
        if (const auto *cached = m_trackLookup.object(url)) return *cached;
        const auto entry = m_database->entry("song", url);
        if (!entry.isEmpty()) m_trackLookup.insert(url, new QVariantMap(entry));
        return entry;
    }
    const int row = m_trackRows.value(QUrl(fileUrl).toString(), -1);
    return row >= 0 ? m_tracks[row].toMap() : QVariantMap();
}

QVariantList TrackListModel::tracksForGroup(const QString &kind, const QString &id) const
{
    if (kind == QStringLiteral("folder") || kind == QStringLiteral("customSmart")) return collectionTracks(kind, id);
    if (kind == QStringLiteral("playlist")) {
        QVariantList tracks;
        for (const auto &entry : m_playlistData) {
            const auto playlist = entry.toMap();
            if (playlist.value(QStringLiteral("id")).toString() != id) continue;
            for (const auto &url : playlist.value(QStringLiteral("urls")).toStringList()) {
                const auto track = trackForUrl(url);
                if (!track.isEmpty()) tracks.append(track);
            }
        }
        return tracks;
    }
    const QString key = kind == QStringLiteral("album") ? QStringLiteral("albumId") : QStringLiteral("artistId");
    QVariantList tracks;
    const QString resolved = kind == QStringLiteral("album") ? m_albumAliases.value(id, id) : id;
    const auto candidates = !m_workingSetLoaded && m_database->valid()
        ? m_database->page("song", m_database->ids("song", {QString(), kind, resolved, "track"}), 0, trackCount()) : m_tracks;
    for (const auto &entry : candidates) {
        const auto track = entry.toMap();
        const bool matches = kind == QStringLiteral("genre") ? GenreGrouping::idFor(track.value("genre").toString()) == resolved
            : kind == QStringLiteral("artist") ? track.value(QStringLiteral("artistIds")).toStringList().contains(resolved)
            : track.value(key).toString() == resolved;
        if (matches) tracks.append(entry);
    }
    std::sort(tracks.begin(), tracks.end(), AlbumGrouping::trackLessThan);
    return tracks;
}

QVariantMap TrackListModel::genreForTag(const QString &tag) const
{
    return {{"id", GenreGrouping::idFor(tag)}, {"name", GenreGrouping::nameFor(tag)}};
}

void TrackListModel::setArtistSeparation(bool enabled, const QString &separators)
{
    if (enabled == m_splitArtists && separators == m_artistSeparators) return;
    m_splitArtists = enabled; m_artistSeparators = separators;
    publishUpdates();
}

QString TrackListModel::createPlaylist(const QString &name, const QString &fileUrl)
{
    return createPlaylistFromUrls(name, fileUrl.isEmpty() ? QStringList() : QStringList{fileUrl});
}

QString TrackListModel::createPlaylistFromUrls(const QString &name, const QStringList &fileUrls)
{
    const auto before = m_playlistData;
    const QString title = name.simplified();
    if (title.isEmpty() || title.size() > 80) {
        emit importError(tr("Give your playlist a name of 1–80 characters."));
        return {};
    }
    for (const auto &entry : m_playlistData) {
        if (entry.toMap().value(QStringLiteral("name")).toString().compare(title, Qt::CaseInsensitive) == 0) {
            emit importError(tr("A playlist with that name already exists."));
            return {};
        }
    }
    QStringList urls;
    QSet<QString> seen;
    for (const auto &input : fileUrls) {
        const auto url = QUrl(input).toString();
        if (seen.contains(url)) continue;
        if (trackForUrl(url).isEmpty()) { emit importError(tr("A song is no longer in your library.")); return {}; }
        seen.insert(url); urls.append(url);
    }
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_playlistData.append(QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("name"), title},
        {QStringLiteral("urls"), urls}});
    recordPlaylistEdit(before, tr("Create playlist"));
    saveSettings();
    publishUpdates();
    return id;
}

bool TrackListModel::addToPlaylist(const QString &id, const QString &fileUrl)
{
    const auto before = m_playlistData;
    if (trackForUrl(fileUrl).isEmpty()) return false;
    for (auto &entry : m_playlistData) {
        auto playlist = entry.toMap();
        if (playlist.value(QStringLiteral("id")).toString() != id) continue;
        auto urls = playlist.value(QStringLiteral("urls")).toStringList();
        if (urls.contains(fileUrl)) return true;
        urls.append(fileUrl);
        playlist.insert(QStringLiteral("urls"), urls);
        entry = playlist;
        recordPlaylistEdit(before, tr("Add song to playlist"));
        saveSettings();
        publishUpdates();
        return true;
    }
    return false;
}

int TrackListModel::addTracksToPlaylist(const QString &id, const QStringList &urls)
{
    const auto before = m_playlistData;
    for (auto &entry : m_playlistData) {
        auto playlist = entry.toMap();
        if (playlist.value("id").toString() != id) continue;
        auto members = playlist.value("urls").toStringList();
        int added = 0;
        for (const auto &url : urls) {
            if (trackForUrl(url).isEmpty() || members.contains(url)) continue;
            members.append(url); ++added;
        }
        playlist["urls"] = members; entry = playlist;
        if (added) { recordPlaylistEdit(before, tr("Add songs to playlist")); publishUpdates(); saveSettings(); }
        return added;
    }
    return 0;
}

void TrackListModel::setTracksFavorite(const QStringList &urls, bool favorite)
{
    if (!m_workingSetLoaded && m_database->valid()) {
        for (const auto &url : urls) {
            auto track = trackForUrl(url); if (track.isEmpty()) continue;
            track["favorite"] = favorite; m_favorites.removeAll(url); if (favorite) m_favorites.append(url);
            m_database->updateSong(url, track);
        }
        m_settings.setValue("library/favorites", m_favorites); emit tracksChanged(); return;
    }
    for (const auto &url : urls) {
        const int row = m_trackRows.value(url, -1);
        if (row < 0) continue;
        auto track = m_tracks[row].toMap(); track["favorite"] = favorite; m_tracks[row] = track;
        m_favorites.removeAll(url);
        if (favorite) m_favorites.append(url);
    }
    publishUpdates(); saveSettings();
}

bool TrackListModel::removeFromPlaylist(const QString &id, const QString &fileUrl)
{
    const auto before = m_playlistData;
    for (auto &entry : m_playlistData) {
        auto playlist = entry.toMap();
        if (playlist.value(QStringLiteral("id")).toString() != id) continue;
        auto urls = playlist.value(QStringLiteral("urls")).toStringList();
        if (!urls.removeAll(fileUrl)) return false;
        playlist.insert(QStringLiteral("urls"), urls);
        entry = playlist;
        recordPlaylistEdit(before, tr("Remove song from playlist"));
        saveSettings();
        publishUpdates();
        return true;
    }
    return false;
}

bool TrackListModel::deletePlaylist(const QString &id)
{
    const auto before = m_playlistData;
    for (int row = 0; row < m_playlistData.size(); ++row) {
        if (m_playlistData[row].toMap().value(QStringLiteral("id")).toString() != id) continue;
        m_playlistData.removeAt(row);
        recordPlaylistEdit(before, tr("Delete playlist"));
        saveSettings();
        publishUpdates();
        return true;
    }
    return false;
}

int TrackListModel::removeTracksFromPlaylist(const QString &id, const QStringList &urls)
{
    const auto before = m_playlistData;
    for (auto &entry : m_playlistData) {
        auto playlist = entry.toMap();
        if (playlist.value("id").toString() != id) continue;
        auto members = playlist.value("urls").toStringList();
        int removed = 0;
        for (const auto &url : urls) removed += members.removeAll(url);
        playlist["urls"] = members; entry = playlist;
        if (removed) { recordPlaylistEdit(before, tr("Remove songs from playlist")); publishUpdates(); saveSettings(); }
        return removed;
    }
    return 0;
}

QVariantMap TrackListModel::trackInfo(const QString &fileUrl) const
{
    auto track = trackForUrl(fileUrl);
    if (track.isEmpty()) return {};
    const QFileInfo file(QUrl(fileUrl).toLocalFile());
    track.insert(QStringLiteral("path"), file.absoluteFilePath());
    track.insert(QStringLiteral("sizeText"), tr("%1 MB").arg(file.size() / (1024.0 * 1024.0), 0, 'f', 2));
    return track;
}

bool TrackListModel::revealTrack(const QString &fileUrl) const
{
    if (trackForUrl(fileUrl).isEmpty()) return false;
    return QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(QUrl(fileUrl).toLocalFile()).absolutePath()));
}

bool TrackListModel::trashTrack(const QString &fileUrl)
{
    const auto result = trashTracks({fileUrl});
    const auto errors = result.value("errors").toList();
    if (!errors.isEmpty()) emit importError(errors.first().toMap().value("error").toString());
    return result.value("removed").toStringList().contains(QUrl(fileUrl).toString());
}

QVariantMap TrackListModel::trashTracks(const QStringList &urls)
{
    ensureTracksLoaded();
    QStringList removed;
    QVariantList errors;
    QSet<QString> seen;
    if (!m_metadataBusy && !m_scanning) suspendMetadataProbes();
    for (const auto &input : urls) {
        const QString url = QUrl(input).toString();
        if (seen.contains(url)) continue;
        seen.insert(url);
        QString error;
        if (m_metadataBusy) error = tr("Wait for metadata editing to finish before deleting songs.");
        else if (m_scanning) error = tr("Wait for file discovery to finish before deleting songs.");
        else if (trackForUrl(url).isEmpty()) error = tr("This song is no longer in your library.");
        else {
            QFile file(QUrl(url).toLocalFile());
            if (!QFileInfo(file).isFile()) error = tr("This song's file could not be found.");
            else if (!file.moveToTrash()) error = tr("Couldn't move the song to Trash: %1").arg(file.errorString());
        }
        if (!error.isEmpty()) { errors.append(QVariantMap{{"fileUrl", url}, {"error", error}}); continue; }
        removed.append(url);
        m_files.removeAll(QUrl(url).toLocalFile());
        m_favorites.removeAll(url);
        m_metadataCache.remove(url);
        m_playCounts.remove(url);
        m_albumOverrides.remove(url);
        m_addedAt.remove(url);
        for (auto &entry : m_albumUndo) { auto edits = entry.toMap(); edits.remove(url); entry = edits; }
        if (m_listening.url() == url) m_listening.begin(QString());
    }
    if (!removed.isEmpty()) {
        m_tracks.erase(std::remove_if(m_tracks.begin(), m_tracks.end(), [&removed](const auto &entry) {
            return removed.contains(entry.toMap().value("fileUrl").toString());
        }), m_tracks.end());
        m_trackRows.clear();
        for (int row = 0; row < m_tracks.size(); ++row)
            m_trackRows.insert(m_tracks[row].toMap().value("fileUrl").toString(), row);
        for (auto &entry : m_playlistData) {
            auto playlist = entry.toMap();
            auto members = playlist.value("urls").toStringList();
            for (const auto &url : removed) members.removeAll(url);
            playlist["urls"] = members; entry = playlist;
        }
        publishUpdates(); saveSettings();
        emit albumEditsChanged();
        for (const auto &url : removed) emit trackRemoved(url);
    }
    if (!m_metadataBusy && !m_scanning) resumeMetadataProbes();
    return {{"removed", removed}, {"errors", errors}};
}
void TrackListModel::beginPlayback(const QString &fileUrl)
{
    const QString url = fileUrl.isEmpty() ? QString() : QUrl(fileUrl).toString();
    if (!url.isEmpty() && trackForUrl(url).isEmpty()) return;
    m_listening.begin(url);
    m_listeningClock.start();
    m_historyEventStarted = false;
}

void TrackListModel::seekPlayback(const QString &fileUrl, qint64 position)
{
    if (QUrl(fileUrl).toString() == m_listening.url() && m_listeningClock.isValid())
        m_listening.seek(position, m_listeningClock.elapsed());
}

void TrackListModel::recordPlaybackProgress(const QString &fileUrl, qint64 position, qint64 duration, bool playing)
{
    const QString url = QUrl(fileUrl).toString();
    if (url.isEmpty() || url != m_listening.url() || !m_listeningClock.isValid()) return;
    const bool qualified = m_listening.observe(position, duration, playing, m_listeningClock.elapsed());
    collectListening(url, m_listening.lastAudibleMs(), qualified);
    if (!qualified) return;
    if (trackForUrl(url).isEmpty()) return;
    m_playCounts.insert(url, m_playCounts.value(url).toLongLong() + 1);
    publishPlayCount(url);
    saveListeningState();
}

void TrackListModel::publishUpdates()
{
    ensureTracksLoaded();
    m_tracksNotification.stop();
    for (auto &entry : m_tracks) {
        auto track = entry.toMap();
        if (track.contains(QStringLiteral("automaticAlbum"))) {
            track[QStringLiteral("album")] = track.take(QStringLiteral("automaticAlbum"));
            track[QStringLiteral("albumId")] = track.take(QStringLiteral("automaticAlbumId"));
            track[QStringLiteral("albumArtist")] = track.take(QStringLiteral("automaticAlbumArtist"));
        }
        track.remove(QStringLiteral("manualAlbum"));
        entry = track;
    }
    auto aliases = AlbumGrouping::reconcile(m_tracks);
    for (auto it = m_albumAliases.cbegin(); it != m_albumAliases.cend(); ++it)
        if (!aliases.contains(it.key())) aliases.insert(it.key(), aliases.value(it.value(), it.value()));
    const auto previousAliases = m_albumAliases;
    m_albumAliases = aliases;
    applyAlbumOverrides();
    QHash<QString, QStringList> membership;
    QHash<QString, QVariantMap> positions;
    for (const auto &entry : m_playlistData) {
        const auto playlist = entry.toMap();
        const auto urls = playlist.value(QStringLiteral("urls")).toStringList();
        const auto id = playlist.value(QStringLiteral("id")).toString();
        for (int i = 0; i < urls.size(); ++i) {
            membership[urls[i]].append(id);
            positions[urls[i]].insert(id, i);
        }
    }
    for (auto &entry : m_tracks) {
        auto track = entry.toMap();
        const QString url = track.value(QStringLiteral("fileUrl")).toString();
        if (!m_addedAt.contains(url)) m_addedAt.insert(url, track.value("addedAt", QDateTime::currentMSecsSinceEpoch()));
        track.insert(QStringLiteral("addedAt"), m_addedAt.value(url));
        track.insert(QStringLiteral("playlistOrder"), positions.value(url));
        track.insert(QStringLiteral("playlistIds"), membership.value(track.value(QStringLiteral("fileUrl")).toString()));
        track.insert(QStringLiteral("playCount"), m_playCounts.value(track.value(QStringLiteral("fileUrl")).toString(), 0).toLongLong());
        const auto credits = AlbumGrouping::artistCredits(track.value(QStringLiteral("artist")).toString(), m_splitArtists, m_artistSeparators);
        QStringList artistIds;
        for (const auto &credit : credits) artistIds.append(credit.toMap().value(QStringLiteral("id")).toString());
        track.insert(QStringLiteral("artistCredits"), credits);
        track.insert(QStringLiteral("artistIds"), artistIds);
        for (const auto &field : {"bpm", "keyId", "musicalKey", "camelot", "beatOffset", "bpmConfidence",
                 "keyConfidence", "analysisNote", "analyzed", "manual", "tagBpm", "tagKeyId", "tagBeatOffset"})
            track.remove(field);
        entry = track;
    }
    if (!m_database->valid()) m_songEntries.synchronize(m_tracks, QStringLiteral("fileUrl"));
    QMap<QString, QVariantMap> albums, artists;
    for (const auto &entry : m_tracks) {
        const auto track = entry.toMap();
        QVariantList groupsForTrack{QVariantMap{{QStringLiteral("id"), track.value(QStringLiteral("albumId"))},
            {QStringLiteral("name"), track.value(QStringLiteral("album"))}, {QStringLiteral("album"), true}}};
        groupsForTrack += track.value(QStringLiteral("artistCredits")).toList();
        for (const auto &creditEntry : groupsForTrack) {
            const auto credit = creditEntry.toMap();
            const QString kind = credit.value(QStringLiteral("album")).toBool() ? QStringLiteral("album") : QStringLiteral("artist");
            auto &groups = kind == QStringLiteral("album") ? albums : artists;
            const QString id = credit.value(QStringLiteral("id")).toString();
            auto found = groups.find(id);
            if (found == groups.end()) {
                found = groups.insert(id, {{QStringLiteral("id"), id},
                    {QStringLiteral("name"), credit.value(QStringLiteral("name"))},
                    {QStringLiteral("subtitle"), kind == QStringLiteral("album") ? track.value(QStringLiteral("albumArtist")) : QVariant(QString())},
                    {QStringLiteral("artwork"), track.value(QStringLiteral("artwork"))},
                    {QStringLiteral("artStyle"), track.value(QStringLiteral("artStyle"))},
                    {QStringLiteral("trackCount"), 0}});
            }
            auto &group = found.value();
            group[QStringLiteral("trackCount")] = group.value(QStringLiteral("trackCount")).toInt() + 1;
            group[QStringLiteral("duration")] = group.value(QStringLiteral("duration")).toLongLong()
                + qMax<qint64>(0, track.value(QStringLiteral("duration")).toLongLong());
            group[QStringLiteral("addedAt")] = qMax(group.value(QStringLiteral("addedAt")).toLongLong(),
                track.value(QStringLiteral("addedAt")).toLongLong());
            if (group.value(QStringLiteral("artwork")).toString().isEmpty())
                group[QStringLiteral("artwork")] = track.value(QStringLiteral("artwork"));
        }
    }
    QVariantList albumRows, artistRows;
    for (const auto &group : albums) albumRows.append(group);
    for (const auto &group : artists) artistRows.append(group);
    if (!m_database->valid()) {
        m_albumEntries.synchronize(albumRows, QStringLiteral("id"));
        m_artistEntries.synchronize(artistRows, QStringLiteral("id"));
        GenreGrouping::Builder genres;
        for (const auto &track : m_tracks) genres.add(track.toMap());
        m_genreEntries.synchronize(genres.entries(), QStringLiteral("id"));
    }
    QVariantList playlistRows;
    for (const auto &entry : m_playlistData) {
        const auto playlist = entry.toMap();
        const auto tracks = tracksForGroup(QStringLiteral("playlist"), playlist.value(QStringLiteral("id")).toString());
        const auto cover = tracks.isEmpty() ? QVariantMap() : tracks.first().toMap();
        playlistRows.append(QVariantMap{{QStringLiteral("id"), playlist.value(QStringLiteral("id"))},
            {QStringLiteral("name"), playlist.value(QStringLiteral("name"))},
            {QStringLiteral("subtitle"), tr("Your playlist")}, {QStringLiteral("trackCount"), tracks.size()},
            {QStringLiteral("artwork"), cover.value(QStringLiteral("artwork"), QString())},
            {QStringLiteral("artStyle"), cover.value(QStringLiteral("artStyle"), 0)}});
    }
    if (m_database->valid()) {
        m_databaseSynchronized = m_database->replace(m_tracks, albumRows, artistRows, playlistRows);
        if (m_databaseSynchronized) {
            m_databaseTrackCount = m_tracks.size();
            m_settings.setValue("library/databaseReady", true);
            m_settings.remove("library/snapshot"); m_settings.remove("library/metadata"); m_settings.remove("library/addedAt");
            QVariantMap aliasesMap; for (auto it = m_albumAliases.cbegin(); it != m_albumAliases.cend(); ++it) aliasesMap[it.key()] = it.value();
            m_database->setValue("albumAliases", aliasesMap);
            m_database->setValue("artistSeparation", QVariantMap{{"enabled", m_splitArtists}, {"separators", m_artistSeparators}});
        } else emit importError(tr("Couldn't save the library database: %1").arg(m_database->error()));
    } else m_playlistEntries.synchronize(playlistRows, QStringLiteral("id"));
    auto collator = libraryCollator();
    std::sort(albumRows.begin(), albumRows.end(), [&collator](const auto &a, const auto &b) {
        return collator.compare(a.toMap().value(QStringLiteral("name")).toString(), b.toMap().value(QStringLiteral("name")).toString()) < 0;
    });
    m_shelfAlbums = albumRows.mid(0, 4);
    for (auto it = aliases.cbegin(); it != aliases.cend(); ++it)
        if (it.key() != it.value() && previousAliases.value(it.key(), it.key()) != it.value())
            emit albumMerged(it.key(), it.value());
    emit tracksChanged();
    emit scanProgressChanged();
    emit historyChanged();
    m_releaseTimer.start();
}

QUrl TrackListModel::musicFolder() const
{
    return QUrl::fromLocalFile(defaultMusicPath());
}

void TrackListModel::setVolume(qreal value)
{
    value = qBound(0.0, value, 1.0);
    if (qFuzzyCompare(value, m_volume))
        return;
    m_volume = value;
    m_settings.setValue(QStringLiteral("player/volume"), value);
    emit volumeChanged();
}

void TrackListModel::rescan()
{
    if (m_openWatcher.isRunning() || !m_activeOpenRequest.isEmpty()) { m_rescanPending = true; return; }
    if (m_metadataSaving) { m_rescanPending = true; return; }
    if (m_scanning) {
        m_rescanPending = true;
        return;
    }
    m_probeTimeout.stop();
    m_nextProbe.stop();
    m_tracksNotification.stop();
    m_probeIndex = -1;
    ++m_generation;
    m_indexing = false;
    m_finishingProbe = false;
    m_probe.setSource(QUrl());
    m_scanning = true;
    emit scanningChanged();
    emit scanProgressChanged();
    const QString artworkDirectory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/artwork");
    ensureTracksLoaded();
    if (m_metadataCache.isEmpty()) for (const auto &entry : m_tracks) m_metadataCache[entry.toMap().value("fileUrl").toString()] = entry;
    m_scanWatcher.setFuture(QtConcurrent::run(scanFiles, m_folders, m_files, m_metadataCache, artworkDirectory));
}

void TrackListModel::addFolder(const QUrl &folder)
{
    if (!folder.isLocalFile() || !QFileInfo(folder.toLocalFile()).isDir()) {
        emit importError(tr("Choose a local music folder that exists."));
        return;
    }
    const QString path = QFileInfo(folder.toLocalFile()).canonicalFilePath();
    if (!m_folders.contains(path)) {
        m_folders.append(path);
        emit foldersChanged();
        saveSettings();
    }
    rescan();
}

void TrackListModel::addFiles(const QList<QUrl> &files)
{
    bool valid = false;
    for (const QUrl &url : files) {
        const QFileInfo info(url.toLocalFile());
        if (!url.isLocalFile() || !info.exists())
            continue;
        if (info.isDir()) {
            const QString path = info.canonicalFilePath();
            if (!m_folders.contains(path))
                m_folders.append(path);
            valid = true;
        } else if (info.isReadable() && audioExtensions.contains(info.suffix().toLower())) {
            const QString path = info.canonicalFilePath();
            if (!m_files.contains(path))
                m_files.append(path);
            valid = true;
        }
    }
    if (!valid) {
        emit importError(tr("No supported audio files found. Try MP3, FLAC, WAV, OGG, or M4A."));
        return;
    }
    emit foldersChanged();
    saveSettings();
    rescan();
}

void TrackListModel::openFiles(const QList<QUrl> &urls, bool append)
{
    QVariantList locations; QSet<QString> seen;
    for (const auto &url : urls) {
        if (!url.isLocalFile()) continue;
        const QFileInfo info(url.toLocalFile());
        if (!info.exists() || (!info.isDir() && (!info.isReadable() || !audioExtensions.contains(info.suffix().toLower())))) continue;
        const auto path = info.canonicalFilePath(), canonical = QUrl::fromLocalFile(path).toString();
        if (seen.contains(canonical)) continue;
        seen.insert(canonical); locations.append(QVariantMap{{"url", canonical}, {"path", path}, {"folder", info.isDir()}});
    }
    if (locations.isEmpty()) { emit importError(tr("Choose readable local audio files or music folders.")); return; }
    auto name = tr("Opened files");
    if (locations.size() == 1) {
        const auto location = locations.first().toMap(); const auto track = trackForUrl(location.value("url").toString());
        name = track.value("title", QFileInfo(location.value("path").toString()).fileName()).toString();
    }
    m_openFileRequests.append({{"locations", locations}, {"name", name}, {"append", append}});
    emit openingFilesChanged(); processOpenRequests();
}

QVariantList TrackListModel::requestedTracks(const QVariantMap &request) const
{
    QVariantList result; QSet<QString> seen;
    for (const auto &entry : request.value("locations").toList()) {
        const auto location = entry.toMap();
        const auto tracks = location.value("folder").toBool() ? collectionTracks("folder", location.value("path").toString())
            : QVariantList{trackForUrl(location.value("url").toString())};
        for (const auto &value : tracks) {
            const auto track = value.toMap(); const auto url = track.value("fileUrl").toString();
            const QFileInfo file(QUrl(url).toLocalFile());
            if (!url.isEmpty() && !seen.contains(url) && file.isFile() && file.isReadable()) { seen.insert(url); result.append(track); }
        }
    }
    return result;
}

void TrackListModel::processOpenRequests()
{
    if (m_scanning || m_metadataBusy || m_openWatcher.isRunning() || !m_activeOpenRequest.isEmpty() || m_openFileRequests.isEmpty()) return;
    const auto request = m_openFileRequests.takeFirst();
    QStringList folders, files; QVariantMap cache;
    for (const auto &entry : request.value("locations").toList()) {
        const auto location = entry.toMap(); const auto path = location.value("path").toString();
        if (location.value("folder").toBool()) { folders.append(path); if (!m_folders.contains(path)) m_folders.append(path); }
        else if (trackForUrl(location.value("url").toString()).isEmpty()) { files.append(path); if (!m_files.contains(path)) m_files.append(path); }
    }
    if (folders.isEmpty() && files.isEmpty()) {
        const auto tracks = requestedTracks(request);
        if (!tracks.isEmpty()) emit filesOpened(tracks, request.value("name").toString(), request.value("append").toBool());
        emit openingFilesChanged(); QTimer::singleShot(0, this, &TrackListModel::processOpenRequests); return;
    }
    m_activeOpenRequest = request;
    for (const auto &folder : folders) for (const auto &entry : collectionTracks("folder", folder)) {
        const auto track = entry.toMap(); cache[track.value("fileUrl").toString()] = track;
    }
    const auto artworkDirectory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/artwork");
    m_openWatcher.setFuture(QtConcurrent::run(scanFiles, folders, files, cache, artworkDirectory));
    emit foldersChanged(); emit openingFilesChanged();
}

void TrackListModel::toggleFavorite(const QString &fileUrl)
{
    if (!m_workingSetLoaded) { const auto track = trackForUrl(fileUrl); if (!track.isEmpty()) setTracksFavorite({fileUrl}, !track.value("favorite").toBool()); return; }
    for (QVariant &entry : m_tracks) {
        QVariantMap track = entry.toMap();
        if (track.value(QStringLiteral("fileUrl")).toString() != fileUrl)
            continue;
        const bool favorite = !track.value(QStringLiteral("favorite")).toBool();
        track.insert(QStringLiteral("favorite"), favorite);
        entry = track;
        m_favorites.removeAll(fileUrl);
        if (favorite)
            m_favorites.append(fileUrl);
        m_settings.setValue(QStringLiteral("library/favorites"), m_favorites);
        publishUpdates();
        return;
    }
}

void TrackListModel::probeNext()
{
    if (m_scanning)
        return;
    if (m_coverWatcher.isRunning()) {
        m_nextProbe.start();
        return;
    }
    do {
        ++m_probeIndex;
    } while (m_probeIndex < m_tracks.size() && m_tracks[m_probeIndex].toMap().value(QStringLiteral("probed")).toBool());
    if (m_probeIndex >= m_tracks.size()) {
        m_indexing = false;
        publishUpdates();
        emit scanningChanged();
        saveSettings();
        return;
    }
    m_probeTimeout.start(2500);
    m_probe.setSource(QUrl(m_tracks[m_probeIndex].toMap().value(QStringLiteral("fileUrl")).toString()));
}

void TrackListModel::finishProbe()
{
    if (m_scanning || m_finishingProbe || m_probeIndex < 0 || m_probeIndex >= m_tracks.size())
        return;
    m_finishingProbe = true;
    m_probeTimeout.stop();
    QVariantMap track = m_tracks[m_probeIndex].toMap();
    if (track.contains(QStringLiteral("automaticAlbum"))) {
        track[QStringLiteral("album")] = track.take(QStringLiteral("automaticAlbum"));
        track[QStringLiteral("albumId")] = track.take(QStringLiteral("automaticAlbumId"));
        track[QStringLiteral("albumArtist")] = track.take(QStringLiteral("automaticAlbumArtist"));
    }
    const auto metadata = m_probe.metaData();
    for (const auto &field : {qMakePair(QStringLiteral("title"), QMediaMetaData::Title),
                              qMakePair(QStringLiteral("artist"), QMediaMetaData::ContributingArtist),
                              qMakePair(QStringLiteral("album"), QMediaMetaData::AlbumTitle),
                              qMakePair(QStringLiteral("genre"), QMediaMetaData::Genre)}) {
        const QString text = metadata.stringValue(field.second).trimmed();
        if (!text.isEmpty())
            track.insert(field.first, text);
    }
    track.insert(QStringLiteral("duration"), m_probe.duration());
    const QString albumArtist = metadata.stringValue(QMediaMetaData::AlbumArtist).trimmed();
    track.insert(QStringLiteral("tagAlbumArtist"), albumArtist);
    const QVariant trackNumber = metadata.value(QMediaMetaData::TrackNumber);
    if (trackNumber.isValid()) track.insert(QStringLiteral("tagTrackNumber"), trackNumber);
    if (track.contains(QStringLiteral("fileTagFields")))
        FileTags::applyToTrack(track, {{QStringLiteral("fields"), track.value(QStringLiteral("fileTagFields"))}});
    track.insert(QStringLiteral("metadataError"), m_probe.mediaStatus() == QMediaPlayer::LoadedMedia
                 ? QString() : (m_probe.errorString().isEmpty() ? tr("Metadata read timed out") : m_probe.errorString()));
    QImage cover = qvariant_cast<QImage>(metadata.value(QMediaMetaData::CoverArtImage));
    if (cover.isNull())
        cover = qvariant_cast<QImage>(metadata.value(QMediaMetaData::ThumbnailImage));
    AlbumGrouping::updateTrackOrder(track, QUrl(track.value(QStringLiteral("fileUrl")).toString()).toLocalFile());
    AlbumGrouping::updateIdentity(track);
    track.insert(QStringLiteral("probed"), true);
    track.insert(QStringLiteral("metadataVersion"), MetadataCacheVersion);
    m_probe.setSource(QUrl());
    if (cover.isNull()) {
        completeProbe(track, m_generation, m_probeIndex);
        return;
    }
    m_coverGeneration = m_generation;
    m_coverIndex = m_probeIndex;
    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/artwork");
    m_coverWatcher.setFuture(QtConcurrent::run([cover, cacheDir, track]() mutable {
        // JPEG encoding and disk I/O must not block touchpad or touch events.
        QByteArray jpeg;
        QBuffer buffer(&jpeg);
        buffer.open(QIODevice::WriteOnly);
        if (cover.scaled(480, 480, Qt::KeepAspectRatio, Qt::SmoothTransformation).save(&buffer, "JPG") && QDir().mkpath(cacheDir)) {
            const QString key = QString::fromLatin1(QCryptographicHash::hash(jpeg, QCryptographicHash::Sha256).toHex());
            const QString path = cacheDir + QLatin1Char('/') + key + QStringLiteral(".jpg");
            QFile output(path);
            if (output.exists() || (output.open(QIODevice::WriteOnly) && output.write(jpeg) == jpeg.size())) {
                track.insert(QStringLiteral("artwork"), QUrl::fromLocalFile(path).toString());
                track.insert(QStringLiteral("artworkKey"), key);
            }
        }
        return track;
    }));
}

void TrackListModel::completeProbe(const QVariantMap &result, int generation, int index)
{
    if (generation != m_generation || index != m_probeIndex || index < 0 || index >= m_tracks.size()) return;
    auto track = result;
    // A favorite can change while its cover is being encoded.
    track.insert(QStringLiteral("favorite"), m_favorites.contains(track.value(QStringLiteral("fileUrl")).toString()));
    track.insert(QStringLiteral("playCount"), m_playCounts.value(track.value(QStringLiteral("fileUrl")).toString(), 0).toLongLong());
    track.insert(QStringLiteral("playlistIds"), m_tracks[index].toMap().value(QStringLiteral("playlistIds"), QStringList()));
    m_tracks[index] = track;
    m_metadataCache.insert(track.value(QStringLiteral("fileUrl")).toString(), track);
    ++m_processed;
    if (!track.value(QStringLiteral("metadataError")).toString().isEmpty()) ++m_failed;
    m_finishingProbe = false;
    if (!m_tracksNotification.isActive())
        m_tracksNotification.start();
    if (!m_settingsTimer.isActive()) m_settingsTimer.start();
    m_nextProbe.start();
}

void TrackListModel::saveSettings()
{
    m_settingsTimer.stop();
    m_settings.setValue(QStringLiteral("library/folders"), m_folders);
    m_settings.setValue(QStringLiteral("library/files"), m_files);
    m_settings.setValue(QStringLiteral("library/favorites"), m_favorites);
    if (!m_database->valid() || !m_databaseSynchronized) {
        m_settings.setValue("library/databaseReady", false);
        m_settings.setValue(QStringLiteral("library/metadata"), m_metadataCache);
        if (m_libraryReady) m_settings.setValue(QStringLiteral("library/snapshot"), m_tracks);
    }
    m_settings.setValue(QStringLiteral("library/playlists"), m_playlistData);
    m_settings.setValue(QStringLiteral("library/playCounts"), m_playCounts);
    if (!m_database->valid()) m_settings.setValue(QStringLiteral("library/addedAt"), m_addedAt);
    m_settings.setValue(QStringLiteral("library/albumOverrides"), m_albumOverrides);
    m_settings.setValue(QStringLiteral("library/albumUndo"), m_albumUndo);
    m_settings.setValue(QStringLiteral("history/days"), m_dailyListening);
    m_settings.setValue(QStringLiteral("history/totals"), m_listeningTotals);
    m_settings.setValue(QStringLiteral("history/recent"), m_recentListening);
    m_settings.sync();
    saveListeningState();
}
