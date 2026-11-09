#include "tracklibrary.h"
#include "filetags.h"

#include <QFileInfo>
#include <cmath>

QString TrackListModel::analysisStatus() const
{
    return analyzing() ? tr("Analyzing tempo & key: %1 / %2").arg(m_analysisDone).arg(m_analysisTotal)
                       : m_analysisTotal > 0 ? tr("Tempo & key analysis complete: %1 songs").arg(m_analysisDone) : tr("Analyze BPM and musical key locally");
}

QVariantList TrackListModel::musicalKeys() const
{
    QVariantList keys{QVariantMap{{"id", -1}, {"label", tr("Unknown")}, {"nativeValue", QString()}}};
    for (int i = 0; i < 24; ++i) keys.append(QVariantMap{{"id", i}, {"label", MusicAnalysis::keyName(i) + QStringLiteral(" · ") + MusicAnalysis::camelot(i)}, {"nativeValue", FileTags::portableKey(i)}});
    return keys;
}

void TrackListModel::analyzeTracks(const QStringList &urls, bool force)
{
    QStringList requested = urls;
    if (requested.isEmpty()) for (const auto &entry : m_tracks) requested.append(entry.toMap().value("fileUrl").toString());
    if (!analyzing()) m_analysisDone = m_analysisTotal = 0;
    for (const auto &url : requested) {
        const auto track = trackForUrl(url);
        if (track.isEmpty() || url == m_analysisUrl || m_analysisQueue.contains(url)) continue;
        const auto cached = m_analysisCache.value(url).toMap();
        if (!force && cached.value("version").toInt() == MusicAnalysis::Version
            && cached.value("modified") == track.value("modified") && cached.value("size") == track.value("size")) continue;
        if (force && cached.value("manual").toBool()) {
            auto value = cached; value["manual"] = false; m_analysisCache[url] = value;
        }
        m_analysisQueue.append(url); ++m_analysisTotal;
    }
    emit analysisChanged();
    if (!m_analysisActive) QTimer::singleShot(0, this, &TrackListModel::analyzeNext);
}

void TrackListModel::analyzeNext()
{
    if (m_analysisActive || m_analysisQueue.isEmpty()) return;
    m_analysisUrl = m_analysisQueue.takeFirst();
    const auto track = trackForUrl(m_analysisUrl);
    if (track.isEmpty()) { m_analysisUrl.clear(); ++m_analysisDone; QTimer::singleShot(0, this, &TrackListModel::analyzeNext); return; }
    m_analysisActive = true;
    m_analysisModified = track.value("modified").toLongLong(); m_analysisSize = track.value("size").toLongLong();
    emit analysisChanged();
    m_analyzer.start(QUrl(m_analysisUrl));
}

void TrackListModel::cancelAnalysis()
{
    m_analyzer.cancel(); m_analysisActive = false; m_analysisQueue.clear(); m_analysisUrl.clear();
    m_analysisTotal = m_analysisDone;
    emit analysisChanged();
}

bool TrackListModel::setMusicalDetails(const QString &url, double bpm, int key, double beatOffset)
{
    const auto track = trackForUrl(url);
    if (track.isEmpty() || !std::isfinite(bpm) || (bpm != 0 && (bpm < 30 || bpm > 300)) || key < -1 || key >= 24
        || !std::isfinite(beatOffset) || beatOffset < 0 || beatOffset > 10) {
        emit importError(tr("Use a BPM of 30–300 (or 0 for unknown), a musical key, and a beat offset of 0–10 seconds.")); return false;
    }
    m_analysisCache[url] = QVariantMap{{"version", MusicAnalysis::Version}, {"manual", true}, {"bpm", bpm}, {"keyId", key},
        {"beatOffset", beatOffset}, {"bpmConfidence", bpm > 0 ? 1.0 : 0.0}, {"keyConfidence", key >= 0 ? 1.0 : 0.0},
        {"modified", track.value("modified")}, {"size", track.value("size")}, {"analysisNote", tr("Set manually in Musica")}};
    publishUpdates(); saveSettings(); return true;
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
