#include "collectionfeatures.h"
#include "librarydatabase.h"
#include "librarymodels.h"
#include "tracklibrary.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

class CollectionFeaturesTests : public QObject
{
    Q_OBJECT
    static QVariantMap song(const QString &path, const QString &genre = "Jazz", int year = 2012, bool favorite = true, int plays = 3)
    {
        return {{"fileUrl", QUrl::fromLocalFile(path).toString()}, {"title", QFileInfo(path).completeBaseName()}, {"artist", "Artist"},
            {"artistIds", QStringList{"artist:a"}}, {"albumId", "album:a"}, {"album", "Album"}, {"genre", genre}, {"year", QString::number(year)},
            {"favorite", favorite}, {"playCount", plays}, {"duration", 90000LL}, {"addedAt", 123456LL}, {"trackSort", "001"}, {"probed", true}};
    }
    static bool seed(const QString &settingsFile, const QVariantList &rows, const QStringList &roots)
    {
        QSettings settings(settingsFile, QSettings::IniFormat);
        settings.setValue("library/databaseReady", true); settings.setValue("library/folders", roots);
        QStringList favorites, files;
        for (const auto &entry : rows) { const auto row = entry.toMap(); if (row.value("favorite").toBool()) favorites.append(row.value("fileUrl").toString()); files.append(QUrl(row.value("fileUrl").toString()).toLocalFile()); }
        settings.setValue("library/favorites", favorites); settings.setValue("library/files", files); settings.sync();
        LibraryDatabase db(settingsFile + ".library.sqlite");
        return db.valid() && db.replace(rows, {QVariantMap{{"id", "album:a"}, {"name", "Album"}}}, {QVariantMap{{"id", "artist:a"}, {"name", "Artist"}}}, {});
    }
private slots:
    void rulesMatchInMemoryAndPagedSql()
    {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const auto settingsFile = temp.filePath("rules.ini");
        QVariantList rows;
        for (int i = 0; i < 260; ++i) rows.append(song(temp.filePath(QStringLiteral("Music/Album/song%1.mp3").arg(i)), i % 3 == 0 ? "Rock" : "JAZZ", i % 7 == 0 ? 0 : 2000 + i % 20, i % 4 != 0, i % 9));
        QVERIFY(seed(settingsFile, rows, {temp.filePath("Music")}));
        TrackListModel library(settingsFile, false);
        QCOMPARE(library.residentTrackCount(), 0);
        const QVariantMap rules{{"genre", "Jazz"}, {"yearFrom", 2005}, {"yearTo", 2015}, {"favorite", "yes"}, {"minPlays", 2}, {"maxPlays", 5}};
        const auto id = library.saveSmartPlaylist({}, "My jazz", rules); QVERIFY(!id.isEmpty());
        LibraryEntryModel source; source.synchronize(rows, "fileUrl");
        LibraryViewModel memory; memory.setSourceModel(&source); memory.setSortKey("title");
        memory.setCustomSmartPlaylists({QVariantMap{{"id", id}, {"rules", rules}}});
        memory.setCollectionKind("customSmart"); memory.setCollectionId(id);
        auto *paged = library.songs(); paged->setSortKey("title"); paged->setCollectionKind("customSmart"); paged->setCollectionId(id);
        QVERIFY(paged->count() > 0); QCOMPARE(paged->fileUrls(), memory.fileUrls());
        QCOMPARE(library.customSmartPlaylists().first().toMap().value("count").toInt(), paged->count());
        QCOMPARE(library.residentTrackCount(), 0);
        paged->setCollectionId({});
        for (int row = 0; row < paged->count(); row += 64) QVERIFY(!paged->get(row).isEmpty());
        QVERIFY(paged->cachedEntryCount() <= 192); QCOMPARE(library.residentTrackCount(), 0);
        paged->setCollectionId(id);
        const auto firstUrl = paged->get(0).value("fileUrl").toString(); library.setTracksFavorite({firstUrl}, false);
        QCOMPARE(paged->count(), memory.count() - 1);
        QVERIFY(!paged->fileUrls().contains(firstUrl));
        QVERIFY(library.deleteSmartPlaylist(id)); QCOMPARE(paged->count(), 0);
    }

    void invalidRulesNeverOverwriteSavedPlaylist()
    {
        QTemporaryDir temp; const auto settingsFile = temp.filePath("validation.ini");
        QVERIFY(seed(settingsFile, {song(temp.filePath("Music/track.mp3"))}, {temp.filePath("Music")}));
        TrackListModel library(settingsFile, false); QSignalSpy errors(&library, &TrackListModel::importError);
        const auto id = library.saveSmartPlaylist({}, "Never played", {{"maxPlays", 0}}); QVERIFY(!id.isEmpty());
        QVERIFY(library.saveSmartPlaylist(id, "Broken", {{"yearFrom", 2020}, {"yearTo", 2000}}).isEmpty());
        QVERIFY(library.saveSmartPlaylist({}, "Broken", {{"minPlays", -1}}).isEmpty());
        QVERIFY(library.saveSmartPlaylist({}, "Broken", {{"favorite", "sometimes"}}).isEmpty());
        QVERIFY(library.saveSmartPlaylist({}, "never played", {}).isEmpty());
        QVERIFY(library.saveSmartPlaylist("missing", "Missing", {}).isEmpty());
        QCOMPARE(errors.count(), 5); QCOMPARE(library.customSmartPlaylists().size(), 1);
        QCOMPARE(library.customSmartPlaylists().first().toMap().value("name").toString(), QString("Never played"));
        QString error; QVERIFY(!CollectionRules::matches(song(temp.filePath("Music/no-year.mp3"), "Jazz", 0), {{"yearTo", 2020}}));
        CollectionRules::normalize({{"maxPlays", "not a number"}}, &error); QVERIFY(!error.isEmpty());
    }
    void portableDateTagsDriveYearRulesAndRefreshAfterEdits()
    {
        QTemporaryDir temp; const auto config = temp.filePath("date-rules.ini");
        auto dated = song(temp.filePath("song.mp3"), "Jazz", 1990); dated["date"] = "2024-07-12";
        QVERIFY(seed(config, {dated}, {})); TrackListModel library(config, false);
        const auto id = library.saveSmartPlaylist({}, "This decade", {{"yearFrom", 2020}, {"yearTo", 2029}}); QVERIFY(!id.isEmpty());
        library.songs()->setCollectionKind("customSmart"); library.songs()->setCollectionId(id); QCOMPARE(library.songs()->count(), 1);
        LibraryEntryModel source; source.synchronize({dated}, "fileUrl"); LibraryViewModel memory; memory.setSourceModel(&source);
        memory.setCustomSmartPlaylists({QVariantMap{{"id", id}, {"rules", QVariantMap{{"yearFrom", 2020}, {"yearTo", 2029}}}}});
        memory.setCollectionKind("customSmart"); memory.setCollectionId(id); QCOMPARE(memory.count(), 1);
        dated["date"] = ""; source.synchronize({dated}, "fileUrl"); QCOMPARE(memory.count(), 0);
        QVERIFY(!CollectionRules::matches(dated, {{"yearTo", 2000}}));
    }

    void folderNavigationRespectsDirectoryBoundaries()
    {
        QTemporaryDir temp; const auto settingsFile = temp.filePath("folders.ini");
        const auto music = temp.filePath(QString::fromUtf8("Music 🎵")), music2 = music + "2";
        const QVariantList rows{song(music + "/Artist/Album/a.mp3"), song(music + "/Artist/Album/Disc 2/b.mp3"), song(music + "/Artist Two/c.mp3"), song(music + "/root.mp3"), song(music2 + "/outside.mp3")};
        QVERIFY(seed(settingsFile, rows, {music, music2})); TrackListModel library(settingsFile, false);
        QCOMPARE(library.folderBrowser().value("folders").toList().size(), 2);
        const auto browser = library.folderBrowser(music);
        QCOMPARE(browser.value("count").toInt(), 4); QCOMPARE(browser.value("folders").toList().size(), 2); QVERIFY(browser.value("parent").toString().isEmpty());
        const auto child = library.folderBrowser(music + "/Artist/Album");
        QCOMPARE(child.value("folders").toList().size(), 1); QCOMPARE(child.value("parent").toString(), music + "/Artist");
        auto *songs = library.songs(); songs->setCollectionKind("folder"); songs->setCollectionId(music);
        QCOMPARE(songs->count(), 4); QVERIFY(!songs->fileUrls().contains(rows.last().toMap().value("fileUrl").toString()));
        QCOMPARE(library.tracksForGroup("folder", music + "/Artist").size(), 2);
        LibraryEntryModel source; source.synchronize(rows, "fileUrl"); LibraryViewModel memory; memory.setSourceModel(&source);
        memory.setCollectionKind("folder"); memory.setCollectionId(music); QCOMPARE(memory.count(), songs->count());
        QCOMPARE(library.residentTrackCount(), 0);
    }

    void pinsAndBookmarksPersistAndFollowMovedFiles()
    {
        QTemporaryDir temp; const auto settingsFile = temp.filePath("collections.ini");
        const auto path = temp.filePath("Music/Album/song.mp3"), movedPath = temp.filePath("New Music/Album/song.mp3");
        const auto url = QUrl::fromLocalFile(path).toString(), movedUrl = QUrl::fromLocalFile(movedPath).toString();
        QVERIFY(seed(settingsFile, {song(path)}, {temp.filePath("Music")}));
        QString bookmarkId, smartId;
        {
            TrackListModel library(settingsFile, false);
            QVERIFY(library.setCollectionPinned("album", "album:a", "Old album name", true));
            QVERIFY(!library.setCollectionPinned("album", "missing-album", "Missing", true));
            QVERIFY(library.setCollectionPinned("folder", temp.filePath("Music"), "Music", true));
            smartId = library.saveSmartPlaylist({}, "Jazz", {{"genre", "jazz"}}); QVERIFY(!smartId.isEmpty());
            QVERIFY(library.setCollectionPinned("customSmart", smartId, "Jazz", true));
            QVERIFY(library.setCollectionPinned("album", "album:a", "Duplicate", true)); QCOMPARE(library.pinnedCollections().size(), 3);
            QCOMPARE(library.pinnedCollections().first().toMap().value("name").toString(), QString("Album"));
            bookmarkId = library.saveTrackBookmark(url, 999999, "End"); QVERIFY(!bookmarkId.isEmpty());
            QCOMPARE(library.trackBookmarks(url).first().toMap().value("position").toLongLong(), 89999LL);
            QCOMPARE(library.saveTrackBookmark(url, -20, "Start", bookmarkId), bookmarkId);
            QCOMPARE(library.trackBookmarks(url).first().toMap().value("position").toLongLong(), 0LL);
            QCOMPARE(library.trackBookmarks(url).first().toMap().value("name").toString(), QString("Start"));
            QVERIFY(!library.saveTrackBookmark(url, 60000, "A minute").isEmpty());
            QVERIFY(library.saveTrackBookmark("file:///missing.mp3", 10, "Missing").isEmpty());
            library.remapCollectionUrls({{url, movedUrl}});
            QVERIFY(library.trackBookmarks(url).isEmpty()); QCOMPARE(library.trackBookmarks(movedUrl).size(), 2);
            QVERIFY(library.collectionPinned("folder", temp.filePath("New Music")));
            QVERIFY(library.removeTrackBookmark(movedUrl, bookmarkId)); QCOMPARE(library.trackBookmarks(movedUrl).size(), 1);
        }
        {
            TrackListModel library(settingsFile, false);
            QCOMPARE(library.pinnedCollections().size(), 3); QCOMPARE(library.trackBookmarks(movedUrl).size(), 1);
            QCOMPARE(library.customSmartPlaylists().first().toMap().value("id").toString(), smartId);
            QVERIFY(library.deleteSmartPlaylist(smartId)); QVERIFY(!library.collectionPinned("customSmart", smartId));
            QVERIFY(library.setCollectionPinned("album", "album:a", {}, false));
            QCOMPARE(library.pinnedCollections().size(), 1);
        }
    }
};

QTEST_MAIN(CollectionFeaturesTests)
#include "tst_collectionfeatures.moc"
