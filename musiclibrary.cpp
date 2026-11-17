#include "tracklibrary.h"

void TrackListModel::publishPlayCount(const QString &url)
{
    if (!m_workingSetLoaded && m_database->valid()) {
        auto track = trackForUrl(url); if (track.isEmpty()) return;
        track["playCount"] = m_playCounts.value(url).toLongLong(); m_database->updateSong(url, track);
        emit tracksChanged(); emit historyChanged(); return;
    }
    const int row = m_trackRows.value(url, -1);
    if (row < 0) return;
    const auto count = m_playCounts.value(url).toLongLong();
    auto track = m_tracks[row].toMap(); track["playCount"] = count; m_tracks[row] = track;
    // A qualified listen changes one row. Publish it immediately without
    // waiting for the metadata batch or regrouping the rest of the library.
    if (m_database->valid()) m_database->updateSong(url, track);
    else m_songEntries.setPlayCount(url, count);
    emit tracksChanged();
    emit historyChanged();
}

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
        publishPlayCount(url);
        saveListeningState();
    }
    if (finished) m_audibleSessions.erase(found);
    while (m_audibleSessions.size() > 4) m_audibleSessions.erase(m_audibleSessions.begin());
}

void TrackListModel::seekAudiblePlayback(const QString &token, qint64 position, qint64 timestamp)
{
    auto found = m_audibleSessions.find(token);
    if (found != m_audibleSessions.end()) found->coverage.seek(position, timestamp);
}
