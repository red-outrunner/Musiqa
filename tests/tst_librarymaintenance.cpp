#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include "librarymaintenance.h"
#include "tracklibrary.h"

class LibraryMaintenanceTest : public QObject
{
    Q_OBJECT
    static bool writeFile(const QString &path, const QByteArray &bytes)
    {
        QDir().mkpath(QFileInfo(path).absolutePath()); QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }
    static QVariantMap song(const QString &path)
    {
        const QFileInfo file(path);
        return {{"fileUrl", QUrl::fromLocalFile(path).toString()}, {"title", file.completeBaseName()}, {"artist", "Artist"},
            {"album", "Album"}, {"albumArtist", "Artist"}, {"duration", 120000}, {"probed", true}, {"metadataVersion", TrackListModel::MetadataCacheVersion},
            {"fileTagVersion", 1}, {"size", file.size()}, {"modified", file.lastModified().toMSecsSinceEpoch()}, {"addedAt", 12345LL}};
    }
    static QVariantMap session(const QString &url, int position)
    {
        const QVariantList entries{QVariantMap{{"id", "1"}, {"fileUrl", url}}};
        return {{"queue", entries}, {"source", entries}, {"currentId", "1"}, {"position", position}, {"shuffle", true}, {"repeat", 1}, {"serial", 1}};
    }
private slots:
    void repairMovesEveryStoredReference()
    {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const auto oldFolder = temp.filePath("old"), newFolder = temp.filePath("new"), oldPath = oldFolder + "/Disc 1/Song.mp3";
        QVERIFY(writeFile(oldPath, "test audio data"));
        const auto oldUrl = QUrl::fromLocalFile(oldPath).toString(), newUrl = QUrl::fromLocalFile(newFolder + "/Disc 1/Song.mp3").toString();
        const auto config = temp.filePath("library.ini");
        QSettings settings(config, QSettings::IniFormat);
        settings.setValue("library/folders", QStringList{oldFolder}); settings.setValue("library/files", QStringList{oldPath});
        settings.setValue("library/snapshot", QVariantList{song(oldPath)}); settings.setValue("library/favorites", QStringList{oldUrl});
        settings.setValue("library/playCounts", QVariantMap{{oldUrl, 7}});
        settings.setValue("library/playlists", QVariantList{QVariantMap{{"id", "mix"}, {"name", "Mix"}, {"urls", QStringList{oldUrl}}}});
        settings.setValue("history/totals", QVariantMap{{oldUrl, 12345LL}});
        settings.setValue("history/recent", QVariantList{QVariantMap{{"fileUrl", oldUrl}, {"at", QDateTime::currentMSecsSinceEpoch()}, {"ms", 12345LL}}});
        settings.setValue("collections/bookmarks", QVariantMap{{oldUrl, QVariantList{QVariantMap{{"id", "mark"}, {"name", "Intro"}, {"position", 6000}}}}});
        settings.sync();
        TrackListModel library(config, false);
        const auto first = library.startPlaybackQueue("First", session(oldUrl, 1000)); QVERIFY(!first.isEmpty());
        const auto second = library.startPlaybackQueue("Second", session(oldUrl, 2000)); QVERIFY(!second.isEmpty());
        auto *maintenance = qobject_cast<LibraryMaintenance *>(library.maintenance()); QVERIFY(maintenance);
        QSignalSpy relinked(maintenance, &LibraryMaintenance::urlsRelinked);
        QVERIFY(QDir().rename(oldFolder, newFolder));
        const auto preview = maintenance->previewRelink(QUrl::fromLocalFile(oldFolder), QUrl::fromLocalFile(newFolder));
        QCOMPARE(preview.value("matches").toList().size(), 1); QVERIFY(maintenance->applyRelink(preview));
        QCOMPARE(relinked.size(), 1); QVERIFY(library.trackForUrl(oldUrl).isEmpty());
        const auto repaired = library.trackForUrl(newUrl); QCOMPARE(repaired.value("playCount").toInt(), 7); QVERIFY(repaired.value("favorite").toBool());
        QCOMPARE(repaired.value("addedAt").toLongLong(), 12345LL);
        QCOMPARE(library.tracksForGroup("playlist", "mix").first().toMap().value("fileUrl").toString(), newUrl);
        QVERIFY(library.activateQueue(first)); QCOMPARE(library.playbackSession().value("position").toInt(), 1000);
        QCOMPARE(library.playbackSession().value("queue").toList().first().toMap().value("fileUrl").toString(), newUrl);
        QVERIFY(library.activateQueue(second)); QCOMPARE(library.playbackSession().value("position").toInt(), 2000);
        QCOMPARE(library.playbackSession().value("shuffle").toBool(), true); QCOMPARE(library.playbackSession().value("repeat").toInt(), 1);
        settings.sync();
        QVERIFY(settings.value("history/totals").toMap().contains(newUrl)); QVERIFY(!settings.value("history/totals").toMap().contains(oldUrl));
        QVERIFY(settings.value("collections/bookmarks").toMap().contains(newUrl));
        QCOMPARE(library.folders(), QStringList{newFolder}); QVERIFY(!library.scanning());
        QVERIFY(!maintenance->applyRelink(preview)); // A used/stale preview cannot be reapplied.
    }
    void backupRestoresLibraryQueuesAndExtraSettings()
    {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const auto path = temp.filePath("Song.mp3"); QVERIFY(writeFile(path, "song"));
        const auto url = QUrl::fromLocalFile(path).toString(), config = temp.filePath("library.ini");
        QSettings settings(config, QSettings::IniFormat); settings.setValue("library/folders", QStringList{});
        settings.setValue("library/snapshot", QVariantList{song(path)}); settings.sync();
        TrackListModel library(config, false); library.toggleFavorite(url); const auto playlist = library.createPlaylist("Saved", url);
        library.startPlaybackQueue("Saved queue", session(url, 1234));
        auto *maintenance = qobject_cast<LibraryMaintenance *>(library.maintenance()); QVERIFY(maintenance);
        const QUrl backup = QUrl::fromLocalFile(temp.filePath("saved.musica-backup"));
        const QVariantMap extra{{"display", QVariantMap{{"appearance/theme", "light"}, {"view/textScale", 1.5}}}};
        QVERIFY(maintenance->createBackup(backup, extra));
        const auto preview = maintenance->inspectBackup(backup); QCOMPARE(preview.value("songs").toInt(), 1); QCOMPARE(preview.value("queues").toInt(), 1);
        library.toggleFavorite(url); QVERIFY(library.renamePlaylist(playlist, "Changed")); library.createQueue("Extra queue");
        QSignalSpy restored(maintenance, &LibraryMaintenance::restored); QVERIFY(maintenance->restoreBackup(backup));
        QCOMPARE(restored.size(), 1); QCOMPARE(restored.first().first().toMap(), extra);
        QVERIFY(library.trackForUrl(url).value("favorite").toBool()); QCOMPARE(library.playlists()->get(0).value("name").toString(), "Saved");
        QCOMPARE(library.savedQueues().size(), 1); QCOMPARE(library.playbackSession().value("position").toInt(), 1234);
        QCOMPARE(library.tracksForGroup("playlist", playlist).first().toMap().value("fileUrl").toString(), url);
        QVERIFY(!library.scanning());
        const QUrl corrupt = QUrl::fromLocalFile(temp.filePath("corrupt.musica-backup"));
        QVERIFY(writeFile(corrupt.toLocalFile(), "{\"format\":\"musica-backup\",\"version\":1,\"tracks\":[],\"settings\":{},\"queues\":{},\"extra\":{}}"));
        QVERIFY(maintenance->inspectBackup(corrupt).contains("error")); QVERIFY(!maintenance->restoreBackup(corrupt));
        QCOMPARE(library.trackCount(), 1); QVERIFY(library.trackForUrl(url).value("favorite").toBool());
        QCOMPARE(library.playbackSession().value("position").toInt(), 1234);
    }
    void duplicatesCompareBytesAndValidateSelections()
    {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const auto one = temp.filePath("one.mp3"), two = temp.filePath("two.mp3"), other = temp.filePath("other.mp3");
        QVERIFY(writeFile(one, "identical")); QVERIFY(writeFile(two, "identical")); QVERIFY(writeFile(other, "different"));
        const auto config = temp.filePath("library.ini"); QSettings settings(config, QSettings::IniFormat);
        settings.setValue("library/folders", QStringList{}); settings.setValue("library/snapshot", QVariantList{song(one), song(two), song(other)}); settings.sync();
        TrackListModel library(config, false); auto *maintenance = qobject_cast<LibraryMaintenance *>(library.maintenance()); QVERIFY(maintenance);
        QVERIFY(maintenance->findDuplicates()); QTRY_VERIFY_WITH_TIMEOUT(!maintenance->busy(), 10000);
        QTRY_COMPARE(maintenance->duplicateGroups().size(), 1);
        const auto group = maintenance->duplicateGroups().first().toMap(); QCOMPARE(group.value("tracks").toList().size(), 2);
        const auto oneUrl = QUrl::fromLocalFile(one).toString(), twoUrl = QUrl::fromLocalFile(two).toString(), otherUrl = QUrl::fromLocalFile(other).toString();
        QVERIFY(!maintenance->trashDuplicates(oneUrl, {oneUrl})); QVERIFY(!maintenance->trashDuplicates(oneUrl, {otherUrl}));
        QSignalSpy error(maintenance, &LibraryMaintenance::error);
        QVERIFY(writeFile(two, "no longer identical")); QVERIFY(maintenance->trashDuplicates(oneUrl, {twoUrl}));
        QTRY_VERIFY_WITH_TIMEOUT(!maintenance->busy(), 10000); QTRY_COMPARE(error.size(), 1);
        QVERIFY(QFileInfo(one).isFile()); QVERIFY(QFileInfo(two).isFile()); QCOMPARE(library.trackCount(), 3);
    }
    void failedDatabaseRestoreKeepsTheCurrentLibrary()
    {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const auto path = temp.filePath("Song.mp3"), config = temp.filePath("library.ini");
        QVERIFY(writeFile(path, "song")); const auto url = QUrl::fromLocalFile(path).toString();
        QSettings settings(config, QSettings::IniFormat); settings.setValue("library/folders", QStringList{});
        settings.setValue("library/snapshot", QVariantList{song(path)}); settings.sync();
        TrackListModel library(config, false); QVERIFY(library.pagedLibrary());
        auto *maintenance = qobject_cast<LibraryMaintenance *>(library.maintenance()); QVERIFY(maintenance);
        const QUrl backup = QUrl::fromLocalFile(temp.filePath("rollback.musica-backup"));
        QVERIFY(maintenance->createBackup(backup)); library.toggleFavorite(url);
        const auto queue = library.createQueue("Keep current"); QVERIFY(!queue.isEmpty());
        const auto connection = QStringLiteral("maintenance-failed-restore");
        {
            auto database = QSqlDatabase::addDatabase("QSQLITE", connection); database.setDatabaseName(config + ".library.sqlite"); QVERIFY(database.open());
            QSqlQuery query(database);
            QVERIFY(query.exec("CREATE TRIGGER reject_test_restore BEFORE INSERT ON entries WHEN NEW.kind='song' BEGIN SELECT RAISE(ABORT,'restore test'); END"));
            QVERIFY(!maintenance->restoreBackup(backup));
            QVERIFY(library.trackForUrl(url).value("favorite").toBool()); QCOMPARE(library.activeQueueId(), queue);
            QCOMPARE(library.savedQueues().size(), 2); settings.sync(); QVERIFY(settings.value("library/favorites").toStringList().contains(url));
            QVERIFY(query.exec("DROP TRIGGER reject_test_restore")); database.close();
        }
        QSqlDatabase::removeDatabase(connection);
        QVERIFY(maintenance->restoreBackup(backup)); QVERIFY(!library.trackForUrl(url).value("favorite").toBool());
        QCOMPARE(library.savedQueues().size(), 1);
    }
};

QTEST_GUILESS_MAIN(LibraryMaintenanceTest)
#include "tst_librarymaintenance.moc"
