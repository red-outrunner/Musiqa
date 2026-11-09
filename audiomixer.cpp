#include "audiomixer.h"

#include <QAudioBuffer>
#include <QDir>
#include <QUuid>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
#include <cstring>

PcmTrack::PcmTrack(const QUrl &source, const QAudioFormat &pcmFormat)
    : url(source), format(pcmFormat), m_pcm(QDir::tempPath() + QStringLiteral("/musica-audio-XXXXXX.pcm"))
{
    if (!m_pcm.open()) error = tr("Couldn't create a temporary audio buffer: %1").arg(m_pcm.errorString());
}

PcmTrack::~PcmTrack()
{
    if (m_decoder) m_decoder->stop();
    if (m_process && m_process->state() != QProcess::NotRunning) { m_process->kill(); m_process->waitForFinished(1000); }
}

void PcmTrack::setInfo(const QVariantMap &info)
{
    if (info.value("duration").toLongLong() > 0) m_estimatedFrames = info.value("duration").toLongLong() * format.sampleRate() / 1000;
}

bool PcmTrack::append(const char *data, qint64 bytes)
{
    if (!error.isEmpty()) return false;
    if (!m_pcm.seek(m_frames * format.bytesPerFrame()) || m_pcm.write(data, bytes) != bytes || !m_pcm.flush()) {
        error = tr("Couldn't buffer audio: %1").arg(m_pcm.errorString()); return false;
    }
    m_frames += bytes / format.bytesPerFrame();
    return true;
}

qint64 PcmTrack::durationFrames() const { return complete ? m_frames : qMax(m_frames, m_estimatedFrames); }

QByteArray PcmTrack::readFrames(qint64 position, int count)
{
    if (position < 0 || position >= m_frames || !m_pcm.seek(position * format.bytesPerFrame())) return {};
    return m_pcm.read(qMin<qint64>(count, m_frames - position) * format.bytesPerFrame());
}

void PcmTrack::decode()
{
    if (!error.isEmpty()) { emit ready(); return; }
    const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (!ffmpeg.isEmpty() && url.isLocalFile()) {
        m_process = std::make_unique<QProcess>();
        m_process->setProgram(ffmpeg);
        m_process->setArguments({"-nostdin", "-v", "error", "-threads", "1", "-i", url.toLocalFile(), "-vn", "-ac", QString::number(format.channelCount()),
            "-ar", QString::number(format.sampleRate()), "-f", QSysInfo::ByteOrder == QSysInfo::LittleEndian ? "f32le" : "f32be", "pipe:1"});
        connect(m_process.get(), &QProcess::readyReadStandardError, this, [this] { m_decodeError = (m_decodeError + m_process->readAllStandardError()).right(2000); });
        connect(m_process.get(), &QProcess::readyReadStandardOutput, this, [this] {
            m_decodeTail.append(m_process->readAllStandardOutput());
            const auto bytes = m_decodeTail.size() / format.bytesPerFrame() * format.bytesPerFrame();
            if (bytes > 0) {
                if (!append(m_decodeTail.constData(), bytes)) m_process->kill();
                m_decodeTail.remove(0, bytes);
                emit ready();
            }
        });
        connect(m_process.get(), &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
            if ((code != 0 || status != QProcess::NormalExit) && error.isEmpty()) error = tr("Couldn't decode audio: %1").arg(QString::fromUtf8(m_decodeError).left(500));
            complete = error.isEmpty(); emit ready();
        });
        connect(m_process.get(), &QProcess::errorOccurred, this, [this](QProcess::ProcessError code) {
            if (code == QProcess::FailedToStart) { error = m_process->errorString(); emit ready(); }
        });
        m_process->start();
        return;
    }
    m_decoder = std::make_unique<QAudioDecoder>();
    m_decoder->setAudioFormat(format);
    connect(m_decoder.get(), &QAudioDecoder::bufferReady, this, [this] {
        const auto buffer = m_decoder->read();
        if (buffer.format() != format) error = tr("The audio decoder couldn't convert this file to the playback format.");
        if (!error.isEmpty() || !append(buffer.constData<char>(), buffer.byteCount())) { m_decoder->stop(); emit ready(); return; }
        emit ready();
    });
    connect(m_decoder.get(), &QAudioDecoder::durationChanged, this, [this](qint64 duration) {
        m_estimatedFrames = duration * format.sampleRate() / 1000; emit ready();
    });
    connect(m_decoder.get(), &QAudioDecoder::finished, this, [this] { complete = true; emit ready(); });
    connect(m_decoder.get(), qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), this, [this](QAudioDecoder::Error code) {
        if (code != QAudioDecoder::NoError) { error = m_decoder->errorString(); emit ready(); }
    });
    m_decoder->setSource(url);
    m_decoder->start();
}

AudioMixer::AudioMixer(int rate, int channels) : m_rate(rate), m_channels(channels), m_tokenPrefix(QUuid::createUuid().toString(QUuid::WithoutBraces)) {}

AudioMixer::Voice AudioMixer::newVoice(const std::shared_ptr<PcmTrack> &track, const QString &id, qint64 frame)
{
    return {track, id, m_tokenPrefix + QLatin1Char(':') + id + QLatin1Char(':') + QString::number(++m_serial), frame};
}

void AudioMixer::clear() { m_current = {}; m_next = {}; m_outgoing = {}; m_startPending = false; m_ended = false; }
void AudioMixer::setCurrent(const std::shared_ptr<PcmTrack> &track, const QString &id, qint64 frame, const QString &token)
{
    m_current = track ? newVoice(track, id, frame) : Voice{};
    if (!token.isEmpty()) m_current.token = token;
    m_outgoing = {}; m_startPending = bool(track); m_ended = false;
}
void AudioMixer::setNext(const std::shared_ptr<PcmTrack> &track, const QString &id)
{
    if (m_next.track == track && m_next.entryId == id) return;
    m_next = track ? newVoice(track, id) : Voice{};
}
void AudioMixer::seek(qint64 frame) { m_current.frame = qMax<qint64>(0, frame); m_outgoing = {}; m_ended = false; }

MixerEvent AudioMixer::event(MixerEvent::Kind kind, const Voice &voice, qint64 offset) const
{
    return {kind, offset, voice.entryId, voice.token, voice.track ? voice.track->url : QUrl(), voice.frame,
        voice.track ? voice.track->durationFrames() : 0, voice.track};
}

QPair<qint64, qint64> AudioMixer::transitionPlan(const PcmTrack &from, const PcmTrack &to, double seconds)
{
    const auto requested = qRound64(qBound(0.0, seconds, 70.0) * from.format.sampleRate());
    // Leave at least one frame in the incoming track after the overlap.
    const auto length = qMin(requested, qMin(from.durationFrames(), qMax<qint64>(0, to.durationFrames() - 1)));
    return {from.durationFrames() - length, 0};
}

MixerBlock AudioMixer::render(int requested)
{
    MixerBlock block;
    if (!m_current.track || m_ended || requested <= 0) return block;
    block.pcm.resize(requested * m_channels * int(sizeof(float)));
    auto *output = reinterpret_cast<float *>(block.pcm.data());
    int rendered = 0;
    if (m_startPending) { block.events.append(event(MixerEvent::Started, m_current, 0)); m_startPending = false; }
    while (rendered < requested && m_current.track) {
        if (!m_outgoing.track && !stopAfterCurrent && crossfadeSeconds > 0 && m_next.track && m_current.track->complete && m_next.track->complete) {
            const auto plan = transitionPlan(*m_current.track, *m_next.track, crossfadeSeconds);
            if (m_current.frame >= plan.first && m_current.frame < m_current.track->frames()) {
                m_outgoing = m_current; m_current = m_next; m_next = {};
                m_current.frame = plan.second;
                m_fadeLength = m_outgoing.track->frames() - m_outgoing.frame; m_fadeFrame = 0;
                block.events.append(event(MixerEvent::Started, m_current, rendered));
            }
        }
        if (m_current.frame >= m_current.track->frames()) {
            if (!m_current.track->complete) { block.waiting = true; break; }
            if (!stopAfterCurrent && gapless && m_next.track && m_next.track->frames() == 0 && m_next.track->error.isEmpty()) { block.waiting = true; break; }
            block.events.append(event(MixerEvent::Finished, m_current, rendered));
            if (!stopAfterCurrent && gapless && m_next.track && m_next.track->frames() > 0 && m_next.track->error.isEmpty()) {
                m_current = m_next; m_next = {}; m_outgoing = {};
                block.events.append(event(MixerEvent::Started, m_current, rendered));
                continue;
            }
            block.events.append(event(MixerEvent::QueueEnded, m_current, rendered)); m_ended = true; break;
        }
        int count = int(qMin<qint64>(requested - rendered, m_current.track->frames() - m_current.frame));
        if (m_outgoing.track) count = int(qMin<qint64>(count, m_outgoing.track->frames() - m_outgoing.frame));
        else if (!stopAfterCurrent && crossfadeSeconds > 0 && m_next.track && m_current.track->complete && m_next.track->complete) {
            const auto plan = transitionPlan(*m_current.track, *m_next.track, crossfadeSeconds);
            if (plan.first > m_current.frame) count = int(qMin<qint64>(count, plan.first - m_current.frame));
        }
        if (count <= 0) { block.waiting = true; break; }
        const auto current = m_current.track->readFrames(m_current.frame, count);
        if (current.size() != count * m_channels * int(sizeof(float))) { block.waiting = true; break; }
        const auto *a = reinterpret_cast<const float *>(current.constData());
        if (m_outgoing.track) {
            const auto outgoing = m_outgoing.track->readFrames(m_outgoing.frame, count);
            if (outgoing.size() != current.size()) { block.waiting = true; break; }
            const auto *b = reinterpret_cast<const float *>(outgoing.constData());
            for (int frame = 0; frame < count; ++frame) {
                const double phase = double(m_fadeFrame + frame) / qMax<qint64>(1, m_fadeLength) * 1.5707963267948966;
                const float in = float(std::sin(phase)), out = float(std::cos(phase));
                for (int channel = 0; channel < m_channels; ++channel) {
                    const float value = a[frame * m_channels + channel] * in + b[frame * m_channels + channel] * out;
                    const float limited = std::abs(value) > 0.95f ? std::copysign(0.95f + 0.05f * std::tanh((std::abs(value) - 0.95f) / 0.05f), value) : value;
                    output[(rendered + frame) * m_channels + channel] = limited;
                }
            }
            m_outgoing.frame += count; m_fadeFrame += count;
            block.events.append(event(MixerEvent::Progress, m_outgoing, rendered + count));
            if (m_outgoing.frame >= m_outgoing.track->frames()) { block.events.append(event(MixerEvent::Finished, m_outgoing, rendered + count)); m_outgoing = {}; }
        } else std::memcpy(output + rendered * m_channels, a, size_t(current.size()));
        rendered += count; m_current.frame += count;
        block.events.append(event(MixerEvent::Progress, m_current, rendered));
    }
    block.pcm.resize(rendered * m_channels * int(sizeof(float)));
    return block;
}
