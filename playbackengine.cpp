#include "playbackengine.h"
#include "playbackworker.h"
#include "lyricsdocument.h"
#include <QCoreApplication>
#include <QEvent>
#include <QScopedValueRollback>
#include <cmath>

namespace {
bool numericValue(const QVariant &value)
{
    const int type = value.metaType().id();
    return type == QMetaType::Int || type == QMetaType::UInt || type == QMetaType::LongLong
        || type == QMetaType::ULongLong || type == QMetaType::Double || type == QMetaType::Float;
}
QString deviceKey(const QAudioDevice &device)
{
    return QString::fromLatin1(device.id().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}
bool validDeviceKey(const QString &key)
{
    if (key.size() > 4096 || key.contains(QChar::Null)) return false;
    const auto bytes = key.toLatin1();
    return QString::fromLatin1(QByteArray::fromBase64(bytes, QByteArray::Base64UrlEncoding).toBase64(
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)) == key;
}
bool readCurve(const QVariant &value, int count, QVector<double> *result)
{
    const auto values = value.toList();
    if (values.size() != count) return false;
    result->clear(); result->reserve(count);
    for (const auto &item : values) {
        bool valid = false; const double gain = item.toDouble(&valid);
        if (!numericValue(item) || !valid || !std::isfinite(gain) || gain < -12 || gain > 12) return false;
        result->append(gain);
    }
    return true;
}
QVariantList curveValues(const QVector<double> &curve)
{
    QVariantList result; result.reserve(curve.size());
    for (double gain : curve) result.append(gain);
    return result;
}
}

PlaybackEngine::PlaybackEngine(QObject *parent) : QObject(parent), m_controls(this)
{
    qRegisterMetaType<PlaybackSnapshot>();
    m_lyrics = new LyricsDocument(this);
    m_worker = new PlaybackWorker;
    m_worker->moveToThread(&m_audioThread);
    m_audioThread.setObjectName(QStringLiteral("Musica audio"));
    connect(&m_audioThread, &QThread::finished, m_worker, &QObject::deleteLater);

    connect(m_worker, &PlaybackWorker::updated, this, &PlaybackEngine::applySnapshot, Qt::QueuedConnection);
    connect(m_worker, &PlaybackWorker::listeningProgress, this, &PlaybackEngine::listeningProgress, Qt::QueuedConnection);
    connect(m_worker, &PlaybackWorker::listeningSeeked, this, &PlaybackEngine::listeningSeeked, Qt::QueuedConnection);

    // Capture state with each event on the audio thread. Older commands' events
    // must not advance a queue or report an error after a different song starts.
    connect(m_worker, &PlaybackWorker::trackAdvanced, m_worker, [this](const QString &id, const QUrl &url) {
        const auto state = m_worker->snapshot();
        QMetaObject::invokeMethod(this, [this, state, id, url] {
            if (state.generation < m_snapshot.generation) return;
            applySnapshot(state); emit trackAdvanced(id, url);
        }, Qt::QueuedConnection);
    });
    connect(m_worker, &PlaybackWorker::advanceRequested, m_worker, [this] {
        const auto state = m_worker->snapshot();
        QMetaObject::invokeMethod(this, [this, state] {
            if (state.generation < m_snapshot.generation) return;
            applySnapshot(state); emit advanceRequested();
        }, Qt::QueuedConnection);
    });
    connect(m_worker, &PlaybackWorker::sleepTriggered, m_worker, [this] {
        const auto state = m_worker->snapshot();
        QMetaObject::invokeMethod(this, [this, state] {
            if (state.generation < m_snapshot.generation) return;
            applySnapshot(state); emit sleepTriggered();
        }, Qt::QueuedConnection);
    });
    connect(m_worker, &PlaybackWorker::errorOccurred, m_worker, [this](QMediaPlayer::Error error, const QString &message) {
        const auto state = m_worker->snapshot();
        QMetaObject::invokeMethod(this, [this, state, error, message] {
            if (state.generation < m_snapshot.generation) return;
            applySnapshot(state); emit errorOccurred(error, message);
        }, Qt::QueuedConnection);
    });

    m_audioThread.start(QThread::HighPriority);
    const bool gapless = m_settings.value("playback/gapless", true).toBool();
    const bool crossfade = m_settings.value("playback/crossfade", false).toBool();
    const bool crossfadeOnSkip = m_settings.value("playback/crossfadeOnSkip", false).toBool();
    const double seconds = m_settings.value("playback/fadeSeconds", 5).toDouble();
    const bool equalizer = m_settings.value("playback/equalizerEnabled", false).toBool();
    const QString preset = m_settings.value("playback/equalizerPreset", QStringLiteral("bass")).toString();
    const int savedCount = m_settings.value("playback/equalizerBandCount", 10).toInt();
    const int count = savedCount >= 5 && savedCount <= 15 ? savedCount : 10;
    QVector<double> custom;
    if (!readCurve(m_settings.value("playback/equalizerCustomGains"), count, &custom)) custom.fill(0, count);
    const QString outputMode = m_settings.value("playback/outputMode", QStringLiteral("stereo")).toString();
    const auto replayGainMode = m_settings.value("playback/replayGainMode", QStringLiteral("off")).toString();
    m_outputDeviceId = m_settings.value("playback/outputDeviceId").toString();
    if (!validDeviceKey(m_outputDeviceId)) m_outputDeviceId.clear();
    m_settings.remove("playback/quantize");
    dispatch([this, gapless, crossfade, crossfadeOnSkip, seconds, equalizer, preset, count, custom, outputMode, replayGainMode] {
        m_worker->setGaplessEnabled(gapless);
        m_worker->setCrossfadeEnabled(crossfade);
        m_worker->setCrossfadeOnSkip(crossfadeOnSkip);
        m_worker->setCrossfadeSeconds(seconds);
        m_worker->setEqualizerBandCount(count); m_worker->setEqualizerCustomGains(custom);
        m_worker->setEqualizerPreset(preset);
        m_worker->setEqualizerEnabled(equalizer);
        m_worker->setOutputMode(outputMode);
        m_worker->setReplayGainMode(replayGainMode);
    });
    connect(&m_mediaDevices, &QMediaDevices::audioOutputsChanged, this, &PlaybackEngine::updateOutputDevice);
    updateOutputDevice();
    connect(&m_controls, &QAudioOutput::volumeChanged, this, &PlaybackEngine::updateVolume);
    connect(&m_controls, &QAudioOutput::mutedChanged, this, &PlaybackEngine::updateVolume);
    updateVolume();
}

PlaybackEngine::~PlaybackEngine()
{
    // Destroy the device, PCM files, decoder processes and timers on their
    // owning thread before stopping its event loop.
    m_audioThread.quit();
    m_audioThread.wait();
    m_worker = nullptr;
}

void PlaybackEngine::dispatch(const std::function<void()> &command)
{
    PlaybackSnapshot state;
    QMetaObject::invokeMethod(m_worker, [this, &command, &state] {
        command();
        state = m_worker->snapshot();
    }, Qt::BlockingQueuedConnection);
    // Commands are immediately visible to QML. Monotonic revisions prevent
    // already queued position/state notifications from undoing a seek or pause.
    applySnapshot(state);
}

void PlaybackEngine::applySnapshot(const PlaybackSnapshot &state)
{
    if (state.revision <= m_snapshot.revision) return;
    const auto old = m_snapshot;
    m_snapshot = state;
    m_snapshotAge.start();
    if (old.entryId != state.entryId) emit entryIdChanged();
    if (old.source != state.source) emit sourceChanged();
    if (old.duration != state.duration) emit durationChanged();
    if ((old.duration > 0) != (state.duration > 0)) emit seekableChanged();
    if (old.position != state.position) emit positionChanged();
    if (old.state != state.state) emit playbackStateChanged();
    if (old.status != state.status) emit mediaStatusChanged();
    if (old.gapless != state.gapless || old.crossfade != state.crossfade || old.crossfadeOnSkip != state.crossfadeOnSkip
        || old.crossfadeSeconds != state.crossfadeSeconds) emit optionsChanged();
    if (old.crossfading != state.crossfading) emit transitionChanged();
    if (old.sleepRevision != state.sleepRevision) emit sleepChanged();
    if (old.equalizerEnabled != state.equalizerEnabled || old.equalizerPreset != state.equalizerPreset
        || old.equalizerBandCount != state.equalizerBandCount || old.equalizerGains != state.equalizerGains
        || old.equalizerCustomGains != state.equalizerCustomGains) emit equalizerChanged();
    if (old.outputMode != state.outputMode) emit audioOutputChanged();
    if (old.replayGainMode != state.replayGainMode) emit replayGainChanged();
}

AudioOutputStatistics PlaybackEngine::outputStatistics() const
{
    AudioOutputStatistics result;
    QMetaObject::invokeMethod(m_worker, [this, &result] { result = m_worker->outputStatistics(); }, Qt::BlockingQueuedConnection);
    return result;
}

qint64 PlaybackEngine::sleepRemainingMs() const
{
    return qMax<qint64>(0, m_snapshot.sleepRemainingMs - (m_snapshotAge.isValid() ? m_snapshotAge.elapsed() : 0));
}

void PlaybackEngine::updateVolume()
{
    const float volume = m_controls.isMuted() ? 0 : m_controls.volume();
    dispatch([this, volume] { m_worker->setVolume(volume); });
}

void PlaybackEngine::setGaplessEnabled(bool value)
{
    dispatch([this, value] { m_worker->setGaplessEnabled(value); });
    m_settings.setValue("playback/gapless", m_snapshot.gapless);
}
void PlaybackEngine::setCrossfadeEnabled(bool value)
{
    dispatch([this, value] { m_worker->setCrossfadeEnabled(value); });
    m_settings.setValue("playback/crossfade", m_snapshot.crossfade);
}
void PlaybackEngine::setCrossfadeSeconds(double value)
{
    dispatch([this, value] { m_worker->setCrossfadeSeconds(value); });
    m_settings.setValue("playback/fadeSeconds", m_snapshot.crossfadeSeconds);
}
void PlaybackEngine::setCrossfadeOnSkip(bool value)
{
    dispatch([this, value] { m_worker->setCrossfadeOnSkip(value); });
    m_settings.setValue("playback/crossfadeOnSkip", m_snapshot.crossfadeOnSkip);
}
void PlaybackEngine::setEntryId(const QString &id) { dispatch([this, id] { m_worker->setEntryId(id); }); }
QObject *PlaybackEngine::lyrics() const { return m_lyrics; }
void PlaybackEngine::setEqualizerEnabled(bool enabled)
{
    dispatch([this, enabled] { m_worker->setEqualizerEnabled(enabled); });
    m_settings.setValue("playback/equalizerEnabled", m_snapshot.equalizerEnabled);
}
void PlaybackEngine::setEqualizerPreset(const QString &preset)
{
    dispatch([this, preset] { m_worker->setEqualizerPreset(preset); });
    m_settings.setValue("playback/equalizerPreset", m_snapshot.equalizerPreset);
}
QVariantList PlaybackEngine::equalizerBands() const
{
    QVariantList result;
    for (int i = 0; i < m_snapshot.equalizerGains.size(); ++i)
        result.append(QVariantMap{{"frequency", m_snapshot.equalizerFrequencies.value(i)}, {"gain", m_snapshot.equalizerGains[i]}});
    return result;
}
void PlaybackEngine::saveEqualizerOptions()
{
    m_settings.setValue("playback/equalizerPreset", m_snapshot.equalizerPreset);
    m_settings.setValue("playback/equalizerBandCount", m_snapshot.equalizerBandCount);
    m_settings.setValue("playback/equalizerCustomGains", curveValues(m_snapshot.equalizerCustomGains));
}
void PlaybackEngine::setEqualizerBandCount(int count)
{
    dispatch([this, count] { m_worker->setEqualizerBandCount(count); }); saveEqualizerOptions();
}
void PlaybackEngine::setEqualizerBandGain(int index, double gain)
{
    dispatch([this, index, gain] { m_worker->setEqualizerBandGain(index, gain); }); saveEqualizerOptions();
}
void PlaybackEngine::resetEqualizer()
{
    dispatch([this] { m_worker->resetEqualizer(); }); saveEqualizerOptions();
}
void PlaybackEngine::setOutputMode(const QString &mode)
{
    dispatch([this, mode] { m_worker->setOutputMode(mode); });
    m_settings.setValue("playback/outputMode", m_snapshot.outputMode);
}
QVariantList PlaybackEngine::outputDevices() const
{
    const auto defaultDevice = QMediaDevices::defaultAudioOutput();
    QVariantList result{QVariantMap{{"id", QString()}, {"name", defaultDevice.isNull() ? tr("System default")
        : tr("System default (%1)").arg(defaultDevice.description())}, {"available", !defaultDevice.isNull()}}};
    bool selectedFound = m_outputDeviceId.isEmpty();
    for (const auto &device : QMediaDevices::audioOutputs()) {
        const auto id = deviceKey(device); selectedFound |= id == m_outputDeviceId;
        result.append(QVariantMap{{"id", id}, {"name", device.description()}, {"available", true}});
    }
    if (!selectedFound) result.append(QVariantMap{{"id", m_outputDeviceId}, {"name", tr("Unavailable device (using system default)")}, {"available", false}});
    return result;
}
void PlaybackEngine::setOutputDeviceId(const QString &id)
{
    if (!validDeviceKey(id) || id == m_outputDeviceId) return;
    m_outputDeviceId = id; m_settings.setValue("playback/outputDeviceId", id); updateOutputDevice();
}
void PlaybackEngine::updateOutputDevice()
{
    auto selected = QMediaDevices::defaultAudioOutput();
    if (!m_outputDeviceId.isEmpty()) {
        for (const auto &device : QMediaDevices::audioOutputs())
            if (deviceKey(device) == m_outputDeviceId) { selected = device; break; }
    }
    m_outputDeviceName = selected.isNull() ? tr("No audio output") : selected.description();
    dispatch([this, selected] { m_worker->setOutputDevice(selected); }); emit audioOutputChanged();
}
QVariantMap PlaybackEngine::exportOptions() const
{
    return {{"gapless", gaplessEnabled()}, {"crossfade", crossfadeEnabled()}, {"crossfadeOnSkip", crossfadeOnSkip()},
        {"crossfadeSeconds", crossfadeSeconds()}, {"equalizerEnabled", equalizerEnabled()}, {"equalizerPreset", equalizerPreset()},
        {"equalizerBandCount", equalizerBandCount()}, {"equalizerCustomGains", curveValues(m_snapshot.equalizerCustomGains)},
        {"outputMode", outputMode()}, {"outputDeviceId", m_outputDeviceId}, {"replayGainMode", replayGainMode()}};
}
void PlaybackEngine::setReplayGainMode(const QString &mode)
{
    if (mode != "off" && mode != "track" && mode != "album") return;
    dispatch([this, mode] { m_worker->setReplayGainMode(mode); });
    m_settings.setValue("playback/replayGainMode", replayGainMode());
}
bool PlaybackEngine::importOptions(const QVariantMap &options)
{
    const auto required = {QStringLiteral("gapless"), QStringLiteral("crossfade"), QStringLiteral("crossfadeSeconds"),
        QStringLiteral("equalizerEnabled"), QStringLiteral("equalizerPreset")};
    for (const auto &key : required) if (!options.contains(key)) return false;
    for (const auto &key : {QStringLiteral("gapless"), QStringLiteral("crossfade"), QStringLiteral("equalizerEnabled")})
        if (options.value(key).metaType().id() != QMetaType::Bool) return false;
    if (options.contains("crossfadeOnSkip") && options.value("crossfadeOnSkip").metaType().id() != QMetaType::Bool) return false;
    for (const auto &key : {QStringLiteral("equalizerPreset"), QStringLiteral("outputMode"), QStringLiteral("outputDeviceId")})
        if (options.contains(key) && options.value(key).metaType().id() != QMetaType::QString) return false;
    bool valid = false; const double seconds = options.value("crossfadeSeconds").toDouble(&valid);
    const QString preset = options.value("equalizerPreset").toString();
    if (!numericValue(options.value("crossfadeSeconds")) || !valid || !std::isfinite(seconds) || seconds < 0 || seconds > 70
        || !QStringList{"flat", "bass", "vocal", "headphones", "custom"}.contains(preset)) return false;
    const auto countValue = options.value("equalizerBandCount", 10);
    const int count = countValue.toInt(&valid);
    if (!numericValue(countValue) || !valid || countValue.toDouble() != count || count < 5 || count > 15) return false;
    QVector<double> custom(count, 0);
    if (options.contains("equalizerCustomGains") && !readCurve(options.value("equalizerCustomGains"), count, &custom)) return false;
    if (preset == QLatin1String("custom") && !options.contains("equalizerCustomGains")) return false;
    const QString mode = options.value("outputMode", QStringLiteral("stereo")).toString();
    const QString deviceId = options.value("outputDeviceId").toString();
    const auto replayModeValue = options.value("replayGainMode", QStringLiteral("off"));
    const auto replayMode = replayModeValue.toString();
    if (replayModeValue.metaType().id() != QMetaType::QString || !QStringList{"off", "track", "album"}.contains(replayMode)) return false;
    if ((mode != QLatin1String("stereo") && mode != QLatin1String("mono")) || !validDeviceKey(deviceId)) return false;
    setGaplessEnabled(options.value("gapless").toBool());
    setCrossfadeSeconds(seconds); setCrossfadeEnabled(options.value("crossfade").toBool());
    setCrossfadeOnSkip(options.value("crossfadeOnSkip", false).toBool());
    dispatch([this, count, custom, preset] {
        m_worker->setEqualizerBandCount(count); m_worker->setEqualizerCustomGains(custom); m_worker->setEqualizerPreset(preset);
    }); saveEqualizerOptions(); setEqualizerEnabled(options.value("equalizerEnabled").toBool());
    setOutputMode(mode); setOutputDeviceId(deviceId);
    setReplayGainMode(replayMode);
    return true;
}
void PlaybackEngine::setSource(const QUrl &url) { dispatch([this, url] { m_worker->setSource(url); }); }
void PlaybackEngine::setPosition(qint64 position) { dispatch([this, position] { m_worker->setPosition(position); }); }
void PlaybackEngine::playEntry(const QString &id, const QUrl &url, const QVariantMap &info)
{
    dispatch([this, id, url, info] { m_worker->playEntry(id, url, info); });
}
bool PlaybackEngine::crossfadeToEntry(const QString &id, const QUrl &url, const QVariantMap &info)
{
    bool started = false;
    dispatch([this, id, url, info, &started] { started = m_worker->crossfadeToEntry(id, url, info); });
    return started;
}
void PlaybackEngine::prepareNext(const QString &id, const QUrl &url, const QVariantMap &info)
{
    dispatch([this, id, url, info] { m_worker->prepareNext(id, url, info); });
}
void PlaybackEngine::updateTrackInfo(const QUrl &url, const QVariantMap &info)
{
    dispatch([this, url, info] { m_worker->updateTrackInfo(url, info); });
}
void PlaybackEngine::play() { dispatch([this] { m_worker->play(); }); }
void PlaybackEngine::pause() { dispatch([this] { m_worker->pause(); }); }
void PlaybackEngine::stop() { dispatch([this] { m_worker->stop(); }); }
void PlaybackEngine::checkpoint()
{
    dispatch([this] { m_worker->checkpoint(); });
    if (m_checkpointing) return;
    QScopedValueRollback<bool> checkpoint(m_checkpointing, true);
    // Session/history saves need the audible track and every listening sample,
    // including a transition that happened while the UI was busy. Deliver only
    // this object's queued audio notifications; don't process user input here.
    QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
}
void PlaybackEngine::startSleepSeconds(int seconds) { dispatch([this, seconds] { m_worker->startSleepSeconds(seconds); }); }
void PlaybackEngine::sleepAtEndOfTrack() { dispatch([this] { m_worker->sleepAtEndOfTrack(); }); }
void PlaybackEngine::cancelSleep() { dispatch([this] { m_worker->cancelSleep(); }); }
