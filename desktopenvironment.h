#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

class DisplaySettings;
class PlaybackEngine;

class DesktopEnvironment : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
    Q_PROPERTY(bool powerMonitoringAvailable READ powerMonitoringAvailable NOTIFY monitoringChanged)
    Q_PROPERTY(bool sleeping READ sleeping NOTIFY sleepingChanged)
public:
    DesktopEnvironment(QObject *window, DisplaySettings *preferences, PlaybackEngine *player);
    ~DesktopEnvironment() override;
    bool powerMonitoringAvailable() const { return m_powerKnown; }
    bool sleeping() const { return m_sleeping; }
    static bool canResume(bool wasPlaying, bool enabled, const QUrl &before, const QUrl &after,
        const QString &entryBefore, const QString &entryAfter, const QString &deviceBefore, const QString &deviceAfter);
public slots:
    void prepareForSleep(bool sleeping);
    void refreshPowerSource();
    void powerPropertiesChanged(const QString &interface, const QVariantMap &values, const QStringList &invalidated);
signals:
    void monitoringChanged();
    void sleepingChanged();
protected:
    bool nativeEventFilter(const QByteArray &type, void *message, qintptr *result) override;
private:
    void setPowerSource(bool battery);
    QPointer<QObject> m_window;
    QPointer<DisplaySettings> m_preferences;
    QPointer<PlaybackEngine> m_player;
    QTimer m_recovery;
    QUrl m_source;
    QString m_entry, m_device;
    qint64 m_sleepStarted = 0, m_sleepTimerRemaining = 0;
    bool m_powerKnown = false, m_sleeping = false, m_wasPlaying = false;
    int m_recoveryAttempts = 0;
};
