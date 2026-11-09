#include "tracklibrary.h"
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
                     {QStringLiteral("album"), album}, {QStringLiteral("genre"), QStringLiteral("Other")},
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
    initialize(true);
}

TrackListModel::TrackListModel(const QString &settingsFile, bool autoScan, QObject *parent)
    : QObject(parent), m_settings(settingsFile, QSettings::IniFormat)
{
    initialize(autoScan);
}

TrackListModel::~TrackListModel()
{
    if (m_metadataWatcher.isRunning()) m_metadataWatcher.waitForFinished();
    saveSettings();
}

void TrackListModel::initialize(bool autoScan)
{
    initializeMetadataEditing();
    m_songs.setSourceModel(&m_songEntries);
    m_songs.setSortKey(QStringLiteral("title"));
    m_albums.setSourceModel(&m_albumEntries);
    m_artists.setSourceModel(&m_artistEntries);
    m_playlists.setSourceModel(&m_playlistEntries);
    m_mostPlayed.setSourceModel(&m_songEntries);
    m_mostPlayed.setPlayedOnly(true);
    m_mostPlayed.setSortKey(QStringLiteral("plays"));
    m_playCounts = m_settings.value(QStringLiteral("library/playCounts")).toMap();
    m_addedAt = m_settings.value(QStringLiteral("library/addedAt")).toMap();
    m_albumOverrides = m_settings.value(QStringLiteral("library/albumOverrides")).toMap();
    m_albumUndo = m_settings.value(QStringLiteral("library/albumUndo")).toList();
    m_dailyListening = m_settings.value(QStringLiteral("history/days")).toMap();
    m_listeningTotals = m_settings.value(QStringLiteral("history/totals")).toMap();
    m_recentListening = m_settings.value(QStringLiteral("history/recent")).toList();
    m_playlistData = m_settings.value(QStringLiteral("library/playlists")).toList();
    m_folders = m_settings.value(QStringLiteral("library/folders"), QStringList{defaultMusicPath()}).toStringList();
    m_files = m_settings.value(QStringLiteral("library/files")).toStringList();
    m_favorites = m_settings.value(QStringLiteral("library/favorites")).toStringList();
    m_metadataCache = m_settings.value(QStringLiteral("library/metadata")).toMap();
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
        m_tracks = m_scanWatcher.result();
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
    });
    if (autoScan)
        QTimer::singleShot(0, this, &TrackListModel::rescan);
}

int TrackListModel::favoriteCount() const
{
    int count = 0;
    for (const auto &entry : m_tracks)
        count += entry.toMap().value(QStringLiteral("favorite")).toBool();
    return count;
}

QVariantList TrackListModel::shelfAlbums() const { return m_shelfAlbums; }

QString TrackListModel::scanStatus() const
{
    if (m_scanning) return tr("Finding music files…");
    if (m_indexing) return tr("Reading tags: %1 / %2").arg(m_processed).arg(m_tracks.size());
    if (m_failed == 1) return tr("Library ready · 1 file without readable tags");
    if (m_failed > 1) return tr("Library ready · %1 files without readable tags").arg(m_failed);
    return tr("Library up to date");
}

QVariantMap TrackListModel::trackForUrl(const QString &fileUrl) const
{
    const int row = m_trackRows.value(QUrl(fileUrl).toString(), -1);
    return row >= 0 ? m_tracks[row].toMap() : QVariantMap();
}

QVariantList TrackListModel::tracksForGroup(const QString &kind, const QString &id) const
{
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
    for (const auto &entry : m_tracks)
        if (entry.toMap().value(key).toString() == resolved) tracks.append(entry);
    std::sort(tracks.begin(), tracks.end(), AlbumGrouping::trackLessThan);
    return tracks;
}

QString TrackListModel::createPlaylist(const QString &name, const QString &fileUrl)
{
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
    if (!fileUrl.isEmpty() && trackForUrl(fileUrl).isEmpty()) {
        emit importError(tr("This song is no longer in your library."));
        return {};
    }
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_playlistData.append(QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("name"), title},
        {QStringLiteral("urls"), fileUrl.isEmpty() ? QStringList() : QStringList{fileUrl}}});
    saveSettings();
    publishUpdates();
    return id;
}

bool TrackListModel::addToPlaylist(const QString &id, const QString &fileUrl)
{
    if (trackForUrl(fileUrl).isEmpty()) return false;
    for (auto &entry : m_playlistData) {
        auto playlist = entry.toMap();
        if (playlist.value(QStringLiteral("id")).toString() != id) continue;
        auto urls = playlist.value(QStringLiteral("urls")).toStringList();
        if (urls.contains(fileUrl)) return true;
        urls.append(fileUrl);
        playlist.insert(QStringLiteral("urls"), urls);
        entry = playlist;
        saveSettings();
        publishUpdates();
        return true;
    }
    return false;
}

bool TrackListModel::removeFromPlaylist(const QString &id, const QString &fileUrl)
{
    for (auto &entry : m_playlistData) {
        auto playlist = entry.toMap();
        if (playlist.value(QStringLiteral("id")).toString() != id) continue;
        auto urls = playlist.value(QStringLiteral("urls")).toStringList();
        if (!urls.removeAll(fileUrl)) return false;
        playlist.insert(QStringLiteral("urls"), urls);
        entry = playlist;
        saveSettings();
        publishUpdates();
        return true;
    }
    return false;
}

bool TrackListModel::deletePlaylist(const QString &id)
{
    for (int row = 0; row < m_playlistData.size(); ++row) {
        if (m_playlistData[row].toMap().value(QStringLiteral("id")).toString() != id) continue;
        m_playlistData.removeAt(row);
        saveSettings();
        publishUpdates();
        return true;
    }
    return false;
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
    if (m_metadataSaving && QUrl(fileUrl).toString() == m_metadataUrl) {
        emit importError(tr("Wait for this song's metadata save to finish before deleting it.")); return false;
    }
    if (trackForUrl(fileUrl).isEmpty()) return false;
    const QString path = QUrl(fileUrl).toLocalFile();
    if (!QFileInfo(path).isFile()) {
        emit importError(tr("This song's file could not be found."));
        return false;
    }
    QFile file(path);
    if (!file.moveToTrash()) {
        emit importError(tr("Couldn't move the song to Trash: %1").arg(file.errorString()));
        return false;
    }
    m_probeTimeout.stop();
    m_nextProbe.stop();
    ++m_generation;
    m_probeIndex = -1;
    m_finishingProbe = false;
    m_probe.setSource(QUrl());
    m_tracks.removeAt(m_trackRows.value(fileUrl));
    m_trackRows.clear();
    for (int row = 0; row < m_tracks.size(); ++row)
        m_trackRows.insert(m_tracks[row].toMap().value(QStringLiteral("fileUrl")).toString(), row);
    m_files.removeAll(path);
    m_favorites.removeAll(fileUrl);
    m_metadataCache.remove(fileUrl);
    m_playCounts.remove(fileUrl);
    if (m_listening.url() == fileUrl) m_listening.begin(QString());
    for (auto &entry : m_playlistData) {
        auto playlist = entry.toMap();
        auto urls = playlist.value(QStringLiteral("urls")).toStringList();
        urls.removeAll(fileUrl);
        playlist.insert(QStringLiteral("urls"), urls);
        entry = playlist;
    }
    saveSettings();
    publishUpdates();
    emit trackRemoved(fileUrl);
    rescan();
    return true;
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
    m_settings.setValue(QStringLiteral("library/playCounts"), m_playCounts);
    publishUpdates();
    m_settings.sync();
}

void TrackListModel::publishUpdates()
{
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
        if (!m_addedAt.contains(url)) m_addedAt.insert(url, QDateTime::currentMSecsSinceEpoch());
        track.insert(QStringLiteral("addedAt"), m_addedAt.value(url));
        track.insert(QStringLiteral("playlistOrder"), positions.value(url));
        track.insert(QStringLiteral("playlistIds"), membership.value(track.value(QStringLiteral("fileUrl")).toString()));
        track.insert(QStringLiteral("playCount"), m_playCounts.value(track.value(QStringLiteral("fileUrl")).toString(), 0).toLongLong());
        for (const auto &field : {"bpm", "keyId", "musicalKey", "camelot", "beatOffset", "bpmConfidence",
                 "keyConfidence", "analysisNote", "analyzed", "manual", "tagBpm", "tagKeyId", "tagBeatOffset"})
            track.remove(field);
        entry = track;
    }
    m_songEntries.synchronize(m_tracks, QStringLiteral("fileUrl"));
    QMap<QString, QVariantMap> albums, artists;
    for (const auto &entry : m_tracks) {
        const auto track = entry.toMap();
        for (const auto &kind : {QStringLiteral("album"), QStringLiteral("artist")}) {
            auto &groups = kind == QStringLiteral("album") ? albums : artists;
            const QString id = track.value(kind + QStringLiteral("Id")).toString();
            auto found = groups.find(id);
            if (found == groups.end()) {
                found = groups.insert(id, {{QStringLiteral("id"), id},
                    {QStringLiteral("name"), track.value(kind)},
                    {QStringLiteral("subtitle"), kind == QStringLiteral("album") ? track.value(QStringLiteral("albumArtist")) : QVariant(QString())},
                    {QStringLiteral("artwork"), track.value(QStringLiteral("artwork"))},
                    {QStringLiteral("artStyle"), track.value(QStringLiteral("artStyle"))},
                    {QStringLiteral("trackCount"), 0}});
            }
            auto &group = found.value();
            group[QStringLiteral("trackCount")] = group.value(QStringLiteral("trackCount")).toInt() + 1;
            if (group.value(QStringLiteral("artwork")).toString().isEmpty())
                group[QStringLiteral("artwork")] = track.value(QStringLiteral("artwork"));
        }
    }
    QVariantList albumRows, artistRows;
    for (const auto &group : albums) albumRows.append(group);
    for (const auto &group : artists) artistRows.append(group);
    m_albumEntries.synchronize(albumRows, QStringLiteral("id"));
    m_artistEntries.synchronize(artistRows, QStringLiteral("id"));
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
    m_playlistEntries.synchronize(playlistRows, QStringLiteral("id"));
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
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

void TrackListModel::toggleFavorite(const QString &fileUrl)
{
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
    m_settings.setValue(QStringLiteral("library/metadata"), m_metadataCache);
    m_settings.setValue(QStringLiteral("library/playlists"), m_playlistData);
    m_settings.setValue(QStringLiteral("library/playCounts"), m_playCounts);
    m_settings.setValue(QStringLiteral("library/addedAt"), m_addedAt);
    m_settings.setValue(QStringLiteral("library/albumOverrides"), m_albumOverrides);
    m_settings.setValue(QStringLiteral("library/albumUndo"), m_albumUndo);
    m_settings.setValue(QStringLiteral("history/days"), m_dailyListening);
    m_settings.setValue(QStringLiteral("history/totals"), m_listeningTotals);
    m_settings.setValue(QStringLiteral("history/recent"), m_recentListening);
    m_settings.sync();
}
