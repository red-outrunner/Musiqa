#include "librarymodels.h"
#include "audioanalysis.h"
#include <cmath>

#include <QSet>
#include <QDateTime>
#include <QTimer>

LibraryEntryModel::LibraryEntryModel(QObject *parent) : QAbstractTableModel(parent) {}

int LibraryEntryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

int LibraryEntryModel::columnCount(const QModelIndex &parent) const
{
    // Quick views display column 0. Column 1 carries ordering changes because
    // QSortFilterProxyModel checks sorting by changed column, even when only
    // an unrelated data role changed. Artwork must not enter that path.
    return parent.isValid() ? 0 : 2;
}

QVariant LibraryEntryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};
    const auto &entry = m_entries[index.row()];
    switch (role) {
    case EntryRole: return entry;
    case NameRole: return entry.value(QStringLiteral("name"));
    case TitleRole: return entry.value(QStringLiteral("title"));
    case ArtistRole: return entry.value(QStringLiteral("artist"));
    case AlbumRole: return entry.value(QStringLiteral("album"));
    case TrackRole: return entry.value(QStringLiteral("trackSort"));
    case PlayCountRole: return entry.value(QStringLiteral("playCount"), 0);
    case OrderRole: return entry.value(QStringLiteral("playlistOrder"));
    case AddedRole: return entry.value(QStringLiteral("addedAt"));
    case FilterRole: {
        QVariantMap fields;
        for (const auto &key : {QStringLiteral("title"), QStringLiteral("artist"), QStringLiteral("album"),
                               QStringLiteral("name"), QStringLiteral("subtitle"), QStringLiteral("favorite"),
                               QStringLiteral("albumId"), QStringLiteral("artistId"), QStringLiteral("albumArtist"), QStringLiteral("playlistIds"), QStringLiteral("playCount"), QStringLiteral("addedAt"), QStringLiteral("fileUrl"), QStringLiteral("bpm"), QStringLiteral("keyId")})
            fields.insert(key, entry.value(key));
        return fields;
    }
    default: return {};
    }
}

QHash<int, QByteArray> LibraryEntryModel::roleNames() const
{
    return {{EntryRole, "modelData"}, {NameRole, "sortName"}, {TitleRole, "sortTitle"},
            {ArtistRole, "sortArtist"}, {AlbumRole, "sortAlbum"}, {TrackRole, "sortTrack"}, {PlayCountRole, "qualifiedPlays"}, {OrderRole, "playlistOrder"}, {AddedRole, "addedAt"}, {FilterRole, "filterFields"}};
}

void LibraryEntryModel::synchronize(const QVariantList &entries, const QString &key)
{
    QHash<QString, QVariantMap> incoming;
    incoming.reserve(entries.size());
    for (const auto &entry : entries) {
        const auto item = entry.toMap();
        incoming.insert(item.value(key).toString(), item);
    }
    for (int row = m_entries.size() - 1; row >= 0; --row) {
        const QString id = m_entries[row].value(key).toString();
        auto found = incoming.find(id);
        if (found == incoming.end()) {
            beginRemoveRows({}, row, row);
            m_entries.removeAt(row);
            endRemoveRows();
        } else {
            if (m_entries[row] != found.value()) {
                QList<int> roles{EntryRole};
                for (const auto &field : {qMakePair(NameRole, QStringLiteral("name")),
                                          qMakePair(TitleRole, QStringLiteral("title")),
                                          qMakePair(ArtistRole, QStringLiteral("artist")),
                                          qMakePair(AlbumRole, QStringLiteral("album")),
                                          qMakePair(TrackRole, QStringLiteral("trackSort")),
                                          qMakePair(PlayCountRole, QStringLiteral("playCount")),
                                          qMakePair(OrderRole, QStringLiteral("playlistOrder")),
                                          qMakePair(AddedRole, QStringLiteral("addedAt"))}) {
                    if (m_entries[row].value(field.second) != found.value().value(field.second))
                        roles.append(field.first);
                }
                for (const auto &key : {QStringLiteral("title"), QStringLiteral("artist"), QStringLiteral("album"),
                                       QStringLiteral("name"), QStringLiteral("subtitle"), QStringLiteral("favorite"),
                                       QStringLiteral("albumId"), QStringLiteral("artistId"), QStringLiteral("albumArtist"), QStringLiteral("playlistIds"), QStringLiteral("playCount"), QStringLiteral("addedAt"), QStringLiteral("fileUrl"), QStringLiteral("bpm"), QStringLiteral("keyId")}) {
                    if (m_entries[row].value(key) != found.value().value(key)) {
                        roles.append(FilterRole);
                        break;
                    }
                }
                m_entries[row] = found.value();
                emit dataChanged(index(row, 0), index(row, 0), roles);
                QList<int> orderingRoles = roles;
                orderingRoles.removeAll(EntryRole);
                orderingRoles.removeAll(FilterRole);
                if (!orderingRoles.isEmpty())
                    emit dataChanged(index(row, 1), index(row, 1), orderingRoles);
            }
            incoming.erase(found);
        }
    }
    // Keep insertion deterministic without reordering the existing entries.
    if (!incoming.isEmpty()) {
        QList<QVariantMap> added;
        added.reserve(incoming.size());
        for (const auto &entry : entries) {
            const auto item = entry.toMap();
            if (incoming.remove(item.value(key).toString()))
                added.append(item);
        }
        const int first = m_entries.size();
        beginInsertRows({}, first, first + added.size() - 1);
        m_entries.append(added);
        endInsertRows();
    }
}

LibraryViewModel::LibraryViewModel(QObject *parent) : QSortFilterProxyModel(parent)
{
    m_collator.setCaseSensitivity(Qt::CaseInsensitive);
    m_collator.setNumericMode(true);
    setDynamicSortFilter(true);
    setSortRole(LibraryEntryModel::NameRole);
    setFilterRole(LibraryEntryModel::FilterRole);
    connect(this, &QAbstractItemModel::rowsInserted, this, &LibraryViewModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &LibraryViewModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &LibraryViewModel::countChanged);
    sort(1);
    auto *refresh = new QTimer(this);
    refresh->setInterval(60000);
    connect(refresh, &QTimer::timeout, this, [this] {
        if (m_collectionKind == QStringLiteral("smart")) { beginRefilter(); endRefilter(); }
    });
    refresh->start();
}

QVariantMap LibraryViewModel::get(int row) const
{
    return data(index(row, 0), LibraryEntryModel::EntryRole).toMap();
}

QVariantList LibraryViewModel::entries(int limit) const
{
    QVariantList result;
    const int size = limit < 0 ? count() : qMin(limit, count());
    result.reserve(size);
    for (int row = 0; row < size; ++row)
        result.append(get(row));
    return result;
}

void LibraryViewModel::setSearch(const QString &value)
{
    const QString query = value.trimmed();
    if (m_search == query) return;
    beginRefilter();
    m_search = query;
    endRefilter();
    emit filterChanged();
}

void LibraryViewModel::setCollectionKind(const QString &value)
{
    if (m_collectionKind == value) return;
    beginRefilter();
    m_collectionKind = value;
    endRefilter();
    emit filterChanged();
}

void LibraryViewModel::setCollectionId(const QString &value)
{
    if (m_collectionId == value) return;
    beginRefilter();
    m_collectionId = value;
    endRefilter();
    if (m_sortKey == QStringLiteral("order")) invalidate();
    emit filterChanged();
}

void LibraryViewModel::setSortKey(const QString &value)
{
    const QString key = value.toLower();
    if (m_sortKey == key) return;
    m_sortKey = key;
    setSortRole(key == QStringLiteral("title") ? LibraryEntryModel::TitleRole
                : key == QStringLiteral("artist") ? LibraryEntryModel::ArtistRole
                : key == QStringLiteral("album") ? LibraryEntryModel::AlbumRole
                : key == QStringLiteral("track") ? LibraryEntryModel::TrackRole
                : key == QStringLiteral("plays") ? LibraryEntryModel::PlayCountRole
                : key == QStringLiteral("rare") ? LibraryEntryModel::PlayCountRole
                : key == QStringLiteral("order") ? LibraryEntryModel::OrderRole
                : key == QStringLiteral("added") ? LibraryEntryModel::AddedRole
                : LibraryEntryModel::NameRole);
    sort(1);
    emit filterChanged();
    if (key == QStringLiteral("plays") || key == QStringLiteral("rare")) invalidate();
}

void LibraryViewModel::setFavoritesOnly(bool value)
{
    if (m_favoritesOnly == value) return;
    beginRefilter();
    m_favoritesOnly = value;
    endRefilter();
    emit filterChanged();
}

void LibraryViewModel::setPlayedOnly(bool value)
{
    if (m_playedOnly == value) return;
    beginRefilter();
    m_playedOnly = value;
    endRefilter();
    emit filterChanged();
}

void LibraryViewModel::setMusicMatch(const QVariantMap &value)
{
    if (value == m_musicMatch) return;
    beginRefilter(); m_musicMatch = value; endRefilter(); emit filterChanged();
}

void LibraryViewModel::beginRefilter()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
#endif
}

void LibraryViewModel::endRefilter()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    endFilterChange(Direction::Rows);
#else
    invalidateFilter();
#endif
}

bool LibraryViewModel::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    const auto entry = sourceModel()->data(sourceModel()->index(row, 0, parent), LibraryEntryModel::FilterRole).toMap();
    if (m_favoritesOnly && !entry.value(QStringLiteral("favorite")).toBool()) return false;
    if (m_playedOnly && entry.value(QStringLiteral("playCount")).toLongLong() <= 0) return false;
    if (!m_musicMatch.isEmpty()) {
        if (entry.value("fileUrl").toString() == m_musicMatch.value("fileUrl").toString()) return false;
        const double bpm = entry.value("bpm").toDouble(), reference = m_musicMatch.value("bpm").toDouble();
        const double tolerance = m_musicMatch.value("tolerance", 1.0).toDouble();
        const bool tempo = bpm > 0 && reference > 0 && (tolerance <= 0 ? qRound(bpm) == qRound(reference) : std::abs(bpm - reference) <= tolerance);
        const int key = entry.value("keyId", -1).toInt(), referenceKey = m_musicMatch.value("keyId", -1).toInt();
        const bool harmonic = key >= 0 && referenceKey >= 0 && (m_musicMatch.value("related", true).toBool() ? MusicAnalysis::relatedKeys(key, referenceKey) : key == referenceKey);
        const QString mode = m_musicMatch.value("mode", QStringLiteral("both")).toString();
        if (!(mode == QStringLiteral("bpm") ? tempo : mode == QStringLiteral("key") ? harmonic : mode == QStringLiteral("either") ? tempo || harmonic : tempo && harmonic)) return false;
    }
    if (!m_collectionId.isEmpty()) {
        if (m_collectionKind == QStringLiteral("smart")) {
            const qint64 plays = entry.value(QStringLiteral("playCount")).toLongLong();
            if (m_collectionId == QStringLiteral("recent") && entry.value(QStringLiteral("addedAt")).toLongLong() < QDateTime::currentMSecsSinceEpoch() - 30LL * 86400000) return false;
            if (m_collectionId == QStringLiteral("never") && plays != 0) return false;
            if (m_collectionId == QStringLiteral("rare") && (plays < 1 || plays > 4)) return false;
        } else if (m_collectionKind == QStringLiteral("playlist")) {
            if (!entry.value(QStringLiteral("playlistIds")).toStringList().contains(m_collectionId)) return false;
        } else {
            const auto key = m_collectionKind == QStringLiteral("album") ? QStringLiteral("albumId") : QStringLiteral("artistId");
            if (entry.value(key).toString() != m_collectionId) return false;
        }
    }
    if (m_search.isEmpty()) return true;
    const QString text = entry.value(QStringLiteral("title")).toString() + QLatin1Char(' ')
        + entry.value(QStringLiteral("artist")).toString() + QLatin1Char(' ')
        + entry.value(QStringLiteral("album")).toString() + QLatin1Char(' ')
        + entry.value(QStringLiteral("albumArtist")).toString() + QLatin1Char(' ')
        + entry.value(QStringLiteral("name")).toString() + QLatin1Char(' ')
        + entry.value(QStringLiteral("subtitle")).toString();
    return text.contains(m_search, Qt::CaseInsensitive);
}

bool LibraryViewModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    if (sortRole() == LibraryEntryModel::OrderRole) {
        const int a = sourceModel()->data(left, sortRole()).toMap().value(m_collectionId, INT_MAX).toInt();
        const int b = sourceModel()->data(right, sortRole()).toMap().value(m_collectionId, INT_MAX).toInt();
        return a == b ? left.row() < right.row() : a < b;
    }
    if (sortRole() == LibraryEntryModel::AddedRole) {
        const qint64 a = sourceModel()->data(left, sortRole()).toLongLong();
        const qint64 b = sourceModel()->data(right, sortRole()).toLongLong();
        return a == b ? left.row() < right.row() : a > b;
    }
    if (sortRole() == LibraryEntryModel::PlayCountRole) {
        const auto a = sourceModel()->data(left, sortRole()).toLongLong();
        const auto b = sourceModel()->data(right, sortRole()).toLongLong();
        if (a != b) return m_sortKey == QStringLiteral("rare") ? a < b : a > b;
        const int title = m_collator.compare(sourceModel()->data(left, LibraryEntryModel::TitleRole).toString(),
                                             sourceModel()->data(right, LibraryEntryModel::TitleRole).toString());
        return title == 0 ? left.row() < right.row() : title < 0;
    }
    const int order = m_collator.compare(sourceModel()->data(left, sortRole()).toString(),
                                        sourceModel()->data(right, sortRole()).toString());
    return order == 0 ? left.row() < right.row() : order < 0;
}
