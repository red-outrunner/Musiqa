#pragma once

#include <QString>
#include <QVector>
#include <array>
#include <vector>

// Small, allocation-free render path. The worker owns this object so filtering
// never competes with QML and library work on the GUI thread.
class AudioEqualizer
{
public:
    explicit AudioEqualizer(int sampleRate = 48000, int channels = 2);
    void setEnabled(bool enabled);
    bool enabled() const { return m_enabled; }
    bool setPreset(const QString &preset);
    QString preset() const { return m_preset; }
    int bandCount() const { return m_bandCount; }
    QVector<double> gains() const;
    QVector<double> customGains() const;
    QVector<double> frequencies() const;
    bool setBandCount(int count);
    bool setBandGain(int index, double gain);
    bool setCustomGains(const QVector<double> &gains);
    void resetCurve();
    void setMono(bool mono) { m_mono = mono; }
    bool mono() const { return m_mono; }
    void reset();
    void process(float *samples, int frames);
private:
    struct Coefficients { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
    struct Delay { double z1 = 0, z2 = 0; };
    struct Bank {
        std::array<Coefficients, 15> filters;
        std::vector<std::array<Delay, 15>> channels;
        int count = 0;
        double gain = 1;
        double apply(double input, int channel);
    };
    Bank makeBank() const;
    QVector<double> presetGains(const QString &preset) const;
    void changeCurve();
    int m_rate, m_channels;
    bool m_enabled = false;
    QString m_preset = QStringLiteral("bass");
    int m_bandCount = 10;
    QVector<double> m_customGains = QVector<double>(10, 0);
    bool m_mono = false;
    Bank m_bank, m_previous;
    double m_wet = 0, m_previousMix = 0, m_monoMix = 0;
};
