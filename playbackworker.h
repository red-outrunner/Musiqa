#pragma once

#include "audiomixer.h"
#include "playbackstate.h"
#include <QAudioSink>
#include <QElapsedTimer>
#include <QMediaPlayer>
#include <QQueue>
#include <QTimer>

class PlaybackWorker : public QObject
{
    Q_OBJECT
public:
    explicit PlaybackWorker(QObject *parent = nullptr);
    ~PlaybackWorker() override;
    QUrl source() const { return m_source; }
    QString entryId() const { return m_entryId; }
    void setEntryId(const QString &id);
    qint64 position() const { return m_position; }
    qint64 duration() const { return m_duration; }
    QMediaPlayer::PlaybackState playbackState() const { return m_state; }
    QMediaPlayer::MediaStatus mediaStatus() const { return m_status; }
    bool isSeekable() const { return m_duration > 0; }
    bool gaplessEnabled() const { return m_gapless; }
    bool crossfadeEnabled() const { return m_crossfade; }
    double crossfadeSeconds() const { return m_crossfadeSeconds; }
    bool quantizeFade() const { return m_quantize; }
    bool crossfading() const { return m_liveVoices.size() > 1; }
    void setGaplessEnabled(bool value);
    void setCrossfadeEnabled(bool value);
    void setCrossfadeSeconds(double value);
    void setQuantizeFade(bool value);
    qint64 sleepRemainingMs() const;
    bool sleepAfterTrack() const { return m_sleepAfterTrack; }
    Q_INVOKABLE void setSource(const QUrl &url);
    Q_INVOKABLE void setPosition(qint64 position);
    Q_INVOKABLE void playEntry(const QString &id, const QUrl &url, const QVariantMap &analysis = {});
    Q_INVOKABLE void prepareNext(const QString &id, const QUrl &url, const QVariantMap &analysis = {});
    Q_INVOKABLE void updateAnalysis(const QUrl &url, const QVariantMap &analysis);
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void startSleepSeconds(int seconds);
    Q_INVOKABLE void sleepAtEndOfTrack();
    Q_INVOKABLE void cancelSleep();
    Q_INVOKABLE void checkpoint() { deliverEvents(); }
    AudioOutputStatistics outputStatistics() const;
    PlaybackSnapshot snapshot();
    void setVolume(float volume);
signals:
    void updated(const PlaybackSnapshot &snapshot);
    void sourceChanged();
    void entryIdChanged();
    void positionChanged();
    void durationChanged();
    void playbackStateChanged();
    void mediaStatusChanged();
    void seekableChanged();
    void optionsChanged();
    void transitionChanged();
    void sleepChanged();
    void sleepTriggered();
    void errorOccurred(QMediaPlayer::Error error, const QString &errorString);
    void trackAdvanced(const QString &entryId, const QUrl &url);
    void advanceRequested();
    void listeningProgress(const QString &token, const QUrl &url, qint64 position, qint64 duration, bool playing, bool finished, qint64 timestamp);
    void listeningSeeked(const QString &token, qint64 position, qint64 timestamp);
private:
    struct ScheduledEvent { qint64 frame; MixerEvent event; };
    std::shared_ptr<PcmTrack> track(const QUrl &url);
    void load(const QUrl &url, bool force);
    void loaded(const std::shared_ptr<PcmTrack> &track);
    void pump();
    void deliverEvents();
    void resetOutput();
    void setState(QMediaPlayer::PlaybackState state);
    void setStatus(QMediaPlayer::MediaStatus status);
    void configureMixer();
    void sendListening(bool playing, bool finished = false);
    QAudioFormat m_format, m_deviceFormat;
    AudioMixer m_mixer;
    std::unique_ptr<QAudioSink> m_sink;
    QIODevice *m_output = nullptr;
    QHash<QString, std::weak_ptr<PcmTrack>> m_buffers;
    QHash<QString, MixerEvent> m_liveVoices;
    QQueue<ScheduledEvent> m_events;
    QByteArray m_pending;
    qint64 m_generatedFrames = 0;
    qint64 m_submittedBytes = 0;
    quint64 m_underruns = 0;
    QString m_displayToken, m_entryId;
    QUrl m_source;
    qint64 m_position = 0, m_duration = 0;
    QMediaPlayer::PlaybackState m_state = QMediaPlayer::StoppedState;
    QMediaPlayer::MediaStatus m_status = QMediaPlayer::NoMedia;
    QTimer m_pump, m_sleep, m_sleepTick;
    QElapsedTimer m_sleepClock;
    QElapsedTimer m_clock;
    qint64 m_audioOrigin = 0, m_pausedAt = 0;
    qint64 m_sleepLength = 0;
    bool m_sleepAfterTrack = false;
    bool m_gapless = true, m_crossfade = false, m_quantize = false;
    double m_crossfadeSeconds = 5;
    float m_volume = 1;
    quint64 m_revision = 0, m_generation = 0, m_sleepRevision = 0;
};
