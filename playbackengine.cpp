#include "playbackengine.h"
#include "playbackworker.h"
#include <QCoreApplication>
#include <QEvent>
#include <QScopedValueRollback>

PlaybackEngine::PlaybackEngine(QObject *parent) : QObject(parent), m_controls(this)
{
    qRegisterMetaType<PlaybackSnapshot>();
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
    const double seconds = m_settings.value("playback/fadeSeconds", 5).toDouble();
    m_settings.remove("playback/quantize");
    dispatch([this, gapless, crossfade, seconds] {
        m_worker->setGaplessEnabled(gapless);
        m_worker->setCrossfadeEnabled(crossfade);
        m_worker->setCrossfadeSeconds(seconds);
    });
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
    if (old.gapless != state.gapless || old.crossfade != state.crossfade
        || old.crossfadeSeconds != state.crossfadeSeconds) emit optionsChanged();
    if (old.crossfading != state.crossfading) emit transitionChanged();
    if (old.sleepRevision != state.sleepRevision) emit sleepChanged();
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
void PlaybackEngine::setEntryId(const QString &id) { dispatch([this, id] { m_worker->setEntryId(id); }); }
void PlaybackEngine::setSource(const QUrl &url) { dispatch([this, url] { m_worker->setSource(url); }); }
void PlaybackEngine::setPosition(qint64 position) { dispatch([this, position] { m_worker->setPosition(position); }); }
void PlaybackEngine::playEntry(const QString &id, const QUrl &url, const QVariantMap &info)
{
    dispatch([this, id, url, info] { m_worker->playEntry(id, url, info); });
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
