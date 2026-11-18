#pragma once
#include <QObject>
#include <QSqlDatabase>
#include <QVariantMap>

struct LibraryQuery {
    QString search, collectionKind, collectionId, sort = QStringLiteral("title");
    bool favorites = false, played = false;
    QString sortDirection;
    QVariantMap filters;
};

// Only identifiers and a few visible pages live in the view. Complete tag maps
// stay in SQLite until requested; all queries use bound values.
class LibraryDatabase : public QObject
{
    Q_OBJECT
public:
    explicit LibraryDatabase(const QString &path, QObject *parent = nullptr);
    ~LibraryDatabase() override;
    bool valid() const { return m_db.isOpen() && m_ready; }
    QString error() const { return m_error; }
    bool hasSnapshot() const;
    void clear();
    bool replace(const QVariantList &songs, const QVariantList &albums, const QVariantList &artists, const QVariantList &playlists);
    QStringList ids(const QString &kind, const LibraryQuery &query = {}) const;
    QStringList matchingSongUrls(const QStringList &urls, const QString &search = {}) const;
    QStringList matchingMetadataNames(const QString &kind, const QString &text) const;
    QVariantMap entry(const QString &kind, const QString &id) const;
    QVariantList page(const QString &kind, const QStringList &ids, int first, int length) const;
    QVariantList all(const QString &kind) const;
    int count(const QString &kind) const;
    int favoriteCount() const;
    int failedCount() const;
    QVariantMap playCounts() const;
    QVariantMap smartCounts() const;
    int customSmartCount(const QVariantMap &rules) const;
    void setCustomSmartPlaylists(const QVariantList &playlists);
    QVariantMap folderSummary(const QString &path) const;
    QVariantList folderChildren(const QString &path) const;
    bool updateSong(const QString &url, const QVariantMap &track);
    QVariant value(const QString &key) const;
    void setValue(const QString &key, const QVariant &value);
signals:
    void changed(const QString &kind);
private:
    bool writeRows(const QString &kind, const QVariantList &rows, const QString &key);
    bool migrateCollectionSorting();
    bool migrateGenres();
    bool migrateCollectionTools();
    bool migrateSearchFilters();
    QSqlDatabase m_db;
    bool m_ready = false;
    QString m_error;
};
