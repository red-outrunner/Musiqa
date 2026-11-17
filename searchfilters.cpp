#include "searchfilters.h"
#include "collectionfeatures.h"
#include <cmath>

QVariantMap SearchFilters::normalized(const QVariantMap &input, bool *valid)
{
    if (valid) *valid = false;
    QVariantMap result;
    for (auto it = input.cbegin(); it != input.cend(); ++it) {
        const auto key = it.key();
        if (QStringList{"title", "artist", "album", "genre"}.contains(key)) {
            if (it.value().metaType().id() != QMetaType::QString || it.value().toString().size() > 512) return {};
            const auto text = it.value().toString().trimmed(); if (!text.isEmpty()) result[key] = text;
        } else if (key == "favorite") {
            const auto text = it.value().toString();
            if (text != "any" && text != "yes" && text != "no" && !text.isEmpty()) return {};
            if (text == "yes" || text == "no") result[key] = text;
        } else if (QStringList{"yearMin", "yearMax", "durationMin", "durationMax"}.contains(key)) {
            if (it.value().metaType().id() == QMetaType::QString || it.value().metaType().id() == QMetaType::Bool) return {};
            bool ok = false; const double number = it.value().toDouble(&ok);
            const auto limit = key.startsWith("year") ? 9999.0 : 60000000000.0;
            if (!ok || !std::isfinite(number) || number < 0 || number > limit || std::floor(number) != number) return {};
            result[key] = qint64(number);
        } else return {};
    }
    for (const auto &prefix : {QString("year"), QString("duration")})
        if (result.contains(prefix + "Min") && result.contains(prefix + "Max")
            && result.value(prefix + "Min").toLongLong() > result.value(prefix + "Max").toLongLong()) return {};
    if (valid) *valid = true;
    return result;
}

bool SearchFilters::matches(const QVariantMap &track, const QVariantMap &filters)
{
    if (filters.isEmpty()) return true;
    for (const auto &field : {"title", "artist", "album", "genre"})
        if (filters.contains(field) && !CollectionRules::folded(track.value(field).toString()).contains(CollectionRules::folded(filters.value(field).toString()))) return false;
    const auto favorite = filters.value("favorite").toString();
    if ((favorite == "yes" && !track.value("favorite").toBool()) || (favorite == "no" && track.value("favorite").toBool())) return false;
    const qint64 year = CollectionRules::releaseYear(track), duration = track.value("duration").toLongLong();
    for (const auto &range : {qMakePair(QString("year"), year), qMakePair(QString("duration"), duration)}) {
        if (filters.contains(range.first + "Min") && range.second < filters.value(range.first + "Min").toLongLong()) return false;
        if (filters.contains(range.first + "Max") && range.second > filters.value(range.first + "Max").toLongLong()) return false;
    }
    return true;
}

QString SearchFilters::sql(const QVariantMap &filters, QVariantList *values)
{
    QString result;
    for (const auto &field : {"title", "artist", "album", "genre"}) if (filters.contains(field)) {
        const auto column = QString(field) == "genre" ? QStringLiteral("genre") : QStringLiteral("search") + QString::fromLatin1(field);
        result += " AND instr(" + column + ",?)>0";
        values->append(CollectionRules::folded(filters.value(field).toString()));
    }
    const auto favorite = filters.value("favorite").toString();
    if (favorite == "yes") result += " AND favorite=1";
    if (favorite == "no") result += " AND favorite=0";
    for (const auto &field : {"year", "duration"}) {
        if (filters.contains(QString(field) + "Min")) { result += " AND " + QString(field) + ">=?"; values->append(filters.value(QString(field) + "Min")); }
        if (filters.contains(QString(field) + "Max")) { result += " AND " + QString(field) + "<=?"; values->append(filters.value(QString(field) + "Max")); }
    }
    return result;
}
