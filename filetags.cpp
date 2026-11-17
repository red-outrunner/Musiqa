#include "filetags.h"
#include "taglibpath.h"

#include <audioproperties.h>
#include <fileref.h>
#include <tfile.h>
#include <tpropertymap.h>
#include <tvariant.h>
#include <tag.h>
#include <wavfile.h>
#include <infotag.h>
#include <asffile.h>
#include <asftag.h>
#include <aifffile.h>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryFile>
#include <cmath>

namespace {
QString qstring(const TagLib::String &value) { return QString::fromUtf8(value.toCString(true)); }
TagLib::String tagString(const QString &value) { return TagLib::String(value.toUtf8().constData(), TagLib::String::UTF8); }
QString digest(QIODevice &file)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return hash.addData(&file) ? QString::fromLatin1(hash.result().toHex()) : QString();
}
QString fingerprint(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? digest(file) : QString();
}
bool frontCover(const TagLib::VariantMap &picture)
{
    const auto value = qstring(picture.value("pictureType").toString()).toLower().remove(QLatin1Char(' '));
    return value == QStringLiteral("frontcover") || value == QStringLiteral("cover(front)");
}
QByteArray bytes(const TagLib::ByteVector &data) { return QByteArray(data.data(), qsizetype(data.size())); }
QVariantMap failure(const QString &message) { return {{"error", message}}; }
const QMap<QString, QString> textFields{{"title", "TITLE"}, {"artist", "ARTIST"}, {"album", "ALBUM"}, {"albumArtist", "ALBUMARTIST"},
    {"genre", "GENRE"}, {"date", "DATE"}, {"composer", "COMPOSER"}, {"comment", "COMMENT"}};
QString first(const TagLib::PropertyMap &properties, const QString &key)
{
    const auto values = properties.value(tagString(key));
    return values.isEmpty() ? QString() : qstring(values.front());
}

// AIFF's legacy NAME/AUTH/ANNO chunks coexist with ID3. Some players prefer
// these chunks, so update any existing ones alongside the portable ID3 tags.
class LegacyAiffFile : public TagLib::RIFF::AIFF::File
{
public:
    using TagLib::RIFF::AIFF::File::File;
    void synchronize(const QVariantMap &changes)
    {
        const QMap<QByteArray, QString> fields{{"NAME", "title"}, {"AUTH", "artist"}, {"ANNO", "comment"}};
        for (int i = int(chunkCount()) - 1; i >= 0; --i) {
            const auto name = bytes(chunkName(unsigned(i)));
            const auto found = fields.constFind(name);
            if (found == fields.cend() || !changes.contains(found.value())) continue;
            const auto value = changes.value(found.value()).toString().toUtf8();
            if (value.isEmpty()) removeChunk(unsigned(i));
            else setChunkData(unsigned(i), TagLib::ByteVector(value.constData(), unsigned(value.size())));
        }
    }
};
QString cacheCover(const QByteArray &data, const QString &directory)
{
    if (data.isEmpty() || data.size() > 25 * 1024 * 1024 || directory.isEmpty()) return {};
    QBuffer buffer; buffer.setData(data); buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer); reader.setDecideFormatFromContent(true);
    const auto size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() > 80000000) return {};
    reader.setScaledSize(size.scaled(480, 480, Qt::KeepAspectRatio));
    const auto image = reader.read();
    if (image.isNull()) return {};
    QByteArray jpeg; QBuffer output(&jpeg); output.open(QIODevice::WriteOnly);
    if (!image.save(&output, "JPEG") || !QDir().mkpath(directory)) return {};
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(jpeg, QCryptographicHash::Sha256).toHex());
    const QString path = QDir(directory).filePath(hash + QStringLiteral(".jpg"));
    if (!QFileInfo::exists(path)) {
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(jpeg) != jpeg.size() || !file.commit()) return {};
    }
    return QUrl::fromLocalFile(path).toString();
}
QVariantMap readImpl(const QString &path, const QString &artworkDirectory, bool withFingerprint)
{
    const QFileInfo info(path);
    if (!info.isFile() || !info.isReadable()) return failure(QStringLiteral("The audio file isn't readable."));
    const auto encoded = TagLibPath::encode(path);
    TagLib::FileRef file(TagLibPath::data(encoded), true, TagLib::AudioProperties::Accurate);
    if (file.isNull() || !file.file() || !file.file()->isValid())
        return failure(QStringLiteral("This file doesn't have a tag format Musica can edit. Raw AAC files need an M4A container for portable metadata."));
    auto properties = file.properties();
    if (auto *wave = dynamic_cast<TagLib::RIFF::WAV::File *>(file.file()); wave && wave->hasInfoTag()) {
        const auto legacy = wave->InfoTag()->properties();
        for (auto it = legacy.begin(); it != legacy.end(); ++it) if (!properties.contains(it->first)) properties.replace(it->first, it->second);
    }
    QVariantMap fields;
    for (auto it = textFields.cbegin(); it != textFields.cend(); ++it) fields[it.key()] = first(properties, it.value());
    if (auto *basic = file.tag()) {
        const QMap<QString, QString> fallback{{"title", qstring(basic->title())}, {"artist", qstring(basic->artist())}, {"album", qstring(basic->album())},
            {"genre", qstring(basic->genre())}, {"comment", qstring(basic->comment())}, {"date", basic->year() ? QString::number(basic->year()) : QString()}};
        for (auto it = fallback.cbegin(); it != fallback.cend(); ++it) if (fields.value(it.key()).toString().isEmpty()) fields[it.key()] = it.value();
    }
    for (const auto &kind : {QStringLiteral("track"), QStringLiteral("disc")}) {
        const QString number = first(properties, kind.toUpper() + QStringLiteral("NUMBER"));
        fields[kind + QStringLiteral("Number")] = number.section(QLatin1Char('/'), 0, 0);
        QString total = number.contains(QLatin1Char('/')) ? number.section(QLatin1Char('/'), 1, 1) : first(properties, kind.toUpper() + QStringLiteral("TOTAL"));
        if (total.isEmpty()) total = first(properties, QStringLiteral("TOTAL") + kind.toUpper() + QLatin1Char('S'));
        fields[kind + QStringLiteral("Total")] = total;
    }
    if (fields.value("trackNumber").toString().isEmpty() && file.tag() && file.tag()->track()) fields["trackNumber"] = QString::number(file.tag()->track());
    if (fields.value("trackNumber").toString().isEmpty()) {
        if (auto *wave = dynamic_cast<TagLib::RIFF::WAV::File *>(file.file()); wave && wave->hasInfoTag()) {
            const auto number = qstring(wave->InfoTag()->fieldText("ITRK"));
            fields["trackNumber"] = number.section(QLatin1Char('/'), 0, 0);
            if (number.contains(QLatin1Char('/'))) fields["trackTotal"] = number.section(QLatin1Char('/'), 1, 1);
        }
    }
    if (fields.value("date").toString().isEmpty()) fields["date"] = first(properties, "YEAR");
    const auto pictures = file.complexProperties("PICTURE");
    QByteArray cover;
    for (const auto &picture : pictures) {
        if (cover.isEmpty() || frontCover(picture)) cover = bytes(picture.value("data").toByteVector());
        if (frontCover(picture)) break;
    }
    QVariantMap result{{"fields", fields}, {"fileUrl", QUrl::fromLocalFile(info.canonicalFilePath()).toString()}, {"path", info.canonicalFilePath()},
        {"format", info.suffix().toUpper()}, {"writable", info.isWritable() && QFileInfo(info.absolutePath()).isWritable()},
        {"modified", info.lastModified().toMSecsSinceEpoch()}, {"size", info.size()}, {"embeddedCover", !pictures.isEmpty()},
        {"coverHash", QString::fromLatin1(QCryptographicHash::hash(cover, QCryptographicHash::Sha256).toHex())},
        {"artwork", cacheCover(cover, artworkDirectory)}, {"tagVersion", FileTags::Version}};
    if (auto *audio = file.audioProperties()) {
        result["duration"] = audio->lengthInMilliseconds(); result["sampleRate"] = audio->sampleRate(); result["channels"] = audio->channels();
    }
    if (withFingerprint) result["revision"] = fingerprint(path);
    if (withFingerprint && result.value("revision").toString().isEmpty()) return failure(QStringLiteral("Couldn't read a complete revision of the song."));
    return result;
}
}

QVariantMap FileTags::read(const QString &path, const QString &directory, bool withFingerprint)
{
    auto result = readImpl(path, directory, withFingerprint);
    if (withFingerprint && !result.contains("error")) {
        const QFileInfo after(path);
        if (result.value("modified").toLongLong() != after.lastModified().toMSecsSinceEpoch() || result.value("size").toLongLong() != after.size())
            return failure(QStringLiteral("The song changed while reading it. Open the editor again."));
    }
    return result;
}

QVariantMap FileTags::write(const QString &path, const QVariantMap &changes, const QString &coverAction,
    const QUrl &coverFile, const QString &expectedRevision, const QString &directory)
{
    const QFileInfo info(path);
    if (!info.isFile() || !info.isReadable() || !info.isWritable() || !QFileInfo(info.absolutePath()).isWritable())
        return failure(QStringLiteral("The song or its folder is read-only. Nothing was changed."));
    if (expectedRevision.isEmpty()) return failure(QStringLiteral("Open the metadata editor before saving."));
    if (!QStringList{"keep", "replace", "remove"}.contains(coverAction)) return failure(QStringLiteral("Choose whether to keep, replace, or remove the artwork."));
    QVariantMap normalized;
    for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
        const QString value = it.value().toString().trimmed();
        if (!textFields.contains(it.key()) && !QStringList{"trackNumber", "trackTotal", "discNumber", "discTotal"}.contains(it.key())) return failure(QStringLiteral("Unknown metadata field: %1").arg(it.key()));
        if (value.contains(QChar(0)) || value.size() > (it.key() == "comment" ? 32000 : 4096)) return failure(QStringLiteral("One of the metadata values is too long or contains a null character."));
        if (QStringList{"trackNumber", "trackTotal", "discNumber", "discTotal"}.contains(it.key()) && !value.isEmpty()) {
            bool ok = false; const int number = value.toInt(&ok);
            if (!ok || number < 0 || number > 9999) return failure(QStringLiteral("Track and disc numbers must be 0–9999, or blank."));
        }
        normalized[it.key()] = value;
    }
    QByteArray coverData; QString mime;
    if (coverAction == "replace") {
        const QFileInfo imageInfo(coverFile.toLocalFile());
        if (!coverFile.isLocalFile() || !imageInfo.isFile() || imageInfo.size() <= 0 || imageInfo.size() > 20 * 1024 * 1024)
            return failure(QStringLiteral("Choose a local JPEG or PNG cover image up to 20 MB."));
        QImageReader reader(imageInfo.absoluteFilePath()); reader.setDecideFormatFromContent(true);
        const auto size = reader.size();
        const auto imageFormat = reader.format();
        if (!size.isValid() || qint64(size.width()) * size.height() > 40000000 || (imageFormat != "jpeg" && imageFormat != "png"))
            return failure(QStringLiteral("Choose a valid JPEG or PNG cover, up to 40 megapixels."));
        reader.setScaledSize(size.scaled(480, 480, Qt::KeepAspectRatio));
        if (reader.read().isNull()) return failure(QStringLiteral("The selected cover image could not be read."));
        QFile image(imageInfo.absoluteFilePath());
        if (!image.open(QIODevice::ReadOnly)) return failure(QStringLiteral("The cover file isn't readable."));
        coverData = image.readAll(); mime = imageFormat == "png" ? QStringLiteral("image/png") : QStringLiteral("image/jpeg");
    }
    const QString lockName = QDir(info.absolutePath()).filePath(QStringLiteral(".musica-tags-") + QString::fromLatin1(QCryptographicHash::hash(info.fileName().toUtf8(), QCryptographicHash::Sha256).toHex()) + QStringLiteral(".lock"));
    QLockFile lock(lockName);
    if (!lock.tryLock(0)) return failure(QStringLiteral("This song is already being edited."));
    const auto before = readImpl(path, {}, true);
    if (before.contains("error")) return before;
    if (before.value("revision").toString() != expectedRevision) return failure(QStringLiteral("The song changed since you opened the editor. Reopen it before saving. Nothing was changed."));
    QTemporaryFile temporary(QDir(info.absolutePath()).filePath(QStringLiteral(".musica-edit-XXXXXX.") + info.suffix()));
    if (!temporary.open()) return failure(QStringLiteral("Couldn't create a temporary copy beside the song: %1").arg(temporary.errorString()));
    QFile original(path);
    if (!original.open(QIODevice::ReadOnly)) return failure(QStringLiteral("Couldn't read the original song."));
    while (!original.atEnd()) {
        const auto chunk = original.read(1024 * 1024);
        if (chunk.isEmpty() || temporary.write(chunk) != chunk.size()) return failure(QStringLiteral("Couldn't copy the song. Check available disk space. Nothing was changed."));
    }
    original.close(); temporary.close();
    const auto encoded = TagLibPath::encode(temporary.fileName());
    {
        TagLib::FileRef file(TagLibPath::data(encoded), true, TagLib::AudioProperties::Accurate);
        if (file.isNull() || !file.file()->isValid()) return failure(QStringLiteral("The temporary song could not be opened for tagging."));
        auto properties = file.properties();
        // WAV INFO tags can exist independently of ID3; retain their basic
        // values when creating the first ID3 tag for richer metadata/artwork.
        if (auto *wave = dynamic_cast<TagLib::RIFF::WAV::File *>(file.file()); wave && wave->hasInfoTag()) {
            for (const auto &key : {"title", "artist", "album", "genre", "comment", "date"}) {
                const auto tag = tagString(textFields.value(key));
                const auto value = before.value("fields").toMap().value(key).toString();
                if (!properties.contains(tag) && !value.isEmpty()) properties.replace(tag, TagLib::StringList(tagString(value)));
            }
        }
        for (auto it = normalized.cbegin(); it != normalized.cend(); ++it) {
            if (!textFields.contains(it.key())) continue;
            const auto key = tagString(textFields.value(it.key())); const QString value = it.value().toString();
            if (value.isEmpty()) properties.erase(key);
            else properties.replace(key, TagLib::StringList(tagString(value)));
        }
        const auto oldFields = before.value("fields").toMap();
        for (const auto &kind : {QStringLiteral("track"), QStringLiteral("disc")}) {
            if (!normalized.contains(kind + "Number") && !normalized.contains(kind + "Total")) continue;
            const QString number = normalized.value(kind + "Number", oldFields.value(kind + "Number")).toString();
            const QString total = normalized.value(kind + "Total", oldFields.value(kind + "Total")).toString();
            const auto key = tagString(kind.toUpper() + QStringLiteral("NUMBER"));
            if (number.isEmpty() && total.isEmpty()) properties.erase(key);
            else properties.replace(key, TagLib::StringList(tagString((number.isEmpty() ? QStringLiteral("0") : number) + (total.isEmpty() ? QString() : QLatin1Char('/') + total))));
            for (const auto &alias : {kind.toUpper() + QStringLiteral("TOTAL"), QStringLiteral("TOTAL") + kind.toUpper() + QLatin1Char('S')}) {
                if (!properties.contains(tagString(alias))) continue;
                if (total.isEmpty()) properties.erase(tagString(alias)); else properties.replace(tagString(alias), TagLib::StringList(tagString(total)));
            }
        }
        const auto unsupported = file.setProperties(properties);
        if (!unsupported.isEmpty()) return failure(QStringLiteral("This format can't store one of those metadata values. Nothing was changed: %1").arg(qstring(unsupported.toString())));
        if (auto *asf = dynamic_cast<TagLib::ASF::File *>(file.file())) {
            const auto attributes = static_cast<const TagLib::ASF::Tag *>(asf->tag())->attributeListMap();
            const QMap<QString, QString> aliases{{"title", "title"}, {"author", "artist"}, {"artist", "artist"}, {"albumtitle", "album"}, {"album", "album"},
                {"albumartist", "albumArtist"}, {"genre", "genre"}, {"year", "date"}, {"date", "date"}, {"composer", "composer"},
                {"description", "comment"}, {"comment", "comment"}};
            for (auto it = attributes.begin(); it != attributes.end(); ++it) {
                auto name = qstring(it->first).toLower().remove(QStringLiteral("wm/")).remove(QLatin1Char('_'));
                const auto field = aliases.value(name);
                if (field.isEmpty() || !normalized.contains(field)) continue;
                const auto value = normalized.value(field).toString();
                if (value.isEmpty()) asf->tag()->removeItem(it->first);
                else asf->tag()->setAttribute(it->first, TagLib::ASF::Attribute(tagString(value)));
            }
            // Content Description fields are separate from extended ASF
            // attributes; keep both views consistent for other WMA players.
            if (normalized.contains("title")) asf->tag()->setTitle(tagString(normalized.value("title").toString()));
            if (normalized.contains("artist")) asf->tag()->setArtist(tagString(normalized.value("artist").toString()));
            if (normalized.contains("comment")) asf->tag()->setComment(tagString(normalized.value("comment").toString()));
        }
        if (auto *wave = dynamic_cast<TagLib::RIFF::WAV::File *>(file.file()); wave && wave->hasInfoTag()) {
            auto infoProperties = wave->InfoTag()->properties();
            for (auto it = normalized.cbegin(); it != normalized.cend(); ++it) {
                if (!textFields.contains(it.key())) continue;
                const auto tag = tagString(textFields.value(it.key()));
                if (it.value().toString().isEmpty()) infoProperties.erase(tag);
                else infoProperties.replace(tag, TagLib::StringList(tagString(it.value().toString())));
            }
            wave->InfoTag()->setProperties(infoProperties);
            if (normalized.contains("trackNumber") || normalized.contains("trackTotal")) {
                const auto values = properties.value("TRACKNUMBER");
                const auto value = values.isEmpty() ? TagLib::String() : values.front();
                wave->InfoTag()->setFieldText("ITRK", value);
                if (!wave->InfoTag()->fieldText("IPRT").isEmpty()) wave->InfoTag()->setFieldText("IPRT", value);
            }
        }
        if (coverAction != "keep") {
            TagLib::List<TagLib::VariantMap> pictures;
            if (coverAction == "replace") {
                pictures.append({{"data", TagLib::ByteVector(coverData.constData(), unsigned(coverData.size()))}, {"pictureType", "Front Cover"}, {"mimeType", tagString(mime)}, {"description", "Cover"}});
                const auto existing = file.complexProperties("PICTURE");
                bool hasTypedFront = false;
                for (const auto &picture : existing) hasTypedFront |= frontCover(picture);
                int index = 0;
                for (const auto &picture : existing) {
                    const auto type = qstring(picture.value("pictureType").toString());
                    const bool defaultFront = index++ == 0 && !hasTypedFront && (type.isEmpty() || type == "Other");
                    if (!frontCover(picture) && !defaultFront) pictures.append(picture);
                }
            }
            if (!file.setComplexProperties("PICTURE", pictures)) return failure(QStringLiteral("This format doesn't support embedding cover artwork. Nothing was changed."));
        }
        if (!file.save()) return failure(QStringLiteral("The metadata could not be saved to the temporary copy. Nothing was changed."));
    }
    if (QStringList{"aif", "aiff"}.contains(info.suffix().toLower())) {
        LegacyAiffFile legacy(TagLibPath::data(encoded), false);
        if (!legacy.isValid()) return failure(QStringLiteral("The edited AIFF file couldn't be reopened. The original was kept."));
        legacy.synchronize(normalized);
    }
    const auto verified = readImpl(temporary.fileName(), {}, false);
    if (verified.contains("error")) return failure(QStringLiteral("The edited file failed verification. The original was kept."));
    const auto verifiedFields = verified.value("fields").toMap();
    for (auto it = normalized.cbegin(); it != normalized.cend(); ++it) {
        const QString expected = it.value().toString(), actual = verifiedFields.value(it.key()).toString();
        const bool numeric = QStringList{"trackNumber", "trackTotal", "discNumber", "discTotal"}.contains(it.key());
        if ((numeric ? std::abs(expected.toDouble() - actual.toDouble()) > 0.001 : expected != actual))
            return failure(QStringLiteral("The format couldn't retain the edited %1 value. The original was kept.").arg(it.key()));
    }
    if (verified.value("sampleRate") != before.value("sampleRate") || verified.value("channels") != before.value("channels")
        || std::abs(verified.value("duration").toLongLong() - before.value("duration").toLongLong()) > 1)
        return failure(QStringLiteral("The edited file's audio properties changed unexpectedly. The original was kept."));
    if (coverAction == "replace" && verified.value("coverHash").toString() != QString::fromLatin1(QCryptographicHash::hash(coverData, QCryptographicHash::Sha256).toHex()))
        return failure(QStringLiteral("The embedded cover failed verification. The original was kept."));
    if (coverAction == "remove" && verified.value("embeddedCover").toBool()) return failure(QStringLiteral("The embedded cover could not be removed. The original was kept."));
    if (coverAction == "keep" && verified.value("coverHash") != before.value("coverHash")) return failure(QStringLiteral("Existing artwork changed unexpectedly. The original was kept."));
    QSaveFile destination(path); destination.setDirectWriteFallback(false);
    QFile edited(temporary.fileName());
    if (!edited.open(QIODevice::ReadOnly) || !destination.open(QIODevice::WriteOnly)) return failure(QStringLiteral("Couldn't replace the song safely. The original was kept."));
    while (!edited.atEnd()) {
        const auto chunk = edited.read(1024 * 1024);
        if (chunk.isEmpty() || destination.write(chunk) != chunk.size()) return failure(QStringLiteral("Couldn't finish writing the song. The original was kept."));
    }
    if (fingerprint(path) != expectedRevision) return failure(QStringLiteral("Another application changed this song while saving. The original was kept."));
    if (!destination.commit()) return failure(QStringLiteral("Couldn't commit the edited song: %1").arg(destination.errorString()));
    return read(path, directory, true);
}

void FileTags::applyToTrack(QVariantMap &track, const QVariantMap &tags, bool clearMissing)
{
    if (tags.contains("error")) return;
    const auto fields = tags.value("fields").toMap();
    track["fileTagFields"] = fields;
    for (const auto &key : {"title", "artist", "album", "genre", "date", "composer", "comment"}) {
        const auto value = fields.value(key).toString();
        if (!value.isEmpty() || clearMissing) track[key] = value;
    }
    if (clearMissing) {
        if (track.value("title").toString().isEmpty()) track["title"] = QFileInfo(QUrl(track.value("fileUrl").toString()).toLocalFile()).completeBaseName();
        if (track.value("artist").toString().isEmpty()) track["artist"] = QStringLiteral("Unknown artist");
        if (track.value("album").toString().isEmpty()) track["album"] = QStringLiteral("Unknown album");
    }
    track["tagAlbumArtist"] = fields.value("albumArtist");
    for (const auto &kind : {QStringLiteral("track"), QStringLiteral("disc")}) {
        const QString key = kind == "track" ? QStringLiteral("tagTrackNumber") : QStringLiteral("tagDiscNumber");
        const auto value = fields.value(kind + "Number").toString();
        if (value.isEmpty()) track.remove(key); else track[key] = value;
        track[kind + "Total"] = fields.value(kind + "Total").toString().toInt();
    }
    if (tags.value("duration").toLongLong() > 0) track["duration"] = tags.value("duration");
    if (!tags.value("artwork").toString().isEmpty()) {
        track["artwork"] = tags.value("artwork");
        track["artworkKey"] = QFileInfo(QUrl(tags.value("artwork").toString()).toLocalFile()).completeBaseName();
    } else if (clearMissing) {
        track["artwork"] = QString(); track.remove("artworkKey");
        const auto folder = QFileInfo(QUrl(track.value("fileUrl").toString()).toLocalFile()).dir();
        for (const auto &name : {"cover.jpg", "cover.png", "folder.jpg", "folder.png", "Cover.jpg", "Cover.png"}) {
            QFile cover(folder.filePath(name));
            if (cover.open(QIODevice::ReadOnly)) { track["artwork"] = QUrl::fromLocalFile(cover.fileName()).toString(); track["artworkKey"] = digest(cover); break; }
        }
    }
    if (tags.contains("embeddedCover")) track["embeddedCover"] = tags.value("embeddedCover");
    track["fileTagVersion"] = Version;
}
