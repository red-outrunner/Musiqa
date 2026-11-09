#pragma once

#include <QAudioDecoder>
#include <QAudioFormat>
#include <QTemporaryFile>
#include <QVariantMap>
#include <QUrl>
#include <QProcess>
#include <memory>

// Only the current and upcoming songs are decoded. PCM lives in temporary
// files so long tracks and a 70-second overlap don't grow the process heap.
class PcmTrack : public QObject
{
    Q_OBJECT
public:
    PcmTrack(const QUrl &url, const QAudioFormat &format);
    ~PcmTrack() override;
    void decode();
    void setInfo(const QVariantMap &info);
    bool append(const char *data, qint64 bytes);
    QByteArray readFrames(qint64 position, int count);
    qint64 frames() const { return m_frames; }
    qint64 durationFrames() const;
    bool complete = false;
    QString error;
    QUrl url;
    QAudioFormat format;
    QVariantMap analysis;
signals:
    void ready();
private:
    QTemporaryFile m_pcm;
    std::unique_ptr<QAudioDecoder> m_decoder;
    std::unique_ptr<QProcess> m_process;
    QByteArray m_decodeTail;
    QByteArray m_decodeError;
    qint64 m_frames = 0;
    qint64 m_estimatedFrames = 0;
};

struct MixerEvent
{
    enum Kind { Started, Progress, Finished, QueueEnded } kind;
    qint64 offset = 0;
    QString entryId, token;
    QUrl url;
    qint64 position = 0, duration = 0; // Frames, relative to the file.
    std::shared_ptr<PcmTrack> track;
};

struct MixerBlock
{
    QByteArray pcm;
    QList<MixerEvent> events;
    bool waiting = false;
};

class AudioMixer
{
public:
    struct Voice {
        std::shared_ptr<PcmTrack> track;
        QString entryId, token;
        qint64 frame = 0;
    };
    explicit AudioMixer(int sampleRate = 48000, int channels = 2);
    void setCurrent(const std::shared_ptr<PcmTrack> &track, const QString &entryId, qint64 frame = 0, const QString &token = {});
    void setNext(const std::shared_ptr<PcmTrack> &track, const QString &entryId);
    void seek(qint64 frame);
    void clear();
    MixerBlock render(int frames);
    const Voice &current() const { return m_current; }
    const Voice &next() const { return m_next; }
    const Voice &outgoing() const { return m_outgoing; }
    bool gapless = true;
    double crossfadeSeconds = 0;
    bool quantize = false;
    bool stopAfterCurrent = false;
    bool ended() const { return m_ended; }
    static QPair<qint64, qint64> transitionPlan(const PcmTrack &from, const PcmTrack &to, double seconds, bool quantize, qint64 position = 0);
private:
    MixerEvent event(MixerEvent::Kind kind, const Voice &voice, qint64 offset) const;
    Voice newVoice(const std::shared_ptr<PcmTrack> &track, const QString &id, qint64 frame = 0);
    int m_rate, m_channels;
    quint64 m_serial = 0;
    QString m_tokenPrefix;
    Voice m_current, m_next, m_outgoing;
    qint64 m_fadeLength = 0, m_fadeFrame = 0;
    bool m_startPending = false, m_ended = false;
};
