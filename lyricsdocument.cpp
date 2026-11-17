#include "lyricsdocument.h"
#include "taglibpath.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QtConcurrent>
#include <fileref.h>
#include <tpropertymap.h>
#include <algorithm>

namespace {
constexpr qint64 MaxLyricsBytes = 2 * 1024 * 1024;
QString canonicalSong(const QUrl &url)
{
    const QFileInfo info(url.toLocalFile());
    const auto path = info.canonicalFilePath();
    return QUrl::fromLocalFile(path.isEmpty() ? info.absoluteFilePath() : path).toString();
}
QVariantMap readText(const QUrl &url)
{
    QFile file(url.toLocalFile());
    if (!url.isLocalFile() || !file.open(QIODevice::ReadOnly)) return {{"error", QStringLiteral("The local lyrics file couldn't be read.")}};
    if (file.size() > MaxLyricsBytes) return {{"error", QStringLiteral("Choose a lyrics file smaller than 2 MB.")}};
    const auto bytes = file.readAll();
    QString text;
    if (bytes.startsWith("\xff\xfe")) { QStringDecoder decoder(QStringDecoder::Utf16LE); text = decoder(bytes.mid(2)); }
    else if (bytes.startsWith("\xfe\xff")) { QStringDecoder decoder(QStringDecoder::Utf16BE); text = decoder(bytes.mid(2)); }
    else text = QString::fromUtf8(bytes);
    auto result = LyricsDocument::parseText(text);
    result["sourceLabel"] = QFileInfo(file).fileName();
    return result;
}
}

LyricsDocument::LyricsDocument(QObject *parent) : QObject(parent) {}

QVariantMap LyricsDocument::parseText(const QString &input)
{
    // Bound both tag values and sidecar input. Keep the latest read small even
    // for malformed files with millions of lines or repeated time tags.
    const QString text = input.left(int(MaxLyricsBytes)).remove(QChar(0xfeff));
    const auto rawLines = text.split(QRegularExpression(QStringLiteral("\r?\n")));
    static const QRegularExpression stamp(QStringLiteral("\\[(?:(\\d{1,3}):)?(\\d{1,4}):([0-5]\\d)(?:[.:](\\d{1,3}))?\\]"));
    static const QRegularExpression offsetTag(QStringLiteral("\\[offset:([+-]?\\d+)\\]"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression meta(QStringLiteral("^\\[(?:ar|al|ti|au|by|re|ve|length|offset):.*\\]$"), QRegularExpression::CaseInsensitiveOption);
    qint64 offset = 0;
    const auto offsetMatch = offsetTag.match(text);
    if (offsetMatch.hasMatch()) offset = qBound<qint64>(-86400000LL, offsetMatch.captured(1).toLongLong(), 86400000LL);
    struct Line { qint64 time; QString text; };
    QList<Line> timed;
    for (const auto &raw : rawLines) {
        auto matches = stamp.globalMatch(raw); QList<qint64> times;
        QString lyric = raw;
        while (matches.hasNext() && times.size() < 200) {
            const auto match = matches.next();
            const auto fraction = match.captured(4).leftJustified(3, QLatin1Char('0')).left(3).toInt();
            const auto time = match.captured(1).toLongLong() * 3600000 + match.captured(2).toLongLong() * 60000
                + match.captured(3).toLongLong() * 1000 + fraction;
            times.append(qMax<qint64>(0, time - offset));
        }
        if (times.isEmpty()) continue;
        lyric.remove(stamp); lyric = lyric.trimmed();
        for (const auto time : times) timed.append({time, lyric});
        if (timed.size() >= 20000) break;
    }
    QVariantList lines;
    if (!timed.isEmpty()) {
        std::stable_sort(timed.begin(), timed.end(), [](const Line &a, const Line &b) { return a.time < b.time; });
        for (const auto &line : timed) {
            if (!lines.isEmpty() && lines.last().toMap().value("time").toLongLong() == line.time) {
                auto merged = lines.last().toMap();
                const auto previous = merged.value("text").toString();
                if (line.text != previous && !line.text.isEmpty()) merged["text"] = previous.isEmpty() ? line.text : previous + QLatin1Char('\n') + line.text;
                lines.last() = merged;
            } else lines.append(QVariantMap{{"time", line.time}, {"text", line.text}});
        }
    } else {
        for (const auto &raw : rawLines) {
            const auto line = raw.trimmed();
            if (meta.match(line).hasMatch()) continue;
            lines.append(QVariantMap{{"time", -1}, {"text", line}});
            if (lines.size() >= 20000) break;
        }
        while (!lines.isEmpty() && lines.last().toMap().value("text").toString().isEmpty()) lines.removeLast();
        while (!lines.isEmpty() && lines.first().toMap().value("text").toString().isEmpty()) lines.removeFirst();
    }
    return {{"lines", lines}, {"synchronized", !timed.isEmpty()}};
}

QVariantMap LyricsDocument::readLyrics(const QUrl &song, const QUrl &attachment)
{
    if (!song.isLocalFile()) return {{"error", QStringLiteral("Choose a local song to display lyrics.")}};
    if (!attachment.isEmpty()) return readText(attachment);
    const QFileInfo info(song.toLocalFile());
    // Exact basename lookup avoids walking a large music directory.
    for (const auto &extension : {QStringLiteral(".lrc"), QStringLiteral(".LRC"), QStringLiteral(".txt"), QStringLiteral(".TXT")}) {
        const auto path = info.dir().filePath(info.completeBaseName() + extension);
        if (QFileInfo(path).isFile()) return readText(QUrl::fromLocalFile(path));
    }
    const auto path = TagLibPath::encode(info.absoluteFilePath());
    TagLib::FileRef file(TagLibPath::data(path), false);
    if (!file.isNull()) {
        const auto properties = file.properties();
        QStringList keys{QStringLiteral("LYRICS"), QStringLiteral("UNSYNCEDLYRICS"), QStringLiteral("UNSYNCED LYRICS")};
        for (auto it = properties.begin(); it != properties.end(); ++it) {
            const auto key = QString::fromUtf8(it->first.toCString(true));
            if (key.startsWith(QLatin1String("LYRICS:"))) keys.append(key);
        }
        for (const auto &key : keys) {
            const auto values = properties.value(TagLib::String(key.toUtf8().constData(), TagLib::String::UTF8));
            for (const auto &value : values) {
                const auto lyric = QString::fromUtf8(value.toCString(true));
                if (lyric.trimmed().isEmpty()) continue;
                auto result = parseText(lyric); result["sourceLabel"] = QStringLiteral("Embedded lyrics"); return result;
            }
        }
    }
    return {{"lines", QVariantList()}, {"synchronized", false}, {"sourceLabel", QString()}};
}

bool LyricsDocument::attached() const
{
    if (m_source.isEmpty()) return false;
    return m_settings.value("lyrics/attachments").toMap().contains(canonicalSong(m_source));
}

void LyricsDocument::load(const QUrl &song)
{
    if (m_source == song && (m_loading || !m_lines.isEmpty())) return;
    m_source = song; reload();
}

void LyricsDocument::reload()
{
    const auto generation = ++m_generation;
    m_lines.clear(); m_sourceLabel.clear(); m_error.clear(); m_synchronized = false;
    m_loading = !m_source.isEmpty(); emit changed();
    if (!m_loading) return;
    const auto song = m_source;
    const auto attachedFile = QUrl(m_settings.value("lyrics/attachments").toMap().value(canonicalSong(song)).toString());
    auto *watcher = new QFutureWatcher<QVariantMap>(this);
    connect(watcher, &QFutureWatcher<QVariantMap>::finished, this, [this, watcher, generation] {
        const auto result = watcher->result(); watcher->deleteLater();
        if (generation != m_generation) return;
        m_lines = result.value("lines").toList(); m_sourceLabel = result.value("sourceLabel").toString();
        m_error = result.value("error").toString(); m_synchronized = result.value("synchronized").toBool();
        m_loading = false; emit changed();
    });
    watcher->setFuture(QtConcurrent::run([song, attachedFile] { return readLyrics(song, attachedFile); }));
}

bool LyricsDocument::attachFile(const QUrl &file)
{
    const QFileInfo info(file.toLocalFile());
    if (m_source.isEmpty() || !file.isLocalFile() || !info.isFile() || !info.isReadable() || info.size() > MaxLyricsBytes) {
        m_error = tr("Choose a readable local LRC or text file smaller than 2 MB."); emit changed(); return false;
    }
    auto attachments = m_settings.value("lyrics/attachments").toMap();
    attachments[canonicalSong(m_source)] = QUrl::fromLocalFile(info.canonicalFilePath()).toString();
    m_settings.setValue("lyrics/attachments", attachments); reload(); return true;
}

void LyricsDocument::clearAttachment()
{
    if (m_source.isEmpty()) return;
    auto attachments = m_settings.value("lyrics/attachments").toMap();
    attachments.remove(canonicalSong(m_source)); m_settings.setValue("lyrics/attachments", attachments); reload();
}

QVariantMap LyricsDocument::exportAttachments() const
{
    return m_settings.value("lyrics/attachments").toMap();
}

bool LyricsDocument::importAttachments(const QVariantMap &attachments)
{
    if (attachments.size() > 100000) return false;
    for (auto it = attachments.cbegin(); it != attachments.cend(); ++it) {
        if (it.key().size() > 32768 || it.value().toString().size() > 32768
            || !QUrl(it.key()).isLocalFile() || !QUrl(it.value().toString()).isLocalFile()) return false;
    }
    m_settings.setValue("lyrics/attachments", attachments);
    reload(); return true;
}

void LyricsDocument::remapSources(const QVariantMap &mapping)
{
    auto attachments = exportAttachments(); bool changed = false;
    for (auto it = mapping.cbegin(); it != mapping.cend(); ++it) {
        const QUrl oldUrl(it.key()), newUrl(it.value().toString());
        if (!oldUrl.isLocalFile() || !newUrl.isLocalFile()) continue;
        const auto oldKey = canonicalSong(oldUrl), newKey = canonicalSong(newUrl);
        if (attachments.contains(oldKey)) {
            QUrl lyrics(attachments.take(oldKey).toString());
            const auto relative = QFileInfo(oldUrl.toLocalFile()).dir().relativeFilePath(lyrics.toLocalFile());
            if (!relative.startsWith(QLatin1String("../")) && !QDir::isAbsolutePath(relative)) {
                const auto moved = QFileInfo(newUrl.toLocalFile()).dir().filePath(relative);
                if (QFileInfo(moved).isFile()) lyrics = QUrl::fromLocalFile(QFileInfo(moved).canonicalFilePath());
            }
            attachments[newKey] = lyrics.toString(); changed = true;
        }
        if (m_source.isLocalFile() && canonicalSong(m_source) == oldKey) { m_source = newUrl; changed = true; }
    }
    if (changed) { m_settings.setValue("lyrics/attachments", attachments); reload(); }
}
