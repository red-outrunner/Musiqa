#include "collectionfeatures.h"
#include "tracklibrary.h"

#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QUrl>
#include <QUuid>
#include <algorithm>

namespace {
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QString clockText(qint64 ms)
{
    const auto seconds = qMax<qint64>(0, ms / 1000);
    return seconds >= 3600 ? QStringLiteral("%1:%2:%3").arg(seconds / 3600).arg(seconds / 60 % 60, 2, 10, QLatin1Char('0')).arg(seconds % 60, 2, 10, QLatin1Char('0'))
                           : QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}
QVariantList savedSmart(const QSettings &settings) { return settings.value("collections/smart").toList(); }
}

QString CollectionRules::folded(const QString &value) { return value.normalized(QString::NormalizationForm_KC).toCaseFolded(); }
int CollectionRules::releaseYear(const QVariantMap &track)
{
    // Portable tag editing stores DATE. An explicitly cleared DATE must also
    // override an older cached year, while older snapshots may have only year.
    const auto text = track.contains("date") ? track.value("date").toString() : track.value("year").toString();
    bool valid = false; const auto year = text.trimmed().left(4).toInt(&valid);
    return valid && year > 0 && year <= 9999 ? year : 0;
}

QVariantMap CollectionRules::normalize(const QVariantMap &input, QString *error)
{
    if (error) error->clear();
    QVariantMap result;
    const auto fail = [error](const QString &message) { if (error) *error = message; return QVariantMap(); };
    const auto genre = input.value("genre").toString().trimmed();
    if (genre.size() > 200) return fail(QObject::tr("Genre must be 200 characters or fewer."));
    if (!genre.isEmpty()) result["genre"] = genre;
    QString favorite = input.value("favorite", input.value("favoritesOnly").toBool() ? QStringLiteral("yes") : QStringLiteral("any")).toString();
    if (favorite != "any" && favorite != "yes" && favorite != "no") return fail(QObject::tr("Choose any songs, favorites, or non-favorites."));
    if (favorite != "any") result["favorite"] = favorite;
    for (const auto &key : {QStringLiteral("yearFrom"), QStringLiteral("yearTo"), QStringLiteral("minPlays"), QStringLiteral("maxPlays")}) {
        if (!input.contains(key) || input.value(key).toString().trimmed().isEmpty()) continue;
        bool ok = false; const auto number = input.value(key).toLongLong(&ok);
        const qint64 maximum = key.startsWith("year") ? 9999 : 1000000000;
        if (!ok || number < 0 || number > maximum) return fail(QObject::tr("Years must be 0–9999 and play counts 0–1,000,000,000."));
        // Zero is useful for never played, while year zero means no year filter.
        if (!key.startsWith("year") || number > 0) result[key] = number;
    }
    if (result.contains("yearFrom") && result.contains("yearTo") && result.value("yearFrom").toInt() > result.value("yearTo").toInt())
        return fail(QObject::tr("The first year must not be later than the last year."));
    if (result.contains("minPlays") && result.contains("maxPlays") && result.value("minPlays").toLongLong() > result.value("maxPlays").toLongLong())
        return fail(QObject::tr("Minimum plays must not exceed maximum plays."));
    return result;
}

bool CollectionRules::inFolder(const QString &directory, const QString &input)
{
    if (directory.isEmpty() || input.isEmpty()) return false;
    const auto path = QDir::cleanPath(directory), root = QDir::cleanPath(input);
    return path == root || path.startsWith(root.endsWith('/') ? root : root + '/');
}

bool CollectionRules::matches(const QVariantMap &track, const QVariantMap &input)
{
    QString error; const auto rules = normalize(input, &error); if (!error.isEmpty()) return false;
    if (rules.contains("genre") && folded(track.value("genre").toString().trimmed()) != folded(rules.value("genre").toString())) return false;
    const auto year = releaseYear(track);
    if (rules.contains("yearFrom") && year < rules.value("yearFrom").toInt()) return false;
    if (rules.contains("yearTo") && (year <= 0 || year > rules.value("yearTo").toInt())) return false;
    const auto favorite = rules.value("favorite").toString();
    if (favorite == "yes" && !track.value("favorite").toBool()) return false;
    if (favorite == "no" && track.value("favorite").toBool()) return false;
    const auto plays = track.value("playCount").toLongLong();
    if (rules.contains("minPlays") && plays < rules.value("minPlays").toLongLong()) return false;
    if (rules.contains("maxPlays") && plays > rules.value("maxPlays").toLongLong()) return false;
    return true;
}

QString CollectionRules::sql(const QVariantMap &input, QVariantList *values)
{
    QString error; const auto rules = normalize(input, &error); if (!error.isEmpty()) return QStringLiteral(" AND 0");
    QString result;
    if (rules.contains("genre")) { result += " AND genre=?"; values->append(folded(rules.value("genre").toString())); }
    if (rules.contains("yearFrom")) { result += " AND year>=?"; values->append(rules.value("yearFrom")); }
    if (rules.contains("yearTo")) { result += " AND year>0 AND year<=?"; values->append(rules.value("yearTo")); }
    if (rules.value("favorite").toString() == "yes") result += " AND favorite=1";
    if (rules.value("favorite").toString() == "no") result += " AND favorite=0";
    if (rules.contains("minPlays")) { result += " AND plays>=?"; values->append(rules.value("minPlays")); }
    if (rules.contains("maxPlays")) { result += " AND plays<=?"; values->append(rules.value("maxPlays")); }
    return result;
}

QString CollectionRules::description(const QVariantMap &rules)
{
    QStringList parts;
    if (rules.contains("genre")) parts.append(rules.value("genre").toString());
    if (rules.contains("yearFrom") || rules.contains("yearTo")) parts.append(QObject::tr("Year %1–%2").arg(rules.value("yearFrom", 1).toInt()).arg(rules.value("yearTo", 9999).toInt()));
    if (rules.value("favorite").toString() == "yes") parts.append(QObject::tr("Favorites"));
    if (rules.value("favorite").toString() == "no") parts.append(QObject::tr("Non-favorites"));
    if (rules.contains("minPlays") || rules.contains("maxPlays")) parts.append(QObject::tr("Plays %1–%2").arg(rules.value("minPlays", 0).toLongLong()).arg(rules.contains("maxPlays") ? rules.value("maxPlays").toString() : QStringLiteral("∞")));
    return parts.isEmpty() ? QObject::tr("All songs") : parts.join(QStringLiteral(" · "));
}

void TrackListModel::initializeCollectionTools()
{
    connect(this, &TrackListModel::tracksChanged, this, &TrackListModel::collectionToolsChanged, Qt::UniqueConnection);
    const auto playlists = savedSmart(m_settings);
    m_songs.setCustomSmartPlaylists(playlists);
    if (m_database && m_database->valid()) m_database->setCustomSmartPlaylists(playlists);
    emit collectionToolsChanged();
}

QVariantList TrackListModel::customSmartPlaylists() const
{
    QVariantList result;
    for (const auto &entry : savedSmart(m_settings)) {
        auto item = entry.toMap(); const auto rules = item.value("rules").toMap();
        int count = 0;
        if (m_database && m_database->valid()) count = m_database->customSmartCount(rules);
        else for (const auto &track : m_tracks) count += CollectionRules::matches(track.toMap(), rules);
        item["count"] = count; item["trackCount"] = count; item["description"] = CollectionRules::description(rules); item["icon"] = "smart";
        result.append(item);
    }
    return result;
}

QString TrackListModel::saveSmartPlaylist(const QString &id, const QString &inputName, const QVariantMap &inputRules)
{
    const auto name = inputName.simplified();
    if (name.isEmpty() || name.size() > 80) { emit importError(tr("Give your smart playlist a name of 1–80 characters.")); return {}; }
    QString error; const auto rules = CollectionRules::normalize(inputRules, &error);
    if (!error.isEmpty()) { emit importError(error); return {}; }
    auto saved = savedSmart(m_settings); int row = -1;
    for (int i = 0; i < saved.size(); ++i) {
        const auto item = saved[i].toMap();
        if (item.value("id").toString() == id) row = i;
        else if (item.value("name").toString().compare(name, Qt::CaseInsensitive) == 0) { emit importError(tr("A smart playlist with that name already exists.")); return {}; }
    }
    if (!id.isEmpty() && row < 0) { emit importError(tr("This smart playlist no longer exists.")); return {}; }
    if (row < 0 && saved.size() >= 64) { emit importError(tr("You can save up to 64 custom smart playlists.")); return {}; }
    const QString key = row < 0 ? newId() : id;
    const QVariantMap item{{"id", key}, {"name", name}, {"rules", rules}};
    if (row < 0) saved.append(item); else saved[row] = item;
    m_settings.setValue("collections/smart", saved);
    initializeCollectionTools();
    return key;
}

bool TrackListModel::deleteSmartPlaylist(const QString &id)
{
    auto saved = savedSmart(m_settings);
    for (int i = 0; i < saved.size(); ++i) if (saved[i].toMap().value("id").toString() == id) {
        saved.removeAt(i); m_settings.setValue("collections/smart", saved);
        setCollectionPinned("customSmart", id, QString(), false);
        initializeCollectionTools(); return true;
    }
    return false;
}

bool TrackListModel::collectionPinned(const QString &kind, const QString &id) const
{
    for (const auto &entry : m_settings.value("collections/pins").toList()) {
        const auto item = entry.toMap();
        const auto storedId = item.value("id").toString();
        if (item.value("kind").toString() == kind && (storedId == id || (kind == "album" && m_albumAliases.value(storedId, storedId) == m_albumAliases.value(id, id)))) return true;
    }
    return false;
}

bool TrackListModel::setCollectionPinned(const QString &kind, const QString &id, const QString &name, bool pinned)
{
    if (!QStringList{"album", "artist", "genre", "playlist", "customSmart", "smart", "folder"}.contains(kind) || id.isEmpty()) return false;
    auto saved = m_settings.value("collections/pins").toList();
    for (int i = 0; i < saved.size(); ++i) {
        const auto item = saved[i].toMap();
        const auto storedId = item.value("id").toString();
        if (item.value("kind").toString() != kind || (storedId != id && (kind != "album" || m_albumAliases.value(storedId, storedId) != m_albumAliases.value(id, id)))) continue;
        if (pinned) return true;
        saved.removeAt(i); m_settings.setValue("collections/pins", saved); emit collectionToolsChanged(); return true;
    }
    if (!pinned) return true;
    bool exists = kind == "folder";
    if (kind == "customSmart") { for (const auto &entry : savedSmart(m_settings)) exists |= entry.toMap().value("id").toString() == id; }
    else if (kind == "smart") { for (const auto &entry : smartPlaylists()) exists |= entry.toMap().value("id").toString() == id; }
    else if (kind != "folder" && m_database && m_database->valid()) exists = !m_database->entry(kind, kind == "album" ? m_albumAliases.value(id, id) : id).isEmpty();
    else if (kind != "folder") {
        const auto *model = kind == "album" ? &m_albums : kind == "artist" ? &m_artists : kind == "genre" ? &m_genres : &m_playlists;
        for (const auto &entry : model->entries()) exists |= entry.toMap().value("id").toString() == id;
    }
    if (!exists) return false;
    if (saved.size() >= 64) { emit importError(tr("You can pin up to 64 collections.")); return false; }
    saved.append(QVariantMap{{"kind", kind}, {"id", id}, {"name", name.simplified().left(200)}});
    m_settings.setValue("collections/pins", saved); emit collectionToolsChanged(); return true;
}

QVariantList TrackListModel::pinnedCollections() const
{
    QVariantList result;
    for (const auto &entry : m_settings.value("collections/pins").toList()) {
        auto pin = entry.toMap(); const auto kind = pin.value("kind").toString(), id = pin.value("id").toString();
        QVariantMap current;
        if (kind == "customSmart") { for (const auto &saved : savedSmart(m_settings)) if (saved.toMap().value("id").toString() == id) current = saved.toMap(); }
        else if (kind == "folder" && m_database && m_database->valid()) current = m_database->folderSummary(id);
        else if (kind == "smart") { for (const auto &saved : smartPlaylists()) if (saved.toMap().value("id").toString() == id) current = saved.toMap(); }
        else if (m_database && m_database->valid()) current = m_database->entry(kind, kind == "album" ? m_albumAliases.value(id, id) : id);
        else if (kind != "folder") {
            const auto *model = kind == "album" ? &m_albums : kind == "artist" ? &m_artists : kind == "genre" ? &m_genres : &m_playlists;
            for (const auto &entry : model->entries()) if (entry.toMap().value("id").toString() == id) { current = entry.toMap(); break; }
        }
        // Keep a saved pin's identity for a future restored library, but omit
        // collections that have since been removed from the visible sidebar.
        if (current.isEmpty() && kind != "folder") continue;
        if (!current.value("name").toString().isEmpty()) pin["name"] = current.value("name");
        if (current.contains("id")) pin["id"] = current.value("id");
        if (current.contains("artwork")) pin["artwork"] = current.value("artwork");
        pin["icon"] = kind == "folder" ? "folder" : kind == "artist" ? "artist" : kind == "genre" || kind == "customSmart" || kind == "smart" ? "smart" : kind == "playlist" ? "playlist" : "album";
        result.append(pin);
    }
    return result;
}

QVariantMap TrackListModel::folderBrowser(const QString &input) const
{
    const QString path = input.isEmpty() ? QString() : QDir::cleanPath(QUrl(input).isLocalFile() ? QUrl(input).toLocalFile() : input);
    QVariantList children; int count = 0;
    const auto summary = [this](const QString &folder) {
        if (m_database && m_database->valid()) return m_database->folderSummary(folder);
        QVariantMap group{{"id", folder}, {"name", QFileInfo(folder).fileName()}, {"subtitle", folder}, {"trackCount", 0}, {"duration", 0LL}, {"addedAt", 0LL}};
        for (const auto &entry : m_tracks) {
            const auto track = entry.toMap();
            if (!CollectionRules::inFolder(QFileInfo(QUrl(track.value("fileUrl").toString()).toLocalFile()).absolutePath(), folder)) continue;
            group["trackCount"] = group.value("trackCount").toInt() + 1;
            group["duration"] = group.value("duration").toLongLong() + track.value("duration").toLongLong();
            group["addedAt"] = qMax(group.value("addedAt").toLongLong(), track.value("addedAt").toLongLong());
        }
        return group;
    };
    QString parent;
    if (path.isEmpty()) {
        QStringList candidates = m_folders;
        for (const auto &file : m_files) {
            const auto directory = QFileInfo(file).absolutePath(); bool contained = false;
            for (const auto &folder : m_folders) contained |= CollectionRules::inFolder(directory, folder);
            if (!contained) candidates.append(directory);
        }
        candidates.removeDuplicates();
        std::sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) { return a.size() < b.size(); });
        QStringList roots;
        for (const auto &candidate : candidates) {
            bool covered = false; for (const auto &root : roots) covered |= CollectionRules::inFolder(candidate, root);
            if (covered) continue;
            const auto item = summary(candidate); if (!item.value("trackCount").toInt()) continue;
            roots.append(candidate); children.append(item); count += item.value("trackCount").toInt();
        }
    } else {
        count = summary(path).value("trackCount").toInt();
        if (m_database && m_database->valid()) children = m_database->folderChildren(path);
        else {
            QSet<QString> dirs;
            for (const auto &entry : m_tracks) {
                const auto dir = QFileInfo(QUrl(entry.toMap().value("fileUrl").toString()).toLocalFile()).absolutePath();
                if (dir == path || !CollectionRules::inFolder(dir, path)) continue;
                dirs.insert(QDir(path).filePath(QDir(path).relativeFilePath(dir).section('/', 0, 0)));
            }
            for (const auto &dir : dirs) children.append(summary(dir));
        }
        parent = QFileInfo(path).absolutePath();
        if (m_folders.contains(path) || parent == path) parent.clear();
        for (const auto &file : m_files) if (QFileInfo(file).absolutePath() == path) {
            bool inRoot = false; for (const auto &folder : m_folders) inRoot |= CollectionRules::inFolder(path, folder);
            if (!inRoot) parent.clear();
        }
    }
    QCollator collator; collator.setNumericMode(true); collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(children.begin(), children.end(), [&collator](const auto &a, const auto &b) { return collator.compare(a.toMap().value("name").toString(), b.toMap().value("name").toString()) < 0; });
    return {{"path", path}, {"parent", parent}, {"folders", children}, {"count", count}};
}

QVariantList TrackListModel::collectionTracks(const QString &kind, const QString &id) const
{
    if (kind != "folder" && kind != "customSmart") return {};
    if (m_database && m_database->valid()) {
        const auto ids = m_database->ids("song", {QString(), kind, id, "track"});
        return m_database->page("song", ids, 0, ids.size());
    }
    QVariantMap rules; bool found = kind == "folder";
    if (kind == "customSmart") for (const auto &saved : savedSmart(m_settings)) if (saved.toMap().value("id").toString() == id) { rules = saved.toMap().value("rules").toMap(); found = true; break; }
    if (!found) return {};
    QVariantList result;
    for (const auto &entry : m_tracks) {
        const auto track = entry.toMap();
        if (kind == "folder" ? CollectionRules::inFolder(QFileInfo(QUrl(track.value("fileUrl").toString()).toLocalFile()).absolutePath(), id) : CollectionRules::matches(track, rules)) result.append(entry);
    }
    return result;
}

QVariantList TrackListModel::trackBookmarks(const QString &input) const
{
    const auto url = QUrl(input).toString();
    auto rows = m_settings.value("collections/bookmarks").toMap().value(url).toList();
    std::stable_sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) { return a.toMap().value("position").toLongLong() < b.toMap().value("position").toLongLong(); });
    return rows;
}

QString TrackListModel::saveTrackBookmark(const QString &input, qint64 position, const QString &inputName, const QString &id)
{
    const auto url = QUrl(input).toString(); const auto track = trackForUrl(url);
    if (track.isEmpty()) { emit importError(tr("Choose a song in your library first.")); return {}; }
    if (inputName.simplified().size() > 80) { emit importError(tr("Bookmark names must be 80 characters or fewer.")); return {}; }
    const auto duration = track.value("duration").toLongLong();
    position = duration > 0 ? qBound<qint64>(0, position, qMax<qint64>(0, duration - 1)) : qMax<qint64>(0, position);
    auto saved = m_settings.value("collections/bookmarks").toMap(); auto rows = saved.value(url).toList();
    int row = -1; for (int i = 0; i < rows.size(); ++i) if (rows[i].toMap().value("id").toString() == id) row = i;
    if (!id.isEmpty() && row < 0) return {};
    if (row < 0 && rows.size() >= 200) { emit importError(tr("You can save up to 200 bookmarks per song.")); return {}; }
    const QString key = row < 0 ? newId() : id;
    const QString name = inputName.simplified().isEmpty() ? tr("Bookmark at %1").arg(clockText(position)) : inputName.simplified();
    const QVariantMap item{{"id", key}, {"name", name}, {"position", position}};
    if (row < 0) rows.append(item); else rows[row] = item;
    saved[url] = rows; m_settings.setValue("collections/bookmarks", saved); emit bookmarksChanged(url); return key;
}

bool TrackListModel::removeTrackBookmark(const QString &input, const QString &id)
{
    const auto url = QUrl(input).toString(); auto saved = m_settings.value("collections/bookmarks").toMap(); auto rows = saved.value(url).toList();
    for (int i = 0; i < rows.size(); ++i) if (rows[i].toMap().value("id").toString() == id) {
        rows.removeAt(i); if (rows.isEmpty()) saved.remove(url); else saved[url] = rows;
        m_settings.setValue("collections/bookmarks", saved); emit bookmarksChanged(url); return true;
    }
    return false;
}

void TrackListModel::remapCollectionUrls(const QVariantMap &mapping)
{
    auto bookmarks = m_settings.value("collections/bookmarks").toMap();
    for (auto it = mapping.cbegin(); it != mapping.cend(); ++it) {
        if (!bookmarks.contains(it.key())) continue;
        auto destination = bookmarks.value(it.value().toString()).toList(); destination += bookmarks.take(it.key()).toList();
        bookmarks[it.value().toString()] = destination; emit bookmarksChanged(it.value().toString());
    }
    m_settings.setValue("collections/bookmarks", bookmarks);
    auto pins = m_settings.value("collections/pins").toList();
    for (auto &entry : pins) {
        auto pin = entry.toMap(); if (pin.value("kind").toString() != "folder") continue;
        const QString oldRoot = pin.value("id").toString(); QSet<QString> destinations;
        for (auto it = mapping.cbegin(); it != mapping.cend(); ++it) {
            const QString oldPath = QUrl(it.key()).toLocalFile(), newPath = QUrl(it.value().toString()).toLocalFile();
            if (!CollectionRules::inFolder(QFileInfo(oldPath).absolutePath(), oldRoot)) continue;
            const auto relative = QDir(oldRoot).relativeFilePath(oldPath);
            if (newPath.endsWith('/' + relative)) destinations.insert(newPath.left(newPath.size() - relative.size() - 1));
        }
        if (destinations.size() == 1) { pin["id"] = *destinations.cbegin(); pin["name"] = QFileInfo(pin.value("id").toString()).fileName(); entry = pin; }
    }
    m_settings.setValue("collections/pins", pins); emit collectionToolsChanged();
}
