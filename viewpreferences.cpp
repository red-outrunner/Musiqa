#include "tracklibrary.h"
#include "searchfilters.h"
#include <QJsonDocument>
#include <cmath>

namespace {
QVariantMap checkedState(const QVariantMap &input)
{
    if (input.value("version").toInt() != 1 || QJsonDocument::fromVariant(input).toJson(QJsonDocument::Compact).size() > 1024 * 1024) return {};
    const auto pages = input.value("pages").toMap();
    if (pages.size() > 100) return {};
    QVariantMap result, accepted;
    const QStringList names{"Overview", "Songs", "Albums", "Artists", "Genres", "Favorites", "Most Played", "Playlists", "Smart playlists", "Folders", "Listening dashboard"};
    const QStringList kinds{"", "album", "artist", "genre", "playlist", "smart", "customSmart", "folder"};
    const QStringList sorts{"Title", "Artist", "Album", "Track", "Plays", "Added", "Rare", "Order"};
    for (auto it = pages.cbegin(); it != pages.cend(); ++it) {
        auto page = it.value().toMap();
        if (it.key().size() > 8192 || !names.contains(page.value("page").toString()) || !kinds.contains(page.value("kind").toString()) || !sorts.contains(page.value("sort", "Title").toString())) return {};
        QVariantMap clean;
        for (const auto &key : {"page", "kind", "id", "name", "artist", "search", "sort"}) {
            const auto text = page.value(key).toString(); if (text.size() > 8192 || text.contains(QChar::Null)) return {};
            clean[key] = text;
        }
        bool valid = false; clean["filters"] = SearchFilters::normalized(page.value("filters").toMap(), &valid); if (!valid) return {};
        QVariantMap scroll;
        for (const auto &key : {"outer", "groups", "songs", "songsX"}) {
            const auto value = page.value("scroll").toMap().value(key, 0).toDouble();
            if (!std::isfinite(value)) return {};
            scroll[key] = qBound(0.0, value, 1e9);
        }
        clean["scroll"] = scroll; accepted[it.key()] = clean;
    }
    result["version"] = 1; result["pages"] = accepted;
    const auto last = input.value("lastKey").toString(); if (accepted.contains(last)) result["lastKey"] = last;
    for (const auto &key : {"albumSortKey", "artistSortKey", "genreSortKey"}) {
        const auto value = input.value(key).toString();
        if (QStringList{"title", "artist", "name", "added", "count", "duration"}.contains(value)) result[key] = value;
    }
    for (const auto &key : {"albumSortDescending", "artistSortDescending", "genreSortDescending"}) result[key] = input.value(key).toBool();
    for (const auto &key : {"albumViewStyle", "artistViewStyle"}) result[key] = input.value(key).toString() == "list" ? "list" : "grid";
    return result;
}
}
QVariantMap TrackListModel::savedViewState() const { return checkedState(m_settings.value("ui/pagePreferences").toMap()); }
bool TrackListModel::saveViewState(const QVariantMap &input)
{
    const auto state = checkedState(input); if (state.isEmpty()) return false;
    if (state != m_settings.value("ui/pagePreferences").toMap()) m_settings.setValue("ui/pagePreferences", state);
    return true;
}
