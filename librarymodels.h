#pragma once

#include <QAbstractTableModel>
#include <QCollator>
#include <QHash>
#include <QSortFilterProxyModel>
#include <QVariantMap>

// Entries keep their identity when tags or artwork change. Views receive row
// updates instead of a replacement JavaScript array and a full model reset.
class LibraryEntryModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum { EntryRole = Qt::UserRole + 1, NameRole, TitleRole, ArtistRole, AlbumRole, TrackRole, PlayCountRole, OrderRole, AddedRole, FilterRole };
    explicit LibraryEntryModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void synchronize(const QVariantList &entries, const QString &key);

private:
    QList<QVariantMap> m_entries;
};

class LibraryViewModel : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY filterChanged)
    Q_PROPERTY(QString collectionKind READ collectionKind WRITE setCollectionKind NOTIFY filterChanged)
    Q_PROPERTY(QString collectionId READ collectionId WRITE setCollectionId NOTIFY filterChanged)
    Q_PROPERTY(QString sortKey READ sortKey WRITE setSortKey NOTIFY filterChanged)
    Q_PROPERTY(bool favoritesOnly READ favoritesOnly WRITE setFavoritesOnly NOTIFY filterChanged)
    Q_PROPERTY(bool playedOnly READ playedOnly WRITE setPlayedOnly NOTIFY filterChanged)

public:
    explicit LibraryViewModel(QObject *parent = nullptr);
    int count() const { return rowCount(); }
    QString search() const { return m_search; }
    QString collectionKind() const { return m_collectionKind; }
    QString collectionId() const { return m_collectionId; }
    QString sortKey() const { return m_sortKey; }
    bool favoritesOnly() const { return m_favoritesOnly; }
    bool playedOnly() const { return m_playedOnly; }
    void setSearch(const QString &value);
    void setCollectionKind(const QString &value);
    void setCollectionId(const QString &value);
    void setSortKey(const QString &value);
    void setFavoritesOnly(bool value);
    void setPlayedOnly(bool value);
    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE QVariantList entries(int limit = -1) const;

signals:
    void countChanged();
    void filterChanged();

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    void beginRefilter();
    void endRefilter();
    QString m_search;
    QString m_collectionKind;
    QString m_collectionId;
    QString m_sortKey = QStringLiteral("name");
    bool m_favoritesOnly = false;
    bool m_playedOnly = false;
    QCollator m_collator;
};
