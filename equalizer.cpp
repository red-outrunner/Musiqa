#include "equalizer.h"

#include <QtGlobal>
#include <algorithm>
#include <cmath>

namespace {
constexpr double Pi = 3.14159265358979323846;
constexpr double Lowest = 31.25, Highest = 16000.0;
double interpolate(const QVector<double> &curve, double position)
{
    const int low = qBound(0, int(position), int(curve.size()) - 1);
    const int high = qMin(low + 1, int(curve.size()) - 1);
    return curve[low] + (curve[high] - curve[low]) * (position - low);
}
}

AudioEqualizer::AudioEqualizer(int rate, int channels)
    : m_rate(qMax(8000, rate)), m_channels(qMax(1, channels)), m_bank(makeBank()) {}

double AudioEqualizer::Bank::apply(double input, int channel)
{
    double value = input * gain;
    for (int i = 0; i < count; ++i) {
        const auto &c = filters[size_t(i)]; auto &d = channels[size_t(channel)][size_t(i)];
        const double output = c.b0 * value + d.z1;
        d.z1 = c.b1 * value - c.a1 * output + d.z2;
        d.z2 = c.b2 * value - c.a2 * output;
        if (std::abs(d.z1) < 1e-25) d.z1 = 0;
        if (std::abs(d.z2) < 1e-25) d.z2 = 0;
        value = output;
    }
    return value;
}

QVector<double> AudioEqualizer::frequencies() const
{
    QVector<double> result(m_bandCount);
    for (int i = 0; i < m_bandCount; ++i)
        result[i] = Lowest * std::pow(Highest / Lowest, double(i) / (m_bandCount - 1));
    return result;
}

QVector<double> AudioEqualizer::presetGains(const QString &preset) const
{
    if (preset == QLatin1String("custom")) return m_customGains;
    QVector<double> result(m_bandCount, 0);
    if (preset == QLatin1String("flat")) return result;
    const auto bands = frequencies();
    // Preserve each preset's shape when changing the number of bands.
    QVector<double> curve;
    if (preset == QLatin1String("bass")) curve = {6, 6, 5, 2, -1, 0, 0, 0, 0, 0};
    else if (preset == QLatin1String("vocal")) curve = {-3, -3, -3, -2, -1, 2, 4, 2, 1, 0};
    else curve = {4, 4, 3, -3, -2, 0, 0, 2, 2, 0};
    for (int i = 0; i < m_bandCount; ++i)
        result[i] = interpolate(curve, std::log2(bands[i] / Lowest));
    return result;
}

QVector<double> AudioEqualizer::gains() const { return presetGains(m_preset); }
QVector<double> AudioEqualizer::customGains() const { return m_customGains; }

AudioEqualizer::Bank AudioEqualizer::makeBank() const
{
    Bank bank; bank.channels.resize(size_t(m_channels));
    const auto bandFrequencies = frequencies(), bandGains = gains();
    const double width = std::log2(Highest / Lowest) / (m_bandCount - 1);
    const double ratio = std::pow(2.0, width);
    const double q = std::sqrt(ratio) / (ratio - 1);
    double boost = 0;
    for (int i = 0; i < m_bandCount; ++i) {
        // Skip bands beyond the usable range rather than stacking several
        // ultrasonic filters onto the same Nyquist-adjacent frequency.
        if (bandFrequencies[i] >= m_rate * 0.45 || qFuzzyIsNull(bandGains[i])) continue;
        boost = qMax(boost, bandGains[i]);
        const double a = std::pow(10.0, bandGains[i] / 40);
        const double omega = 2 * Pi * bandFrequencies[i] / m_rate;
        const double cosine = std::cos(omega), alpha = std::sin(omega) / (2 * q);
        const double a0 = 1 + alpha / a;
        bank.filters[size_t(bank.count++)] = {(1 + alpha * a) / a0, -2 * cosine / a0,
            (1 - alpha * a) / a0, -2 * cosine / a0, (1 - alpha / a) / a0};
    }
    bank.gain = std::pow(10.0, -(boost > 0 ? boost + 1 : 0) / 20);
    return bank;
}

void AudioEqualizer::changeCurve()
{
    m_previous = std::move(m_bank); m_bank = makeBank();
    m_previousMix = m_enabled && m_wet > 0 ? 1 : 0;
}

void AudioEqualizer::setEnabled(bool enabled)
{
    if (m_enabled == enabled) return;
    m_enabled = enabled;
}

bool AudioEqualizer::setPreset(const QString &preset)
{
    if (preset != QLatin1String("flat") && preset != QLatin1String("bass")
        && preset != QLatin1String("vocal") && preset != QLatin1String("headphones")
        && preset != QLatin1String("custom")) return false;
    if (m_preset == preset) return true;
    m_preset = preset; changeCurve(); return true;
}

bool AudioEqualizer::setBandCount(int count)
{
    if (count < 5 || count > 15) return false;
    if (count == m_bandCount) return true;
    QVector<double> resampled(count);
    for (int i = 0; i < count; ++i)
        resampled[i] = interpolate(m_customGains, double(i) * (m_bandCount - 1) / (count - 1));
    m_bandCount = count; m_customGains = resampled; changeCurve(); return true;
}

bool AudioEqualizer::setBandGain(int index, double gain)
{
    if (index < 0 || index >= m_bandCount || !std::isfinite(gain) || gain < -12 || gain > 12) return false;
    if (m_preset != QLatin1String("custom")) m_customGains = gains();
    m_customGains[index] = gain; m_preset = QStringLiteral("custom"); changeCurve(); return true;
}

bool AudioEqualizer::setCustomGains(const QVector<double> &curve)
{
    if (curve.size() != m_bandCount) return false;
    for (double gain : curve) if (!std::isfinite(gain) || gain < -12 || gain > 12) return false;
    m_customGains = curve;
    if (m_preset == QLatin1String("custom")) changeCurve();
    return true;
}

void AudioEqualizer::resetCurve()
{
    m_customGains.fill(0); m_preset = QStringLiteral("custom"); changeCurve();
}

void AudioEqualizer::reset()
{
    m_bank = makeBank(); m_previousMix = 0;
    m_wet = m_enabled ? 1 : 0; m_monoMix = m_mono ? 1 : 0;
}

void AudioEqualizer::process(float *samples, int frames)
{
    if (!samples || frames <= 0 || (!m_enabled && m_wet <= 0 && !m_mono && m_monoMix <= 0)) return;
    const double step = 1.0 / (m_rate * 0.02);
    for (int frame = 0; frame < frames; ++frame) {
        m_wet = m_enabled ? qMin(1.0, m_wet + step) : qMax(0.0, m_wet - step);
        m_monoMix = m_mono ? qMin(1.0, m_monoMix + step) : qMax(0.0, m_monoMix - step);
        for (int channel = 0; channel < m_channels; ++channel) {
            auto &sample = samples[frame * m_channels + channel];
            double dry = std::isfinite(sample) ? sample : 0;
            double wet = dry;
            if (m_wet > 0) {
                wet = m_bank.apply(dry, channel);
                if (m_previousMix > 0) wet = wet * (1 - m_previousMix) + m_previous.apply(dry, channel) * m_previousMix;
            }
            const double value = dry * (1 - m_wet) + wet * m_wet;
            sample = float(qBound(-1.0, std::isfinite(value) ? value : 0, 1.0));
        }
        if (m_channels >= 2 && m_monoMix > 0) {
            auto *pair = samples + frame * m_channels;
            const double mono = (double(pair[0]) + pair[1]) * 0.5;
            pair[0] = float(pair[0] + (mono - pair[0]) * m_monoMix);
            pair[1] = float(pair[1] + (mono - pair[1]) * m_monoMix);
        }
        m_previousMix = qMax(0.0, m_previousMix - step);
    }
}
