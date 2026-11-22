#include "playbackworker.h"

#include <QAudioDevice>
#include <QFileInfo>
#include <QMediaDevices>
#include <algorithm>
#include <cmath>
#include <cstring>

PlaybackWorker::PlaybackWorker(QObject *parent) : QObject(parent), m_mixer(48000, 2), m_pump(this), m_sleep(this), m_sleepTick(this)
{
    m_clock.start();
    const auto device = QMediaDevices::defaultAudioOutput(); m_device = device;
    m_deviceFormat.setSampleRate(48000); m_deviceFormat.setChannelCount(2); m_deviceFormat.setSampleFormat(QAudioFormat::Float);
    if (!device.isNull() && !device.isFormatSupported(m_deviceFormat)) m_deviceFormat = device.preferredFormat();
    m_format = m_deviceFormat; m_format.setSampleFormat(QAudioFormat::Float);
    m_mixer = AudioMixer(m_format.sampleRate(), m_format.channelCount());
    m_equalizer = AudioEqualizer(m_format.sampleRate(), m_format.channelCount());
    configureMixer();
    m_pump.setInterval(10); m_pump.setTimerType(Qt::PreciseTimer);
    connect(&m_pump, &QTimer::timeout, this, &PlaybackWorker::pump);
    m_sleep.setSingleShot(true); m_sleep.setTimerType(Qt::PreciseTimer);
    m_sleepTick.setInterval(250);
    connect(&m_sleepTick, &QTimer::timeout, this, &PlaybackWorker::sleepChanged);
    connect(&m_sleep, &QTimer::timeout, this, [this] { pause(); cancelSleep(); emit sleepTriggered(); });
    const auto publish = [this] { emit updated(snapshot()); };
    connect(this, &PlaybackWorker::sourceChanged, this, publish);
    connect(this, &PlaybackWorker::entryIdChanged, this, publish);
    connect(this, &PlaybackWorker::positionChanged, this, [this, publish] {
        const auto now = m_clock.elapsed();
        if (m_economyMode && now - m_lastPositionPublish < 200) return;
        m_lastPositionPublish = now; publish();
    });
    connect(this, &PlaybackWorker::durationChanged, this, publish);
    connect(this, &PlaybackWorker::playbackStateChanged, this, publish);
    connect(this, &PlaybackWorker::mediaStatusChanged, this, publish);
    connect(this, &PlaybackWorker::seekableChanged, this, publish);
    connect(this, &PlaybackWorker::optionsChanged, this, publish);
    connect(this, &PlaybackWorker::transitionChanged, this, publish);
    connect(this, &PlaybackWorker::sleepChanged, this, [this, publish] { ++m_sleepRevision; publish(); });
}

PlaybackWorker::~PlaybackWorker()
{
    m_pump.stop(); m_sleep.stop(); m_sleepTick.stop();
    // Stopping a decoder process can emit ready/finished synchronously. Detach
    // those callbacks before releasing PCM buffers or dismantling the timers.
    for (const auto &weak : m_buffers)
        if (auto buffer = weak.lock()) disconnect(buffer.get(), nullptr, this, nullptr);
    resetOutput(); m_liveVoices.clear(); m_mixer.clear();
}

AudioOutputStatistics PlaybackWorker::outputStatistics() const
{
    return {m_sink ? m_sink->processedUSecs() : 0,
        m_submittedBytes / m_deviceFormat.bytesPerFrame(), m_format.sampleRate(), m_underruns};
}

PlaybackSnapshot PlaybackWorker::snapshot()
{
    PlaybackSnapshot result;
    result.revision = ++m_revision; result.generation = m_generation; result.sleepRevision = m_sleepRevision;
    result.source = m_source; result.entryId = m_entryId; result.position = m_position; result.duration = m_duration;
    result.state = m_state; result.status = m_status;
    result.gapless = m_gapless; result.crossfade = m_crossfade; result.crossfadeSeconds = m_crossfadeSeconds;
    result.crossfadeOnSkip = m_crossfadeOnSkip;
    result.crossfading = crossfading();
    result.sleepRemainingMs = sleepRemainingMs(); result.sleepAfterTrack = m_sleepAfterTrack;
    result.economyMode = m_economyMode;
    result.equalizerEnabled = m_equalizer.enabled(); result.equalizerPreset = m_equalizer.preset();
    result.equalizerBandCount = m_equalizer.bandCount(); result.equalizerGains = m_equalizer.gains();
    result.equalizerCustomGains = m_equalizer.customGains(); result.equalizerFrequencies = m_equalizer.frequencies();
    result.outputMode = m_equalizer.mono() ? QStringLiteral("mono") : QStringLiteral("stereo");
    result.replayGainMode = m_replayGainMode;
    return result;
}

void PlaybackWorker::setVolume(float volume)
{
    m_volume = volume;
    if (m_sink) m_sink->setVolume(volume);
}

void PlaybackWorker::setEqualizerEnabled(bool enabled)
{
    if (m_equalizer.enabled() == enabled) return;
    m_equalizer.setEnabled(enabled); emit optionsChanged();
}

void PlaybackWorker::setEqualizerPreset(const QString &preset)
{
    if (m_equalizer.preset() == preset) return;
    if (m_equalizer.setPreset(preset)) emit optionsChanged();
}

void PlaybackWorker::setEqualizerBandCount(int count)
{
    if (m_equalizer.bandCount() != count && m_equalizer.setBandCount(count)) emit optionsChanged();
}
void PlaybackWorker::setEqualizerBandGain(int index, double gain)
{
    if (m_equalizer.setBandGain(index, gain)) emit optionsChanged();
}
void PlaybackWorker::setEqualizerCustomGains(const QVector<double> &gains)
{
    if (m_equalizer.setCustomGains(gains)) emit optionsChanged();
}
void PlaybackWorker::resetEqualizer() { m_equalizer.resetCurve(); emit optionsChanged(); }
void PlaybackWorker::setOutputMode(const QString &mode)
{
    if (mode != QLatin1String("mono") && mode != QLatin1String("stereo")) return;
    if (m_equalizer.mono() == (mode == QLatin1String("mono"))) return;
    m_equalizer.setMono(mode == QLatin1String("mono")); emit optionsChanged();
}

void PlaybackWorker::setOutputDevice(const QAudioDevice &device)
{
    if (m_device.id() == device.id() && m_device.isNull() == device.isNull()) {
        m_device = device; return;
    }
    rebuildOutputForDevice(device);
}

void PlaybackWorker::rebuildOutputForDevice(const QAudioDevice &device)
{
    deliverEvents();
    const bool playing = m_state == QMediaPlayer::PlayingState;
    m_pump.stop(); sendListening(false);
    const auto current = m_mixer.current();
    const auto next = current.track && (current.entryId != m_entryId || current.track->url != m_source)
        ? current : m_mixer.next();
    // A pending hardware event can own the last reference to the displayed
    // track after the mixer has generated the next song. Keep it alive before
    // resetting the sink clears those events.
    auto active = m_buffers.value(m_source.toString()).lock();
    const int oldRate = m_format.sampleRate();
    QAudioFormat target = m_deviceFormat;
    if (!device.isNull() && !device.isFormatSupported(target)) target = device.preferredFormat();
    m_device = device;
    resetOutput();
    if (target.sampleRate() != m_format.sampleRate() || target.channelCount() != m_format.channelCount()) {
        const auto preset = m_equalizer.preset(); const auto custom = m_equalizer.customGains();
        const int count = m_equalizer.bandCount();
        const bool enabled = m_equalizer.enabled(), mono = m_equalizer.mono();
        for (const auto &weak : m_buffers) if (auto data = weak.lock()) disconnect(data.get(), nullptr, this, nullptr);
        m_buffers.clear(); m_liveVoices.clear();
        m_deviceFormat = target; m_format = target; m_format.setSampleFormat(QAudioFormat::Float);
        m_mixer = AudioMixer(m_format.sampleRate(), m_format.channelCount()); configureMixer();
        m_equalizer = AudioEqualizer(m_format.sampleRate(), m_format.channelCount());
        m_equalizer.setBandCount(count); m_equalizer.setCustomGains(custom); m_equalizer.setPreset(preset);
        m_equalizer.setEnabled(enabled); m_equalizer.setMono(mono); m_equalizer.reset();
        if (!m_source.isEmpty()) {
            auto replacement = track(m_source); replacement->setInfo({{"duration", m_duration}});
            m_mixer.setCurrent(replacement, m_entryId, m_position * m_format.sampleRate() / 1000, m_displayToken);
        }
        if (next.track) {
            auto upcoming = track(next.track->url);
            upcoming->setInfo({{"duration", next.track->durationFrames() * 1000 / oldRate}});
            m_mixer.setNext(upcoming, next.entryId);
        }
    } else {
        m_deviceFormat = target;
        if (!active && !m_source.isEmpty()) active = track(m_source);
        if (active) m_mixer.setCurrent(active, m_entryId, m_position * m_format.sampleRate() / 1000, m_displayToken);
        m_mixer.setNext(next.track, next.entryId);
    }
    emit transitionChanged();
    if (playing) {
        if (device.isNull()) {
            setState(QMediaPlayer::PausedState);
            emit errorOccurred(QMediaPlayer::ResourceError, tr("No audio output device is available. Reconnect it and press Play."));
        } else { sendListening(true); m_pump.start(); pump(); }
    }
}

void PlaybackWorker::configureMixer()
{
    m_mixer.replayGainMode = m_replayGainMode;
    m_mixer.gapless = m_gapless;
    m_mixer.crossfadeSeconds = m_crossfade ? m_crossfadeSeconds : 0;
    m_mixer.stopAfterCurrent = m_sleepAfterTrack;
}
void PlaybackWorker::setReplayGainMode(const QString &mode)
{
    if ((mode != "off" && mode != "track" && mode != "album") || mode == m_replayGainMode) return;
    m_replayGainMode = mode; configureMixer();
    if (mode != "off") for (const auto &weak : m_buffers) if (auto buffer = weak.lock()) buffer->loadReplayGain();
    emit optionsChanged();
}
void PlaybackWorker::setGaplessEnabled(bool value) { if (m_gapless == value) return; m_gapless = value; configureMixer(); emit optionsChanged(); }
void PlaybackWorker::setCrossfadeEnabled(bool value) { if (m_crossfade == value) return; m_crossfade = value; configureMixer(); emit optionsChanged(); }
void PlaybackWorker::setCrossfadeOnSkip(bool value) { if (m_crossfadeOnSkip == value) return; m_crossfadeOnSkip = value; emit optionsChanged(); }
void PlaybackWorker::setCrossfadeSeconds(double value) { if (!std::isfinite(value)) return; value = qBound(0.0, value, 70.0); if (qFuzzyCompare(value, m_crossfadeSeconds)) return; m_crossfadeSeconds = value; configureMixer(); emit optionsChanged(); }
void PlaybackWorker::setEntryId(const QString &id) { if (id == m_entryId) return; m_entryId = id; emit entryIdChanged(); }
void PlaybackWorker::setState(QMediaPlayer::PlaybackState state) { if (state == m_state) return; m_state = state; emit playbackStateChanged(); }
void PlaybackWorker::setStatus(QMediaPlayer::MediaStatus status) { if (status == m_status) return; m_status = status; emit mediaStatusChanged(); }

std::shared_ptr<PcmTrack> PlaybackWorker::track(const QUrl &url)
{
    if (auto existing = m_buffers.value(url.toString()).lock()) return existing;
    auto buffer = std::make_shared<PcmTrack>(url, m_format);
    if (m_replayGainMode != "off") buffer->loadReplayGain();
    m_buffers.insert(url.toString(), buffer);
    const std::weak_ptr<PcmTrack> weak = buffer;
    connect(buffer.get(), &PcmTrack::ready, this, [this, weak] {
        if (auto data = weak.lock()) loaded(data);
        // Decoder callbacks must return before a transition releases their buffer.
        QTimer::singleShot(0, this, &PlaybackWorker::pump);
    });
    buffer->decode();
    return buffer;
}

void PlaybackWorker::loaded(const std::shared_ptr<PcmTrack> &buffer)
{
    if (buffer->url != m_source) return;
    if (!buffer->error.isEmpty()) {
        if (m_status == QMediaPlayer::InvalidMedia) return;
        m_pump.stop(); sendListening(false, true); m_liveVoices.clear(); resetOutput();
        setState(QMediaPlayer::StoppedState); setStatus(QMediaPlayer::InvalidMedia);
        emit errorOccurred(QMediaPlayer::ResourceError, buffer->error); return;
    }
    const auto duration = buffer->durationFrames() * 1000 / m_format.sampleRate();
    if (duration != m_duration) { m_duration = duration; emit durationChanged(); emit seekableChanged(); }
    if (buffer->frames() > 0 && (m_status == QMediaPlayer::LoadingMedia || m_status == QMediaPlayer::NoMedia)) setStatus(QMediaPlayer::LoadedMedia);
}

void PlaybackWorker::resetOutput()
{
    if (m_sink) { m_sink->reset(); m_sink.reset(); }
    m_output = nullptr; m_pending.clear(); m_events.clear(); m_generatedFrames = 0; m_submittedBytes = 0;
    m_equalizer.reset();
}

void PlaybackWorker::setSource(const QUrl &url) { load(url, false); }
void PlaybackWorker::load(const QUrl &url, bool force)
{
    if (!force && url == m_source) return;
    ++m_generation;
    sendListening(false, true); m_liveVoices.clear(); m_displayToken.clear();
    m_pump.stop(); resetOutput(); setState(QMediaPlayer::StoppedState);
    auto buffer = url.isEmpty() ? std::shared_ptr<PcmTrack>() : track(url);
    m_mixer.clear(); m_mixer.setCurrent(buffer, m_entryId); configureMixer();
    m_source = url; m_position = 0; m_duration = 0;
    setStatus(url.isEmpty() ? QMediaPlayer::NoMedia : QMediaPlayer::LoadingMedia);
    emit sourceChanged(); emit positionChanged(); emit durationChanged(); emit seekableChanged(); emit transitionChanged();
    if (buffer) loaded(buffer);
}

void PlaybackWorker::playEntry(const QString &id, const QUrl &url, const QVariantMap &info)
{
    setEntryId(id); load(url, true);
    if (auto buffer = m_buffers.value(url.toString()).lock()) { buffer->setInfo(info); loaded(buffer); }
    play();
}
bool PlaybackWorker::crossfadeToEntry(const QString &id, const QUrl &url, const QVariantMap &info)
{
    if (!m_crossfadeOnSkip || !m_crossfade || m_crossfadeSeconds <= 0
        || m_state != QMediaPlayer::PlayingState || id.isEmpty() || url.isEmpty()) return false;
    deliverEvents();
    auto active = m_buffers.value(m_source.toString()).lock();
    auto incoming = m_buffers.value(url.toString()).lock();
    const auto frame = m_position * m_format.sampleRate() / 1000;
    // Next is normally prebuffered. If it isn't ready, let the caller perform
    // the normal immediate skip rather than delaying the user's command.
    if (!active || !incoming || !active->error.isEmpty() || !incoming->error.isEmpty()
        || active->frames() <= frame || incoming->frames() <= 1) return false;
    incoming->setInfo(info);
    QString token;
    const auto displayed = m_liveVoices.constFind(m_displayToken);
    if (displayed != m_liveVoices.cend() && displayed->url == m_source && displayed->entryId == m_entryId)
        token = m_displayToken;
    else if (m_mixer.current().track == active && m_mixer.current().entryId == m_entryId)
        token = m_mixer.current().token;
    auto mixer = m_mixer;
    mixer.setCurrent(active, m_entryId, frame, token);
    if (!mixer.crossfadeTo(incoming, id, m_crossfadeSeconds)) return false;
    ++m_generation;
    m_pump.stop(); sendListening(false);
    // Rewind rendered-ahead PCM to the audible position before replacing the
    // sink. Retain the listening token so the outgoing song isn't counted twice.
    resetOutput();
    m_mixer = std::move(mixer);
    for (auto it = m_liveVoices.begin(); it != m_liveVoices.end();) {
        if (it.key() != token) {
            emit listeningProgress(it->token, it->url, it->position * 1000 / m_format.sampleRate(), it->duration * 1000 / m_format.sampleRate(), false, true, m_clock.elapsed());
            it = m_liveVoices.erase(it);
        } else ++it;
    }
    m_source = url; m_entryId = id; m_position = 0;
    m_duration = incoming->durationFrames() * 1000 / m_format.sampleRate();
    setStatus(QMediaPlayer::LoadedMedia);
    emit sourceChanged(); emit entryIdChanged(); emit positionChanged(); emit durationChanged(); emit seekableChanged(); emit transitionChanged();
    sendListening(true); m_pump.start();
    return true;
}
void PlaybackWorker::prepareNext(const QString &id, const QUrl &url, const QVariantMap &info)
{
    if (url.isEmpty() || id.isEmpty() || m_sleepAfterTrack) { m_mixer.setNext({}, {}); return; }
    if (m_mixer.next().entryId == id && m_mixer.next().track && m_mixer.next().track->url == url) { m_mixer.next().track->setInfo(info); return; }
    auto buffer = track(url); buffer->setInfo(info); m_mixer.setNext(buffer, id);
}

void PlaybackWorker::updateTrackInfo(const QUrl &url, const QVariantMap &info)
{
    if (auto buffer = m_buffers.value(url.toString()).lock()) { buffer->setInfo(info); loaded(buffer); }
}

void PlaybackWorker::play()
{
    if (!m_mixer.current().track || m_status == QMediaPlayer::InvalidMedia) return;
    if (m_status == QMediaPlayer::EndOfMedia || m_mixer.ended()) {
        auto data = m_mixer.current().track; m_mixer.setCurrent(data, m_entryId); m_position = 0; resetOutput();
        setStatus(QMediaPlayer::LoadedMedia); emit positionChanged();
    }
    setState(QMediaPlayer::PlayingState);
    if (m_sink) { m_audioOrigin += qMax<qint64>(0, m_clock.elapsed() - m_pausedAt); m_sink->resume(); }
    sendListening(true); m_pump.start(); pump();
}

void PlaybackWorker::pause()
{
    if (m_state != QMediaPlayer::PlayingState) return;
    deliverEvents();
    if (m_sink) m_sink->suspend();
    m_pausedAt = m_clock.elapsed();
    m_pump.stop(); sendListening(false); setState(QMediaPlayer::PausedState);
}

void PlaybackWorker::stop()
{
    ++m_generation;
    sendListening(false, true); m_liveVoices.clear(); m_displayToken.clear();
    m_pump.stop(); resetOutput();
    auto data = m_buffers.value(m_source.toString()).lock();
    m_mixer.setCurrent(data, m_entryId); m_position = 0;
    setState(QMediaPlayer::StoppedState);
    if (data) setStatus(QMediaPlayer::LoadedMedia);
    emit positionChanged(); emit transitionChanged();
}

void PlaybackWorker::setPosition(qint64 position)
{
    if (!m_mixer.current().track) return;
    const bool playing = m_state == QMediaPlayer::PlayingState;
    deliverEvents(); sendListening(false);
    position = qBound<qint64>(0, position, qMax<qint64>(0, m_duration - 1));
    // Seeking cancels an overlap and flushes already queued hardware audio.
    auto active = m_buffers.value(m_source.toString()).lock();
    if (!active) return;
    if (m_mixer.current().track != active) m_mixer.setCurrent(active, m_entryId, 0, m_displayToken);
    m_mixer.seek(position * m_format.sampleRate() / 1000);
    resetOutput();
    for (auto it = m_liveVoices.begin(); it != m_liveVoices.end();) {
        if (it.key() != m_displayToken) {
            emit listeningProgress(it->token, it->url, it->position * 1000 / m_format.sampleRate(), it->duration * 1000 / m_format.sampleRate(), false, true, m_clock.elapsed());
            it = m_liveVoices.erase(it);
        }
        else { it->position = position * m_format.sampleRate() / 1000; ++it; }
    }
    emit listeningSeeked(m_mixer.current().token, position, m_clock.elapsed());
    m_position = position; emit positionChanged(); emit transitionChanged();
    if (m_status == QMediaPlayer::EndOfMedia) setStatus(QMediaPlayer::LoadedMedia);
    if (playing) { sendListening(true); pump(); }
}

void PlaybackWorker::sendListening(bool playing, bool finished)
{
    for (const auto &voice : m_liveVoices)
        reportListening(voice.token, voice.url, voice.position * 1000 / m_format.sampleRate(), voice.duration * 1000 / m_format.sampleRate(), playing, finished, m_clock.elapsed());
}
void PlaybackWorker::reportListening(const QString &token, const QUrl &url, qint64 position, qint64 duration, bool playing, bool finished, qint64 timestamp)
{
    const auto previous = m_lastListeningReport.constFind(token);
    if (m_economyMode && playing && !finished && previous != m_lastListeningReport.cend()
        && timestamp >= *previous && timestamp - *previous < 100) return;
    if (playing && !finished) m_lastListeningReport.insert(token, timestamp);
    else m_lastListeningReport.remove(token);
    emit listeningProgress(token, url, position, duration, playing, finished, timestamp);
}

void PlaybackWorker::deliverEvents()
{
    if (!m_sink) return;
    const qint64 played = qRound64(double(m_sink->processedUSecs()) * m_format.sampleRate() / 1000000) + 2;
    while (!m_events.isEmpty() && m_events.head().frame <= played) {
        const auto scheduled = m_events.dequeue();
        const auto event = scheduled.event;
        const qint64 timestamp = m_audioOrigin + scheduled.frame * 1000 / m_format.sampleRate();
        const auto position = event.position * 1000 / m_format.sampleRate(), duration = event.duration * 1000 / m_format.sampleRate();
        if (event.kind == MixerEvent::Started) {
            const bool changed = event.entryId != m_entryId || event.url != m_source;
            m_liveVoices.insert(event.token, event); m_displayToken = event.token;
            m_source = event.url; m_entryId = event.entryId; m_position = position; m_duration = duration;
            if (changed) { emit trackAdvanced(event.entryId, event.url); emit sourceChanged(); emit entryIdChanged(); }
            setStatus(QMediaPlayer::LoadedMedia); emit durationChanged(); emit positionChanged(); emit seekableChanged(); emit transitionChanged();
            reportListening(event.token, event.url, position, duration, true, false, timestamp);
        } else if (event.kind == MixerEvent::Progress || event.kind == MixerEvent::Finished) {
            m_liveVoices[event.token] = event;
            reportListening(event.token, event.url, position, duration, event.kind != MixerEvent::Finished, event.kind == MixerEvent::Finished, timestamp);
            if (event.token == m_displayToken && m_position != position) { m_position = position; emit positionChanged(); }
            if (event.kind == MixerEvent::Finished) { m_liveVoices.remove(event.token); emit transitionChanged(); }
        } else if (event.kind == MixerEvent::QueueEnded) {
            m_pump.stop(); m_liveVoices.clear(); setState(QMediaPlayer::StoppedState);
            const bool sleeping = m_sleepAfterTrack;
            setStatus(QMediaPlayer::EndOfMedia); emit transitionChanged();
            if (sleeping) { cancelSleep(); emit sleepTriggered(); }
            else emit advanceRequested();
            return;
        }
    }
}

void PlaybackWorker::pump()
{
    if (m_state != QMediaPlayer::PlayingState) return;
    deliverEvents();
    if (m_state != QMediaPlayer::PlayingState) return;
    if (!m_sink) {
        if (m_mixer.current().track && m_mixer.current().track->frames() <= m_mixer.current().frame) return;
        const auto device = m_device;
        if (device.isNull()) { setState(QMediaPlayer::StoppedState); emit errorOccurred(QMediaPlayer::ResourceError, tr("No audio output device is available.")); return; }
        m_sink = std::make_unique<QAudioSink>(device, m_deviceFormat);
        auto *sink = m_sink.get();
        connect(sink, &QAudioSink::stateChanged, this, [this, sink](QAudio::State state) {
            if (state == QAudio::IdleState && m_state == QMediaPlayer::PlayingState
                && m_submittedBytes > 0 && !m_mixer.ended()) ++m_underruns;
            if (state != QAudio::StoppedState || !m_sink || m_sink.get() != sink || sink->error() == QAudio::NoError) return;
            QTimer::singleShot(0, this, [this, sink] {
                if (!m_sink || m_sink.get() != sink || sink->error() == QAudio::NoError) return;
                m_pump.stop(); sendListening(false); setState(QMediaPlayer::PausedState); resetOutput();
                emit errorOccurred(QMediaPlayer::ResourceError, tr("The audio output became unavailable. Reconnect it and press Play."));
            });
        });
        m_sink->setBufferSize(m_deviceFormat.bytesForDuration(200000));
        m_sink->setVolume(m_volume);
        m_output = m_sink->start();
        m_audioOrigin = m_clock.elapsed();
        if (!m_output) { setState(QMediaPlayer::StoppedState); emit errorOccurred(QMediaPlayer::ResourceError, tr("Couldn't open the audio output device.")); return; }
    }
    int iterations = 0;
    while (++iterations <= 8 && m_sink->bytesFree() >= m_deviceFormat.bytesPerFrame()) {
        if (m_pending.isEmpty()) {
            const int frames = int(qMin<qint64>(m_format.sampleRate() / 50, m_sink->bytesFree() / m_deviceFormat.bytesPerFrame()));
            auto block = m_mixer.render(frames);
            m_equalizer.process(reinterpret_cast<float *>(block.pcm.data()), int(block.pcm.size() / m_format.bytesPerFrame()));
            for (const auto &event : block.events) m_events.enqueue({m_generatedFrames + event.offset, event});
            const int count = int(block.pcm.size() / sizeof(float));
            m_generatedFrames += count / m_format.channelCount();
            if (m_deviceFormat.sampleFormat() == QAudioFormat::Float) m_pending = std::move(block.pcm);
            else {
                m_pending.resize(count * m_deviceFormat.bytesPerSample());
                const auto *samples = reinterpret_cast<const float *>(block.pcm.constData());
                for (int i = 0; i < count; ++i) {
                    const auto value = qBound(-1.0f, samples[i], 1.0f);
                    if (m_deviceFormat.sampleFormat() == QAudioFormat::Int16) reinterpret_cast<qint16 *>(m_pending.data())[i] = qint16(value * 32767);
                    else if (m_deviceFormat.sampleFormat() == QAudioFormat::Int32) reinterpret_cast<qint32 *>(m_pending.data())[i] = qint32(double(value) * 2147483647);
                    else reinterpret_cast<quint8 *>(m_pending.data())[i] = quint8((value + 1) * 127.5f);
                }
            }
            if (m_pending.isEmpty()) break;
        }
        const auto written = m_output->write(m_pending.constData(), qMin<qint64>(m_pending.size(), m_sink->bytesFree()));
        if (written <= 0) break;
        m_submittedBytes += written;
        m_pending.remove(0, written);
    }
}

qint64 PlaybackWorker::sleepRemainingMs() const { return m_sleepLength > 0 && m_sleepClock.isValid() ? qMax<qint64>(0, m_sleepLength - m_sleepClock.elapsed()) : 0; }
void PlaybackWorker::cancelSleep() { m_sleep.stop(); m_sleepTick.stop(); m_sleepLength = 0; m_sleepAfterTrack = false; configureMixer(); emit sleepChanged(); }
void PlaybackWorker::startSleepSeconds(int seconds) { cancelSleep(); if (seconds <= 0) return; m_sleepLength = qMin(seconds, 86400) * 1000LL; m_sleepClock.start(); m_sleep.start(int(m_sleepLength)); m_sleepTick.start(); emit sleepChanged(); }
void PlaybackWorker::sleepAtEndOfTrack() { cancelSleep(); m_sleepAfterTrack = true; configureMixer(); m_mixer.setNext({}, {}); emit sleepChanged(); }
