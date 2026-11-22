#include <QtTest>
#include <QTemporaryDir>
#include "librarymodels.h"
#include "searchfilters.h"
#include "albumgrouping.h"
#include "tracklibrary.h"
#include "displaysettings.h"

class LibraryUsabilityTests : public QObject
{
    Q_OBJECT
    static QVariantList fixtureTracks(const QString &root = QStringLiteral("/cached"))
    {
        QVariantList tracks;
        for (int i = 0; i < 3; ++i) {
            const auto path = QDir(root).filePath(QString("Artist/Album/Song %1.flac").arg(i));
            QVariantMap track{{"fileUrl", QUrl::fromLocalFile(path).toString()}, {"title", QString("Song %1").arg(i)},
                {"artist", i == 1 ? "Björk" : "Artist"}, {"albumArtist", "Artist"}, {"album", "Album"}, {"genre", "Ambient"},
                {"duration", 120000LL}, {"probed", true}, {"metadataVersion", TrackListModel::MetadataCacheVersion}};
            AlbumGrouping::updateTrackOrder(track, path); AlbumGrouping::updateIdentity(track); tracks.append(track);
        }
        return tracks;
    }
    static void seed(const QString &file, const QString &root = QStringLiteral("/cached"))
    {
        QSettings settings(file, QSettings::IniFormat); settings.setValue("library/folders", QStringList{});
        settings.setValue("library/snapshot", fixtureTracks(root)); settings.sync();
    }
private slots:
    void collectionViewsSearchGloballyAndFindAnArtistsAlbumsWithoutScanning()
    {
        QTemporaryDir temp; const auto file = temp.filePath("library.ini"); seed(file);
        { TrackListModel migrate(file, false); }
        TrackListModel library(file, false); QObject owner;
        auto *songs = library.createCollectionView("song", &owner);
        auto *albums = library.createCollectionView("album", &owner);
        QVERIFY(songs && albums); QCOMPARE(songs->count(), 3);
        library.songs()->setFavoritesOnly(true); QCOMPARE(library.songs()->count(), 0);
        songs->setSearch("BJÖRK"); QCOMPARE(songs->count(), 1);
        const auto track = songs->get(0);
        const auto artistId = track.value("artistIds").toStringList().first();
        albums->setCollectionKind("artist"); albums->setCollectionId(artistId); QCOMPARE(albums->count(), 1);
        const auto entries = library.artistAlbumTracks(artistId, albums->get(0).value("id").toString());
        QCOMPARE(entries.size(), 1); QCOMPARE(entries.first().toMap().value("fileUrl"), track.value("fileUrl"));
        QVERIFY(!library.createCollectionView("invalid", &owner)); QVERIFY(!library.createCollectionView("song", nullptr));
        QVERIFY(library.artistAlbumTracks("unknown", "unknown").isEmpty());
        QCOMPARE(library.residentTrackCount(), 0); QVERIFY(!library.scanning());
    }

    void metadataSuggestionsUseSavedNamesWithoutScanningOrLoadingSongs()
    {
        QTemporaryDir temp; QVERIFY(temp.isValid()); const auto file = temp.filePath("library.ini"); seed(file);
        { TrackListModel migrate(file, false); }
        TrackListModel library(file, false);
        QCOMPARE(library.residentTrackCount(), 0);
        QSignalSpy scanned(&library, &TrackListModel::scanningChanged);
        // Suggestions are independent of the collection currently filtered on screen.
        library.artists()->setSearch("no visible artists"); QCOMPARE(library.artists()->count(), 0);
        QCOMPARE(library.metadataSuggestions("artist", "JÖR"), QStringList{QStringLiteral("Björk")});
        QCOMPARE(library.metadataSuggestions("albumArtist", "rti"), QStringList{QStringLiteral("Artist")});
        QCOMPARE(library.metadataSuggestions("album", " ALB "), QStringList{QStringLiteral("Album")});
        QCOMPARE(library.metadataSuggestions("genre", "MBI"), QStringList{QStringLiteral("Ambient")});
        QVERIFY(library.metadataSuggestions("title", "Song").isEmpty());
        QVERIFY(library.metadataSuggestions("artist", " ").isEmpty());
        QVERIFY(library.metadataSuggestions("album", "%").isEmpty());
        QVERIFY(library.metadataSuggestions("artist", "' OR 1=1 --").isEmpty());
        QCOMPARE(library.residentTrackCount(), 0); QVERIFY(!library.scanning()); QCOMPARE(scanned.size(), 0);

        LibraryDatabase database(temp.filePath("suggestions.sqlite")); QVERIFY(database.valid());
        QVariantList albums;
        for (int i = 0; i < 30; ++i) albums.append(QVariantMap{{"id", QString::number(i)}, {"name", QString("Live album %1").arg(i)}, {"subtitle", "Solo Artist"}});
        QVERIFY(database.replace({}, albums, {}, {}));
        QCOMPARE(database.matchingMetadataNames("album", "live").size(), 8);
        QVERIFY(database.matchingMetadataNames("album", "Solo Artist").isEmpty());
    }

    void combinedFilters_data()
    {
        QTest::addColumn<bool>("paged");
        QTest::newRow("memory") << false; QTest::newRow("sqlite") << true;
    }
    void combinedFilters()
    {
        QFETCH(bool, paged);
        QTemporaryDir temp; QVERIFY(temp.isValid());
        LibraryDatabase database(temp.filePath("library.sqlite")); QVERIFY(database.valid());
        QVariantList tracks;
        for (int i = 0; i < 256; ++i) {
            QVariantMap track{{"fileUrl", QString("file:///unopened/%1.flac").arg(i)},
            {"title", QString("Song %1").arg(i)}, {"artist", i % 2 ? "Björk" : "Other"}, {"album", "Atmosphere"},
            {"genre", "Ambient"}, {"year", "2015"}, {"duration", 180000LL + i * 1000}, {"favorite", i % 3 == 0}};
            AlbumGrouping::updateTrackOrder(track, QString("/unopened/%1.flac").arg(i)); AlbumGrouping::updateIdentity(track); tracks.append(track);
        }
        LibraryEntryModel source; source.synchronize(tracks, "fileUrl");
        QVERIFY(database.replace(tracks, {}, {}, {}));
        QCOMPARE(database.matchingSongUrls({"file:///unopened/0.flac", "file:///unopened/1.flac", "file:///unopened/1.flac"}, "BJÖRK"), QStringList{"file:///unopened/1.flac"});
        LibraryViewModel view;
        if (paged) view.setDatabase(&database, "song"); else view.setSourceModel(&source);
        view.setFilters({{"artist", "BJÖRK"}, {"album", "atmos"}, {"genre", "ambient"}, {"yearMin", 2010}, {"yearMax", 2020},
            {"durationMin", 200000LL}, {"durationMax", 220000LL}, {"favorite", "yes"}});
        QCOMPARE(view.count(), 4);
        for (int i = 0; i < view.count(); ++i) {
            const auto track = view.get(i); QVERIFY(track.value("favorite").toBool()); QCOMPARE(track.value("artist").toString(), QString("Björk"));
        }
        view.setSearch("Song 27"); QCOMPARE(view.count(), 1); QVERIFY(view.get(0).value("fileUrl").toString().endsWith("/27.flac"));
        view.setSearch({}); view.setFilters({{"favorite", "no"}}); QCOMPARE(view.count(), 170);
        const auto accepted = view.filters(); view.setFilters({{"durationMin", -1}}); QCOMPARE(view.filters(), accepted);
        view.setFilters({{"yearMin", 2025}, {"yearMax", 2020}}); QCOMPARE(view.filters(), accepted);
        view.setFilters({}); QCOMPARE(view.count(), 256);
        if (paged) QVERIFY(view.cachedEntryCount() <= 64);
    }
    void playlistUndoRestoresNamesMembershipAndOrder()
    {
        QTemporaryDir temp; const auto file = temp.filePath("library.ini"); seed(file);
        TrackListModel library(file, false); QCOMPARE(library.trackCount(), 3);
        const auto tracks = fixtureTracks(); QStringList urls; for (const auto &track : tracks) urls.append(track.toMap().value("fileUrl").toString());
        QSignalSpy edits(&library, &TrackListModel::playlistEditRecorded);
        const auto id = library.createPlaylistFromUrls("Original", urls); QVERIFY(!id.isEmpty()); QCOMPARE(edits.size(), 1);
        const auto creation = edits.last()[1].toString();
        QVERIFY(library.renamePlaylist(id, "Renamed")); const auto rename = edits.last()[1].toString();
        QVERIFY(library.movePlaylistTrack(id, urls[2], urls[0])); const auto reorder = edits.last()[1].toString();
        QVERIFY(library.removeFromPlaylist(id, urls[1])); const auto removal = edits.last()[1].toString();
        QVERIFY(library.deletePlaylist(id)); const auto deletion = edits.last()[1].toString(); QCOMPARE(library.playlists()->count(), 0);
        QVERIFY(library.undoPlaylistEdit(deletion)); QCOMPARE(library.tracksForGroup("playlist", id).size(), 2);
        QVERIFY(library.undoPlaylistEdit(removal)); QCOMPARE(library.tracksForGroup("playlist", id).size(), 3);
        QVERIFY(library.undoPlaylistEdit(reorder));
        const auto restored = library.tracksForGroup("playlist", id); QCOMPARE(restored[0].toMap().value("fileUrl").toString(), urls[0]);
        QVERIFY(library.undoPlaylistEdit(rename)); QCOMPARE(library.playlists()->get(0).value("name").toString(), QString("Original"));
        QVERIFY(library.undoPlaylistEdit(creation)); QCOMPARE(library.playlists()->count(), 0); QCOMPARE(library.trackCount(), 3);
        QVERIFY(!library.undoPlaylistEdit(creation)); QCOMPARE(edits.size(), 5);
    }
    void queueSearchAndInactiveQueueUndoPreserveDuplicatesAndActivePlayback()
    {
        QTemporaryDir temp; const auto file = temp.filePath("library.ini"), root = temp.filePath("Music"); seed(file, root);
        const auto tracks = fixtureTracks(root);
        for (const auto &track : tracks) {
            const auto path = QUrl(track.toMap().value("fileUrl").toString()).toLocalFile(); QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
            QFile audio(path); QVERIFY(audio.open(QIODevice::WriteOnly)); audio.write("fixture");
        }
        TrackListModel library(file, false);
        const auto first = tracks[0].toMap().value("fileUrl").toString(), second = tracks[1].toMap().value("fileUrl").toString();
        const QVariantList queue{QVariantMap{{"id", "one"}, {"fileUrl", first}}, QVariantMap{{"id", "two"}, {"fileUrl", second}}, QVariantMap{{"id", "again"}, {"fileUrl", second}}};
        const QVariantMap before{{"queue", queue}, {"source", queue}, {"currentId", "two"}, {"position", 12345LL}};
        const auto bankA = library.startPlaybackQueue("A", before); QVERIFY(!bankA.isEmpty());
        const auto bankB = library.startPlaybackQueue("B", {{"queue", QVariantList{queue[0]}}, {"source", QVariantList{queue[0]}}, {"currentId", "one"}, {"position", 2345LL}});
        QVERIFY(!bankB.isEmpty()); const auto active = library.playbackSession();
        QVERIFY(library.restoreQueueEdit(bankA, before)); QCOMPARE(library.activeQueueId(), bankB); QCOMPARE(library.playbackSession(), active);
        QCOMPARE(library.queueSearchUrls("BJÖRK", {first, second, second}), QStringList({second, second}));
        QCOMPARE(library.queueSearchUrls("no matches", {first, second}).size(), 0);
        QVERIFY(library.activateQueue(bankA)); QCOMPARE(library.playbackSession().value("queue").toList().size(), 3);
        QCOMPARE(library.playbackSession().value("currentId").toString(), QString("two"));
        QCOMPARE(library.playbackSession().value("position").toLongLong(), 12345LL);
    }
    void viewStateAndColumnsPersistAndRejectInvalidInput()
    {
        QTemporaryDir temp; const auto file = temp.filePath("library.ini"); seed(file);
        const QVariantMap page{{"page", "Songs"}, {"kind", ""}, {"id", ""}, {"name", ""}, {"artist", ""}, {"search", "Song"}, {"sort", "Artist"},
            {"filters", QVariantMap{{"genre", "ambient"}}}, {"scroll", QVariantMap{{"songs", 321.0}}}};
        const QVariantMap state{{"version", 1}, {"pages", QVariantMap{{"Songs||", page}}}, {"lastKey", "Songs||"}, {"albumViewStyle", "list"}, {"artistSortKey", "duration"}};
        {
            TrackListModel library(file, false); QVERIFY(library.saveViewState(state)); const auto accepted = library.savedViewState();
            auto bad = state; bad["version"] = 999; QVERIFY(!library.saveViewState(bad)); QCOMPARE(library.savedViewState(), accepted);
        }
        TrackListModel restored(file, false); QCOMPARE(restored.savedViewState().value("lastKey").toString(), QString("Songs||"));
        QCOMPARE(restored.savedViewState().value("pages").toMap().value("Songs||").toMap().value("scroll").toMap().value("songs").toDouble(), 321.0);
        QVariantList expected;
        {
            DisplaySettings display(temp.filePath("display.ini")); expected = display.songColumns();
            auto year = expected.takeAt(2).toMap(); year["visible"] = true; year["width"] = 130; expected.prepend(year);
            display.setSongColumns(expected); display.setAvoidRecentShuffle(true); display.setShuffleRecentDays(14);
            auto invalid = expected; auto item = invalid[0].toMap(); item["width"] = -1; invalid[0] = item; display.setSongColumns(invalid); QCOMPARE(display.songColumns(), expected);
        }
        DisplaySettings display(temp.filePath("display.ini")); QCOMPARE(display.songColumns(), expected); QVERIFY(display.columnsCustomized());
        QVERIFY(display.avoidRecentShuffle()); QCOMPARE(display.shuffleRecentDays(), 14);
        display.resetSongColumns(); QVERIFY(!display.columnsCustomized());
    }
    void recentShuffleUsesAudibleHistoryWithoutOpeningMusic()
    {
        QTemporaryDir temp; const auto file = temp.filePath("library.ini"); seed(file); TrackListModel library(file, false);
        const auto url = fixtureTracks()[0].toMap().value("fileUrl").toString(); QVERIFY(library.recentlyPlayedUrls().isEmpty());
        for (int step = 0; step <= 24; ++step) library.observeAudiblePlayback("heard", url, step * 500, 120000, true, step == 24, step * 500);
        QVERIFY(library.recentlyPlayedUrls().contains(url)); QVERIFY(!library.scanning());
    }
    void undoDefaultDoesNotOverrideAnExistingShortcut()
    {
        QTemporaryDir temp; const auto file = temp.filePath("display.ini");
        { QSettings settings(file, QSettings::IniFormat); settings.setValue("keyboard/shortcuts", QVariantMap{{"playPause", "Ctrl+Z"}}); settings.sync(); }
        DisplaySettings display(file); QCOMPARE(display.shortcuts().value("playPause").toString(), QString("Ctrl+Z"));
        QVERIFY(display.shortcuts().value("undo").toString().isEmpty());
        QVERIFY(!display.setShortcut("undo", "Ctrl+Z").isEmpty());
        display.resetShortcuts(); QCOMPARE(display.shortcuts().value("undo").toString(), QString("Ctrl+Z"));
    }
};
QTEST_MAIN(LibraryUsabilityTests)
#include "tst_libraryusability.moc"
