#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QUrl>
#include <QVariantMap>
#include <atomic>
#include <memory>

class TrackListModel;

// Explicit library maintenance only: none of these operations runs at startup.
class LibraryMaintenance : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QVariantList duplicateGroups READ duplicateGroups NOTIFY duplicateGroupsChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
public:
    explicit LibraryMaintenance(TrackListModel *library);
    ~LibraryMaintenance() override;
    bool busy() const { return m_worker.isRunning(); }
    QVariantList duplicateGroups() const { return m_duplicates; }
    QString status() const { return m_status; }
    Q_INVOKABLE QVariantMap previewRelink(const QUrl &oldFolder, const QUrl &newFolder) const;
    Q_INVOKABLE bool applyRelink(const QVariantMap &preview);
    Q_INVOKABLE bool findDuplicates();
    Q_INVOKABLE bool trashDuplicates(const QString &keepUrl, const QStringList &removeUrls);
    Q_INVOKABLE bool createBackup(const QUrl &file, const QVariantMap &extraSettings = {});
    Q_INVOKABLE QVariantMap inspectBackup(const QUrl &file) const;
    Q_INVOKABLE bool restoreBackup(const QUrl &file);
signals:
    void busyChanged();
    void duplicateGroupsChanged();
    void statusChanged();
    void error(const QString &message);
    void completed(const QString &message);
    void urlsRelinked(const QVariantMap &mapping);
    void restored(const QVariantMap &extraSettings);
private:
    void setStatus(const QString &text);
    bool available();
    bool applyBackup(const QVariantMap &bundle);
    QVariantMap snapshot(const QVariantMap &extraSettings) const;
    TrackListModel *m_library;
    QFutureWatcher<QVariantMap> m_worker;
    QVariantList m_duplicates;
    QString m_status, m_operation;
    std::shared_ptr<std::atomic_bool> m_cancel;
};
