#include "listeninghistory.h"

#include <algorithm>

void ListeningSession::begin(const QString &url)
{
    m_url = url;
    m_intervals.clear();
    m_position = 0;
    m_timestamp = 0;
    m_hasSample = false;
    m_wasPlaying = false;
    m_counted = false;
    m_lastAudibleMs = 0;
}

void ListeningSession::seek(qint64 position, qint64 now)
{
    m_position = qMax<qint64>(0, position);
    m_timestamp = now;
    m_hasSample = true;
}

qint64 ListeningSession::listened() const
{
    qint64 total = 0;
    for (const auto &interval : m_intervals) total += interval.second - interval.first;
    return total;
}

bool ListeningSession::observe(qint64 position, qint64 duration, bool playing, qint64 now)
{
    position = qMax<qint64>(0, position);
    m_lastAudibleMs = 0;
    if (m_hasSample && m_wasPlaying && position > m_position) {
        const qint64 elapsed = qMax<qint64>(0, now - m_timestamp);
        const qint64 distance = position - m_position;
        // Normal media-position notifications can lag the monotonic clock a little.
        if (elapsed > 0 && distance <= elapsed + 100) {
            const qint64 end = duration > 0 ? qMin(position, duration) : position;
            m_lastAudibleMs = qMax<qint64>(0, end - m_position);
            if (!m_counted && end > m_position) {
                m_intervals.append(qMakePair(m_position, end));
                std::sort(m_intervals.begin(), m_intervals.end());
                QList<QPair<qint64, qint64>> merged;
                for (const auto &interval : m_intervals) {
                    if (!merged.isEmpty() && interval.first <= merged.last().second)
                        merged.last().second = qMax(merged.last().second, interval.second);
                    else merged.append(interval);
                }
                m_intervals = merged;
            }
        }
    }
    m_position = position;
    m_timestamp = now;
    m_hasSample = true;
    m_wasPlaying = playing;
    if (!m_counted && duration > 0 && listened() >= (duration + 1) / 2) {
        m_counted = true;
        return true;
    }
    return false;
}
