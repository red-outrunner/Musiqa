#include "sharing.h"
#include <QClipboard>
#if MUSICA_HAS_DBUS
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QDBusMetaType>
#endif
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <QtConcurrentRun>

Sharing::Sharing(QObject *parent) : QObject(parent)
{
#if MUSICA_HAS_DBUS
    qDBusRegisterMetaType<QList<QDBusUnixFileDescriptor>>();
#endif
    connect(&m_captureWriter, &QFutureWatcher<QVariantMap>::finished, this, [this] {
        const auto result = m_captureWriter.result(); m_busy = false; emit busyChanged();
        if (result.contains("error")) emit error(result.value("error").toString());
        else publish(result.value("path").toString(), true, tr("Now Playing screenshot"));
        m_grab.reset();
    });
    connect(&m_exportWriter, &QFutureWatcher<QVariantMap>::finished, this, [this] {
        const auto result = m_exportWriter.result(); m_busy = false; emit busyChanged();
        if (result.contains("error")) emit error(result.value("error").toString());
        else emit exported(QUrl::fromLocalFile(result.value("path").toString()));
    });
}
Sharing::~Sharing()
{
    if (m_captureWriter.isRunning()) m_captureWriter.waitForFinished();
    if (m_exportWriter.isRunning()) m_exportWriter.waitForFinished();
}
void Sharing::publish(const QString &path, bool image, const QString &title)
{
    m_path = path; m_image = image; m_title = title;
    auto folder = QStandardPaths::writableLocation(image ? QStandardPaths::PicturesLocation : QStandardPaths::DownloadLocation);
    if (!QDir(folder).exists()) folder = QDir::homePath();
    auto name = image ? QStringLiteral("Musica-Now-Playing.png") : QFileInfo(path).fileName();
    auto destination = QDir(folder).filePath(name);
    if (QFileInfo(destination).absoluteFilePath() == QFileInfo(path).absoluteFilePath())
        destination = QDir(folder).filePath(QFileInfo(name).completeBaseName() + " (shared copy)." + QFileInfo(name).suffix());
    emit ready({{"fileUrl", QUrl::fromLocalFile(path)}, {"title", title}, {"image", image},
        {"name", QFileInfo(path).fileName()}, {"size", QFileInfo(path).size()}, {"suggestedFile", QUrl::fromLocalFile(destination)}});
}
void Sharing::prepareSong(const QVariantMap &track)
{
    if (m_busy) return;
    const auto url = QUrl(track.value("fileUrl").toString());
    if (!url.isLocalFile() || !QFileInfo(url.toLocalFile()).isFile()) { emit error(tr("This song's local file is unavailable.")); return; }
    publish(url.toLocalFile(), false, track.value("artist").toString() + " — " + track.value("title").toString());
}
void Sharing::captureNowPlaying(QQuickItem *item)
{
    if (m_busy) return;
    if (!item || !item->isVisible() || item->width() <= 0 || item->height() <= 0 || !m_directory.isValid()) {
        emit error(tr("Open Now Playing before capturing a screenshot.")); return;
    }
    m_busy = true; emit busyChanged();
    auto size = QSize(qRound(item->width() * 2), qRound(item->height() * 2));
    if (size.width() > 2400 || size.height() > 2400) size = size.scaled(2400, 2400, Qt::KeepAspectRatio);
    m_grab = item->grabToImage(size);
    if (!m_grab) { m_busy = false; emit busyChanged(); emit error(tr("Couldn't capture Now Playing.")); return; }
    connect(m_grab.get(), &QQuickItemGrabResult::ready, this, [this] {
        const auto image = m_grab->image();
        const auto path = m_directory.filePath("Musica-Now-Playing-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".png");
        m_captureWriter.setFuture(QtConcurrent::run([path, image] {
            if (image.isNull() || !image.save(path, "PNG")) return QVariantMap{{"error", QStringLiteral("Couldn't save the screenshot.")}};
            return QVariantMap{{"path", path}};
        }));
    });
}
bool Sharing::copyToClipboard()
{
    if (m_path.isEmpty() || !QFileInfo::exists(m_path)) { emit error(tr("Choose a song or screenshot to share.")); return false; }
    auto *mime = new QMimeData;
    const auto url = QUrl::fromLocalFile(m_path);
    mime->setUrls({url});
    mime->setData("x-special/gnome-copied-files", "copy\n" + url.toEncoded());
    if (m_image) {
        const QImage image(m_path);
        if (image.isNull()) { delete mime; emit error(tr("Couldn't read the screenshot.")); return false; }
        mime->setImageData(image);
    } else mime->setText(m_title);
    QGuiApplication::clipboard()->setMimeData(mime);
    emit copied(); return true;
}
bool Sharing::exportTo(const QUrl &destination)
{
    if (m_busy || !destination.isLocalFile() || m_path.isEmpty()) return false;
    const auto source = m_path, target = destination.toLocalFile();
    if (QFileInfo(source).absoluteFilePath() == QFileInfo(target).absoluteFilePath()) { emit error(tr("Choose a different destination for the shared copy.")); return false; }
    m_busy = true; emit busyChanged();
    m_exportWriter.setFuture(QtConcurrent::run([source, target] {
        QFile input(source); QSaveFile output(target);
        if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) return QVariantMap{{"error", QStringLiteral("Couldn't open the source or destination.")}};
        while (!input.atEnd()) {
            const auto chunk = input.read(1024 * 1024);
            if (chunk.isEmpty() || output.write(chunk) != chunk.size()) return QVariantMap{{"error", QStringLiteral("The copy failed. Check available disk space.")}};
        }
        if (!output.commit()) return QVariantMap{{"error", output.errorString()}};
        return QVariantMap{{"path", target}};
    }));
    return true;
}
void Sharing::composeEmail()
{
#if MUSICA_HAS_DBUS
    if (m_path.isEmpty()) return;
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) { emit error(tr("Couldn't read the attachment.")); return; }
    QDBusUnixFileDescriptor descriptor(file.handle());
    const auto message = QDBusMessage::createMethodCall("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop", "org.freedesktop.portal.Email", "ComposeEmail");
    QVariantMap options{{"subject", m_title}, {"body", tr("Shared from Musica.")},
        {"attachment_fds", QVariant::fromValue(QList<QDBusUnixFileDescriptor>{descriptor})}};
    auto request = message;
    request.setArguments({QString(), options});
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(request, 10000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        QDBusPendingReply<QDBusObjectPath> reply = *watcher; watcher->deleteLater();
        if (reply.isError()) { emit error(tr("The desktop email service is unavailable. Use Copy or Save a copy instead.")); return; }
        m_requestPath = reply.value().path();
        QDBusConnection::sessionBus().connect("org.freedesktop.portal.Desktop", m_requestPath, "org.freedesktop.portal.Request", "Response", this, SLOT(portalResponse(uint,QVariantMap)));
    });
#else
    emit error(tr("Use Copy or Save a copy, then attach the file in your email app."));
#endif
}
void Sharing::portalResponse(uint response, const QVariantMap &)
{
#if MUSICA_HAS_DBUS
    QDBusConnection::sessionBus().disconnect("org.freedesktop.portal.Desktop", m_requestPath, "org.freedesktop.portal.Request", "Response", this, SLOT(portalResponse(uint,QVariantMap)));
    if (response > 1) emit error(tr("The desktop email composer couldn't attach this file. Use Copy or Save a copy instead."));
#else
    Q_UNUSED(response);
#endif
}
