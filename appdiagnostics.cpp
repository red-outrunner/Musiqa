#include "appdiagnostics.h"
#include <QCoreApplication>
#include <QFile>
#include <QSysInfo>
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

AppDiagnostics::AppDiagnostics(QObject *parent) : QObject(parent)
{
    m_clock.start(); m_previousCpuNs = processCpuNs();
}
void AppDiagnostics::setStartupMs(qint64 value)
{
    if (m_startupMs >= 0 || value < 0) return;
    m_startupMs = value; emit sampled();
}
qint64 AppDiagnostics::processCpuNs()
{
#ifdef Q_OS_WIN
    FILETIME created{}, ended{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &ended, &kernel, &user)) return 0;
    ULARGE_INTEGER k{}, u{};
    k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime; u.HighPart = user.dwHighDateTime;
    return qint64(k.QuadPart + u.QuadPart) * 100;
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
    return (qint64(usage.ru_utime.tv_sec) + usage.ru_stime.tv_sec) * 1000000000LL
        + (qint64(usage.ru_utime.tv_usec) + usage.ru_stime.tv_usec) * 1000;
#endif
}
qint64 AppDiagnostics::residentBytes()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS counters{}; counters.cb = sizeof(counters);
    return GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) ? qint64(counters.WorkingSetSize) : 0;
#elif defined(Q_OS_LINUX)
    QFile status("/proc/self/status");
    if (status.open(QIODevice::ReadOnly)) for (const auto &line : status.readAll().split('\n')) {
        if (line.startsWith("VmRSS:")) return line.mid(6).simplified().split(' ').value(0).toLongLong() * 1024;
    }
    return 0;
#else
    return 0;
#endif
}
void AppDiagnostics::refresh()
{
    const auto elapsed = m_clock.nsecsElapsed(), cpu = processCpuNs();
    m_sampleWindowMs = elapsed / 1000000;
    if (elapsed > 0) m_cpuPercent = qMax(0.0, double(cpu - m_previousCpuNs) * 100 / elapsed);
    m_previousCpuNs = cpu; m_clock.restart();
    m_memoryMiB = residentBytes() / 1048576.0; emit sampled();
}
QVariantMap AppDiagnostics::snapshot() const
{
    return {{"version", QCoreApplication::applicationVersion()}, {"os", QSysInfo::prettyProductName()},
        {"architecture", QSysInfo::currentCpuArchitecture()}, {"startupMs", m_startupMs},
        {"residentMiB", m_memoryMiB}, {"cpuPercentOneCore", m_cpuPercent}, {"cpuSampleMs", m_sampleWindowMs}};
}
