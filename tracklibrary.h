#pragma once

#include <QFutureWatcher>
#include <QElapsedTimer>
#include <QMediaPlayer>
#include <QObject>
#include <QSettings>
#include <QTimer>
#include <QVariantList>
#include "librarymodels.h"
#include "listeninghistory.h"

class TrackListModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY tracksChanged)
    Q_PROPERTY(LibraryViewModel *songs READ songs CONSTANT)
    Q_PROPERTY(LibraryViewModel *albums READ albums CONSTANT)
    Q_PROPERTY(LibraryViewModel *artists READ artists CONSTANT)
    Q_PROPERTY(LibraryViewModel *playlists READ playlists CONSTANT)
    Q_PROPERTY(LibraryViewModel *mostPlayed READ mostPlayed CONSTANT)
    Q_PROPERTY(int trackCount READ trackCount NOTIFY tracksChanged)
    Q_PROPERTY(int favoriteCount READ favoriteCount NOTIFY tracksChanged)
    Q_PROPERTY(QVariantList shelfAlbums READ shelfAlbums NOTIFY tracksChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(QString scanStatus READ scanStatus NOTIFY scanProgressChanged)
    Q_PROPERTY(int metadataFailures READ metadataFailures NOTIFY scanProgressChanged)
    Q_PROPERTY(QUrl musicFolder READ musicFolder CONSTANT)
    Q_PROPERTY(QStringList folders READ folders NOTIFY foldersChanged)
    Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(QVariantMap dashboard READ dashboard NOTIFY historyChanged)
    Q_PROPERTY(bool canUndoAlbumEdit READ canUndoAlbumEdit NOTIFY albumEditsChanged)
    Q_PROPERTY(bool libraryReady READ libraryReady NOTIFY libraryReadyChanged)
    Q_PROPERTY(QVariantList smartPlaylists READ smartPlaylists NOTIFY tracksChanged)
    Q_PROPERTY(bool metadataBusy READ metadataBusy NOTIFY metadataBusyChanged)

public:
    static constexpr int MetadataCacheVersion = 4;
    explicit TrackListModel(QObject *parent = nullptr);
    TrackListModel(const QString &settingsFile, bool autoScan, QObject *parent = nullptr);
    ~TrackListModel() override;

    QVariantList tracks() const { return m_tracks; }
    LibraryViewModel *songs() { return &m_songs; }
    LibraryViewModel *albums() { return &m_albums; }
    LibraryViewModel *artists() { return &m_artists; }
    LibraryViewModel *playlists() { return &m_playlists; }
    LibraryViewModel *mostPlayed() { return &m_mostPlayed; }
    int trackCount() const { return m_tracks.size(); }
    int favoriteCount() const;
    QVariantList shelfAlbums() const;
    bool scanning() const { return m_scanning || m_indexing; }
    QString scanStatus() const;
    int metadataFailures() const { return m_failed; }
    QUrl musicFolder() const;
    QStringList folders() const { return m_folders; }
    qreal volume() const { return m_volume; }
    void setVolume(qreal value);
    QVariantMap dashboard() const;
    bool canUndoAlbumEdit() const { return !m_albumUndo.isEmpty(); }
    bool libraryReady() const { return m_libraryReady; }
    QVariantList smartPlaylists() const;
    bool metadataBusy() const { return m_metadataBusy; }
    Q_INVOKABLE void loadSongMetadata(const QString &url);
    Q_INVOKABLE bool saveSongMetadata(const QString &url, const QVariantMap &changes, const QString &coverAction, const QUrl &coverFile, const QString &revision);
    Q_INVOKABLE void observeAudiblePlayback(const QString &token, const QString &url, qint64 position, qint64 duration, bool playing, bool finished, qint64 timestamp);
    Q_INVOKABLE void seekAudiblePlayback(const QString &token, qint64 position, qint64 timestamp);
    Q_INVOKABLE QVariantList albumOptions() const;

    Q_INVOKABLE void rescan();
    Q_INVOKABLE void addFolder(const QUrl &folder);
    Q_INVOKABLE void addFiles(const QList<QUrl> &files);
    Q_INVOKABLE void toggleFavorite(const QString &fileUrl);
    Q_INVOKABLE QVariantMap trackForUrl(const QString &fileUrl) const;
    Q_INVOKABLE QVariantList tracksForGroup(const QString &kind, const QString &id) const;
    Q_INVOKABLE QString createPlaylist(const QString &name, const QString &fileUrl = QString());
    Q_INVOKABLE bool addToPlaylist(const QString &id, const QString &fileUrl);
    Q_INVOKABLE bool removeFromPlaylist(const QString &id, const QString &fileUrl);
    Q_INVOKABLE bool deletePlaylist(const QString &id);
    Q_INVOKABLE bool renamePlaylist(const QString &id, const QString &name);
    Q_INVOKABLE bool movePlaylistTrack(const QString &id, const QString &fileUrl, const QString &beforeUrl);
    Q_INVOKABLE QString importPlaylist(const QUrl &file);
    Q_INVOKABLE bool exportPlaylist(const QString &id, const QUrl &file);
    Q_INVOKABLE QVariantMap playbackSession() const;
    Q_INVOKABLE void savePlaybackSession(const QVariantMap &session);
    Q_INVOKABLE QVariantMap previewAlbumEdit(const QString &operation, const QStringList &albumIds,
        const QStringList &selectedUrls, const QString &name, const QString &artist, const QString &splitBy) const;
    Q_INVOKABLE bool applyAlbumEdit(const QVariantMap &preview);
    Q_INVOKABLE bool undoAlbumEdit();
    Q_INVOKABLE QVariantMap trackInfo(const QString &fileUrl) const;
    Q_INVOKABLE bool revealTrack(const QString &fileUrl) const;
    Q_INVOKABLE bool trashTrack(const QString &fileUrl);
    Q_INVOKABLE void beginPlayback(const QString &fileUrl);
    Q_INVOKABLE void recordPlaybackProgress(const QString &fileUrl, qint64 position, qint64 duration, bool playing);
    Q_INVOKABLE void seekPlayback(const QString &fileUrl, qint64 position);

signals:
    void tracksChanged();
    void scanningChanged();
    void scanProgressChanged();
    void foldersChanged();
    void volumeChanged();
    void importError(const QString &message);
    void albumMerged(const QString &previousId, const QString &albumId);
    void trackRemoved(const QString &fileUrl);
    void historyChanged();
    void albumEditsChanged();
    void libraryReadyChanged();
    void metadataBusyChanged();
    void metadataLoaded(const QVariantMap &details);
    void metadataSaved(const QString &url);
    void metadataError(const QString &message);

private:
    void initialize(bool autoScan);
    void probeNext();
    void finishProbe();
    void completeProbe(const QVariantMap &track, int generation, int index);
    void publishUpdates();
    void saveSettings();
    void collectListening(const QString &url, qint64 audibleMs, bool qualified, const QString &session = {});
    void applyAlbumOverrides();
    void initializeMetadataEditing();
    QString albumSignature() const;

    QSettings m_settings;
    LibraryEntryModel m_songEntries;
    LibraryEntryModel m_albumEntries;
    LibraryEntryModel m_artistEntries;
    LibraryEntryModel m_playlistEntries;
    LibraryViewModel m_songs;
    LibraryViewModel m_albums;
    LibraryViewModel m_artists;
    LibraryViewModel m_playlists;
    LibraryViewModel m_mostPlayed;
    QVariantMap m_playCounts;
    QVariantMap m_addedAt;
    QVariantMap m_albumOverrides;
    QVariantList m_albumUndo;
    QVariantMap m_dailyListening;
    QVariantMap m_listeningTotals;
    QVariantList m_recentListening;
    bool m_historyEventStarted = false;
    bool m_libraryReady = false;
    ListeningSession m_listening;
    QElapsedTimer m_listeningClock;
    QVariantList m_playlistData;
    QVariantList m_tracks;
    QHash<QString, int> m_trackRows;
    QHash<QString, QString> m_albumAliases;
    QVariantList m_shelfAlbums;
    QStringList m_folders;
    QStringList m_files;
    QStringList m_favorites;
    QVariantMap m_metadataCache;
    struct AudibleSession { ListeningSession coverage; QElapsedTimer clock; };
    QHash<QString, AudibleSession> m_audibleSessions;
    QFutureWatcher<QVariantMap> m_metadataWatcher;
    bool m_metadataBusy = false, m_metadataSaving = false;
    QString m_metadataUrl;
    QVariantMap m_metadataChanges;
    QFutureWatcher<QVariantList> m_scanWatcher;
    QFutureWatcher<QVariantMap> m_coverWatcher;
    QMediaPlayer m_probe;
    QTimer m_probeTimeout;
    QTimer m_nextProbe;
    QTimer m_tracksNotification;
    QTimer m_settingsTimer;
    int m_probeIndex = -1;
    bool m_scanning = false;
    bool m_indexing = false;
    bool m_finishingProbe = false;
    int m_generation = 0;
    int m_coverGeneration = 0;
    int m_coverIndex = -1;
    int m_processed = 0;
    int m_failed = 0;
    bool m_rescanPending = false;
    qreal m_volume = 0.65;
};
