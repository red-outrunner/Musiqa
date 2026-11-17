#include "artworkcache.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QMutexLocker>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QDateTime>
#include <QCoreApplication>
#include <QtConcurrentRun>
#include <algorithm>

ArtworkProvider::ArtworkProvider(std::shared_ptr<ArtworkMemory> memory)
    : QQuickImageProvider(Image, ForceAsynchronousImageLoading), m_memory(std::move(memory)) {}
QString CacheManager::thumbnailDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/thumbnails";
}
QImage ArtworkProvider::requestImage(const QString &id, QSize *size, const QSize &requested)
{
    const QUrl url(QUrl::fromPercentEncoding(id.section('?', 0, 0).toUtf8()));
    if (!url.isLocalFile() && url.scheme() != "qrc") return {};
    const QString path = url.isLocalFile() ? url.toLocalFile() : ":" + url.path();
    const QFileInfo info(path);
    const int pixels = qBound(64, ((qMax(requested.width(), requested.height()) + 31) / 32) * 32, 2048);
    const auto fingerprint = url.toString() + ':' + QString::number(info.size()) + ':'
        + QString::number(info.lastModified().toMSecsSinceEpoch()) + ':' + QString::number(pixels);
    const auto key = QString::fromLatin1(QCryptographicHash::hash(fingerprint.toUtf8(), QCryptographicHash::Sha256).toHex());
    {
        QMutexLocker lock(&m_memory->mutex);
        if (const auto *cached = m_memory->images.object(key)) { if (size) *size = cached->size(); return *cached; }
    }
    const auto cachedPath = CacheManager::thumbnailDirectory() + '/' + key + ".png";
    QImage image(cachedPath);
    if (image.isNull()) {
        QImageReader reader(path); reader.setAutoTransform(true);
        const auto original = reader.size();
        if (!original.isValid() || qint64(original.width()) * original.height() > 64000000) return {};
        reader.setScaledSize(original.scaled(pixels, pixels, Qt::KeepAspectRatio));
        image = reader.read();
        if (image.isNull()) return {};
        const int edge = qMin(image.width(), image.height());
        image = image.copy((image.width() - edge) / 2, (image.height() - edge) / 2, edge, edge).scaled(pixels, pixels, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        if (QDir().mkpath(CacheManager::thumbnailDirectory())) {
            QSaveFile file(cachedPath);
            if (file.open(QIODevice::WriteOnly) && image.save(&file, "PNG")) file.commit();
        }
        qint64 budget = 0;
        {
            QMutexLocker lock(&m_memory->mutex);
            const auto now = QDateTime::currentMSecsSinceEpoch();
            if (now - m_memory->lastTrim >= 5000) { budget = qint64(m_memory->diskLimitMiB) * 1048576; m_memory->lastTrim = now; }
        }
        if (budget) {
            const auto files = QDir(CacheManager::thumbnailDirectory()).entryInfoList({"*.png"}, QDir::Files | QDir::NoSymLinks, QDir::Time | QDir::Reversed);
            qint64 bytes = 0; for (const auto &file : files) bytes += file.size();
            for (const auto &file : files) { if (bytes <= budget) break; if (QFile::remove(file.absoluteFilePath())) bytes -= file.size(); }
        }
    }
    if (size) *size = image.size();
    QMutexLocker lock(&m_memory->mutex);
    m_memory->images.insert(key, new QImage(image), qMax<qint64>(1, image.sizeInBytes() / 1024));
    return image;
}
CacheManager::CacheManager(QObject *parent)
    : QObject(parent), m_settings(QFileInfo(QSettings().fileName()).absoluteDir().filePath("cache.ini"), QSettings::IniFormat)
{
    m_memoryLimit = qBound(8, m_settings.value("memoryMiB", 32).toInt(), 256);
    m_diskLimit = qBound(32, m_settings.value("diskMiB", 128).toInt(), 2048);
    m_memory->images.setMaxCost(m_memoryLimit * 1024);
    m_memory->diskLimitMiB = m_diskLimit;
    connect(&m_worker, &QFutureWatcher<QVariantMap>::finished, this, [this] {
        const auto result = m_worker.result(); m_diskBytes = result.value("bytes").toLongLong();
        emit busyChanged(); emit statisticsChanged();
        if (result.value("clear").toBool()) { ++m_revision; emit revisionChanged(); }
        if (result.value("removed").toInt()) emit completed(tr("Removed %1 cached files.").arg(result.value("removed").toInt()));
    });
}
CacheManager::~CacheManager() { if (m_worker.isRunning()) m_worker.waitForFinished(); }
int CacheManager::memoryKiB() const { QMutexLocker lock(&m_memory->mutex); return m_memory->images.totalCost(); }
void CacheManager::setMemoryLimitMiB(int value)
{
    value = qBound(8, value, 256); if (value == m_memoryLimit) return;
    m_memoryLimit = value;
    { QMutexLocker lock(&m_memory->mutex); m_memory->images.setMaxCost(value * 1024); }
    m_settings.setValue("memoryMiB", value); emit limitsChanged(); emit statisticsChanged();
}
void CacheManager::setDiskLimitMiB(int value)
{
    value = qBound(32, value, 2048); if (value == m_diskLimit) return;
    m_diskLimit = value; { QMutexLocker lock(&m_memory->mutex); m_memory->diskLimitMiB = value; }
    m_settings.setValue("diskMiB", value); emit limitsChanged(); refresh();
}
QString CacheManager::thumbnail(const QString &url) const
{
    return url.isEmpty() ? QString() : "image://artwork/" + QString::fromLatin1(QUrl::toPercentEncoding(url)) + "?revision=" + QString::number(m_revision);
}
void CacheManager::refresh() { maintain(false, false); }
void CacheManager::clearThumbnails()
{
    if (busy()) return;
    { QMutexLocker lock(&m_memory->mutex); m_memory->images.clear(); }
    maintain(true, false);
}
void CacheManager::cleanAbandonedAudio() { maintain(false, true); }
void CacheManager::invalidate()
{
    { QMutexLocker lock(&m_memory->mutex); m_memory->images.clear(); }
    ++m_revision; emit revisionChanged();
}
void CacheManager::maintain(bool clear, bool audio)
{
    if (busy()) return;
    const auto directory = thumbnailDirectory(); const qint64 limit = qint64(m_diskLimit) * 1024 * 1024;
    m_worker.setFuture(QtConcurrent::run([directory, limit, clear, audio] {
        auto files = QDir(directory).entryInfoList({"*.png"}, QDir::Files | QDir::NoSymLinks, QDir::Time | QDir::Reversed);
        qint64 bytes = 0; int removed = 0;
        for (const auto &file : files) bytes += file.size();
        for (const auto &file : files) {
            if (!clear && bytes <= limit) break;
            if (QFile::remove(file.absoluteFilePath())) { bytes -= file.size(); ++removed; }
        }
        if (audio && QDir("/proc").exists()) {
            const QRegularExpression pattern("^musica-pcm-([0-9]+)-[A-Za-z0-9]+$");
            const auto cutoff = QDateTime::currentDateTimeUtc().addDays(-1);
            for (const auto &file : QDir(QDir::tempPath()).entryInfoList({"musica-pcm-*"}, QDir::Files | QDir::NoSymLinks)) {
                const auto match = pattern.match(file.fileName());
                if (match.hasMatch() && file.lastModified() < cutoff && !QFileInfo::exists("/proc/" + match.captured(1))
                    && QFile::remove(file.absoluteFilePath())) ++removed;
            }
        }
        return QVariantMap{{"bytes", bytes}, {"removed", removed}, {"clear", clear}};
    }));
    emit busyChanged();
}
void registerPerformanceTypes()
{
    qmlRegisterSingletonType<CacheManager>("MusicPlayer", 1, 0, "ArtworkCache", [](QQmlEngine *engine, QJSEngine *) -> QObject * {
        auto *manager = new CacheManager;
        engine->addImageProvider("artwork", new ArtworkProvider(manager->memory()));
        return manager;
    });
}
