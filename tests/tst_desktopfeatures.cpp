#include "albumgrouping.h"
#include "appdiagnostics.h"
#include "desktopenvironment.h"
#include "displaysettings.h"
#include "librarydatabase.h"
#include "playbackengine.h"
#include "playlistmigration.h"
#include "tracklibrary.h"
#include "windowsmediacontrols.h"
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

class DesktopCommandRecorder : public QObject
{
    Q_OBJECT
public:
    QString lastCommand;
    Q_INVOKABLE void desktopCommand(const QVariant &command, const QVariant &) { lastCommand = command.toString(); }
};

class DesktopFeatureTests : public QObject
{
    Q_OBJECT
    QTemporaryDir m_settings;
    static bool writeFile(const QString &path, const QByteArray &bytes)
    {
        QDir().mkpath(QFileInfo(path).absolutePath()); QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }
    static bool writeWave(const QString &path)
    {
        QDir().mkpath(QFileInfo(path).absolutePath()); QFile file(path); if (!file.open(QIODevice::WriteOnly)) return false;
        QDataStream stream(&file); stream.setByteOrder(QDataStream::LittleEndian);
        stream.writeRawData("RIFF", 4); stream << quint32(36 + 16000); stream.writeRawData("WAVEfmt ", 8);
        stream << quint32(16) << quint16(1) << quint16(1) << quint32(8000) << quint32(16000) << quint16(2) << quint16(16);
        stream.writeRawData("data", 4); stream << quint32(16000); stream.writeRawData(QByteArray(16000, 0).constData(), 16000);
        return stream.status() == QDataStream::Ok;
    }
private slots:
    void initTestCase()
    {
        QVERIFY(m_settings.isValid()); QCoreApplication::setOrganizationName("MusicaDesktopTests");
        QCoreApplication::setApplicationName("MusicaDesktopTests"); QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
    }
    void powerAndAccessibilityPreferencesPersistAndRestore()
    {
        const auto path = m_settings.filePath("desktop.ini"); QVariantMap backup;
        {
            DisplaySettings preferences(path);
            QCOMPARE(preferences.batterySaverMode(), QString("battery")); QVERIFY(!preferences.batterySaverActive());
            preferences.setOnBattery(true); QVERIFY(preferences.batterySaverActive()); QVERIFY(preferences.reduceEffects());
            preferences.setBatterySaverMode("off"); QVERIFY(!preferences.batterySaverActive());
            preferences.setBatterySaverMode("invalid"); QCOMPARE(preferences.batterySaverMode(), QString("off"));
            preferences.setBatterySaverMode("always"); preferences.setResumeAfterSleep(true); preferences.setPauseOnDisconnect(false);
            preferences.setHighContrast(true); preferences.setLargeControls(true); backup = preferences.exportPreferences();
        }
        DisplaySettings reopened(path); QCOMPARE(reopened.batterySaverMode(), QString("always")); QVERIFY(reopened.batterySaverActive());
        QVERIFY(reopened.resumeAfterSleep()); QVERIFY(!reopened.pauseOnDisconnect()); QVERIFY(reopened.highContrast()); QVERIFY(reopened.largeControls());
        reopened.importPreferences({}); QVERIFY(!reopened.resumeAfterSleep()); QVERIFY(reopened.pauseOnDisconnect());
        QVERIFY(!reopened.highContrast()); QVERIFY(!reopened.largeControls());
        reopened.importPreferences(backup); QVERIFY(reopened.highContrast()); QVERIFY(reopened.largeControls());
    }
    void reconnectAndWakePoliciesAvoidUnexpectedSpeakerPlayback()
    {
        QVERIFY(PlaybackEngine::shouldPauseForMissingDevice("headphones", {"speakers"}, true, true));
        QVERIFY(!PlaybackEngine::shouldPauseForMissingDevice("headphones", {"headphones", "speakers"}, true, true));
        QVERIFY(!PlaybackEngine::shouldPauseForMissingDevice("headphones", {}, false, true));
        QVERIFY(!PlaybackEngine::shouldPauseForMissingDevice("headphones", {}, true, false));
        const auto song = QUrl::fromLocalFile("/song.wav");
        QVERIFY(DesktopEnvironment::canResume(true, true, song, song, "entry", "entry", "headphones", "headphones"));
        QVERIFY(!DesktopEnvironment::canResume(true, true, song, song, "entry", "entry", "headphones", "speakers"));
        QVERIFY(!DesktopEnvironment::canResume(true, false, song, song, "entry", "entry", "headphones", "headphones"));
        QVERIFY(!DesktopEnvironment::canResume(false, true, song, song, "entry", "entry", "headphones", "headphones"));
        QVERIFY(!DesktopEnvironment::canResume(true, true, song, song, "entry", "other", "headphones", "headphones"));
        DisplaySettings preferences(m_settings.filePath("wake.ini")); PlaybackEngine player; QObject window;
        DesktopEnvironment environment(&window, &preferences, &player);
        environment.powerPropertiesChanged("other.service", {{"OnBattery", true}}, {}); QVERIFY(!preferences.onBattery());
        environment.powerPropertiesChanged("org.freedesktop.UPower", {{"OnBattery", true}}, {}); QVERIFY(preferences.onBattery());
        environment.prepareForSleep(true); QVERIFY(environment.sleeping()); environment.prepareForSleep(true);
        environment.prepareForSleep(false); QVERIFY(!environment.sleeping()); QCOMPARE(player.playbackState(), QMediaPlayer::StoppedState);
    }
    void mediaCommandsAndMetadataUseTheExistingPlaybackController()
    {
        DesktopCommandRecorder window; PlaybackEngine player;
        window.setProperty("currentUrl", "file:///song.wav"); window.setProperty("currentQueueIndex", 0); window.setProperty("queueCount", 2);
        window.setProperty("repeatMode", 0); window.setProperty("currentTrack", QVariantMap{{"title", "Song"}, {"artist", "Artist"}, {"album", "Album"}, {"duration", 120000LL}});
        WindowsMediaControls desktop(&window, nullptr, &player);
        QCOMPARE(desktop.playerProperties().value("title").toString(), QString("Song"));
        QVERIFY(desktop.playerProperties().value("canNext").toBool());
        desktop.command("next"); QCOMPARE(window.lastCommand, QString("next"));
        desktop.command("quit"); QCOMPARE(window.lastCommand, QString("next"));
        desktop.command("seek", -100); QCOMPARE(window.lastCommand, QString("next")); // No seekable song yet.
        window.setProperty("currentQueueIndex", 1); QVERIFY(!desktop.playerProperties().value("canNext").toBool());
        window.setProperty("repeatMode", 1); QVERIFY(desktop.playerProperties().value("canNext").toBool());
    }
    void measurementsDescribeThisProcessAndNeverIncludeMusicPaths()
    {
        AppDiagnostics diagnostics; diagnostics.setStartupMs(123); diagnostics.setStartupMs(456); QTest::qWait(10); diagnostics.refresh();
        QCOMPARE(diagnostics.startupMs(), 123); QVERIFY(diagnostics.memoryMiB() > 0); QVERIFY(diagnostics.cpuPercent() >= 0);
        const auto report = diagnostics.snapshot(); QVERIFY(report.value("cpuSampleMs").toLongLong() >= 1);
        const auto encoded = QJsonDocument::fromVariant(report).toJson();
        QVERIFY(!encoded.contains("file://")); QVERIFY(!encoded.contains(m_settings.path().toUtf8()));
    }
    void commonPlaylistFormatsKeepUnicodeOrderAndLiteralPercentNames_data()
    {
        QTest::addColumn<QString>("extension"); QTest::addColumn<QByteArray>("contents");
        QTest::newRow("m3u8") << QString("m3u8") << QByteArray("#EXTM3U\n#EXTINF:1,First song\nMusic/First%20.wav\nMusic/\xc3\xa9t\xc3\xa9.wav\n");
        QTest::newRow("pls") << QString("pls") << QByteArray("[playlist]\nFile2=Music/\xc3\xa9t\xc3\xa9.wav\nFile1=Music/First%20.wav\nTitle1=First song\nNumberOfEntries=2\n");
        QTest::newRow("xspf") << QString("xspf") << QByteArray("<playlist xmlns='http://xspf.org/ns/0/'><trackList><track><location>Music/First%2520.wav</location><title>First song</title></track><track><location>Music/%C3%A9t%C3%A9.wav</location></track></trackList></playlist>");
        QTest::newRow("wpl") << QString("wpl") << QByteArray("<smil><body><seq><media src='Music/First%20.wav'/><media src='Music/\xc3\xa9t\xc3\xa9.wav'/></seq></body></smil>");
        QByteArray utf16("\xff\xfe", 2);
        for (const auto character : QString::fromUtf8("#EXTM3U\nMusic/First%20.wav\nMusic/été.wav\n")) {
            utf16.append(char(character.unicode() & 255)); utf16.append(char(character.unicode() >> 8));
        }
        QTest::newRow("m3u-utf16") << QString("m3u") << utf16;
    }
    void commonPlaylistFormatsKeepUnicodeOrderAndLiteralPercentNames()
    {
        QFETCH(QString, extension); QFETCH(QByteArray, contents); QTemporaryDir temp; QVERIFY(temp.isValid());
        QVERIFY(writeWave(temp.filePath("Music/First%20.wav"))); QVERIFY(writeWave(temp.filePath(QString::fromUtf8("Music/été.wav"))));
        const auto playlist = temp.filePath("Export." + extension); QVERIFY(writeFile(playlist, contents));
        const auto preview = PlaylistMigration::inspect(QUrl::fromLocalFile(playlist)); QVERIFY2(!preview.contains("error"), qPrintable(preview.value("error").toString()));
        QCOMPARE(preview.value("selected").toInt(), 2);
        const auto rows = preview.value("entries").toList();
        QCOMPARE(QUrl(rows[0].toMap().value("fileUrl").toString()).toLocalFile(), QFileInfo(temp.filePath("Music/First%20.wav")).canonicalFilePath());
        QCOMPARE(QUrl(rows[1].toMap().value("fileUrl").toString()).toLocalFile(), QFileInfo(temp.filePath(QString::fromUtf8("Music/été.wav"))).canonicalFilePath());
    }
    void movedPlaylistPathsRequireUniqueMatchesAndImportOnlyAfterConfirmation()
    {
        QTemporaryDir temp; QVERIFY(temp.isValid()); const auto root = temp.filePath("Music");
        const auto first = root + "/Artist/Album/First.wav", other = root + "/Other/Album/First.wav", last = root + "/Artist/Album/Last.wav";
        QVERIFY(writeWave(first)); QVERIFY(writeWave(other)); QVERIFY(writeWave(last));
        const auto playlist = temp.filePath("Mix.m3u8");
        QVERIFY(writeFile(playlist, "#EXTM3U\nD:\\Old Music\\Artist\\Album\\First.wav\nFirst.wav\nD:\\Old Music\\Artist\\Album\\Last.wav\nhttps://example.invalid/stream.mp3\n"));
        QSettings settings(temp.filePath("library.ini"), QSettings::IniFormat); settings.setValue("library/folders", QStringList{}); settings.sync();
        TrackListModel library(settings.fileName(), false); auto *migration = qobject_cast<PlaylistMigration *>(library.playlistMigration()); QVERIFY(migration);
        QVERIFY(!library.scanning()); QVERIFY(migration->preview(QUrl::fromLocalFile(playlist), QUrl::fromLocalFile(root)));
        QTRY_VERIFY(!migration->busy()); QCOMPARE(migration->summary().value("matched").toInt(), 2);
        QCOMPARE(migration->summary().value("ambiguous").toInt(), 1); QCOMPARE(migration->summary().value("unsupported").toInt(), 1);
        QCOMPARE(migration->selectedCount(), 2); QCOMPARE(library.playlists()->count(), 0); QVERIFY(!library.scanning());
        QVERIFY(migration->resolve(1, QUrl::fromLocalFile(other))); QCOMPARE(migration->selectedCount(), 3);
        migration->select(1, false); QCOMPARE(migration->selectedCount(), 2);
        QSignalSpy imported(migration, &PlaylistMigration::imported); const auto id = migration->importSelected("Moved mix"); QVERIFY(!id.isEmpty());
        QCOMPARE(imported.size(), 1); QTRY_VERIFY(!library.scanning());
        const auto tracks = library.tracksForGroup("playlist", id); QCOMPARE(tracks.size(), 2);
        QCOMPARE(QUrl(tracks[0].toMap().value("fileUrl").toString()).toLocalFile(), QFileInfo(first).canonicalFilePath());
        QCOMPARE(QUrl(tracks[1].toMap().value("fileUrl").toString()).toLocalFile(), QFileInfo(last).canonicalFilePath());
        QFile source(playlist); QVERIFY(source.open(QIODevice::ReadOnly)); QVERIFY(source.readAll().contains("D:\\Old Music"));
    }
    void malformedCanceledAndStalePlaylistImportsLeaveTheLibraryAlone()
    {
        QTemporaryDir temp; QVERIFY(temp.isValid()); const auto song = temp.filePath("Song.wav"); QVERIFY(writeWave(song));
        const auto broken = temp.filePath("Broken.xspf"); QVERIFY(writeFile(broken, "<playlist><trackList>"));
        QVERIFY(PlaylistMigration::inspect(QUrl::fromLocalFile(broken)).contains("error"));
        const auto playlist = temp.filePath("Mix.m3u"); QVERIFY(writeFile(playlist, "Song.wav\n"));
        auto canceled = std::make_shared<std::atomic_bool>(true);
        QVERIFY(PlaylistMigration::inspect(QUrl::fromLocalFile(playlist), {}, canceled).isEmpty());
        QSettings settings(temp.filePath("library.ini"), QSettings::IniFormat); settings.setValue("library/folders", QStringList{}); settings.sync();
        TrackListModel library(settings.fileName(), false); auto *migration = qobject_cast<PlaylistMigration *>(library.playlistMigration());
        QVERIFY(migration->preview(QUrl::fromLocalFile(playlist))); QTRY_VERIFY(!migration->busy());
        QVERIFY(QFile::remove(song)); QVERIFY(migration->importSelected("Stale").isEmpty());
        QCOMPARE(library.playlists()->count(), 0); QVERIFY(!library.scanning());
    }
    void largeLibraryPageBenchmarks_data()
    {
        QTest::addColumn<int>("songs"); QTest::newRow("10000") << 10000; QTest::newRow("50000") << 50000;
    }
    void largeLibraryPageBenchmarks()
    {
        QFETCH(int, songs); QTemporaryDir temp; QVERIFY(temp.isValid()); const auto file = temp.filePath("library.sqlite");
        {
            LibraryDatabase database(file); QVERIFY(database.valid()); QVariantList tracks; tracks.reserve(songs);
            for (int i = 0; i < songs; ++i) {
                const auto path = temp.filePath(QString("Music/Album %1/Track %2.flac").arg(i / 10).arg(i, 5, 10, QLatin1Char('0')));
                QVariantMap track{{"fileUrl", QUrl::fromLocalFile(path).toString()}, {"title", QString("Track %1").arg(i, 5, 10, QLatin1Char('0'))},
                    {"artist", "Artist"}, {"album", QString("Album %1").arg(i / 10)}, {"duration", 180000LL}, {"probed", true}};
                AlbumGrouping::updateTrackOrder(track, path); AlbumGrouping::updateIdentity(track); tracks.append(track);
            }
            QVERIFY(database.replace(tracks, {}, {}, {}));
        }
        QElapsedTimer clock; clock.start(); LibraryDatabase database(file); QVERIFY(database.valid()); const auto openMs = clock.elapsed();
        clock.restart(); const auto ids = database.ids("song"); const auto sortMs = clock.elapsed(); QCOMPARE(ids.size(), songs);
        LibraryQuery query; query.search = QString("Track %1").arg(songs - 1, 5, 10, QLatin1Char('0'));
        clock.restart(); QCOMPARE(database.ids("song", query).size(), 1); const auto searchMs = clock.elapsed();
        LibraryEntryModel source; source.setDatabase(&database, "song"); source.queryDatabase({});
        clock.restart();
        for (int page = 0; page < 100; ++page) {
            const int index = page * (songs - 1) / 99;
            QVERIFY(source.data(source.index(index, 0), LibraryEntryModel::EntryRole).toMap().contains("title"));
        }
        const auto pageMs = clock.elapsed(); QVERIFY(source.cachedEntryCount() <= 3 * 128);
        qInfo().noquote() << "PERFORMANCE" << QJsonDocument::fromVariant(QVariantMap{{"songs", songs}, {"databaseOpenMs", openMs},
            {"titleSortMs", sortMs}, {"searchMs", searchMs}, {"hundredPageFetchesMs", pageMs}, {"cachedTrackRecords", source.cachedEntryCount()}}).toJson(QJsonDocument::Compact);
    }
};
QTEST_MAIN(DesktopFeatureTests)
#include "tst_desktopfeatures.moc"
