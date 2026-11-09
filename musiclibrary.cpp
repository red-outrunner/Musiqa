#include "tracklibrary.h"

void TrackListModel::observeAudiblePlayback(const QString &token, const QString &fileUrl, qint64 position, qint64 duration, bool playing, bool finished, qint64 timestamp)
{
    const QString url = QUrl(fileUrl).toString();
    if (token.isEmpty() || trackForUrl(url).isEmpty()) return;
    auto found = m_audibleSessions.find(token);
    if (found == m_audibleSessions.end()) {
        AudibleSession session; session.coverage.begin(url); session.clock.start();
        found = m_audibleSessions.insert(token, session);
    }
    const bool qualified = found->coverage.observe(position, duration, playing, timestamp);
    collectListening(url, found->coverage.lastAudibleMs(), qualified, token);
    if (qualified) {
        m_playCounts[url] = m_playCounts.value(url).toLongLong() + 1;
        m_settings.setValue("library/playCounts", m_playCounts);
        // A listen changes ranking but never needs to restart metadata probing.
        if (!m_tracksNotification.isActive()) m_tracksNotification.start();
    }
    if (finished) m_audibleSessions.erase(found);
    while (m_audibleSessions.size() > 4) m_audibleSessions.erase(m_audibleSessions.begin());
}

void TrackListModel::seekAudiblePlayback(const QString &token, qint64 position, qint64 timestamp)
{
    auto found = m_audibleSessions.find(token);
    if (found != m_audibleSessions.end()) found->coverage.seek(position, timestamp);
}
