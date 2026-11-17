#include "genregrouping.h"

QString GenreGrouping::nameFor(const QString &tag)
{
    auto name = tag.normalized(QString::NormalizationForm_KC);
    name.remove(QChar(0x200b)); name.remove(QChar(0xfeff));
    name = name.simplified();
    return name.isEmpty() ? QStringLiteral("Unknown genre") : name;
}

QString GenreGrouping::idFor(const QString &tag)
{
    return QStringLiteral("genre:") + nameFor(tag).toCaseFolded();
}

void GenreGrouping::Builder::add(const QVariantMap &track)
{
    const auto tag = track.value("genre").toString(), id = idFor(tag);
    auto found = m_groups.find(id);
    if (found == m_groups.end()) {
        found = m_groups.insert(id, {{"id", id}, {"name", nameFor(tag)}, {"subtitle", QString()},
            {"artwork", track.value("artwork", QString())}, {"artStyle", track.value("artStyle", 0)},
            {"trackCount", 0}, {"duration", 0LL}, {"addedAt", 0LL}});
    }
    auto &group = found.value();
    group["trackCount"] = group.value("trackCount").toInt() + 1;
    group["duration"] = group.value("duration").toLongLong() + qMax<qint64>(0, track.value("duration").toLongLong());
    group["addedAt"] = qMax(group.value("addedAt").toLongLong(), track.value("addedAt").toLongLong());
    if (group.value("artwork").toString().isEmpty() && !track.value("artwork").toString().isEmpty()) {
        group["artwork"] = track.value("artwork"); group["artStyle"] = track.value("artStyle", 0);
    }
}

QVariantList GenreGrouping::Builder::entries() const
{
    QVariantList result;
    result.reserve(m_groups.size());
    for (const auto &group : m_groups) result.append(group);
    return result;
}
