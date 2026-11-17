#include "librarymodels.h"
#include "genregrouping.h"
#include "collectionfeatures.h"
#include "searchfilters.h"
#include <QFileInfo>
#include <QUrl>
#include <cmath>

#include <QSet>
#include <QDateTime>
#include <QTimer>

LibraryEntryModel::LibraryEntryModel(QObject *parent) : QAbstractTableModel(parent) {}

int LibraryEntryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_database ? m_ids.size() : m_entries.size();
}

int LibraryEntryModel::columnCount(const QModelIndex &parent) const
{
    // Quick views display column 0. Column 1 carries tag ordering changes because
    // QSortFilterProxyModel checks sorting by changed column, even when only
    // an unrelated data role changed. Column 2 carries collection totals, so
    // duration updates do not re-sort a view ordered by name. Artwork stays in 0.
    return parent.isValid() ? 0 : 3;
}

QVariant LibraryEntryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};
    // Selection bookkeeping needs identifiers only; avoid decoding tag pages.
    if (role == IdentityRole) {
        if (m_database) return m_ids[index.row()];
        const auto &entry = m_entries[index.row()];
        return entry.value(QStringLiteral("fileUrl"), entry.value(QStringLiteral("id")));
    }
    QVariantMap entry;
    if (m_database) {
        const int page = index.row() / 64;
        auto *cached = m_pages.object(page);
        if (!cached) {
            cached = new QVariantList(m_database->page(m_kind, m_ids, page * 64, 64));
            m_pages.insert(page, cached);
        }
        entry = cached->value(index.row() % 64).toMap();
    } else entry = m_entries[index.row()];
    switch (role) {
    case EntryRole: return entry;
    case NameRole: return entry.value(QStringLiteral("name"));
    case TitleRole: return entry.value(QStringLiteral("title"), entry.value(QStringLiteral("name")));
    case ArtistRole: return entry.value(QStringLiteral("artist"), entry.value(QStringLiteral("subtitle")));
    case AlbumRole: return entry.value(QStringLiteral("album"));
    case TrackRole: return entry.value(QStringLiteral("trackSort"));
    case PlayCountRole: return entry.value(QStringLiteral("playCount"), 0);
    case OrderRole: return entry.value(QStringLiteral("playlistOrder"));
    case AddedRole: return entry.value(QStringLiteral("addedAt"));
    case TrackCountRole: return entry.value(QStringLiteral("trackCount"), 0);
    case DurationRole: return entry.value(QStringLiteral("duration"), 0);
    case FilterRole: {
        QVariantMap fields;
        for (const auto &key : {QStringLiteral("title"), QStringLiteral("artist"), QStringLiteral("album"),
                               QStringLiteral("name"), QStringLiteral("subtitle"), QStringLiteral("favorite"),
                               QStringLiteral("albumId"), QStringLiteral("artistId"), QStringLiteral("artistIds"), QStringLiteral("albumArtist"), QStringLiteral("genre"), QStringLiteral("year"), QStringLiteral("date"), QStringLiteral("duration"), QStringLiteral("playlistIds"), QStringLiteral("playCount"), QStringLiteral("addedAt"), QStringLiteral("fileUrl")})
            if (entry.contains(key)) fields.insert(key, entry.value(key));
        return fields;
    }
    default: return {};
    }
}

QHash<int, QByteArray> LibraryEntryModel::roleNames() const
{
    return {{EntryRole, "modelData"}, {NameRole, "sortName"}, {TitleRole, "sortTitle"},
            {ArtistRole, "sortArtist"}, {AlbumRole, "sortAlbum"}, {TrackRole, "sortTrack"}, {PlayCountRole, "qualifiedPlays"}, {OrderRole, "playlistOrder"}, {AddedRole, "addedAt"}, {FilterRole, "filterFields"}, {IdentityRole, "entryIdentity"}, {TrackCountRole, "sortTrackCount"}, {DurationRole, "sortDuration"}};
}

void LibraryEntryModel::synchronize(const QVariantList &entries, const QString &key)
{
    m_synchronizing = true;
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
                                          qMakePair(TitleRole, QStringLiteral("name")),
                                          qMakePair(ArtistRole, QStringLiteral("artist")),
                                          qMakePair(ArtistRole, QStringLiteral("subtitle")),
                                          qMakePair(AlbumRole, QStringLiteral("album")),
                                          qMakePair(TrackRole, QStringLiteral("trackSort")),
                                          qMakePair(PlayCountRole, QStringLiteral("playCount")),
                                          qMakePair(OrderRole, QStringLiteral("playlistOrder")),
                                          qMakePair(AddedRole, QStringLiteral("addedAt")),
                                          qMakePair(TrackCountRole, QStringLiteral("trackCount")),
                                          qMakePair(DurationRole, QStringLiteral("duration"))}) {
                    if (!roles.contains(field.first) && m_entries[row].value(field.second) != found.value().value(field.second))
                        roles.append(field.first);
                }
                for (const auto &key : {QStringLiteral("title"), QStringLiteral("artist"), QStringLiteral("album"),
                                       QStringLiteral("name"), QStringLiteral("subtitle"), QStringLiteral("favorite"),
                                       QStringLiteral("albumId"), QStringLiteral("artistId"), QStringLiteral("artistIds"), QStringLiteral("albumArtist"), QStringLiteral("genre"), QStringLiteral("year"), QStringLiteral("date"), QStringLiteral("duration"), QStringLiteral("playlistIds"), QStringLiteral("playCount"), QStringLiteral("addedAt"), QStringLiteral("fileUrl")}) {
                    if (m_entries[row].value(key) != found.value().value(key)) {
                        roles.append(FilterRole);
                        break;
                    }
                }
                m_entries[row] = found.value();
                QList<int> orderingRoles = roles;
                orderingRoles.removeAll(EntryRole);
                orderingRoles.removeAll(FilterRole);
                orderingRoles.removeAll(TrackCountRole);
                orderingRoles.removeAll(DurationRole);
                // Notify display/filter data and ordering together. Splitting
                // them lets a Quick delegate cache old row data across a move.
                // Artwork-only changes still avoid the ordering column.
                emit dataChanged(index(row, 0), index(row, orderingRoles.isEmpty() ? 0 : 1), roles);
                if (roles.contains(TrackCountRole) || roles.contains(DurationRole) || roles.contains(NameRole))
                    emit dataChanged(index(row, 2), index(row, 2), roles);
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
    m_synchronizing = false;
    emit synchronized();
}

bool LibraryEntryModel::setPlayCount(const QString &fileUrl, qint64 count)
{
    for (int row = 0; row < m_entries.size(); ++row) {
        auto &entry = m_entries[row];
        if (entry.value(QStringLiteral("fileUrl")).toString() != fileUrl) continue;
        if (entry.value(QStringLiteral("playCount")).toLongLong() == count) return false;
        entry[QStringLiteral("playCount")] = count;
        emit dataChanged(index(row, 0), index(row, 1), {EntryRole, PlayCountRole, FilterRole});
        return true;
    }
    return false;
}

void LibraryEntryModel::setDatabase(LibraryDatabase *database, const QString &kind)
{
    m_database = database; m_kind = kind;
}
int LibraryEntryModel::cachedEntryCount() const
{
    if (!m_database) return m_entries.size();
    int total = 0; for (const auto key : m_pages.keys()) total += m_pages.object(key)->size(); return total;
}
void LibraryEntryModel::queryDatabase(const LibraryQuery &filter, bool reset)
{
    if (!m_database) return;
    const auto ids = m_database->ids(m_kind, filter);
    m_synchronizing = true; m_pages.clear();
    const QSet<QString> wanted(ids.cbegin(), ids.cend());
    if (reset && ids.size() == m_ids.size() && wanted == QSet<QString>(m_ids.cbegin(), m_ids.cend())) {
        emit layoutAboutToBeChanged({}, QAbstractItemModel::VerticalSortHint);
        const auto oldIndexes = persistentIndexList();
        QModelIndexList newIndexes;
        QHash<QString, int> positions;
        for (int row = 0; row < ids.size(); ++row) positions[ids[row]] = row;
        for (const auto &old : oldIndexes) newIndexes.append(index(positions.value(m_ids.value(old.row()), old.row()), old.column()));
        m_ids = ids; m_pages.clear(); changePersistentIndexList(oldIndexes, newIndexes);
        emit layoutChanged({}, QAbstractItemModel::VerticalSortHint);
    } else {
        for (int row = m_ids.size() - 1; row >= 0;) {
            if (wanted.contains(m_ids[row])) { --row; continue; }
            int first = row; while (first > 0 && !wanted.contains(m_ids[first - 1])) --first;
            beginRemoveRows({}, first, row); m_ids.remove(first, row - first + 1); m_pages.clear(); endRemoveRows(); row = first - 1;
        }
        for (int row = 0; row < ids.size(); ++row) {
            if (row < m_ids.size() && m_ids[row] == ids[row]) continue;
            const int found = m_ids.indexOf(ids[row], row);
            if (found >= 0) { beginMoveRows({}, found, found, {}, row); m_ids.move(found, row); m_pages.clear(); endMoveRows(); }
            else {
                int end = row + 1; while (end < ids.size() && !m_ids.contains(ids[end])) ++end;
                beginInsertRows({}, row, end - 1);
                for (int i = row; i < end; ++i) m_ids.insert(i, ids[i]);
                m_pages.clear();
                endInsertRows(); row = end - 1;
            }
        }
        m_pages.clear();
        if (!m_ids.isEmpty()) emit dataChanged(index(0, 0), index(m_ids.size() - 1, 1), {EntryRole});
    }
    m_visibleIds = wanted;
    m_synchronizing = false; emit synchronized();
}

LibraryViewModel::LibraryViewModel(QObject *parent) : QSortFilterProxyModel(parent)
{
    m_collator.setCaseSensitivity(Qt::CaseInsensitive);
    m_collator.setNumericMode(true);
    setDynamicSortFilter(true);
    setSortRole(LibraryEntryModel::NameRole);
    setFilterRole(LibraryEntryModel::FilterRole);
    const auto entriesUpdated = [this] {
        m_pendingNotifications = true;
        publishNotifications();
    };
    connect(this, &QAbstractItemModel::dataChanged, this, entriesUpdated);
    connect(this, &QAbstractItemModel::layoutChanged, this, entriesUpdated);
    connect(this, &QAbstractItemModel::rowsInserted, this, entriesUpdated);
    connect(this, &QAbstractItemModel::rowsRemoved, this, entriesUpdated);
    connect(this, &QAbstractItemModel::rowsMoved, this, entriesUpdated);
    connect(this, &QAbstractItemModel::modelReset, this, entriesUpdated);
    sort(1);
    auto *refresh = new QTimer(this);
    refresh->setInterval(60000);
    connect(refresh, &QTimer::timeout, this, [this] {
        if (m_collectionKind == QStringLiteral("smart")) { beginRefilter(); endRefilter(); }
    });
    refresh->start();
}

void LibraryViewModel::setSourceModel(QAbstractItemModel *source)
{
    disconnect(m_batchConnection);
    QSortFilterProxyModel::setSourceModel(source);
    if (auto *entries = qobject_cast<LibraryEntryModel *>(source)) {
        m_batchConnection = connect(entries, &LibraryEntryModel::synchronized, this, [this] {
            m_pendingNotifications = true; publishNotifications();
        });
    }
}

void LibraryViewModel::publishNotifications()
{
    const auto synchronizing = [this] {
        const auto *source = qobject_cast<LibraryEntryModel *>(sourceModel());
        return source && source->synchronizing();
    };
    if (m_updateDepth || m_publishingNotifications || synchronizing()) return;
    m_publishingNotifications = true;
    while (m_pendingNotifications && !m_updateDepth && !synchronizing()) {
        m_pendingNotifications = false;
        const int currentCount = count();
        if (currentCount != m_announcedCount) { m_announcedCount = currentCount; emit countChanged(); }
        ++m_revision; emit entriesChanged();
    }
    m_publishingNotifications = false;
}

void LibraryViewModel::setDatabase(LibraryDatabase *database, const QString &kind)
{
    m_database = database;
    m_databaseEntries = new LibraryEntryModel(this);
    m_databaseEntries->setDatabase(database, kind);
    setDynamicSortFilter(false); sort(-1);
    setSourceModel(m_databaseEntries);
    connect(database, &LibraryDatabase::changed, this, [this, kind](const QString &changed) { if (changed.isEmpty() || changed == kind) refreshDatabase(); });
    refreshDatabase();
}
int LibraryViewModel::cachedEntryCount() const
{
    const auto *source = qobject_cast<LibraryEntryModel *>(sourceModel()); return source ? source->cachedEntryCount() : 0;
}
void LibraryViewModel::refreshDatabase(bool reset)
{
    if (!m_databaseEntries) return;
    m_databaseEntries->queryDatabase({m_search, m_collectionKind, m_collectionId, m_sortKey, m_favoritesOnly, m_playedOnly, m_sortDirection, m_filters}, reset);
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

QStringList LibraryViewModel::fileUrls() const
{
    QStringList result; result.reserve(count());
    for (int row = 0; row < count(); ++row) result.append(data(index(row, 0), LibraryEntryModel::IdentityRole).toString());
    return result;
}
int LibraryViewModel::countMatchingUrls(const QStringList &urls) const
{
    const QSet<QString> selected(urls.cbegin(), urls.cend());
    int matches = 0;
    if (m_databaseEntries) {
        for (const auto &url : selected) matches += m_databaseEntries->containsIdentity(url);
    } else {
        for (int row = 0; row < count(); ++row) matches += selected.contains(data(index(row, 0), LibraryEntryModel::IdentityRole).toString());
    }
    return matches;
}
QVariantList LibraryViewModel::entriesForUrls(const QStringList &urls) const
{
    const QSet<QString> selected(urls.cbegin(), urls.cend());
    QVariantList result;
    for (int row = 0; row < count(); ++row)
        if (selected.contains(data(index(row, 0), LibraryEntryModel::IdentityRole).toString())) result.append(get(row));
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

void LibraryViewModel::setFilters(const QVariantMap &input)
{
    bool valid = false; const auto filters = SearchFilters::normalized(input, &valid);
    if (!valid || filters == m_filters) return;
    beginRefilter(); m_filters = filters; endRefilter(); emit filterChanged();
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
    if (m_database) { refreshDatabase(true); emit filterChanged(); return; }
    setSortRole(key == QStringLiteral("title") ? LibraryEntryModel::TitleRole
                : key == QStringLiteral("artist") ? LibraryEntryModel::ArtistRole
                : key == QStringLiteral("album") ? LibraryEntryModel::AlbumRole
                : key == QStringLiteral("track") ? LibraryEntryModel::TrackRole
                : key == QStringLiteral("plays") ? LibraryEntryModel::PlayCountRole
                : key == QStringLiteral("rare") ? LibraryEntryModel::PlayCountRole
                : key == QStringLiteral("order") ? LibraryEntryModel::OrderRole
                : key == QStringLiteral("added") ? LibraryEntryModel::AddedRole
                : key == QStringLiteral("count") ? LibraryEntryModel::TrackCountRole
                : key == QStringLiteral("duration") ? LibraryEntryModel::DurationRole
                : LibraryEntryModel::NameRole);
    sort(key == QStringLiteral("count") || key == QStringLiteral("duration") ? 2 : 1);
    emit filterChanged();
    if (key == QStringLiteral("plays") || key == QStringLiteral("rare")) invalidate();
}

void LibraryViewModel::setSortDirection(const QString &value)
{
    const auto direction = value.toLower();
    if (direction != QStringLiteral("ascending") && direction != QStringLiteral("descending") && !direction.isEmpty()) return;
    if (m_sortDirection == direction) return;
    m_sortDirection = direction;
    if (m_database) refreshDatabase(true);
    else invalidate();
    emit filterChanged();
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

void LibraryViewModel::setCustomSmartPlaylists(const QVariantList &playlists)
{
    if (m_customSmartPlaylists == playlists) return;
    // SQLite publishes its own change after storing the new rules. Avoid a
    // redundant query against the previous state before that publication.
    if (m_database) { m_customSmartPlaylists = playlists; return; }
    beginRefilter();
    m_customSmartPlaylists = playlists;
    endRefilter();
}

void LibraryViewModel::beginRefilter()
{
    ++m_updateDepth;
    if (m_database) return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
#endif
}

void LibraryViewModel::endRefilter()
{
    if (m_database) refreshDatabase(true);
    else {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    endFilterChange(Direction::Rows);
#else
    invalidateFilter();
#endif
    }
    --m_updateDepth;
    m_pendingNotifications = true;
    publishNotifications();
}

bool LibraryViewModel::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    if (m_database) return true;
    const auto entry = sourceModel()->data(sourceModel()->index(row, 0, parent), LibraryEntryModel::FilterRole).toMap();
    if (!SearchFilters::matches(entry, m_filters)) return false;
    if (m_favoritesOnly && !entry.value(QStringLiteral("favorite")).toBool()) return false;
    if (m_playedOnly && entry.value(QStringLiteral("playCount")).toLongLong() <= 0) return false;
    if (!m_collectionId.isEmpty()) {
        if (m_collectionKind == QStringLiteral("smart")) {
            const qint64 plays = entry.value(QStringLiteral("playCount")).toLongLong();
            if (m_collectionId == QStringLiteral("recent") && entry.value(QStringLiteral("addedAt")).toLongLong() < QDateTime::currentMSecsSinceEpoch() - 30LL * 86400000) return false;
            if (m_collectionId == QStringLiteral("never") && plays != 0) return false;
            if (m_collectionId == QStringLiteral("rare") && (plays < 1 || plays > 4)) return false;
        } else if (m_collectionKind == QStringLiteral("customSmart")) {
            bool found = false;
            for (const auto &saved : m_customSmartPlaylists) {
                const auto playlist = saved.toMap();
                if (playlist.value("id").toString() != m_collectionId) continue;
                found = CollectionRules::matches(entry, playlist.value("rules").toMap());
                break;
            }
            if (!found) return false;
        } else if (m_collectionKind == QStringLiteral("folder")) {
            if (!CollectionRules::inFolder(QFileInfo(QUrl(entry.value("fileUrl").toString()).toLocalFile()).absolutePath(), m_collectionId)) return false;
        } else if (m_collectionKind == QStringLiteral("playlist")) {
            if (!entry.value(QStringLiteral("playlistIds")).toStringList().contains(m_collectionId)) return false;
        } else if (m_collectionKind == QStringLiteral("artist") && entry.value(QStringLiteral("artistIds")).isValid()) {
            if (!entry.value(QStringLiteral("artistIds")).toStringList().contains(m_collectionId)) return false;
        } else if (m_collectionKind == QStringLiteral("genre")) {
            if (GenreGrouping::idFor(entry.value("genre").toString()) != m_collectionId) return false;
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
        + entry.value(QStringLiteral("genre")).toString() + QLatin1Char(' ')
        + entry.value(QStringLiteral("name")).toString() + QLatin1Char(' ')
        + entry.value(QStringLiteral("subtitle")).toString();
    return text.contains(m_search, Qt::CaseInsensitive);
}

bool LibraryViewModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    const bool descending = m_sortDirection.isEmpty()
        ? m_sortKey == QStringLiteral("plays") || m_sortKey == QStringLiteral("added")
        : m_sortDirection == QStringLiteral("descending");
    if (sortRole() == LibraryEntryModel::OrderRole) {
        const int a = sourceModel()->data(left, sortRole()).toMap().value(m_collectionId, INT_MAX).toInt();
        const int b = sourceModel()->data(right, sortRole()).toMap().value(m_collectionId, INT_MAX).toInt();
        return a == b ? left.row() < right.row() : descending ? a > b : a < b;
    }
    if (sortRole() == LibraryEntryModel::AddedRole) {
        const qint64 a = sourceModel()->data(left, sortRole()).toLongLong();
        const qint64 b = sourceModel()->data(right, sortRole()).toLongLong();
        return a == b ? left.row() < right.row() : descending ? a > b : a < b;
    }
    if (sortRole() == LibraryEntryModel::TrackCountRole || sortRole() == LibraryEntryModel::DurationRole) {
        const auto a = sourceModel()->data(left, sortRole()).toLongLong();
        const auto b = sourceModel()->data(right, sortRole()).toLongLong();
        if (a != b) return descending ? a > b : a < b;
        const int name = m_collator.compare(sourceModel()->data(left, LibraryEntryModel::NameRole).toString(),
                                            sourceModel()->data(right, LibraryEntryModel::NameRole).toString());
        return name == 0 ? left.row() < right.row() : name < 0;
    }
    if (sortRole() == LibraryEntryModel::PlayCountRole) {
        const auto a = sourceModel()->data(left, sortRole()).toLongLong();
        const auto b = sourceModel()->data(right, sortRole()).toLongLong();
        if (a != b) return descending ? a > b : a < b;
        const int title = m_collator.compare(sourceModel()->data(left, LibraryEntryModel::TitleRole).toString(),
                                             sourceModel()->data(right, LibraryEntryModel::TitleRole).toString());
        return title == 0 ? left.row() < right.row() : title < 0;
    }
    const int order = m_collator.compare(sourceModel()->data(left, sortRole()).toString(),
                                        sourceModel()->data(right, sortRole()).toString());
    return order == 0 ? left.row() < right.row() : descending ? order > 0 : order < 0;
}
