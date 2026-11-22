#pragma once

#include "playbackstate.h"
#include <QAudioOutput>
#include <QElapsedTimer>
#include <QMediaPlayer>
#include <QMediaDevices>
#include <QSettings>
#include <QThread>
#include <functional>

class PlaybackWorker;
class LyricsDocument;

class PlaybackEngine : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QString entryId READ entryId WRITE setEntryId NOTIFY entryIdChanged)
    Q_PROPERTY(qint64 position READ position WRITE setPosition NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(QMediaPlayer::PlaybackState playbackState READ playbackState NOTIFY playbackStateChanged)
    Q_PROPERTY(QMediaPlayer::MediaStatus mediaStatus READ mediaStatus NOTIFY mediaStatusChanged)
    Q_PROPERTY(bool seekable READ isSeekable NOTIFY seekableChanged)
    Q_PROPERTY(QAudioOutput *audioOutput READ audioOutput CONSTANT)
    Q_PROPERTY(bool gaplessEnabled READ gaplessEnabled WRITE setGaplessEnabled NOTIFY optionsChanged)
    Q_PROPERTY(bool crossfadeEnabled READ crossfadeEnabled WRITE setCrossfadeEnabled NOTIFY optionsChanged)
    Q_PROPERTY(bool crossfadeOnSkip READ crossfadeOnSkip WRITE setCrossfadeOnSkip NOTIFY optionsChanged)
    Q_PROPERTY(double crossfadeSeconds READ crossfadeSeconds WRITE setCrossfadeSeconds NOTIFY optionsChanged)
    Q_PROPERTY(bool crossfading READ crossfading NOTIFY transitionChanged)
    Q_PROPERTY(qint64 sleepRemainingMs READ sleepRemainingMs NOTIFY sleepChanged)
    Q_PROPERTY(bool sleepAfterTrack READ sleepAfterTrack NOTIFY sleepChanged)
    Q_PROPERTY(bool equalizerEnabled READ equalizerEnabled WRITE setEqualizerEnabled NOTIFY equalizerChanged)
    Q_PROPERTY(QString equalizerPreset READ equalizerPreset WRITE setEqualizerPreset NOTIFY equalizerChanged)
    Q_PROPERTY(int equalizerBandCount READ equalizerBandCount WRITE setEqualizerBandCount NOTIFY equalizerChanged)
    Q_PROPERTY(QVariantList equalizerBands READ equalizerBands NOTIFY equalizerChanged)
    Q_PROPERTY(QString outputMode READ outputMode WRITE setOutputMode NOTIFY audioOutputChanged)
    Q_PROPERTY(QVariantList outputDevices READ outputDevices NOTIFY audioOutputChanged)
    Q_PROPERTY(QString outputDeviceId READ outputDeviceId WRITE setOutputDeviceId NOTIFY audioOutputChanged)
    Q_PROPERTY(QString outputDeviceName READ outputDeviceName NOTIFY audioOutputChanged)
    Q_PROPERTY(QString replayGainMode READ replayGainMode WRITE setReplayGainMode NOTIFY replayGainChanged)
    Q_PROPERTY(QObject *lyrics READ lyrics CONSTANT)
public:
    explicit PlaybackEngine(QObject *parent = nullptr);
    ~PlaybackEngine() override;
    QUrl source() const { return m_snapshot.source; }
    QString entryId() const { return m_snapshot.entryId; }
    void setEntryId(const QString &id);
    qint64 position() const { return m_snapshot.position; }
    qint64 duration() const { return m_snapshot.duration; }
    QMediaPlayer::PlaybackState playbackState() const { return m_snapshot.state; }
    QMediaPlayer::MediaStatus mediaStatus() const { return m_snapshot.status; }
    bool isSeekable() const { return m_snapshot.duration > 0; }
    QAudioOutput *audioOutput() { return &m_controls; }
    bool gaplessEnabled() const { return m_snapshot.gapless; }
    bool crossfadeEnabled() const { return m_snapshot.crossfade; }
    bool crossfadeOnSkip() const { return m_snapshot.crossfadeOnSkip; }
    double crossfadeSeconds() const { return m_snapshot.crossfadeSeconds; }
    bool crossfading() const { return m_snapshot.crossfading; }
    void setGaplessEnabled(bool value);
    void setCrossfadeEnabled(bool value);
    void setCrossfadeOnSkip(bool value);
    void setCrossfadeSeconds(double value);
    qint64 sleepRemainingMs() const;
    bool sleepAfterTrack() const { return m_snapshot.sleepAfterTrack; }
    bool equalizerEnabled() const { return m_snapshot.equalizerEnabled; }
    QString equalizerPreset() const { return m_snapshot.equalizerPreset; }
    void setEqualizerEnabled(bool enabled);
    void setEqualizerPreset(const QString &preset);
    int equalizerBandCount() const { return m_snapshot.equalizerBandCount; }
    QVariantList equalizerBands() const;
    void setEqualizerBandCount(int count);
    Q_INVOKABLE void setEqualizerBandGain(int index, double gain);
    Q_INVOKABLE void resetEqualizer();
    QString outputMode() const { return m_snapshot.outputMode; }
    void setOutputMode(const QString &mode);
    QVariantList outputDevices() const;
    QString outputDeviceId() const { return m_outputDeviceId; }
    void setOutputDeviceId(const QString &id);
    QString outputDeviceName() const { return m_outputDeviceName; }
    QString replayGainMode() const { return m_snapshot.replayGainMode; }
    void setReplayGainMode(const QString &mode);
    QObject *lyrics() const;
    Q_INVOKABLE void setSource(const QUrl &url);
    Q_INVOKABLE void setPosition(qint64 position);
    Q_INVOKABLE void playEntry(const QString &id, const QUrl &url, const QVariantMap &info = {});
    Q_INVOKABLE bool crossfadeToEntry(const QString &id, const QUrl &url, const QVariantMap &info = {});
    Q_INVOKABLE void prepareNext(const QString &id, const QUrl &url, const QVariantMap &info = {});
    Q_INVOKABLE void updateTrackInfo(const QUrl &url, const QVariantMap &info);
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void startSleepSeconds(int seconds);
    Q_INVOKABLE void sleepAtEndOfTrack();
    Q_INVOKABLE void cancelSleep();
    Q_INVOKABLE void checkpoint();
    Q_INVOKABLE QVariantMap exportOptions() const;
    Q_INVOKABLE bool importOptions(const QVariantMap &options);
    AudioOutputStatistics outputStatistics() const;
signals:
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
    void equalizerChanged();
    void audioOutputChanged();
    void replayGainChanged();
    void sleepTriggered();
    void errorOccurred(QMediaPlayer::Error error, const QString &errorString);
    void trackAdvanced(const QString &entryId, const QUrl &url);
    void advanceRequested();
    void listeningProgress(const QString &token, const QUrl &url, qint64 position, qint64 duration, bool playing, bool finished, qint64 timestamp);
    void listeningSeeked(const QString &token, qint64 position, qint64 timestamp);
private:
    void dispatch(const std::function<void()> &command);
    void applySnapshot(const PlaybackSnapshot &snapshot);
    void updateVolume();
    void updateOutputDevice();
    void saveEqualizerOptions();
    QAudioOutput m_controls;
    QSettings m_settings;
    QMediaDevices m_mediaDevices;
    QString m_outputDeviceId, m_outputDeviceName;
    QThread m_audioThread;
    PlaybackWorker *m_worker = nullptr;
    LyricsDocument *m_lyrics = nullptr;
    PlaybackSnapshot m_snapshot;
    QElapsedTimer m_snapshotAge;
    bool m_checkpointing = false;
};
