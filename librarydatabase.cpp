#include "librarydatabase.h"
#include "genregrouping.h"
#include "collectionfeatures.h"
#include "searchfilters.h"
#include "librarycollation.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QUrl>
#include <algorithm>
#include <numeric>

namespace {
QByteArray encoded(const QVariant &value) { return QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact); }
QVariant decoded(const QVariant &value) { return QJsonDocument::fromJson(value.toByteArray()).toVariant(); }
QString folded(const QString &text) { return text.normalized(QString::NormalizationForm_KC).toCaseFolded(); }
QString searchableText(const QVariantMap &row)
{
    QString text;
    for (const auto &field : {"title", "artist", "album", "albumArtist", "genre", "name", "subtitle"}) text += row.value(field).toString() + ' ';
    return folded(text);
}
}
LibraryDatabase::LibraryDatabase(const QString &path, QObject *parent) : QObject(parent)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    m_db = QSqlDatabase::addDatabase("QSQLITE", "musica-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
    m_db.setDatabaseName(path);
    if (!m_db.open()) { m_error = m_db.lastError().text(); return; }
    const QStringList statements{
        "PRAGMA journal_mode=WAL", "PRAGMA synchronous=NORMAL", "PRAGMA cache_size=-2048", "PRAGMA busy_timeout=3000",
        "CREATE TABLE IF NOT EXISTS entries(kind TEXT NOT NULL,id TEXT NOT NULL,pos INTEGER,payload BLOB NOT NULL,nrank INTEGER,trank INTEGER,arrank INTEGER,alrank INTEGER,trackrank INTEGER,plays INTEGER,added INTEGER,favorite INTEGER,search TEXT,failed INTEGER,PRIMARY KEY(kind,id))",
        "CREATE TABLE IF NOT EXISTS membership(kind TEXT NOT NULL,groupid TEXT NOT NULL,songid TEXT NOT NULL,ord INTEGER,PRIMARY KEY(kind,groupid,songid))",
        "CREATE INDEX IF NOT EXISTS membership_song ON membership(songid)",
        "CREATE INDEX IF NOT EXISTS entry_title ON entries(kind,trank)",
        "CREATE INDEX IF NOT EXISTS entry_plays ON entries(kind,plays DESC,trank)",
        "CREATE INDEX IF NOT EXISTS entry_name ON entries(kind,nrank)",
        "CREATE TABLE IF NOT EXISTS state(key TEXT PRIMARY KEY,value BLOB)"};
    for (const auto &statement : statements) {
        QSqlQuery query(m_db);
        if (!query.exec(statement)) { m_error = query.lastError().text(); return; }
    }
    m_ready = migrateCollectionTools() && migrateSearchFilters() && migrateCollectionSorting() && migrateGenres();
}
LibraryDatabase::~LibraryDatabase()
{
    const auto name = m_db.connectionName(); m_db.close(); m_db = QSqlDatabase(); QSqlDatabase::removeDatabase(name);
}
QVariant LibraryDatabase::value(const QString &key) const
{
    QSqlQuery query(m_db); query.prepare("SELECT value FROM state WHERE key=?"); query.addBindValue(key);
    return query.exec() && query.next() ? decoded(query.value(0)).toList().value(0) : QVariant();
}
void LibraryDatabase::setValue(const QString &key, const QVariant &value)
{
    QSqlQuery query(m_db); query.prepare("INSERT OR REPLACE INTO state(key,value) VALUES(?,?)");
    query.addBindValue(key); query.addBindValue(encoded(QVariantList{value})); query.exec();
}
bool LibraryDatabase::hasSnapshot() const { return valid() && value("snapshot").toBool(); }
void LibraryDatabase::clear()
{
    QSqlQuery query(m_db); query.exec("DELETE FROM entries"); query.exec("DELETE FROM membership"); query.exec("DELETE FROM state");
    setValue("collectionSortingVersion", 1);
    setValue("genresVersion", 1);
    setValue("collectionToolsVersion", 2);
    setValue("searchFiltersVersion", 1);
}
bool LibraryDatabase::migrateSearchFilters()
{
    if (value("searchFiltersVersion").toInt() >= 1) return true;
    if (!m_db.transaction()) return false;
    QSqlQuery query(m_db); QStringList columns;
    if (!query.exec("PRAGMA table_info(entries)")) { m_db.rollback(); return false; }
    while (query.next()) columns.append(query.value(1).toString()); query.finish();
    for (const auto &field : {"searchtitle", "searchartist", "searchalbum"})
        if (!columns.contains(field) && !query.exec("ALTER TABLE entries ADD COLUMN " + QString(field) + " TEXT")) { m_error = query.lastError().text(); m_db.rollback(); return false; }
    QSqlQuery songs(m_db), update(m_db); songs.setForwardOnly(true);
    if (!songs.exec("SELECT id,payload FROM entries WHERE kind='song'")) { m_db.rollback(); return false; }
    update.prepare("UPDATE entries SET searchtitle=?,searchartist=?,searchalbum=? WHERE kind='song' AND id=?");
    while (songs.next()) {
        const auto track = decoded(songs.value(1)).toMap();
        update.bindValue(0, folded(track.value("title").toString())); update.bindValue(1, folded(track.value("artist").toString()));
        update.bindValue(2, folded(track.value("album").toString())); update.bindValue(3, songs.value(0));
        if (!update.exec()) { m_error = update.lastError().text(); m_db.rollback(); return false; }
    }
    songs.finish(); setValue("searchFiltersVersion", 1);
    if (!m_db.commit()) { m_error = m_db.lastError().text(); m_db.rollback(); return false; } return true;
}
bool LibraryDatabase::migrateCollectionTools()
{
    if (value("collectionToolsVersion").toInt() >= 2) return true;
    if (!m_db.transaction()) { m_error = m_db.lastError().text(); return false; }
    const auto fail = [this](const QString &message) { m_error = message; m_db.rollback(); return false; };
    QSqlQuery query(m_db);
    QStringList columns;
    if (!query.exec("PRAGMA table_info(entries)")) return fail(query.lastError().text());
    while (query.next()) columns.append(query.value(1).toString());
    query.finish();
    const QList<QPair<QString, QString>> additions{{"folder", "TEXT"}, {"genre", "TEXT"}, {"year", "INTEGER NOT NULL DEFAULT 0"}};
    for (const auto &column : additions)
        if (!columns.contains(column.first) && !query.exec("ALTER TABLE entries ADD COLUMN " + column.first + ' ' + column.second))
            return fail(query.lastError().text());
    // Derive the new index only from saved metadata. Startup never opens audio.
    QSqlQuery songs(m_db), update(m_db);
    songs.setForwardOnly(true);
    if (!songs.exec("SELECT id,payload FROM entries WHERE kind='song'")) return fail(songs.lastError().text());
    update.prepare("UPDATE entries SET folder=?,genre=?,year=? WHERE kind='song' AND id=?");
    while (songs.next()) {
        const auto song = decoded(songs.value(1)).toMap();
        update.bindValue(0, QDir::cleanPath(QFileInfo(QUrl(songs.value(0).toString()).toLocalFile()).absolutePath()));
        update.bindValue(1, CollectionRules::folded(song.value("genre").toString().trimmed()));
        update.bindValue(2, CollectionRules::releaseYear(song));
        update.bindValue(3, songs.value(0));
        if (!update.exec()) return fail(update.lastError().text());
    }
    songs.finish();
    if (!query.exec("CREATE INDEX IF NOT EXISTS entry_folder ON entries(kind,folder)")) return fail(query.lastError().text());
    setValue("collectionToolsVersion", 2);
    if (!m_db.commit()) return fail(m_db.lastError().text());
    return true;
}
bool LibraryDatabase::migrateGenres()
{
    if (value("genresVersion").toInt() >= 1) return true;
    if (!m_db.transaction()) { m_error = m_db.lastError().text(); return false; }
    const auto fail = [this](const QString &message) { m_error = message; m_db.rollback(); return false; };
    QSqlQuery query(m_db), songs(m_db), insert(m_db), search(m_db);
    if (!query.exec("DELETE FROM membership WHERE kind='genre'")) return fail(query.lastError().text());
    insert.prepare("INSERT INTO membership(kind,groupid,songid,ord) VALUES('genre',?,?,0)");
    search.prepare("UPDATE entries SET search=? WHERE kind='song' AND id=?");
    songs.setForwardOnly(true);
    if (!songs.exec("SELECT id,payload FROM entries WHERE kind='song'")) return fail(songs.lastError().text());
    GenreGrouping::Builder genres;
    while (songs.next()) {
        const auto track = decoded(songs.value(1)).toMap();
        genres.add(track);
        insert.bindValue(0, GenreGrouping::idFor(track.value("genre").toString())); insert.bindValue(1, songs.value(0));
        if (!insert.exec()) return fail(insert.lastError().text());
        search.bindValue(0, searchableText(track)); search.bindValue(1, songs.value(0));
        if (!search.exec()) return fail(search.lastError().text());
    }
    songs.finish();
    const auto groups = genres.entries();
    if (!writeRows("genre", groups, "id")) return fail(m_error);
    setValue("hash/genre", QString::fromLatin1(QCryptographicHash::hash(encoded(groups), QCryptographicHash::Sha256).toHex()));
    setValue("genresVersion", 1);
    if (!m_db.commit()) return fail(m_db.lastError().text());
    return true;
}
bool LibraryDatabase::migrateCollectionSorting()
{
    if (value("collectionSortingVersion").toInt() >= 1) return true;
    if (!m_db.transaction()) { m_error = m_db.lastError().text(); return false; }
    const auto fail = [this](const QString &message) { m_error = message; m_db.rollback(); return false; };
    QSqlQuery query(m_db);
    QStringList columns;
    if (!query.exec("PRAGMA table_info(entries)")) return fail(query.lastError().text());
    while (query.next()) columns.append(query.value(1).toString());
    query.finish();
    for (const auto &column : {QStringLiteral("duration"), QStringLiteral("trackcount")}) {
        if (!columns.contains(column) && !query.exec("ALTER TABLE entries ADD COLUMN " + column + " INTEGER NOT NULL DEFAULT 0"))
            return fail(query.lastError().text());
    }
    // Upgrade saved metadata only. Audio files are never opened or rescanned.
    // Stream song payloads so migrating a large library keeps memory bounded.
    QSqlQuery songs(m_db), update(m_db);
    songs.setForwardOnly(true);
    if (!songs.exec("SELECT id,payload FROM entries WHERE kind='song'")) return fail(songs.lastError().text());
    update.prepare("UPDATE entries SET duration=? WHERE kind='song' AND id=?");
    while (songs.next()) {
        update.bindValue(0, qMax<qint64>(0, decoded(songs.value(1)).toMap().value("duration").toLongLong()));
        update.bindValue(1, songs.value(0));
        if (!update.exec()) return fail(update.lastError().text());
    }
    songs.finish();
    for (const auto &kind : {QStringLiteral("album"), QStringLiteral("artist")}) {
        auto groups = all(kind);
        QSqlQuery totals(m_db);
        totals.prepare("SELECT count(*),coalesce(sum(s.duration),0),coalesce(max(s.added),0) FROM membership m "
                       "JOIN entries s ON s.kind='song' AND s.id=m.songid WHERE m.kind=? AND m.groupid=?");
        for (auto &entry : groups) {
            auto group = entry.toMap();
            totals.bindValue(0, kind); totals.bindValue(1, group.value("id"));
            if (!totals.exec() || !totals.next()) return fail(totals.lastError().text());
            group["trackCount"] = totals.value(0); group["duration"] = totals.value(1); group["addedAt"] = totals.value(2);
            entry = group;
        }
        totals.finish();
        if (!writeRows(kind, groups, "id")) return fail(m_error);
        setValue("hash/" + kind, QString::fromLatin1(QCryptographicHash::hash(encoded(groups), QCryptographicHash::Sha256).toHex()));
    }
    setValue("collectionSortingVersion", 1);
    if (!m_db.commit()) return fail(m_db.lastError().text());
    return true;
}
bool LibraryDatabase::writeRows(const QString &kind, const QVariantList &rows, const QString &key)
{
    // Store locale-aware numeric ranks, so SQLite can order without decoding
    // tag JSON or asking the proxy to materialize the whole library.
    QList<QVector<int>> ranks;
    auto collator = libraryCollator();
    for (const auto &field : {"name", "title", "artist", "album", "trackSort"}) {
        QVector<int> order(rows.size()), rank(rows.size()); std::iota(order.begin(), order.end(), 0);
        const auto fieldName = QString::fromLatin1(field);
        const auto fallback = fieldName == "title" ? QStringLiteral("name") : fieldName == "artist" ? QStringLiteral("subtitle") : fieldName;
        const auto text = [&](int row) {
            const auto entry = rows[row].toMap();
            return entry.value(fieldName, entry.value(fallback)).toString();
        };
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return collator.compare(text(a), text(b)) < 0; });
        for (int i = 0; i < order.size(); ++i) rank[order[i]] = i;
        ranks.append(rank);
    }
    QSqlQuery remove(m_db); remove.prepare("DELETE FROM entries WHERE kind=?"); remove.addBindValue(kind);
    if (!remove.exec()) return false;
    QSqlQuery insert(m_db);
    insert.prepare("INSERT INTO entries(kind,id,pos,payload,nrank,trank,arrank,alrank,trackrank,plays,added,favorite,search,failed,duration,trackcount,folder,genre,year,searchtitle,searchartist,searchalbum) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    for (int i = 0; i < rows.size(); ++i) {
        const auto row = rows[i].toMap();
        insert.bindValue(0, kind); insert.bindValue(1, row.value(key)); insert.bindValue(2, i); insert.bindValue(3, encoded(row));
        for (int r = 0; r < ranks.size(); ++r) insert.bindValue(4 + r, ranks[r][i]);
        insert.bindValue(9, row.value("playCount", 0)); insert.bindValue(10, row.value("addedAt", 0)); insert.bindValue(11, row.value("favorite", false));
        insert.bindValue(12, searchableText(row)); insert.bindValue(13, !row.value("metadataError").toString().isEmpty());
        insert.bindValue(14, qMax<qint64>(0, row.value("duration").toLongLong())); insert.bindValue(15, row.value("trackCount", 0));
        insert.bindValue(16, kind == "song" ? QDir::cleanPath(QFileInfo(QUrl(row.value("fileUrl").toString()).toLocalFile()).absolutePath()) : QString());
        insert.bindValue(17, CollectionRules::folded(row.value("genre").toString().trimmed()));
        insert.bindValue(18, CollectionRules::releaseYear(row));
        insert.bindValue(19, folded(row.value("title").toString())); insert.bindValue(20, folded(row.value("artist").toString())); insert.bindValue(21, folded(row.value("album").toString()));
        if (!insert.exec()) { m_error = insert.lastError().text(); return false; }
    }
    return true;
}
bool LibraryDatabase::replace(const QVariantList &songs, const QVariantList &albums, const QVariantList &artists, const QVariantList &playlists)
{
    GenreGrouping::Builder genres;
    for (const auto &track : songs) genres.add(track.toMap());
    const QList<QPair<QString, QVariantList>> datasets{{"song", songs}, {"album", albums}, {"artist", artists}, {"playlist", playlists}, {"genre", genres.entries()}};
    QHash<QString, QByteArray> hashes;
    QStringList changedKinds;
    for (const auto &dataset : datasets) {
        const auto hash = QCryptographicHash::hash(encoded(dataset.second), QCryptographicHash::Sha256).toHex();
        hashes[dataset.first] = hash;
        if (value("hash/" + dataset.first).toString() != QString::fromLatin1(hash)) changedKinds.append(dataset.first);
    }
    if (changedKinds.isEmpty()) return true;
    if (!valid() || !m_db.transaction()) return false;
    bool ok = true;
    for (const auto &dataset : datasets)
        if (changedKinds.contains(dataset.first)) ok = ok && writeRows(dataset.first, dataset.second, dataset.first == "song" ? "fileUrl" : "id");
    QSqlQuery query(m_db);
    if (changedKinds.contains("song")) ok = ok && query.exec("DELETE FROM membership");
    query.prepare("INSERT OR REPLACE INTO membership(kind,groupid,songid,ord) VALUES(?,?,?,?)");
    for (const auto &value : songs) {
        if (!ok || !changedKinds.contains("song")) break;
        const auto song = value.toMap(); const auto url = song.value("fileUrl");
        const auto add = [&](const QString &kind, const QString &id, int order) {
            query.bindValue(0, kind); query.bindValue(1, id); query.bindValue(2, url); query.bindValue(3, order);
            return query.exec();
        };
        ok = add("album", song.value("albumId").toString(), 0);
        ok = ok && add("genre", GenreGrouping::idFor(song.value("genre").toString()), 0);
        for (const auto &id : song.value("artistIds").toStringList()) ok = ok && add("artist", id, 0);
        const auto positions = song.value("playlistOrder").toMap();
        for (auto it = positions.cbegin(); it != positions.cend(); ++it) ok = ok && add("playlist", it.key(), it.value().toInt());
    }
    if (!ok) { m_db.rollback(); return false; }
    for (const auto &kind : changedKinds) setValue("hash/" + kind, QString::fromLatin1(hashes.value(kind)));
    setValue("snapshot", true);
    if (!m_db.commit()) { m_error = m_db.lastError().text(); return false; }
    emit changed(changedKinds.size() == 1 ? changedKinds.first() : QString()); return true;
}
QStringList LibraryDatabase::ids(const QString &kind, const LibraryQuery &filter) const
{
    QString statement = "SELECT id FROM entries WHERE kind=?";
    QVariantList values{kind};
    if (filter.favorites) statement += " AND favorite=1";
    if (filter.played) statement += " AND plays>0";
    if (!filter.search.trimmed().isEmpty()) { statement += " AND instr(search,?)>0"; values.append(folded(filter.search.trimmed())); }
    if (kind == "song") statement += SearchFilters::sql(filter.filters, &values);
    if (!filter.collectionId.isEmpty()) {
        if (kind == "album" && filter.collectionKind == "artist") {
            statement += " AND id IN (SELECT a.groupid FROM membership a JOIN membership b ON a.songid=b.songid WHERE a.kind='album' AND b.kind='artist' AND b.groupid=?)";
            values.append(filter.collectionId);
        } else if (filter.collectionKind == "smart") {
            if (filter.collectionId == "never") statement += " AND plays=0";
            else if (filter.collectionId == "rare") statement += " AND plays BETWEEN 1 AND 4";
            else if (filter.collectionId == "recent") { statement += " AND added>=?"; values.append(QDateTime::currentMSecsSinceEpoch() - 30LL * 86400000); }
        } else if (filter.collectionKind == "customSmart") {
            bool found = false;
            for (const auto &entry : value("customSmartPlaylists").toList()) {
                const auto playlist = entry.toMap();
                if (playlist.value("id").toString() != filter.collectionId) continue;
                statement += CollectionRules::sql(playlist.value("rules").toMap(), &values); found = true; break;
            }
            if (!found) statement += " AND 0";
        } else if (filter.collectionKind == "folder") {
            const auto path = QDir::cleanPath(filter.collectionId);
            const auto prefix = path.endsWith('/') ? path : path + '/';
            statement += " AND (folder=? OR substr(folder,1,length(?))=?)";
            values.append(path); values.append(prefix); values.append(prefix);
        } else { statement += " AND id IN (SELECT songid FROM membership WHERE kind=? AND groupid=?)"; values.append(filter.collectionKind); values.append(filter.collectionId); }
    }
    const bool descending = filter.sortDirection.isEmpty() ? filter.sort == "plays" || filter.sort == "added" : filter.sortDirection == "descending";
    const auto direction = descending ? QStringLiteral(" DESC") : QStringLiteral(" ASC");
    if (filter.sort == "plays" || filter.sort == "rare") statement += " ORDER BY plays" + direction + ",trank,pos";
    else if (filter.sort == "added") statement += " ORDER BY added" + direction + ",pos";
    else if (filter.sort == "count" || filter.sort == "duration") statement += " ORDER BY " + QString(filter.sort == "count" ? "trackcount" : "duration") + direction + ",nrank,pos";
    else if (filter.sort == "order") { statement += " ORDER BY (SELECT ord FROM membership WHERE kind='playlist' AND groupid=? AND songid=entries.id)" + direction + ",pos"; values.append(filter.collectionId); }
    else {
        const auto rank = filter.sort == "artist" ? "arrank" : filter.sort == "album" ? "alrank" : filter.sort == "track" ? "trackrank" : filter.sort == "name" ? "nrank" : "trank";
        statement += " ORDER BY " + QString::fromLatin1(rank) + direction + ",pos";
    }
    QSqlQuery query(m_db); query.prepare(statement); for (const auto &value : values) query.addBindValue(value);
    QStringList result; if (query.exec()) while (query.next()) result.append(query.value(0).toString());
    return result;
}
QStringList LibraryDatabase::matchingSongUrls(const QStringList &urls, const QString &search) const
{
    const auto selected = QSet<QString>(urls.cbegin(), urls.cend()).values();
    QStringList found;
    for (int first = 0; first < selected.size(); first += 128) {
        const auto batch = selected.mid(first, 128);
        QStringList placeholders; for (int i = 0; i < batch.size(); ++i) placeholders.append("?");
        QString statement = "SELECT id FROM entries WHERE kind='song' AND id IN (" + placeholders.join(',') + ')';
        if (!search.trimmed().isEmpty()) statement += " AND instr(search,?)>0";
        QSqlQuery query(m_db); query.prepare(statement);
        for (const auto &url : batch) query.addBindValue(url);
        if (!search.trimmed().isEmpty()) query.addBindValue(folded(search.trimmed()));
        if (query.exec()) while (query.next()) found.append(query.value(0).toString());
    }
    return found;
}
QStringList LibraryDatabase::matchingMetadataNames(const QString &kind, const QString &text) const
{
    const auto needle = folded(text.trimmed());
    if (needle.isEmpty() || (kind != "artist" && kind != "album" && kind != "genre")) return {};
    QSqlQuery query(m_db);
    query.setForwardOnly(true);
    query.prepare("SELECT payload FROM entries WHERE kind=? AND instr(search,?)>0 ORDER BY nrank");
    query.addBindValue(kind); query.addBindValue(needle);
    QStringList result; QSet<QString> seen;
    if (query.exec()) while (query.next() && result.size() < 8) {
        const auto name = decoded(query.value(0)).toMap().value("name").toString().trimmed();
        const auto key = folded(name);
        // Collection search also includes subtitles; only suggest matching names.
        if (!key.contains(needle) || seen.contains(key)) continue;
        seen.insert(key); result.append(name);
    }
    return result;
}

QVariantMap LibraryDatabase::entry(const QString &kind, const QString &id) const
{
    QSqlQuery query(m_db); query.prepare("SELECT payload FROM entries WHERE kind=? AND id=?"); query.addBindValue(kind); query.addBindValue(id);
    return query.exec() && query.next() ? decoded(query.value(0)).toMap() : QVariantMap();
}
QVariantList LibraryDatabase::page(const QString &kind, const QStringList &keys, int first, int length) const
{
    const auto selected = keys.mid(first, length); if (selected.isEmpty()) return {};
    if (selected.size() > 128) {
        QVariantList result;
        for (int row = 0; row < selected.size(); row += 128) result += page(kind, selected, row, 128);
        return result;
    }
    QStringList placeholders; for (int i = 0; i < selected.size(); ++i) placeholders.append("?");
    QSqlQuery query(m_db); query.prepare("SELECT id,payload FROM entries WHERE kind=? AND id IN (" + placeholders.join(',') + ')');
    query.addBindValue(kind); for (const auto &key : selected) query.addBindValue(key);
    QHash<QString, QVariantMap> found;
    if (query.exec()) while (query.next()) found.insert(query.value(0).toString(), decoded(query.value(1)).toMap());
    QVariantList result; for (const auto &key : selected) result.append(found.value(key)); return result;
}
QVariantList LibraryDatabase::all(const QString &kind) const
{
    QSqlQuery query(m_db); query.prepare("SELECT payload FROM entries WHERE kind=? ORDER BY pos"); query.addBindValue(kind);
    QVariantList result; if (query.exec()) while (query.next()) result.append(decoded(query.value(0))); return result;
}
int LibraryDatabase::count(const QString &kind) const
{
    QSqlQuery query(m_db); query.prepare("SELECT count(*) FROM entries WHERE kind=?"); query.addBindValue(kind);
    return query.exec() && query.next() ? query.value(0).toInt() : 0;
}
int LibraryDatabase::favoriteCount() const
{
    QSqlQuery query(m_db); return query.exec("SELECT count(*) FROM entries WHERE kind='song' AND favorite=1") && query.next() ? query.value(0).toInt() : 0;
}
int LibraryDatabase::failedCount() const
{
    QSqlQuery query(m_db); return query.exec("SELECT count(*) FROM entries WHERE kind='song' AND failed=1") && query.next() ? query.value(0).toInt() : 0;
}
bool LibraryDatabase::updateSong(const QString &url, const QVariantMap &track)
{
    QSqlQuery query(m_db); query.prepare("UPDATE entries SET payload=?,plays=?,favorite=?,search=?,searchtitle=?,searchartist=?,searchalbum=?,genre=?,year=?,duration=? WHERE kind='song' AND id=?");
    query.addBindValue(encoded(track)); query.addBindValue(track.value("playCount", 0)); query.addBindValue(track.value("favorite", false));
    query.addBindValue(searchableText(track)); query.addBindValue(folded(track.value("title").toString()));
    query.addBindValue(folded(track.value("artist").toString())); query.addBindValue(folded(track.value("album").toString()));
    query.addBindValue(CollectionRules::folded(track.value("genre").toString().trimmed())); query.addBindValue(CollectionRules::releaseYear(track));
    query.addBindValue(qMax<qint64>(0, track.value("duration").toLongLong())); query.addBindValue(url);
    if (!query.exec()) return false;
    emit changed("song"); return true;
}
QVariantMap LibraryDatabase::playCounts() const
{
    QSqlQuery query(m_db); QVariantMap counts;
    if (query.exec("SELECT id,plays FROM entries WHERE kind='song' AND plays>0")) while (query.next()) counts[query.value(0).toString()] = query.value(1);
    return counts;
}
QVariantMap LibraryDatabase::smartCounts() const
{
    QSqlQuery query(m_db);
    query.prepare("SELECT coalesce(sum(added>=?),0),coalesce(sum(plays=0),0),coalesce(sum(plays BETWEEN 1 AND 4),0) FROM entries WHERE kind='song'");
    query.addBindValue(QDateTime::currentMSecsSinceEpoch() - 30LL * 86400000);
    if (!query.exec() || !query.next()) return {};
    return {{"recent", query.value(0)}, {"never", query.value(1)}, {"rare", query.value(2)}};
}

int LibraryDatabase::customSmartCount(const QVariantMap &rules) const
{
    QVariantList values;
    const auto where = CollectionRules::sql(rules, &values);
    QSqlQuery query(m_db); query.prepare("SELECT count(*) FROM entries WHERE kind='song'" + where);
    for (const auto &value : values) query.addBindValue(value);
    return query.exec() && query.next() ? query.value(0).toInt() : 0;
}

void LibraryDatabase::setCustomSmartPlaylists(const QVariantList &playlists)
{
    if (value("customSmartPlaylists").toList() == playlists) return;
    setValue("customSmartPlaylists", playlists);
    emit changed("song");
}

QVariantMap LibraryDatabase::folderSummary(const QString &input) const
{
    const auto path = QDir::cleanPath(input);
    const auto prefix = path.endsWith('/') ? path : path + '/';
    QSqlQuery query(m_db);
    query.prepare("SELECT count(*),coalesce(sum(duration),0),coalesce(max(added),0) FROM entries WHERE kind='song' AND (folder=? OR substr(folder,1,length(?))=?)");
    query.addBindValue(path); query.addBindValue(prefix); query.addBindValue(prefix);
    if (!query.exec() || !query.next()) return {};
    return {{"id", path}, {"name", QFileInfo(path).fileName().isEmpty() ? path : QFileInfo(path).fileName()},
            {"subtitle", path}, {"trackCount", query.value(0)}, {"duration", query.value(1)}, {"addedAt", query.value(2)}};
}

QVariantList LibraryDatabase::folderChildren(const QString &input) const
{
    const auto path = QDir::cleanPath(input);
    const auto prefix = path.endsWith('/') ? path : path + '/';
    QSqlQuery query(m_db);
    // Aggregate stored directory names in SQLite before constructing the small
    // list of children; no complete song maps or filesystem scan are needed.
    query.prepare("SELECT folder,count(*),coalesce(sum(duration),0),coalesce(max(added),0) FROM entries WHERE kind='song' AND substr(folder,1,length(?))=? GROUP BY folder");
    query.addBindValue(prefix); query.addBindValue(prefix);
    QMap<QString, QVariantMap> grouped;
    if (query.exec()) while (query.next()) {
        const QString relative = query.value(0).toString().mid(prefix.size());
        const QString child = relative.section('/', 0, 0);
        if (child.isEmpty()) continue;
        const QString fullPath = prefix + child;
        auto item = grouped.value(fullPath, QVariantMap{{"id", fullPath}, {"name", child}, {"subtitle", fullPath}, {"trackCount", 0}, {"duration", 0LL}, {"addedAt", 0LL}});
        item["trackCount"] = item.value("trackCount").toInt() + query.value(1).toInt();
        item["duration"] = item.value("duration").toLongLong() + query.value(2).toLongLong();
        item["addedAt"] = qMax(item.value("addedAt").toLongLong(), query.value(3).toLongLong());
        grouped[fullPath] = item;
    }
    QVariantList result; for (const auto &item : grouped) result.append(item);
    auto collator = libraryCollator();
    std::sort(result.begin(), result.end(), [&collator](const auto &a, const auto &b) { return collator.compare(a.toMap().value("name").toString(), b.toMap().value("name").toString()) < 0; });
    return result;
}
