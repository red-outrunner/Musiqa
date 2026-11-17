#pragma once

#include <QAbstractTableModel>
#include <QCollator>
#include "librarycollation.h"
#include <QHash>
#include <QSortFilterProxyModel>
#include <QVariantMap>
#include <QCache>
#include <QSet>
#include "librarydatabase.h"

// Entries keep their identity when tags or artwork change. Views receive row
// updates instead of a replacement JavaScript array and a full model reset.
class LibraryEntryModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum { EntryRole = Qt::UserRole + 1, NameRole, TitleRole, ArtistRole, AlbumRole, TrackRole, PlayCountRole, OrderRole, AddedRole, FilterRole, IdentityRole, TrackCountRole, DurationRole };
    explicit LibraryEntryModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void synchronize(const QVariantList &entries, const QString &key);
    bool setPlayCount(const QString &fileUrl, qint64 count);
    bool synchronizing() const { return m_synchronizing; }
    void setDatabase(LibraryDatabase *database, const QString &kind);
    void queryDatabase(const LibraryQuery &query, bool reset = false);
    int cachedEntryCount() const;
    bool containsIdentity(const QString &id) const { return m_visibleIds.contains(id); }
signals:
    void synchronized();

private:
    QList<QVariantMap> m_entries;
    bool m_synchronizing = false;
    LibraryDatabase *m_database = nullptr;
    QString m_kind;
    QStringList m_ids;
    QSet<QString> m_visibleIds;
    mutable QCache<int, QVariantList> m_pages{3};
};

class LibraryViewModel : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(quint64 revision READ revision NOTIFY entriesChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY filterChanged)
    Q_PROPERTY(QVariantMap filters READ filters WRITE setFilters NOTIFY filterChanged)
    Q_PROPERTY(QString collectionKind READ collectionKind WRITE setCollectionKind NOTIFY filterChanged)
    Q_PROPERTY(QString collectionId READ collectionId WRITE setCollectionId NOTIFY filterChanged)
    Q_PROPERTY(QString sortKey READ sortKey WRITE setSortKey NOTIFY filterChanged)
    Q_PROPERTY(QString sortDirection READ sortDirection WRITE setSortDirection NOTIFY filterChanged)
    Q_PROPERTY(bool favoritesOnly READ favoritesOnly WRITE setFavoritesOnly NOTIFY filterChanged)
    Q_PROPERTY(bool playedOnly READ playedOnly WRITE setPlayedOnly NOTIFY filterChanged)
    Q_PROPERTY(int cachedEntryCount READ cachedEntryCount NOTIFY entriesChanged)

public:
    explicit LibraryViewModel(QObject *parent = nullptr);
    void setSourceModel(QAbstractItemModel *source) override;
    void setDatabase(LibraryDatabase *database, const QString &kind);
    int cachedEntryCount() const;
    int count() const { return rowCount(); }
    quint64 revision() const { return m_revision; }
    QString search() const { return m_search; }
    QVariantMap filters() const { return m_filters; }
    void setFilters(const QVariantMap &filters);
    QString collectionKind() const { return m_collectionKind; }
    QString collectionId() const { return m_collectionId; }
    QString sortKey() const { return m_sortKey; }
    QString sortDirection() const { return m_sortDirection; }
    bool favoritesOnly() const { return m_favoritesOnly; }
    bool playedOnly() const { return m_playedOnly; }
    void setSearch(const QString &value);
    void setCollectionKind(const QString &value);
    void setCollectionId(const QString &value);
    void setSortKey(const QString &value);
    void setSortDirection(const QString &value);
    void setFavoritesOnly(bool value);
    void setPlayedOnly(bool value);
    void setCustomSmartPlaylists(const QVariantList &playlists);
    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE QVariantList entries(int limit = -1) const;
    Q_INVOKABLE QStringList fileUrls() const;
    Q_INVOKABLE int countMatchingUrls(const QStringList &urls) const;
    Q_INVOKABLE QVariantList entriesForUrls(const QStringList &urls) const;

signals:
    void countChanged();
    void entriesChanged();
    void filterChanged();

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    void beginRefilter();
    void endRefilter();
    void publishNotifications();
    QString m_search;
    QVariantMap m_filters;
    QString m_collectionKind;
    QString m_collectionId;
    QString m_sortKey = QStringLiteral("name");
    QString m_sortDirection;
    bool m_favoritesOnly = false;
    bool m_playedOnly = false;
    QVariantList m_customSmartPlaylists;
    QCollator m_collator = libraryCollator();
    quint64 m_revision = 0;
    int m_updateDepth = 0, m_announcedCount = 0;
    bool m_pendingNotifications = false, m_publishingNotifications = false;
    QMetaObject::Connection m_batchConnection;
    LibraryDatabase *m_database = nullptr;
    LibraryEntryModel *m_databaseEntries = nullptr;
    void refreshDatabase(bool reset = false);
};
