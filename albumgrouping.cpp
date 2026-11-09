#include "albumgrouping.h"

#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <numeric>

namespace {
QString normalizedText(QString text)
{
    text = text.normalized(QString::NormalizationForm_KC).toCaseFolded();
    text.remove(QChar(0x200b));
    text.remove(QChar(0xfeff));
    text.replace(QChar(0x2019), QLatin1Char('\''));
    text.replace(QChar(0x2013), QLatin1Char('-'));
    text.replace(QChar(0x2014), QLatin1Char('-'));
    return text.simplified();
}

QString nameKey(const QString &text)
{
    static const QRegularExpression punctuation(QStringLiteral("[\\s\\p{P}]+"));
    return normalizedText(text).remove(punctuation);
}

QString titleKey(QString text)
{
    // Credit suffixes vary between tags and downloaded folder names.
    static const QRegularExpression credit(QStringLiteral("\\s*[\\[(](?:(?:mixed\\s*(?:&|and)\\s*)?compiled|curated|presented)\\s+by\\b[^\\])]*[\\])]\\s*$"), QRegularExpression::CaseInsensitiveOption);
    text.remove(credit);
    return nameKey(text);
}

bool genericTitle(const QString &key)
{
    static const QSet<QString> generic = {QString(), QStringLiteral("singles"), QStringLiteral("unknown"),
        QStringLiteral("unknownalbum"), QStringLiteral("untitled"), QStringLiteral("greatesthits"),
        QStringLiteral("bestof"), QStringLiteral("live"), QStringLiteral("album"),
        QStringLiteral("compilation"), QStringLiteral("soundtrack")};
    return generic.contains(key);
}

bool variousArtists(const QString &artist)
{
    static const QSet<QString> names = {QStringLiteral("variousartists"), QStringLiteral("various"),
        QStringLiteral("va"), QStringLiteral("originalsoundtrack")};
    return names.contains(nameKey(artist));
}

bool compilationCredit(const QVariantMap &track)
{
    static const QRegularExpression credit(QStringLiteral("\\b(?:compilation|compiled\\s+by|curated\\s+by|soundtrack|various\\s+artists)\\b"), QRegularExpression::CaseInsensitiveOption);
    return track.value(QStringLiteral("tagCompilation")).toBool()
        || variousArtists(track.value(QStringLiteral("tagAlbumArtist")).toString())
        || credit.match(track.value(QStringLiteral("album")).toString()).hasMatch();
}

int discFromDirectory(const QDir &directory)
{
    static const QRegularExpression disc(QStringLiteral("^(?:cd|disc|disk)[\\s._-]*(\\d+)$"), QRegularExpression::CaseInsensitiveOption);
    const auto match = disc.match(directory.dirName());
    return match.hasMatch() ? match.captured(1).toInt() : 0;
}
}

void AlbumGrouping::updateIdentity(QVariantMap &track)
{
    QString artist = track.value(QStringLiteral("artist")).toString().simplified();
    const QString hint = track.value(QStringLiteral("folderArtist")).toString();
    static const QRegularExpression trackNumber(QStringLiteral("^\\d{1,3}[\\s._-]+(.+)$"));
    const auto match = trackNumber.match(artist);
    if (match.hasMatch() && !hint.isEmpty() && nameKey(match.captured(1)) == nameKey(hint))
        artist = hint;
    track.insert(QStringLiteral("artist"), artist);
    QString albumArtist = track.value(QStringLiteral("tagAlbumArtist")).toString().simplified();
    if (albumArtist.isEmpty()) {
        albumArtist = hint.isEmpty() ? artist : hint;
        static const QRegularExpression featured(QStringLiteral("\\s+(?:feat\\.?|ft\\.?|featuring)\\s+.*$"), QRegularExpression::CaseInsensitiveOption);
        albumArtist.remove(featured);
    }
    const QString title = titleKey(track.value(QStringLiteral("album")).toString());
    const QString base = nameKey(albumArtist) + QChar(0x1f) + title;
    track.insert(QStringLiteral("baseAlbumArtist"), albumArtist);
    track.insert(QStringLiteral("baseAlbumId"), base);
    track.insert(QStringLiteral("albumTitleKey"), title);
    track.insert(QStringLiteral("albumArtist"), albumArtist);
    if (track.value(QStringLiteral("albumId")).toString().isEmpty())
        track.insert(QStringLiteral("albumId"), base);
    track.insert(QStringLiteral("artistId"), nameKey(artist));
    track.insert(QStringLiteral("artStyle"), int(qHash(title) % 4));
}

void AlbumGrouping::updateTrackOrder(QVariantMap &track, const QString &filePath)
{
    const QFileInfo file(filePath);
    const QString name = file.completeBaseName();
    static const QRegularExpression paired(QStringLiteral("^(\\d{1,2})-(\\d{2,3})[\\s._-]+"));
    static const QRegularExpression numbered(QStringLiteral("^(\\d{1,3})[\\s._-]+"));
    const auto pair = paired.match(name);
    const auto number = numbered.match(name);
    QDir albumDir = file.dir();
    const int folderDisc = discFromDirectory(albumDir);
    if (folderDisc > 0) albumDir.setPath(QFileInfo(albumDir.absolutePath()).absolutePath());
    int disc = track.value(QStringLiteral("tagDiscNumber")).toInt();
    if (disc <= 0) disc = folderDisc > 0 ? folderDisc : pair.hasMatch() ? pair.captured(1).toInt() : 1;
    int index = -1;
    const QVariant tagged = track.value(QStringLiteral("tagTrackNumber"));
    if (tagged.isValid()) {
        bool ok = false;
        index = tagged.toString().section(QLatin1Char('/'), 0, 0).toInt(&ok);
        if (!ok || index < 0) index = -1;
    }
    if (index < 0)
        index = pair.hasMatch() ? pair.captured(2).toInt() : number.hasMatch() ? number.captured(1).toInt() : -1;
    track.insert(QStringLiteral("discNumber"), disc);
    track.insert(QStringLiteral("trackNumber"), index);
    track.insert(QStringLiteral("albumDirectory"), albumDir.absolutePath());
    track.insert(QStringLiteral("trackSort"), QStringLiteral("%1:%2:%3").arg(disc, 6, 10, QLatin1Char('0'))
        .arg(index < 0 ? 999999 : index, 6, 10, QLatin1Char('0'))
        .arg(normalizedText(track.value(QStringLiteral("title")).toString())));
}

QHash<QString, QString> AlbumGrouping::reconcile(QVariantList &tracks)
{
    const int count = tracks.size();
    QVector<int> parents(count);
    std::iota(parents.begin(), parents.end(), 0);
    const auto root = [&parents](int row) {
        while (parents[row] != row) {
            parents[row] = parents[parents[row]];
            row = parents[row];
        }
        return row;
    };
    const auto join = [&](int a, int b) { parents[root(b)] = root(a); };
    QHash<QString, int> evidence;
    QHash<QString, QList<int>> titles;
    QList<QVariantMap> rows;
    rows.reserve(count);
    for (int i = 0; i < count; ++i) {
        auto track = tracks[i].toMap();
        if (!track.contains(QStringLiteral("baseAlbumId"))) updateIdentity(track);
        rows.append(track);
        const QString title = track.value(QStringLiteral("albumTitleKey")).toString();
        titles[title].append(i);
        QStringList keys{QStringLiteral("artist:") + track.value(QStringLiteral("baseAlbumId")).toString()};
        const QString directory = track.value(QStringLiteral("albumDirectory")).toString();
        const QString artwork = track.value(QStringLiteral("artworkKey")).toString();
        const bool named = !title.isEmpty() && title != QStringLiteral("singles")
            && title != QStringLiteral("unknown") && title != QStringLiteral("unknownalbum");
        if (!directory.isEmpty() && named) keys.append(QStringLiteral("folder:") + title + QChar(0x1f) + directory);
        if (!artwork.isEmpty()) keys.append(QStringLiteral("cover:") + title + QChar(0x1f) + artwork);
        for (const auto &key : keys) {
            const auto found = evidence.constFind(key);
            if (found != evidence.constEnd()) join(i, found.value());
            else evidence.insert(key, i);
        }
    }
    for (auto it = titles.cbegin(); it != titles.cend(); ++it) {
        const auto &members = it.value();
        if (members.size() < 2 || genericTitle(it.key())) continue;
        bool compilation = false;
        bool complementary = true;
        QHash<QString, QString> positions;
        QSet<QString> contributors;
        for (int row : members) {
            const auto &track = rows[row];
            compilation |= compilationCredit(track);
            const QString base = track.value(QStringLiteral("baseAlbumId")).toString();
            contributors.insert(base);
            const int number = track.value(QStringLiteral("trackNumber"), -1).toInt();
            if (number < 0) { complementary = false; continue; }
            const QString position = QString::number(track.value(QStringLiteral("discNumber"), 1).toInt())
                + QLatin1Char(':') + QString::number(number);
            const auto found = positions.constFind(position);
            if (found != positions.constEnd() && found.value() != base) complementary = false;
            positions.insert(position, base);
        }
        if (compilation || (complementary && contributors.size() > 1))
            for (int row : members) join(members.first(), row);
    }
    QHash<int, QList<int>> groups;
    for (int i = 0; i < count; ++i) groups[root(i)].append(i);
    QHash<QString, QString> aliases, bases;
    for (const auto &members : groups) {
        QString id;
        QMap<QString, QString> taggedArtists, baseArtists, folderArtists;
        bool allHaveFolder = true;
        for (int row : members) {
            const auto &track = rows[row];
            const QString base = track.value(QStringLiteral("baseAlbumId")).toString();
            if (id.isEmpty() || base < id) id = base;
            const QString tag = track.value(QStringLiteral("tagAlbumArtist")).toString().simplified();
            const QString artist = track.value(QStringLiteral("baseAlbumArtist")).toString();
            const QString folder = track.value(QStringLiteral("folderArtist")).toString();
            if (!tag.isEmpty()) taggedArtists.insert(nameKey(tag), tag);
            baseArtists.insert(nameKey(artist), artist);
            if (!folder.isEmpty()) folderArtists.insert(nameKey(folder), folder);
            else allHaveFolder = false;
        }
        const QString display = taggedArtists.size() == 1 ? taggedArtists.first()
            : baseArtists.size() == 1 ? baseArtists.first()
            : allHaveFolder && folderArtists.size() == 1 ? folderArtists.first()
            : QStringLiteral("Various artists");
        for (int row : members) {
            auto track = rows[row];
            aliases.insert(track.value(QStringLiteral("albumId")).toString(), id);
            bases.insert(track.value(QStringLiteral("baseAlbumId")).toString(), id);
            track.insert(QStringLiteral("albumId"), id);
            track.insert(QStringLiteral("albumArtist"), display);
            track.insert(QStringLiteral("isCompilation"), baseArtists.size() > 1);
            tracks[row] = track;
        }
    }
    for (auto it = bases.cbegin(); it != bases.cend(); ++it) aliases.insert(it.key(), it.value());
    return aliases;
}

bool AlbumGrouping::trackLessThan(const QVariant &left, const QVariant &right)
{
    const auto a = left.toMap();
    const auto b = right.toMap();
    const int discA = qMax(1, a.value(QStringLiteral("discNumber"), 1).toInt());
    const int discB = qMax(1, b.value(QStringLiteral("discNumber"), 1).toInt());
    if (discA != discB) return discA < discB;
    const int trackA = a.value(QStringLiteral("trackNumber"), -1).toInt();
    const int trackB = b.value(QStringLiteral("trackNumber"), -1).toInt();
    const int positionA = trackA < 0 ? 999999 : trackA;
    const int positionB = trackB < 0 ? 999999 : trackB;
    if (positionA != positionB) return positionA < positionB;
    const int title = QString::localeAwareCompare(a.value(QStringLiteral("title")).toString(), b.value(QStringLiteral("title")).toString());
    return title == 0 ? a.value(QStringLiteral("fileUrl")).toString() < b.value(QStringLiteral("fileUrl")).toString() : title < 0;
}
