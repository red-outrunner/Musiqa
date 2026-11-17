#pragma once

#include <QHash>
#include <QLockFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QQueue>
#include <QUrl>
#include <memory>

// One local, per-user endpoint accepts Open With requests. Requests received
// during QML/library startup stay queued until the window connects its slots.
class ApplicationRequests : public QObject
{
    Q_OBJECT
public:
    enum StartResult { Listening, Forwarded, Failed };
    Q_ENUM(StartResult)
    explicit ApplicationRequests(QObject *parent = nullptr, const QString &serverName = {});
    ~ApplicationRequests() override;
    StartResult startOrForward(const QList<QUrl> &urls = {});
    void setReady(bool ready = true);
    QString errorString() const { return m_error; }
    QString serverName() const { return m_name; }
    static QString defaultServerName();
    static QList<QUrl> urlsFromArguments(const QStringList &arguments, const QString &workingDirectory = {});
signals:
    void urlsRequested(const QList<QUrl> &urls);
    void activateRequested();
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    enum ForwardResult { NoListener, Accepted, Rejected };
    static QByteArray requestBytes(const QList<QUrl> &urls);
    static bool validUrls(const QList<QUrl> &urls);
    ForwardResult forward(const QByteArray &request);
    bool enqueue(const QList<QUrl> &urls);
    void scheduleFlush();
    void flush();
    void acceptConnections();
    void readRequest(QLocalSocket *socket);
    void reply(QLocalSocket *socket, bool success, const QString &error = {});
    QLocalServer m_server;
    QHash<QLocalSocket *, QByteArray> m_clients;
    QQueue<QList<QUrl>> m_pending;
    QList<QUrl> m_fileEvents;
    QString m_name, m_error;
#ifdef Q_OS_WIN
    std::unique_ptr<QLockFile> m_instanceLock;
#endif
    bool m_ready = false, m_flushScheduled = false;
    bool m_fileEventScheduled = false;
};
