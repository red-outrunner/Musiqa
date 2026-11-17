#include "applicationrequests.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QThread>
#include <utility>

namespace {
constexpr qsizetype MaximumRequestBytes = 1024 * 1024;
constexpr qsizetype MaximumUrls = 2048;
constexpr qsizetype MaximumQueuedRequests = 32;
constexpr qsizetype MaximumConnections = 16;
}

ApplicationRequests::ApplicationRequests(QObject *parent, const QString &serverName)
    : QObject(parent), m_server(this), m_name(serverName.isEmpty() ? defaultServerName() : serverName)
{
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    m_server.setMaxPendingConnections(int(MaximumConnections));
#ifdef Q_OS_WIN
    // Named pipes permit several servers with the same name. Hold a separate
    // per-user lock so simultaneous Open With launches still have one owner.
    const auto lockId = QString::fromLatin1(QCryptographicHash::hash(m_name.toUtf8(), QCryptographicHash::Sha256).toHex());
    m_instanceLock = std::make_unique<QLockFile>(QDir(QDir::tempPath()).filePath(QStringLiteral("musica-instance-") + lockId + QStringLiteral(".lock")));
    m_instanceLock->setStaleLockTime(0);
#endif
    connect(&m_server, &QLocalServer::newConnection, this, &ApplicationRequests::acceptConnections);
    if (QCoreApplication::instance()) QCoreApplication::instance()->installEventFilter(this);
}

ApplicationRequests::~ApplicationRequests()
{
    if (QCoreApplication::instance()) QCoreApplication::instance()->removeEventFilter(this);
    for (auto *socket : m_server.findChildren<QLocalSocket *>()) {
        disconnect(socket, nullptr, this, nullptr); socket->abort();
    }
    m_clients.clear();
    m_server.close();
}

QString ApplicationRequests::defaultServerName()
{
    const auto settingsFile = QSettings().fileName();
#ifdef Q_OS_WIN
    // Native QSettings names are registry keys on Windows. Resolving one as
    // a relative filesystem path makes the IPC identity change with the
    // launcher's working directory. Include the user home as well: HKCU key
    // names are identical for different signed-in users, but pipes are not.
    const auto settingsPath = QDir::homePath() + QLatin1Char('\n') + settingsFile;
#else
    const auto settingsPath = QFileInfo(settingsFile).absoluteFilePath();
#endif
    const auto hash = QString::fromLatin1(QCryptographicHash::hash(settingsPath.toUtf8(), QCryptographicHash::Sha256).toHex().left(24));
    const auto name = QStringLiteral("musica-open-") + hash;
#ifdef Q_OS_WIN
    // Windows local sockets are named pipes, not paths under a runtime dir.
    // A simple name also avoids drive-letter/backslash ambiguity.
    return name;
#else
    const auto runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    // Unix sockets have a short path limit. QLocalServer's name-only fallback
    // still uses the user-specific hash and owner-only socket permissions.
    return !runtime.isEmpty() && runtime.size() < 60 ? QDir(runtime).filePath(name) : name;
#endif
}

QList<QUrl> ApplicationRequests::urlsFromArguments(const QStringList &arguments, const QString &workingDirectory)
{
    const QDir directory(workingDirectory.isEmpty() ? QDir::currentPath() : workingDirectory);
    QList<QUrl> urls;
    for (const auto &argument : arguments) {
        if (argument.isEmpty()) continue;
        QUrl url;
        if (argument.startsWith(QLatin1String("file:"), Qt::CaseInsensitive)) url = QUrl(argument);
        else if (argument.contains(QLatin1String("://"))) continue;
        else url = QUrl::fromLocalFile(QFileInfo(directory.filePath(argument)).absoluteFilePath());
        if (url.isValid() && url.isLocalFile() && !url.toLocalFile().isEmpty()) urls.append(url);
    }
    return urls;
}

bool ApplicationRequests::validUrls(const QList<QUrl> &urls)
{
    if (urls.size() > MaximumUrls) return false;
    for (const auto &url : urls) {
        if (!url.isValid() || !url.isLocalFile() || url.toLocalFile().isEmpty()
            || !QDir::isAbsolutePath(url.toLocalFile()) || url.toString(QUrl::FullyEncoded).size() > 32768
            || url.toLocalFile().contains(QChar(0))) return false;
    }
    return true;
}

QByteArray ApplicationRequests::requestBytes(const QList<QUrl> &urls)
{
    QJsonArray array;
    for (const auto &url : urls) array.append(url.toString(QUrl::FullyEncoded));
    return QJsonDocument(QJsonObject{{"version", 1}, {"urls", array}}).toJson(QJsonDocument::Compact) + '\n';
}

ApplicationRequests::ForwardResult ApplicationRequests::forward(const QByteArray &request)
{
    QLocalSocket socket;
    socket.connectToServer(m_name, QIODevice::ReadWrite);
    if (!socket.waitForConnected(180)) {
        if (socket.error() == QLocalSocket::ServerNotFoundError || socket.error() == QLocalSocket::ConnectionRefusedError) return NoListener;
        m_error = tr("Couldn't contact Musica: %1").arg(socket.errorString()); return Rejected;
    }
    if (socket.write(request) != request.size() || (socket.bytesToWrite() > 0 && !socket.waitForBytesWritten(1000))) {
        m_error = tr("Couldn't send the files to the running player."); return Rejected;
    }
    QByteArray response;
    while (!response.contains('\n') && response.size() < 4096) {
        response.append(socket.readAll());
        if (response.contains('\n')) break;
        if (!socket.waitForReadyRead(1800)) break;
    }
    const auto json = QJsonDocument::fromJson(response.trimmed());
    if (json.isObject() && json.object().value("ok").toBool()) return Accepted;
    m_error = json.object().value("error").toString();
    if (m_error.isEmpty()) m_error = tr("The running player didn't accept the open request. Try again when it finishes starting.");
    return Rejected;
}

ApplicationRequests::StartResult ApplicationRequests::startOrForward(const QList<QUrl> &urls)
{
    m_error.clear();
    if (!validUrls(urls)) { m_error = tr("Choose at most 2,048 local files per open request."); return Failed; }
    const auto request = requestBytes(urls);
    if (request.size() > MaximumRequestBytes) { m_error = tr("The open request is too large. Open fewer files together."); return Failed; }
    if (m_server.isListening()) {
        if (enqueue(urls)) return Listening;
        m_error = tr("Too many pending open requests. Try again shortly."); return Failed;
    }
    const auto existing = forward(request);
    if (existing == Accepted) return Forwarded;
    if (existing == Rejected) return Failed;
#ifdef Q_OS_WIN
    bool locked = m_instanceLock->tryLock(0);
    QElapsedTimer startupWait; startupWait.start();
    while (!locked && startupWait.elapsed() < 2500 && m_instanceLock->error() == QLockFile::LockFailedError) {
        // Another launcher may have acquired the lock just before creating
        // its pipe. Forward once it listens instead of opening a second UI.
        const auto raced = forward(request);
        if (raced == Accepted) return Forwarded;
        if (raced == Rejected) return Failed;
        QThread::msleep(25);
        locked = m_instanceLock->tryLock(0);
    }
    if (!locked) {
        m_error = tr("Musica is already starting, or its local instance lock is unavailable. Try opening the file again shortly.");
        return Failed;
    }
#endif
    if (!m_server.listen(m_name)) {
        if (m_server.serverError() != QAbstractSocket::AddressInUseError) { m_error = m_server.errorString(); return Failed; }
        // A simultaneous first launch may have won the listen race. Probe it
        // before removing a genuinely stale endpoint left after a crash.
        const auto raced = forward(request);
        if (raced == Accepted) return Forwarded;
        if (raced == Rejected) return Failed;
        if (!QLocalServer::removeServer(m_name) || !m_server.listen(m_name)) { m_error = m_server.errorString(); return Failed; }
    }
    enqueue(urls);
    return Listening;
}

bool ApplicationRequests::enqueue(const QList<QUrl> &urls)
{
    if (m_pending.size() >= MaximumQueuedRequests) return false;
    QList<QUrl> unique; QSet<QString> seen;
    for (const auto &url : urls) {
        const auto key = url.toString(QUrl::FullyEncoded);
        if (!seen.contains(key)) { unique.append(url); seen.insert(key); }
    }
    m_pending.enqueue(unique); scheduleFlush(); return true;
}

void ApplicationRequests::setReady(bool ready)
{
    m_ready = ready; scheduleFlush();
}

void ApplicationRequests::scheduleFlush()
{
    if (!m_ready || m_flushScheduled || m_pending.isEmpty()) return;
    m_flushScheduled = true;
    QTimer::singleShot(0, this, &ApplicationRequests::flush);
}

void ApplicationRequests::flush()
{
    m_flushScheduled = false;
    if (!m_ready || m_pending.isEmpty()) return;
    emit activateRequested();
    while (m_ready && !m_pending.isEmpty()) {
        const auto urls = m_pending.dequeue();
        if (!urls.isEmpty()) emit urlsRequested(urls);
    }
}

void ApplicationRequests::acceptConnections()
{
    while (m_server.hasPendingConnections()) {
        auto *socket = m_server.nextPendingConnection();
        if (m_clients.size() >= MaximumConnections) { socket->abort(); socket->deleteLater(); continue; }
        m_clients.insert(socket, {});
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { readRequest(socket); });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] { m_clients.remove(socket); socket->deleteLater(); });
        QTimer::singleShot(2500, socket, [socket] { socket->abort(); });
        if (socket->bytesAvailable()) readRequest(socket);
    }
}

void ApplicationRequests::reply(QLocalSocket *socket, bool success, const QString &error)
{
    m_clients.remove(socket);
    const auto data = QJsonDocument(QJsonObject{{"ok", success}, {"error", error}}).toJson(QJsonDocument::Compact) + '\n';
    socket->write(data); socket->disconnectFromServer();
}

void ApplicationRequests::readRequest(QLocalSocket *socket)
{
    auto found = m_clients.find(socket); if (found == m_clients.end()) return;
    const auto remaining = MaximumRequestBytes - found->size();
    found->append(socket->read(qMin<qint64>(socket->bytesAvailable(), remaining + 1)));
    if (found->size() > MaximumRequestBytes) { reply(socket, false, tr("The open request is too large.")); return; }
    const auto newline = found->indexOf('\n'); if (newline < 0) return;
    if (!found->mid(newline + 1).trimmed().isEmpty()) { reply(socket, false, tr("Send one request per connection.")); return; }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(found->left(newline), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()
        || document.object().value("version").toInt() != 1 || !document.object().value("urls").isArray()) {
        reply(socket, false, tr("Invalid open request.")); return;
    }
    const auto array = document.object().value("urls").toArray();
    if (array.size() > MaximumUrls) { reply(socket, false, tr("Open fewer files together.")); return; }
    QList<QUrl> urls;
    for (const auto &value : array) {
        if (!value.isString()) { reply(socket, false, tr("Invalid file URL.")); return; }
        urls.append(QUrl::fromEncoded(value.toString().toUtf8(), QUrl::StrictMode));
    }
    if (!validUrls(urls)) { reply(socket, false, tr("Only local file URLs are accepted.")); return; }
    if (!enqueue(urls)) { reply(socket, false, tr("Too many pending open requests. Try again shortly.")); return; }
    reply(socket, true);
}

bool ApplicationRequests::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::FileOpen) {
        const auto *open = static_cast<QFileOpenEvent *>(event);
        QUrl url = open->url();
        if (url.isEmpty() && !open->file().isEmpty()) url = QUrl::fromLocalFile(QFileInfo(open->file()).absoluteFilePath());
        if (validUrls({url}) && m_fileEvents.size() < MaximumUrls) {
            m_fileEvents.append(url);
            if (!m_fileEventScheduled) {
                m_fileEventScheduled = true;
                QTimer::singleShot(0, this, [this] {
                    m_fileEventScheduled = false;
                    const auto files = std::exchange(m_fileEvents, {});
                    enqueue(files);
                });
            }
            event->accept(); return true;
        }
    }
    return QObject::eventFilter(watched, event);
}
