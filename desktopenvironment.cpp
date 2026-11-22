#include "desktopenvironment.h"
#include "displaysettings.h"
#include "playbackengine.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#if MUSICA_HAS_DBUS
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#endif

DesktopEnvironment::DesktopEnvironment(QObject *window, DisplaySettings *preferences, PlaybackEngine *player)
    : QObject(window), m_window(window), m_preferences(preferences), m_player(player), m_recovery(this)
{
    m_recovery.setSingleShot(true); m_recovery.setInterval(750);
    connect(&m_recovery, &QTimer::timeout, this, [this] {
        if (!m_player || !m_preferences || m_player->source() != m_source || m_player->entryId() != m_entry) return;
        m_player->recoverAudioOutput();
        if (m_player->playbackState() == QMediaPlayer::PausedState
            && canResume(m_wasPlaying, m_preferences->resumeAfterSleep(), m_source, m_player->source(),
                m_entry, m_player->entryId(), m_device, m_player->activeOutputDeviceKey())) m_player->play();
        else if (m_wasPlaying && m_preferences->resumeAfterSleep() && !m_device.isEmpty()
            && m_player->playbackState() == QMediaPlayer::PausedState && ++m_recoveryAttempts < 5) {
            m_recovery.start(); return; // Bluetooth outputs may reappear after the wake notification.
        }
        m_wasPlaying = false;
    });
    if (auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        app->installNativeEventFilter(this);
        connect(app, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
            if (state == Qt::ApplicationActive) refreshPowerSource();
        });
    }
#if MUSICA_HAS_DBUS
    auto bus = QDBusConnection::systemBus();
    bus.connect("org.freedesktop.login1", "/org/freedesktop/login1", "org.freedesktop.login1.Manager",
        "PrepareForSleep", this, SLOT(prepareForSleep(bool)));
    bus.connect("org.freedesktop.UPower", "/org/freedesktop/UPower", "org.freedesktop.DBus.Properties",
        "PropertiesChanged", this, SLOT(powerPropertiesChanged(QString,QVariantMap,QStringList)));
#endif
    refreshPowerSource();
}
DesktopEnvironment::~DesktopEnvironment()
{
    if (QCoreApplication::instance()) QCoreApplication::instance()->removeNativeEventFilter(this);
}
bool DesktopEnvironment::canResume(bool wasPlaying, bool enabled, const QUrl &before, const QUrl &after,
    const QString &entryBefore, const QString &entryAfter, const QString &deviceBefore, const QString &deviceAfter)
{
    return wasPlaying && enabled && !before.isEmpty() && before == after && entryBefore == entryAfter
        && !deviceBefore.isEmpty() && deviceBefore == deviceAfter;
}
void DesktopEnvironment::prepareForSleep(bool sleeping)
{
    if (!m_player || !m_preferences) return;
    if (sleeping) {
        if (m_sleeping) return;
        m_recovery.stop(); m_sleeping = true;
        m_wasPlaying = m_player->playbackState() == QMediaPlayer::PlayingState;
        m_source = m_player->source(); m_entry = m_player->entryId(); m_device = m_player->activeOutputDeviceKey();
        m_sleepStarted = QDateTime::currentMSecsSinceEpoch(); m_sleepTimerRemaining = m_player->sleepRemainingMs();
        m_player->checkpoint(); m_player->pause();
        if (m_window && m_window->metaObject()->indexOfMethod("persistSession()") >= 0)
            QMetaObject::invokeMethod(m_window, "persistSession");
        emit sleepingChanged();
    } else {
        refreshPowerSource();
        if (!m_sleeping) return;
        m_sleeping = false; emit sleepingChanged();
        if (m_sleepTimerRemaining > 0) {
            const auto remaining = m_sleepTimerRemaining - qMax<qint64>(0, QDateTime::currentMSecsSinceEpoch() - m_sleepStarted);
            if (remaining <= 0) { m_player->cancelSleep(); m_wasPlaying = false; }
            else m_player->startSleepSeconds(int((remaining + 999) / 1000));
        }
        m_recoveryAttempts = 0;
        if (!m_source.isEmpty()) m_recovery.start();
    }
}
void DesktopEnvironment::setPowerSource(bool battery)
{
    if (m_preferences) m_preferences->setOnBattery(battery);
    if (!m_powerKnown) { m_powerKnown = true; emit monitoringChanged(); }
}
void DesktopEnvironment::refreshPowerSource()
{
#ifdef Q_OS_WIN
    SYSTEM_POWER_STATUS status{};
    if (GetSystemPowerStatus(&status) && status.ACLineStatus != 255) setPowerSource(status.ACLineStatus == 0);
#else
    // Sysfs supplies an immediate fallback when UPower isn't installed.
    const QDir supplies("/sys/class/power_supply");
    for (const auto &name : supplies.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QFile status(supplies.filePath(name + "/status"));
        if (status.open(QIODevice::ReadOnly) && status.readAll().trimmed() == "Discharging") { setPowerSource(true); break; }
        QFile online(supplies.filePath(name + "/online"));
        if (online.open(QIODevice::ReadOnly) && online.readAll().trimmed() == "1") { setPowerSource(false); break; }
    }
#endif
#if MUSICA_HAS_DBUS
    auto message = QDBusMessage::createMethodCall("org.freedesktop.UPower", "/org/freedesktop/UPower",
        "org.freedesktop.DBus.Properties", "Get");
    message << "org.freedesktop.UPower" << "OnBattery";
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message, 2000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        const QDBusPendingReply<QDBusVariant> reply = *watcher;
        if (!reply.isError() && reply.value().variant().metaType().id() == QMetaType::Bool)
            setPowerSource(reply.value().variant().toBool());
        watcher->deleteLater();
    });
#endif
}
void DesktopEnvironment::powerPropertiesChanged(const QString &interface, const QVariantMap &values, const QStringList &invalidated)
{
    if (interface != "org.freedesktop.UPower") return;
    if (values.value("OnBattery").metaType().id() == QMetaType::Bool) setPowerSource(values.value("OnBattery").toBool());
    else if (invalidated.contains("OnBattery")) refreshPowerSource();
}
bool DesktopEnvironment::nativeEventFilter(const QByteArray &, void *message, qintptr *)
{
#ifdef Q_OS_WIN
    const auto *event = static_cast<MSG *>(message);
    if (event->message == WM_POWERBROADCAST) {
        if (event->wParam == PBT_APMSUSPEND) prepareForSleep(true);
        else if (event->wParam == PBT_APMRESUMEAUTOMATIC || event->wParam == PBT_APMRESUMESUSPEND) prepareForSleep(false);
        else if (event->wParam == PBT_APMPOWERSTATUSCHANGE) refreshPowerSource();
    }
#else
    Q_UNUSED(message);
#endif
    return false;
}
