#pragma once

#include <QMediaPlayer>
#include <QUrl>
#include <QVector>

// Measured at the output, independently of UI position notifications.
struct AudioOutputStatistics {
    qint64 processedUs = 0;
    qint64 submittedFrames = 0;
    int sampleRate = 0;
    quint64 underruns = 0;
};

// Value-only notifications cross the audio/UI thread boundary. PCM, decoders,
// the mixer and the output device always stay on the audio thread.
struct PlaybackSnapshot {
    quint64 revision = 0, generation = 0, sleepRevision = 0;
    QUrl source;
    QString entryId;
    qint64 position = 0, duration = 0, sleepRemainingMs = 0;
    QMediaPlayer::PlaybackState state = QMediaPlayer::StoppedState;
    QMediaPlayer::MediaStatus status = QMediaPlayer::NoMedia;
    bool gapless = true, crossfade = false, crossfading = false;
    bool sleepAfterTrack = false;
    double crossfadeSeconds = 5;
    bool equalizerEnabled = false;
    QString equalizerPreset = QStringLiteral("bass");
    int equalizerBandCount = 10;
    QVector<double> equalizerGains, equalizerCustomGains, equalizerFrequencies;
    QString outputMode = QStringLiteral("stereo");
    QString replayGainMode = QStringLiteral("off");
};
Q_DECLARE_METATYPE(PlaybackSnapshot)
