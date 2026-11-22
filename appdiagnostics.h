#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QVariantMap>

class AppDiagnostics : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double memoryMiB READ memoryMiB NOTIFY sampled)
    Q_PROPERTY(double cpuPercent READ cpuPercent NOTIFY sampled)
    Q_PROPERTY(qint64 startupMs READ startupMs NOTIFY sampled)
public:
    explicit AppDiagnostics(QObject *parent = nullptr);
    double memoryMiB() const { return m_memoryMiB; }
    double cpuPercent() const { return m_cpuPercent; }
    qint64 startupMs() const { return m_startupMs; }
    void setStartupMs(qint64 milliseconds);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QVariantMap snapshot() const;
    static qint64 processCpuNs();
    static qint64 residentBytes();
signals:
    void sampled();
private:
    QElapsedTimer m_clock;
    qint64 m_previousCpuNs = 0, m_startupMs = -1;
    double m_memoryMiB = 0, m_cpuPercent = 0;
    qint64 m_sampleWindowMs = 0;
};
