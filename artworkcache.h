#pragma once
#include <QCache>
#include <QFutureWatcher>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QQuickImageProvider>
#include <QSettings>
#include <memory>

struct ArtworkMemory {
    QMutex mutex;
    QCache<QString, QImage> images{32 * 1024}; // cost in KiB
    int diskLimitMiB = 128;
    qint64 lastTrim = 0;
};

class ArtworkProvider : public QQuickImageProvider
{
public:
    explicit ArtworkProvider(std::shared_ptr<ArtworkMemory> memory);
    QImage requestImage(const QString &id, QSize *size, const QSize &requested) override;
private:
    std::shared_ptr<ArtworkMemory> m_memory;
};

class CacheManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int memoryLimitMiB READ memoryLimitMiB WRITE setMemoryLimitMiB NOTIFY limitsChanged)
    Q_PROPERTY(int diskLimitMiB READ diskLimitMiB WRITE setDiskLimitMiB NOTIFY limitsChanged)
    Q_PROPERTY(qint64 diskBytes READ diskBytes NOTIFY statisticsChanged)
    Q_PROPERTY(int memoryKiB READ memoryKiB NOTIFY statisticsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
public:
    explicit CacheManager(QObject *parent = nullptr);
    ~CacheManager() override;
    int memoryLimitMiB() const { return m_memoryLimit; }
    int diskLimitMiB() const { return m_diskLimit; }
    qint64 diskBytes() const { return m_diskBytes; }
    int memoryKiB() const;
    bool busy() const { return m_worker.isRunning(); }
    int revision() const { return m_revision; }
    void setMemoryLimitMiB(int value);
    void setDiskLimitMiB(int value);
    Q_INVOKABLE QString thumbnail(const QString &url) const;
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void clearThumbnails();
    Q_INVOKABLE void cleanAbandonedAudio();
    Q_INVOKABLE void invalidate();
    std::shared_ptr<ArtworkMemory> memory() const { return m_memory; }
    static QString thumbnailDirectory();
signals:
    void limitsChanged();
    void statisticsChanged();
    void busyChanged();
    void revisionChanged();
    void completed(const QString &message);
private:
    void maintain(bool clear, bool audio);
    QSettings m_settings;
    std::shared_ptr<ArtworkMemory> m_memory = std::make_shared<ArtworkMemory>();
    QFutureWatcher<QVariantMap> m_worker;
    int m_memoryLimit = 32, m_diskLimit = 128, m_revision = 0;
    qint64 m_diskBytes = 0;
};

void registerPerformanceTypes();
