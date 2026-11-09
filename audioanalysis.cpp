#include "audioanalysis.h"

#include <QAudioBuffer>
#include <QRegularExpression>
#include <QtConcurrentRun>
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numeric>
#include <utility>
#include <vector>

namespace {
constexpr double pi = 3.14159265358979323846;
const std::array<int, 12> majorWheel{8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6, 1};
const std::array<int, 12> minorWheel{5, 12, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10};
void fft(std::vector<std::complex<double>> &a)
{
    const int n = int(a.size());
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (int length = 2; length <= n; length <<= 1) {
        const auto step = std::polar(1.0, -2 * pi / length);
        for (int start = 0; start < n; start += length) {
            std::complex<double> w(1, 0);
            for (int j = 0; j < length / 2; ++j) {
                const auto u = a[start + j], v = a[start + j + length / 2] * w;
                a[start + j] = u + v;
                a[start + j + length / 2] = u - v;
                w *= step;
            }
        }
    }
}
double correlation(const std::array<double, 12> &chroma, const std::array<double, 12> &profile, int shift)
{
    const double xMean = std::accumulate(chroma.begin(), chroma.end(), 0.0) / 12;
    const double yMean = std::accumulate(profile.begin(), profile.end(), 0.0) / 12;
    double xy = 0, xx = 0, yy = 0;
    for (int note = 0; note < 12; ++note) {
        const double x = chroma[(note + shift) % 12] - xMean, y = profile[note] - yMean;
        xy += x * y; xx += x * x; yy += y * y;
    }
    return xy / std::sqrt(std::max(1e-15, xx * yy));
}
}

QString MusicAnalysis::keyName(int key)
{
    static const QStringList names{"C", "D♭", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B"};
    if (key < 0 || key >= 24) return {};
    return names[key % 12] + (key >= 12 ? QStringLiteral(" minor") : QStringLiteral(" major"));
}

QString MusicAnalysis::camelot(int key)
{
    if (key < 0 || key >= 24) return {};
    return QString::number((key >= 12 ? minorWheel : majorWheel)[key % 12]) + (key >= 12 ? QLatin1Char('A') : QLatin1Char('B'));
}

int MusicAnalysis::parseKey(const QString &name)
{
    const auto value = name.simplified().toLower().replace(QChar(0x266d), QLatin1Char('b')).replace(QChar(0x266f), QLatin1Char('#'));
    if (value.isEmpty() || value == QStringLiteral("unknown")) return -1;
    for (int key = 0; key < 24; ++key)
        if (camelot(key).compare(value, Qt::CaseInsensitive) == 0 || keyName(key).toLower().replace(QChar(0x266d), QLatin1Char('b')).replace(QChar(0x266f), QLatin1Char('#')) == value) return key;
    static const QRegularExpression pattern(QStringLiteral("^([a-g])([#b]?)(?:\\s*(major|minor|maj|min|m))?$"));
    const auto match = pattern.match(value);
    if (!match.hasMatch()) return -1;
    const QMap<QString, int> notes{{"c", 0}, {"d", 2}, {"e", 4}, {"f", 5}, {"g", 7}, {"a", 9}, {"b", 11}};
    int pitch = notes.value(match.captured(1));
    if (match.captured(2) == QStringLiteral("#")) ++pitch;
    if (match.captured(2) == QStringLiteral("b")) --pitch;
    const bool minor = QStringList{"minor", "min", "m"}.contains(match.captured(3));
    return (pitch + 12) % 12 + (minor ? 12 : 0);
}

bool MusicAnalysis::relatedKeys(int first, int second)
{
    if (first < 0 || first >= 24 || second < 0 || second >= 24) return false;
    const int a = (first >= 12 ? minorWheel : majorWheel)[first % 12];
    const int b = (second >= 12 ? minorWheel : majorWheel)[second % 12];
    if (a == b) return true;
    return (first >= 12) == (second >= 12) && (std::abs(a - b) == 1 || std::abs(a - b) == 11);
}

double MusicAnalysis::nearestBeat(double seconds, double bpm, double offset)
{
    if (!std::isfinite(bpm) || bpm < 30 || bpm > 300 || !std::isfinite(offset)) return seconds;
    const double beat = 60.0 / bpm;
    return std::max(0.0, offset + std::round((seconds - offset) / beat) * beat);
}

QVariantMap MusicAnalysis::analyze(const QVector<float> &samples, int sampleRate)
{
    QVariantMap result{{"version", Version}, {"bpm", 0.0}, {"keyId", -1}, {"bpmConfidence", 0.0}, {"keyConfidence", 0.0}, {"beatOffset", 0.0}};
    if (sampleRate <= 0 || samples.size() < sampleRate * 3) {
        result["analysisNote"] = QStringLiteral("Not enough audio to estimate tempo and key"); return result;
    }
    constexpr int size = 2048, hop = 256;
    std::vector<std::complex<double>> spectrum(size);
    std::array<double, size / 2> previous{};
    std::array<double, 12> chroma{};
    QVector<double> onsets;
    double energy = 0;
    for (int start = 0; start + size <= samples.size(); start += hop) {
        for (int i = 0; i < size; ++i) spectrum[i] = double(samples[start + i]) * (0.5 - 0.5 * std::cos(2 * pi * i / (size - 1)));
        fft(spectrum);
        double flux = 0;
        for (int bin = 3; bin < size / 2; ++bin) {
            const double magnitude = std::abs(spectrum[bin]);
            flux += std::max(0.0, magnitude - previous[bin]);
            previous[bin] = magnitude;
            const double frequency = double(bin) * sampleRate / size;
            if (frequency < 65 || frequency > 4200) continue;
            const double midi = 69 + 12 * std::log2(frequency / 440.0);
            const int note = int(std::lround(midi));
            const double tuningWeight = std::max(0.0, 1 - std::abs(midi - note) * 2);
            chroma[(note % 12 + 12) % 12] += magnitude * magnitude * tuningWeight / std::sqrt(frequency);
            energy += magnitude * magnitude;
        }
        onsets.append(flux);
    }
    if (energy < 1e-7) { result["analysisNote"] = QStringLiteral("Silence or no reliable musical signal"); return result; }
    // Suppress the slowly varying level so a louder section isn't treated as a beat.
    QVector<double> novelty(onsets.size());
    double mean = 0;
    for (int i = 0; i < onsets.size(); ++i) {
        mean = mean * 0.94 + onsets[i] * 0.06;
        novelty[i] = std::max(0.0, onsets[i] - mean);
    }
    const double rate = double(sampleRate) / hop;
    const int maxLag = std::min(int(rate * 3.2), int(novelty.size() / 2));
    QVector<double> autocorrelation(maxLag + 1);
    for (int lag = 1; lag <= maxLag; ++lag) {
        double xy = 0, xx = 0, yy = 0;
        for (int i = lag; i < novelty.size(); ++i) { xy += novelty[i] * novelty[i - lag]; xx += novelty[i] * novelty[i]; yy += novelty[i - lag] * novelty[i - lag]; }
        autocorrelation[lag] = xy / std::sqrt(std::max(1e-15, xx * yy));
    }
    const auto at = [&autocorrelation](double lag) {
        const int index = int(lag);
        return index >= 1 && index + 1 < autocorrelation.size() ? autocorrelation[index] * (1 - lag + index) + autocorrelation[index + 1] * (lag - index) : 0.0;
    };
    double bestTempo = 0, bestScore = 0;
    for (int tenth = 600; tenth <= 2000; ++tenth) {
        const double bpm = tenth / 10.0, lag = rate * 60 / bpm;
        const double prior = bpm < 75 ? 0.88 : bpm > 170 ? 0.94 : 1.0;
        const double score = (at(lag) + 0.5 * at(2 * lag) + 0.25 * at(3 * lag) - 0.15 * at(lag / 2)) * prior;
        if (score > bestScore) { bestScore = score; bestTempo = bpm; }
    }
    if (bestScore > 0.18) {
        // Refine against a higher resolution energy envelope, including beat phase.
        constexpr int step = 128;
        QVector<double> envelope;
        for (int start = 0; start + step <= samples.size(); start += step) {
            double value = 0;
            for (int i = 1; i < step; ++i) { const double d = samples[start + i] - samples[start + i - 1]; value += d * d; }
            envelope.append(std::sqrt(value));
        }
        double refined = bestTempo, best = -1, bestPhase = 0;
        const double envelopeRate = double(sampleRate) / step;
        for (int fine = -25; fine <= 25; ++fine) {
            const double tempo = bestTempo + fine / 10.0, period = envelopeRate * 60 / tempo;
            for (int phase = 0; phase < std::ceil(period); ++phase) {
                double score = 0; int beats = 0;
                for (double pos = phase; pos + 1 < envelope.size(); pos += period) {
                    const int index = int(pos); const double part = pos - index;
                    score += envelope[index] * (1 - part) + envelope[index + 1] * part; ++beats;
                }
                score /= std::max(1, beats);
                if (score > best) { best = score; refined = tempo; bestPhase = phase; }
            }
        }
        result["bpm"] = std::round(refined * 10) / 10;
        result["beatOffset"] = (bestPhase + 0.5) / envelopeRate;
        result["bpmConfidence"] = std::min(1.0, bestScore / 1.5);
    }
    const std::array<double, 12> major{6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
    const std::array<double, 12> minor{6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};
    double best = -2, runnerUp = -2; int key = -1;
    for (int candidate = 0; candidate < 24; ++candidate) {
        const double score = correlation(chroma, candidate < 12 ? major : minor, candidate % 12);
        if (score > best) { runnerUp = best; best = score; key = candidate; } else runnerUp = std::max(runnerUp, score);
    }
    if (best > 0.5 && best - runnerUp > 0.025) {
        result["keyId"] = key;
        result["keyConfidence"] = std::min(1.0, (best - runnerUp) * 5);
    }
    result["analysisNote"] = QStringLiteral("Estimated locally from the first %1 seconds; tempo may be half/double time").arg(samples.size() / sampleRate);
    return result;
}

AudioAnalyzer::AudioAnalyzer(QObject *parent) : QObject(parent)
{
    QAudioFormat format; format.setSampleRate(11025); format.setChannelCount(1); format.setSampleFormat(QAudioFormat::Float);
    m_decoder.setAudioFormat(format);
    m_timeout.setSingleShot(true); m_timeout.setInterval(45000);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        m_finishing = true;
        m_decoder.stop(); m_samples.clear();
        if (!m_cancelled) emit completed(m_url, {{"version", MusicAnalysis::Version}, {"analysisNote", QStringLiteral("Audio analysis timed out")}});
    });
    connect(&m_decoder, &QAudioDecoder::bufferReady, this, [this] {
        if (m_finishing || m_cancelled) return;
        const auto buffer = m_decoder.read();
        if (buffer.format().sampleFormat() != QAudioFormat::Float || buffer.format().channelCount() != 1 || buffer.format().sampleRate() != 11025) {
            m_finishing = true;
            m_decoder.stop(); m_timeout.stop();
            emit completed(m_url, {{"version", MusicAnalysis::Version}, {"analysisNote", QStringLiteral("Decoder did not provide the requested analysis format")}}); return;
        }
        const int count = std::min(int(buffer.sampleCount()), 11025 * 180 - int(m_samples.size()));
        const float *data = buffer.constData<float>();
        const auto old = m_samples.size(); m_samples.resize(old + count);
        std::copy(data, data + count, m_samples.begin() + old);
        if (m_samples.size() >= 11025 * 180) finishDecode();
    });
    connect(&m_decoder, &QAudioDecoder::finished, this, &AudioAnalyzer::finishDecode);
    connect(&m_decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), this, [this](QAudioDecoder::Error error) {
        if (error == QAudioDecoder::NoError || m_cancelled || m_finishing) return;
        m_finishing = true;
        m_timeout.stop();
        emit completed(m_url, {{"version", MusicAnalysis::Version}, {"analysisNote", m_decoder.errorString()}});
    });
    connect(&m_worker, &QFutureWatcher<QVariantMap>::finished, this, [this] {
        if (!m_cancelled) emit completed(m_url, m_worker.result());
    });
}

void AudioAnalyzer::start(const QUrl &url)
{
    m_cancelled = false; m_finishing = false; m_samples.clear(); m_url = url;
    m_decoder.setSource(url); m_timeout.start(); m_decoder.start();
}

void AudioAnalyzer::cancel()
{
    m_cancelled = true; m_decoder.stop(); m_timeout.stop();
}

void AudioAnalyzer::finishDecode()
{
    if (m_finishing || m_cancelled) return;
    m_finishing = true; m_timeout.stop(); m_decoder.stop();
    const auto samples = std::exchange(m_samples, {});
    m_worker.setFuture(QtConcurrent::run([samples] { return MusicAnalysis::analyze(samples, 11025); }));
}
