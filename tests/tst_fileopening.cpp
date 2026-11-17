#include "tracklibrary.h"
#include "filetags.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

class FileOpeningTests : public QObject
{
    Q_OBJECT
    static bool wave(const QString &path)
    {
        QDir().mkpath(QFileInfo(path).absolutePath()); QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return false;
        QDataStream out(&file); out.setByteOrder(QDataStream::LittleEndian);
        out.writeRawData("RIFF", 4); out << quint32(16036); out.writeRawData("WAVEfmt ", 8);
        out << quint32(16) << quint16(1) << quint16(1) << quint32(8000) << quint32(16000) << quint16(2) << quint16(16);
        out.writeRawData("data", 4); out << quint32(16000); out.writeRawData(QByteArray(16000, '\0').constData(), 16000);
        return file.error() == QFile::NoError;
    }
    static QVariantMap track(const QString &path)
    {
        return {{"fileUrl", QUrl::fromLocalFile(path).toString()}, {"title", QFileInfo(path).completeBaseName()}, {"artist", "Artist"},
            {"album", "Album"}, {"genre", "Jazz"}, {"duration", 1000LL}, {"probed", true},
            {"metadataVersion", TrackListModel::MetadataCacheVersion}, {"fileTagVersion", FileTags::Version}, {"artwork", QString()}, {"artStyle", 0}};
    }
    static void seed(const QString &config, const QStringList &folders, const QVariantList &tracks)
    {
        QSettings settings(config, QSettings::IniFormat); settings.setValue("library/folders", folders); settings.setValue("library/snapshot", tracks); settings.sync();
    }
    static QStringList urls(const QSignalSpy &opened, int request)
    {
        QStringList result;
        for (const auto &entry : opened[request][0].toList()) result.append(entry.toMap().value("fileUrl").toString());
        return result;
    }
private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("MusicaFileOpeningTests"); QCoreApplication::setApplicationName("FileOpening");
        QStandardPaths::setTestModeEnabled(true);
    }
    void knownSongsOpenImmediatelyWithoutScanning()
    {
        QTemporaryDir temp; QVERIFY(temp.isValid()); const auto path = temp.filePath("known # 100% é.wav"), config = temp.filePath("library.ini");
        QVERIFY(wave(path)); seed(config, {}, {track(path)});
        { TrackListModel original(config, false); QCOMPARE(original.trackCount(), 1); }
        TrackListModel library(config, false); QCOMPARE(library.residentTrackCount(), 0);
        QSignalSpy opened(&library, &TrackListModel::filesOpened), scans(&library, &TrackListModel::scanningChanged);
        library.openFiles({QUrl::fromLocalFile(path)});
        QCOMPARE(opened.size(), 1); QCOMPARE(urls(opened, 0), QStringList{QUrl::fromLocalFile(path).toString()});
        QCOMPARE(opened[0][2].toBool(), false); QVERIFY(!library.openingFiles()); QCOMPARE(scans.size(), 0); QCOMPARE(library.residentTrackCount(), 0);
    }
    void importsOnlyRequestedFilesAndKeepsIncomingOrder()
    {
        QTemporaryDir temp; const auto config = temp.filePath("library.ini"), root = temp.filePath("Music"), known = root + "/Known.wav";
        const auto outside = temp.filePath("Elsewhere/New.wav"), undiscovered = root + "/Do not discover.wav";
        QVERIFY(wave(known)); seed(config, {root}, {track(known)});
        { TrackListModel original(config, false); QCOMPARE(original.trackCount(), 1); }
        QVERIFY(wave(outside)); QVERIFY(wave(undiscovered));
        TrackListModel library(config, false); QSignalSpy opened(&library, &TrackListModel::filesOpened), scans(&library, &TrackListModel::scanningChanged);
        library.openFiles({QUrl::fromLocalFile(outside), QUrl::fromLocalFile(known), QUrl::fromLocalFile(outside)}, true);
        QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, 10000);
        QCOMPARE(urls(opened, 0), QStringList({QUrl::fromLocalFile(outside).toString(), QUrl::fromLocalFile(known).toString()}));
        QVERIFY(opened[0][2].toBool()); QCOMPARE(library.trackCount(), 2);
        QVERIFY(library.trackForUrl(QUrl::fromLocalFile(undiscovered).toString()).isEmpty());
        QCOMPARE(scans.size(), 0); QTRY_VERIFY(!library.openingFiles()); QVERIFY(!library.scanning());
    }
    void queuesImportsAndSkipsMissingFilesWhenOpeningFolders()
    {
        QTemporaryDir temp; const auto config = temp.filePath("library.ini"), root = temp.filePath("Music");
        const auto missing = root + "/missing.wav", one = root + "/One.wav", two = root + "/Sub/Two.wav";
        QVERIFY(wave(one)); QVERIFY(wave(two)); seed(config, {root}, {track(missing)});
        TrackListModel library(config, false); QSignalSpy opened(&library, &TrackListModel::filesOpened);
        library.openFiles({QUrl::fromLocalFile(one)}); library.openFiles({QUrl::fromLocalFile(two)}, true);
        QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 2, 10000);
        QCOMPARE(urls(opened, 0), QStringList{QUrl::fromLocalFile(one).toString()});
        QCOMPARE(urls(opened, 1), QStringList{QUrl::fromLocalFile(two).toString()}); QVERIFY(opened[1][2].toBool());
        library.openFiles({QUrl::fromLocalFile(root)}); QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 3, 10000);
        const auto folderUrls = urls(opened, 2); QCOMPARE(folderUrls.size(), 2);
        QVERIFY(folderUrls.contains(QUrl::fromLocalFile(one).toString())); QVERIFY(folderUrls.contains(QUrl::fromLocalFile(two).toString()));
        QVERIFY(!folderUrls.contains(QUrl::fromLocalFile(missing).toString())); QVERIFY(!library.openingFiles());
    }
    void rejectsRemoteAndUnsupportedLocations()
    {
        QTemporaryDir temp; const auto config = temp.filePath("library.ini"), text = temp.filePath("not music.txt");
        QFile file(text); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("text"); file.close(); seed(config, {}, {});
        TrackListModel library(config, false); QSignalSpy errors(&library, &TrackListModel::importError), opened(&library, &TrackListModel::filesOpened);
        library.openFiles({QUrl("https://example.com/song.mp3"), QUrl::fromLocalFile(text), QUrl::fromLocalFile(temp.filePath("missing.wav"))});
        QCOMPARE(errors.size(), 1); QCOMPARE(opened.size(), 0); QVERIFY(!library.openingFiles()); QCOMPARE(library.trackCount(), 0);
    }
};

QTEST_GUILESS_MAIN(FileOpeningTests)
#include "tst_fileopening.moc"
