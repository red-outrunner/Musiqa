#pragma once

#include <QList>
#include <QString>
#include <QPair>

// A play qualifies after distinct, actually traversed audio covers half a song.
// The caller supplies monotonic time so seeks and pauses are not listening time.
class ListeningSession
{
public:
    void begin(const QString &url);
    void seek(qint64 position, qint64 now);
    bool observe(qint64 position, qint64 duration, bool playing, qint64 now);
    QString url() const { return m_url; }
    qint64 listened() const;
    qint64 lastAudibleMs() const { return m_lastAudibleMs; }

private:
    QString m_url;
    QList<QPair<qint64, qint64>> m_intervals;
    qint64 m_position = 0;
    qint64 m_timestamp = 0;
    bool m_hasSample = false;
    bool m_wasPlaying = false;
    bool m_counted = false;
    qint64 m_lastAudibleMs = 0;
};
