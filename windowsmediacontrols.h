#pragma once
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>
#include <memory>

class PlaybackEngine;
class TrackListModel;

class WindowsMediaControls : public QObject
{
    Q_OBJECT
public:
    WindowsMediaControls(QObject *window, TrackListModel *library, PlaybackEngine *player);
    ~WindowsMediaControls() override;
    bool available() const { return m_available; }
    QVariantMap playerProperties() const;
public slots:
    void refresh();
    void updateTimeline();
    void command(const QString &name, const QVariant &value = {});
signals:
    void commandRequested(const QString &name, const QVariant &value);
private:
    struct Native;
    std::unique_ptr<Native> m_native;
    QPointer<QObject> m_window;
    TrackListModel *m_library;
    PlaybackEngine *m_player;
    QTimer m_timeline;
    bool m_available = false;
};
