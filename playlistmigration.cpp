#include "playlistmigration.h"
#include "tracklibrary.h"
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>
#include <QXmlStreamReader>
#include <QtConcurrentRun>

namespace {
const QStringList formats{"mp3", "flac", "ogg", "oga", "opus", "wav", "m4a", "aac", "aif", "aiff", "wma"};
QString normalized(QString path) { return path.replace('\\', '/'); }
bool audioFile(const QString &path)
{
    const QFileInfo file(path);
    return file.isFile() && file.isReadable() && formats.contains(file.suffix(), Qt::CaseInsensitive);
}
QStringList components(const QString &path)
{
    return normalized(path).normalized(QString::NormalizationForm_KC).toCaseFolded().split('/', Qt::SkipEmptyParts);
}
}
QVariant PlaylistMigrationModel::data(const QModelIndex &index, int role) const
{
    return index.isValid() && index.row() >= 0 && index.row() < m_rows.size() && role == Qt::UserRole + 1 ? m_rows[index.row()] : QVariant();
}
void PlaylistMigrationModel::replace(const QVariantList &rows) { beginResetModel(); m_rows = rows; endResetModel(); }
QVariantMap PlaylistMigrationModel::row(int index) const { return index >= 0 && index < m_rows.size() ? m_rows[index].toMap() : QVariantMap(); }
bool PlaylistMigrationModel::update(int index, const QVariantMap &row)
{
    if (index < 0 || index >= m_rows.size()) return false;
    m_rows[index] = row; emit dataChanged(this->index(index), this->index(index), {Qt::UserRole + 1}); return true;
}
QObject *TrackListModel::playlistMigration()
{
    if (!m_playlistMigration) m_playlistMigration = new PlaylistMigration(this);
    return m_playlistMigration;
}
PlaylistMigration::PlaylistMigration(TrackListModel *library) : QObject(library), m_library(library), m_rows(this)
{
    connect(&m_worker, &QFutureWatcher<QVariantMap>::finished, this, [this] {
        m_busy = false;
        if (m_cancel && m_cancel->load()) { m_summary = {}; m_rows.replace({}); m_selected = 0; emit changed(); return; }
        auto result = m_worker.result();
        m_rows.replace(result.take("entries").toList()); m_summary = result;
        m_selected = result.value("selected").toInt(); emit changed();
        if (result.contains("error")) emit error(result.value("error").toString());
    });
}
PlaylistMigration::~PlaylistMigration() { cancel(); }
void PlaylistMigration::cancel() { if (m_cancel) m_cancel->store(true); }
void PlaylistMigration::clear()
{
    if (m_busy) return;
    m_rows.replace({}); m_summary = {}; m_selected = 0; emit changed();
}
bool PlaylistMigration::preview(const QUrl &playlist, const QUrl &folder)
{
    if (m_busy) return false;
    m_busy = true; m_rows.replace({}); m_summary = {}; m_selected = 0;
    m_cancel = std::make_shared<std::atomic_bool>(false); const auto cancel = m_cancel;
    emit changed();
    m_worker.setFuture(QtConcurrent::run([playlist, folder, cancel] { return inspect(playlist, folder, cancel); }));
    return true;
}
QVariantMap PlaylistMigration::inspect(const QUrl &playlist, const QUrl &searchFolder, const std::shared_ptr<std::atomic_bool> &cancel)
{
    const auto canceled = [&] { return cancel && cancel->load(); };
    QFile source(playlist.toLocalFile());
    if (!playlist.isLocalFile() || !source.open(QIODevice::ReadOnly) || source.size() > 8 * 1024 * 1024)
        return {{"error", tr("Choose a readable local playlist up to 8 MB.")}};
    const QFileInfo playlistInfo(source);
    const auto bytes = source.readAll();
    QStringDecoder decoder(QStringConverter::encodingForData(bytes).value_or(QStringConverter::Utf8));
    QString text = decoder(bytes);
    if (decoder.hasError()) return {{"error", tr("Export the playlist as UTF-8 or UTF-16 and try again.")}};
    if (text.startsWith(QChar(0xfeff))) text.remove(0, 1);
    QList<QPair<QString, QString>> locations;
    const auto extension = playlistInfo.suffix().toLower();
    if (extension == "xspf" || extension == "wpl") {
        QXmlStreamReader xml(text); QString location, title;
        while (!xml.atEnd() && !canceled()) {
            xml.readNext();
            if (xml.isStartElement()) {
                if (xml.name() == QStringLiteral("location")) location = xml.readElementText();
                else if (xml.name() == QStringLiteral("title")) title = xml.readElementText();
                else if (xml.name() == QStringLiteral("media")) locations.append({xml.attributes().value("src").toString(), {}});
            } else if (xml.isEndElement() && xml.name() == QStringLiteral("track")) {
                if (!location.isEmpty()) locations.append({location, title}); location.clear(); title.clear();
            }
            if (locations.size() > 100000) break;
        }
        if (xml.hasError()) return {{"error", tr("The playlist XML is incomplete or invalid: %1").arg(xml.errorString())}};
    } else if (extension == "pls") {
        QMap<int, QPair<QString, QString>> ordered;
        const QRegularExpression field("^(File|Title)([0-9]+)=(.*)$", QRegularExpression::CaseInsensitiveOption);
        for (const auto &line : text.split('\n')) {
            const auto match = field.match(line.trimmed()); if (!match.hasMatch()) continue;
            const auto index = match.captured(2).toInt();
            if (index < 1 || index > 100000) return {{"error", tr("Playlist track numbers must be between 1 and 100,000.")}};
            if (match.captured(1).compare("file", Qt::CaseInsensitive) == 0) ordered[index].first = match.captured(3);
            else ordered[index].second = match.captured(3);
        }
        for (const auto &entry : ordered) if (!entry.first.isEmpty()) locations.append(entry);
    } else if (extension == "m3u" || extension == "m3u8") {
        QString title;
        for (auto line : text.split('\n')) {
            if (canceled()) return {};
            line = line.trimmed();
            if (line.startsWith("#EXTINF:", Qt::CaseInsensitive)) { title = line.section(',', 1); continue; }
            if (line.isEmpty() || line.startsWith('#')) continue;
            locations.append({line, title}); title.clear();
            if (locations.size() > 100000) break;
        }
    } else return {{"error", tr("Choose an M3U, M3U8, PLS, XSPF, or WPL playlist.")}};
    if (locations.size() > 100000) return {{"error", tr("Import up to 100,000 playlist entries at a time.")}};
    QVariantList rows; int missing = 0;
    const QRegularExpression drive("^[A-Za-z]:[/\\\\]");
    for (const auto &entry : locations) {
        if (canceled()) return {};
        const QString original = entry.first.trimmed(); QString path; bool remote = false;
        if (original.startsWith("file:", Qt::CaseInsensitive)) path = QUrl(original).toLocalFile();
        else if (drive.match(original).hasMatch() || original.startsWith("\\\\")) path = normalized(original);
        else if (!QUrl(original).scheme().isEmpty()) remote = true;
        else path = playlistInfo.absoluteDir().absoluteFilePath(normalized(extension == "xspf" ? QUrl::fromPercentEncoding(original.toUtf8()) : original));
        const bool found = !remote && audioFile(path);
        const auto resolved = found ? QUrl::fromLocalFile(QFileInfo(path).canonicalFilePath()).toString() : QString();
        rows.append(QVariantMap{{"original", original}, {"path", normalized(path)},
            {"title", entry.second.isEmpty() ? normalized(original).section('/', -1) : entry.second},
            {"fileUrl", resolved}, {"status", remote ? "unsupported" : found ? "ready" : "missing"}, {"selected", found}});
        if (!found && !remote) ++missing;
    }
    if (rows.isEmpty()) return {{"error", tr("This playlist contains no track entries.")}};
    if (!searchFolder.isEmpty() && missing) {
        if (!searchFolder.isLocalFile() || !QFileInfo(searchFolder.toLocalFile()).isDir()) return {{"error", tr("Choose an existing local music folder to match moved files.")}};
        QHash<QString, QStringList> candidates;
        QDirIterator iterator(searchFolder.toLocalFile(), QDir::Files | QDir::NoSymLinks | QDir::Readable, QDirIterator::Subdirectories);
        int count = 0;
        while (iterator.hasNext()) {
            if (canceled()) return {};
            const auto path = iterator.next();
            if (!formats.contains(QFileInfo(path).suffix(), Qt::CaseInsensitive)) continue;
            if (++count > 500000) return {{"error", tr("Choose a smaller search folder with fewer than 500,000 audio files.")}};
            candidates[QFileInfo(path).fileName().normalized(QString::NormalizationForm_KC).toCaseFolded()].append(path);
        }
        for (auto &value : rows) {
            if (canceled()) return {};
            auto row = value.toMap(); if (row.value("status") != "missing") continue;
            const auto original = components(row.value("path").toString()); if (original.isEmpty()) continue;
            const auto matching = candidates.value(original.last()); QString best; int score = 0, ties = 0;
            for (const auto &candidate : matching) {
                const auto parts = components(candidate); int common = 0;
                while (common < qMin(original.size(), parts.size()) && original[original.size() - common - 1] == parts[parts.size() - common - 1]) ++common;
                if (common > score) { score = common; best = candidate; ties = 1; } else if (common == score) ++ties;
            }
            if (!best.isEmpty() && ties == 1) {
                row["fileUrl"] = QUrl::fromLocalFile(QFileInfo(best).canonicalFilePath()).toString(); row["status"] = "matched"; row["selected"] = true;
            } else if (ties > 1) row["status"] = "ambiguous";
            value = row;
        }
    }
    QVariantMap result{{"name", playlistInfo.completeBaseName()}, {"count", rows.size()}, {"selected", 0},
        {"ready", 0}, {"matched", 0}, {"missing", 0}, {"ambiguous", 0}, {"unsupported", 0}};
    for (const auto &value : rows) {
        const auto row = value.toMap(); const auto status = row.value("status").toString();
        result[status] = result.value(status).toInt() + 1;
        if (row.value("selected").toBool()) result["selected"] = result.value("selected").toInt() + 1;
    }
    result["entries"] = rows; return result;
}
void PlaylistMigration::select(int index, bool selected)
{
    if (m_busy) return;
    auto row = m_rows.row(index);
    if (row.isEmpty() || row.value("fileUrl").toString().isEmpty() || row.value("selected").toBool() == selected) return;
    row["selected"] = selected; m_rows.update(index, row); m_selected += selected ? 1 : -1;
    m_summary["selected"] = m_selected; emit changed();
}
bool PlaylistMigration::resolve(int index, const QUrl &file)
{
    if (m_busy || !file.isLocalFile() || !audioFile(file.toLocalFile())) return false;
    auto row = m_rows.row(index); if (row.isEmpty() || row.value("status") == "unsupported") return false;
    const auto previous = row.value("status").toString();
    m_summary[previous] = m_summary.value(previous).toInt() - 1; m_summary["matched"] = m_summary.value("matched").toInt() + 1;
    if (!row.value("selected").toBool()) ++m_selected;
    row["status"] = "matched"; row["selected"] = true;
    row["fileUrl"] = QUrl::fromLocalFile(QFileInfo(file.toLocalFile()).canonicalFilePath()).toString();
    m_rows.update(index, row); m_summary["selected"] = m_selected; emit changed(); return true;
}
QString PlaylistMigration::importSelected(const QString &name)
{
    if (m_busy || !m_library || !m_selected) return {};
    QStringList urls;
    for (int i = 0; i < m_rows.rowCount(); ++i) {
        const auto row = m_rows.row(i); if (row.value("selected").toBool()) urls.append(row.value("fileUrl").toString());
    }
    const auto id = m_library->importResolvedPlaylist(name, urls);
    if (!id.isEmpty()) emit imported(id, name.simplified());
    return id;
}
