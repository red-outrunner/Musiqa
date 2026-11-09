#pragma once

#include <QDBusConnection>
#include <QDBusVirtualObject>
#include "playbackengine.h"
#include <QPointer>
#include <QVariantMap>

class TrackListModel;

class MprisService : public QDBusVirtualObject
{
    Q_OBJECT
public:
    MprisService(QObject *window, TrackListModel *library, PlaybackEngine *player,
        const QDBusConnection &connection = QDBusConnection::sessionBus(),
        const QString &service = QStringLiteral("org.mpris.MediaPlayer2.Musica"));
    ~MprisService() override;
    QString introspect(const QString &path) const override;
    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override;
    bool available() const { return m_registered; }
    QString serviceName() const { return m_service; }
    QVariantMap playerProperties() const;

public slots:
    void refresh();
    void notifySeeked(double position);

private:
    void command(const QString &command, const QVariant &value = {});
    QVariantMap rootProperties() const;
    QPointer<QObject> m_window;
    TrackListModel *m_library;
    PlaybackEngine *m_player;
    QDBusConnection m_connection;
    QString m_service;
    QVariantMap m_lastProperties;
    bool m_registered = false;
};
