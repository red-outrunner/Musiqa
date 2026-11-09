#pragma once

#include <QAudioDecoder>
#include <QFutureWatcher>
#include <QTimer>
#include <QVariantMap>
#include <QVector>
#include <QUrl>

namespace MusicAnalysis {
constexpr int Version = 1;
QVariantMap analyze(const QVector<float> &samples, int sampleRate);
QString keyName(int key);
QString camelot(int key);
int parseKey(const QString &name);
bool relatedKeys(int first, int second);
double nearestBeat(double seconds, double bpm, double offset);
}

// Decode one local file at a time; FFT/tempo work runs off the UI thread.
class AudioAnalyzer : public QObject
{
    Q_OBJECT
public:
    explicit AudioAnalyzer(QObject *parent = nullptr);
    void start(const QUrl &url);
    void cancel();
signals:
    void completed(const QUrl &url, const QVariantMap &result);
private:
    void finishDecode();
    QAudioDecoder m_decoder;
    QFutureWatcher<QVariantMap> m_worker;
    QTimer m_timeout;
    QVector<float> m_samples;
    QUrl m_url;
    bool m_finishing = false;
    bool m_cancelled = false;
};
