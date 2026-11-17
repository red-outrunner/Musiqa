#pragma once
#include <QFutureWatcher>
#include <QObject>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantMap>
#include <QQuickItem>
#include <QQuickItemGrabResult>

class Sharing : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool emailAttachmentsAvailable READ emailAttachmentsAvailable CONSTANT)
public:
    explicit Sharing(QObject *parent = nullptr);
    ~Sharing() override;
    bool busy() const { return m_busy; }
    bool emailAttachmentsAvailable() const { return MUSICA_HAS_DBUS; }
    Q_INVOKABLE void prepareSong(const QVariantMap &track);
    Q_INVOKABLE void captureNowPlaying(QQuickItem *item);
    Q_INVOKABLE bool copyToClipboard();
    Q_INVOKABLE bool exportTo(const QUrl &destination);
    Q_INVOKABLE void composeEmail();
signals:
    void ready(const QVariantMap &details);
    void busyChanged();
    void error(const QString &message);
    void exported(const QUrl &file);
    void copied();
private slots:
    void portalResponse(uint response, const QVariantMap &results);
private:
    void publish(const QString &path, bool image, const QString &title);
    QTemporaryDir m_directory;
    QString m_path, m_title, m_requestPath;
    bool m_image = false, m_busy = false;
    QSharedPointer<QQuickItemGrabResult> m_grab;
    QFutureWatcher<QVariantMap> m_captureWriter, m_exportWriter;
};
