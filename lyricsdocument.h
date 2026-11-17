#pragma once

#include <QObject>
#include <QSettings>
#include <QUrl>
#include <QVariantList>

// Lyrics are read only when requested, from a local sidecar or the song's
// embedded tag. Audio files are never changed and no network lookup occurs.
class LyricsDocument : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QUrl source READ source NOTIFY changed)
    Q_PROPERTY(QVariantList lines READ lines NOTIFY changed)
    Q_PROPERTY(bool synchronized READ synchronized NOTIFY changed)
    Q_PROPERTY(QString sourceLabel READ sourceLabel NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(bool attached READ attached NOTIFY changed)
public:
    explicit LyricsDocument(QObject *parent = nullptr);
    QUrl source() const { return m_source; }
    QVariantList lines() const { return m_lines; }
    bool synchronized() const { return m_synchronized; }
    QString sourceLabel() const { return m_sourceLabel; }
    QString error() const { return m_error; }
    bool loading() const { return m_loading; }
    bool attached() const;
    Q_INVOKABLE void load(const QUrl &song);
    Q_INVOKABLE void reload();
    Q_INVOKABLE bool attachFile(const QUrl &file);
    Q_INVOKABLE void clearAttachment();
    Q_INVOKABLE QVariantMap exportAttachments() const;
    Q_INVOKABLE bool importAttachments(const QVariantMap &attachments);
    Q_INVOKABLE void remapSources(const QVariantMap &mapping);
    static QVariantMap parseText(const QString &text);
    static QVariantMap readLyrics(const QUrl &song, const QUrl &attachment = {});
signals:
    void changed();
private:
    QSettings m_settings;
    QUrl m_source;
    QVariantList m_lines;
    QString m_sourceLabel, m_error;
    bool m_synchronized = false, m_loading = false;
    quint64 m_generation = 0;
};
