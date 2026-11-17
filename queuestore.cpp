#include "queuestore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <QtConcurrentRun>

namespace {
QString writeSnapshot(const QString &file, const QVariantMap &snapshot)
{
    if (!QDir().mkpath(QFileInfo(file).absolutePath())) return QStringLiteral("Could not create the playback settings folder.");
    QSaveFile output(file);
    if (!output.open(QIODevice::WriteOnly)) return output.errorString();
    const auto bytes = QJsonDocument::fromVariant(snapshot).toJson(QJsonDocument::Compact);
    if (output.write(bytes) != bytes.size() || !output.commit()) return output.errorString();
    return {};
}
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
}
QueueStore::QueueStore(const QString &file, const QVariantMap &legacy, bool fresh, QObject *parent)
    : QObject(parent), m_file(file)
{
    QVariantMap saved;
    if (!fresh) {
        QFile input(file);
        if (input.open(QIODevice::ReadOnly) && input.size() <= 64 * 1024 * 1024)
            saved = QJsonDocument::fromJson(input.readAll()).toVariant().toMap();
    }
    QSet<QString> ids;
    for (const auto &value : saved.value("queues").toList()) {
        auto queue = value.toMap();
        const auto id = queue.value("id").toString(), name = queue.value("name").toString().simplified().left(80);
        if (id.isEmpty() || name.isEmpty() || ids.contains(id)) continue;
        ids.insert(id); queue["name"] = name; m_queues.append(queue);
        if (m_queues.size() == 4) break;
    }
    if (m_queues.isEmpty()) m_queues.append(QVariantMap{{"id", newId()}, {"name", "Queue 1"}, {"session", legacy}});
    m_active = saved.value("active").toString();
    if (!ids.contains(m_active)) m_active = m_queues.first().toMap().value("id").toString();
    m_history = saved.value("history").toMap();
    m_saveTimer.setSingleShot(true); m_saveTimer.setInterval(60);
    connect(&m_saveTimer, &QTimer::timeout, this, &QueueStore::writePending);
    connect(&m_writer, &QFutureWatcher<QString>::finished, this, [this] {
        const auto message = m_writer.result();
        if (message.isEmpty()) emit persisted(); else emit error(tr("Couldn't save playback state: %1").arg(message));
        if (m_dirty && !m_stopping) writePending();
    });
}
QueueStore::~QueueStore() { flush(); }
QVariantMap QueueStore::snapshot() const { return {{"version", 1}, {"active", m_active}, {"queues", m_queues}, {"history", m_history}}; }
bool QueueStore::replaceState(const QVariantMap &state)
{
    const auto queues = state.value("queues").toList();
    const auto active = state.value("active").toString();
    if (queues.isEmpty() || queues.size() > 4) return false;
    QSet<QString> ids;
    for (const auto &value : queues) {
        const auto queue = value.toMap();
        const auto id = queue.value("id").toString(), name = queue.value("name").toString().simplified();
        if (id.isEmpty() || ids.contains(id) || name.isEmpty() || name.size() > 80) return false;
        ids.insert(id);
    }
    if (!ids.contains(active)) return false;
    if (m_writer.isRunning()) m_writer.waitForFinished();
    m_queues = queues; m_active = active; m_history = state.value("history").toMap();
    m_stopping = false; markDirty(); emit changed();
    return true;
}
QVariantMap QueueStore::session() const
{
    for (const auto &value : m_queues) {
        const auto queue = value.toMap();
        if (queue.value("id").toString() == m_active) return queue.value("session").toMap();
    }
    return {};
}
void QueueStore::markDirty() { m_dirty = true; if (!m_stopping) m_saveTimer.start(); }
void QueueStore::writePending()
{
    if (!m_dirty || m_writer.isRunning()) return;
    m_dirty = false;
    const auto data = snapshot();
    const auto file = m_file;
    m_writer.setFuture(QtConcurrent::run([file, data] { return writeSnapshot(file, data); }));
}
void QueueStore::flush()
{
    m_stopping = true; m_saveTimer.stop();
    if (m_writer.isRunning()) m_writer.waitForFinished();
    if (m_dirty) { writeSnapshot(m_file, snapshot()); m_dirty = false; }
}
void QueueStore::saveSession(const QVariantMap &session)
{
    updateSession(m_active, session);
}
bool QueueStore::updateSession(const QString &id, const QVariantMap &session)
{
    for (auto &value : m_queues) {
        auto queue = value.toMap();
        if (queue.value("id").toString() != id) continue;
        if (queue.value("session").toMap() == session) return true;
        const auto old = queue.value("session").toMap();
        queue["session"] = session; value = queue; markDirty();
        // Position checkpoints do not rebuild the queue selector.
        if (old.value("queue") != session.value("queue") || old.value("currentId") != session.value("currentId")) emit changed();
        return true;
    }
    return false;
}
void QueueStore::saveHistory(const QVariantMap &history)
{
    if (history == m_history) return;
    m_history = history; markDirty();
}
QString QueueStore::createQueue(const QString &name)
{
    const auto title = name.simplified();
    if (m_queues.size() >= 4) { emit error(tr("You can keep up to four queues. Delete a queue before creating another.")); return {}; }
    if (title.isEmpty() || title.size() > 80) { emit error(tr("Use a queue name of 1–80 characters.")); return {}; }
    const auto id = newId();
    m_queues.append(QVariantMap{{"id", id}, {"name", title}, {"session", QVariantMap{}}});
    m_active = id; markDirty(); emit changed(); return id;
}
QString QueueStore::startPlaybackQueue(const QString &name, const QVariantMap &session)
{
    if (session.value("queue").toList().isEmpty()) { emit error(tr("Choose songs to start a queue.")); return {}; }
    const auto title = name.simplified().left(80);
    if (title.isEmpty()) { emit error(tr("Give the queue a name.")); return {}; }
    QString id;
    // A manually opened empty queue (including the initial Queue 1) is an
    // available slot. Filling it makes it the newest queue in creation order.
    for (int row = 0; row < m_queues.size(); ++row) {
        const auto queue = m_queues[row].toMap();
        if (queue.value("id").toString() == m_active && queue.value("session").toMap().value("queue").toList().isEmpty()) {
            id = m_active; m_queues.removeAt(row); break;
        }
    }
    if (m_queues.size() >= 4) m_queues.removeFirst();
    if (id.isEmpty()) id = newId();
    m_queues.append(QVariantMap{{"id", id}, {"name", title}, {"session", session}});
    m_active = id; markDirty(); emit changed(); return id;
}

bool QueueStore::activate(const QString &id)
{
    if (id == m_active) return true;
    for (const auto &value : m_queues) if (value.toMap().value("id").toString() == id) {
        m_active = id; markDirty(); emit changed(); return true;
    }
    return false;
}
bool QueueStore::renameQueue(const QString &id, const QString &name)
{
    const auto title = name.simplified();
    if (title.isEmpty() || title.size() > 80) { emit error(tr("Use a queue name of 1–80 characters.")); return false; }
    for (auto &value : m_queues) {
        auto queue = value.toMap(); if (queue.value("id").toString() != id) continue;
        queue["name"] = title; value = queue; markDirty(); emit changed(); return true;
    }
    return false;
}
bool QueueStore::removeQueue(const QString &id)
{
    if (m_queues.size() <= 1) { emit error(tr("Keep at least one queue.")); return false; }
    for (int row = 0; row < m_queues.size(); ++row) {
        if (m_queues[row].toMap().value("id").toString() != id) continue;
        m_queues.removeAt(row);
        if (m_active == id) m_active = m_queues.first().toMap().value("id").toString();
        markDirty(); emit changed(); return true;
    }
    return false;
}
void QueueStore::removeOtherQueues()
{
    if (m_queues.size() <= 1) return;
    for (const auto &value : m_queues) if (value.toMap().value("id").toString() == m_active) {
        m_queues = {value}; markDirty(); emit changed(); return;
    }
}
