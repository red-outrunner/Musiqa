#include "displaysettings.h"
#include <cmath>

namespace {
QVariantList defaults()
{
    return {QVariantMap{{"key", "album"}, {"width", 220}, {"visible", true}},
        QVariantMap{{"key", "duration"}, {"width", 70}, {"visible", true}},
        QVariantMap{{"key", "year"}, {"width", 80}, {"visible", false}},
        QVariantMap{{"key", "format"}, {"width", 80}, {"visible", false}}};
}
QVariantList checkedColumns(const QVariantList &input)
{
    if (input.size() != 4) return {};
    QSet<QString> keys; QVariantList result;
    for (const auto &value : input) {
        const auto item = value.toMap(); const auto key = item.value("key").toString();
        if (!QStringList{"album", "duration", "year", "format"}.contains(key) || keys.contains(key)
            || item.value("visible").metaType().id() != QMetaType::Bool) return {};
        bool valid = false; const double width = item.value("width").toDouble(&valid);
        if (!valid || !std::isfinite(width) || width < 50 || width > 500) return {};
        keys.insert(key); result.append(QVariantMap{{"key", key}, {"width", qRound(width)}, {"visible", item.value("visible")}});
    }
    return result;
}
}

void DisplaySettings::loadLibraryViewSettings()
{
    m_songColumns = checkedColumns(m_settings.value("libraryView/songColumns").toList());
    if (m_songColumns.isEmpty()) m_songColumns = defaults();
    m_avoidRecentShuffle = m_settings.value("playback/avoidRecentShuffle", false).toBool();
    m_shuffleRecentDays = qBound(1, m_settings.value("playback/shuffleRecentDays", 7).toInt(), 90);
}
void DisplaySettings::setSongColumns(const QVariantList &input)
{
    const auto columns = checkedColumns(input);
    if (columns.isEmpty() || columns == m_songColumns) return;
    m_songColumns = columns; m_saveTimer.start(); emit libraryViewChanged();
}
void DisplaySettings::resetSongColumns() { setSongColumns(defaults()); }
QVariantList DisplaySettings::defaultSongColumns() const { return defaults(); }
bool DisplaySettings::columnsCustomized() const { return m_songColumns != defaults(); }
void DisplaySettings::setAvoidRecentShuffle(bool enabled)
{
    if (enabled == m_avoidRecentShuffle) return;
    m_avoidRecentShuffle = enabled; m_saveTimer.start(); emit libraryViewChanged();
}
void DisplaySettings::setShuffleRecentDays(int days)
{
    days = qBound(1, days, 90);
    if (days == m_shuffleRecentDays) return;
    m_shuffleRecentDays = days; m_saveTimer.start(); emit libraryViewChanged();
}
