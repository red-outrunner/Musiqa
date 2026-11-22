#pragma once
#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QPointer>
#include <QUrl>
#include <QVariantMap>
#include <atomic>
#include <memory>

class TrackListModel;
class PlaylistMigrationModel : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit PlaylistMigrationModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : m_rows.size(); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole + 1, "entry"}}; }
    void replace(const QVariantList &rows);
    QVariantMap row(int index) const;
    bool update(int index, const QVariantMap &row);
private:
    QVariantList m_rows;
};
class PlaylistMigration : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QAbstractItemModel *entries READ entries CONSTANT)
    Q_PROPERTY(QVariantMap summary READ summary NOTIFY changed)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY changed)
public:
    explicit PlaylistMigration(TrackListModel *library);
    ~PlaylistMigration() override;
    bool busy() const { return m_busy; }
    QAbstractItemModel *entries() { return &m_rows; }
    QVariantMap summary() const { return m_summary; }
    int selectedCount() const { return m_selected; }
    Q_INVOKABLE bool preview(const QUrl &playlist, const QUrl &searchFolder = {});
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clear();
    Q_INVOKABLE void select(int index, bool selected);
    Q_INVOKABLE bool resolve(int index, const QUrl &file);
    Q_INVOKABLE QString importSelected(const QString &name);
    static QVariantMap inspect(const QUrl &playlist, const QUrl &searchFolder = {},
        const std::shared_ptr<std::atomic_bool> &cancel = {});
signals:
    void changed();
    void error(const QString &message);
    void imported(const QString &id, const QString &name);
private:
    QPointer<TrackListModel> m_library;
    PlaylistMigrationModel m_rows;
    QFutureWatcher<QVariantMap> m_worker;
    std::shared_ptr<std::atomic_bool> m_cancel;
    QVariantMap m_summary;
    int m_selected = 0;
    bool m_busy = false;
};
