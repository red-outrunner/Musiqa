#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QTimer>
#include <QVariantMap>

class QueueStore : public QObject
{
    Q_OBJECT
public:
    explicit QueueStore(const QString &file, const QVariantMap &legacy = {}, bool fresh = false, QObject *parent = nullptr);
    ~QueueStore() override;
    QVariantList queues() const { return m_queues; }
    QString activeId() const { return m_active; }
    QVariantMap session() const;
    QVariantMap history() const { return m_history; }
    QVariantMap exportState() const { return snapshot(); }
    bool replaceState(const QVariantMap &state);
    void saveSession(const QVariantMap &session);
    bool updateSession(const QString &id, const QVariantMap &session);
    void saveHistory(const QVariantMap &history);
    QString createQueue(const QString &name);
    QString startPlaybackQueue(const QString &name, const QVariantMap &session);
    bool activate(const QString &id);
    bool renameQueue(const QString &id, const QString &name);
    bool removeQueue(const QString &id);
    void removeOtherQueues();
    void flush();
signals:
    void changed();
    void error(const QString &message);
    void persisted();
private:
    void markDirty();
    void writePending();
    QVariantMap snapshot() const;
    QString m_file, m_active;
    QVariantList m_queues;
    QVariantMap m_history;
    QFutureWatcher<QString> m_writer;
    QTimer m_saveTimer;
    bool m_dirty = false, m_stopping = false;
};
