#include <QAudioOutput>
#include <QClipboard>
#include <QMimeData>
#include <QAbstractItemModelTester>
#include <QBuffer>
#include <QDataStream>
#if MUSICA_HAS_DBUS
#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#endif
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QImage>
#include <QCryptographicHash>
#include <QSignalSpy>
#include <QScreen>
#include <QFontInfo>
#include <QScopeGuard>
#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QWheelEvent>
#include <QtTest>

#include "tracklibrary.h"
#include "artworkcache.h"
#include "displaysettings.h"
#include "sharing.h"
#include "albumgrouping.h"
#include "mprisservice.h"
#include "audiomixer.h"
#include "filetags.h"
#include <cmath>
#include <algorithm>
#include <limits>

#if MUSICA_HAS_DBUS
class DesktopSignalRecorder : public QObject
{
    Q_OBJECT
public:
    QVariantMap values;
    QStringList changedKeys;
    qlonglong position = -1;
public slots:
    void changed(const QString &interface, const QVariantMap &changes, const QStringList &)
    {
        if (interface != QStringLiteral("org.mpris.MediaPlayer2.Player")) return;
        for (auto it = changes.cbegin(); it != changes.cend(); ++it) { values[it.key()] = it.value(); changedKeys.append(it.key()); }
    }
    void seeked(qlonglong microseconds) { position = microseconds; }
};
#endif

class CountingLibraryViewModel : public LibraryViewModel
{
public:
    mutable int comparisons = 0;
protected:
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override
    {
        ++comparisons;
        return LibraryViewModel::lessThan(left, right);
    }
};

class TrackMapReadCounter : public LibraryEntryModel
{
public:
    mutable int mapReads = 0;
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (role == EntryRole) ++mapReads;
        return LibraryEntryModel::data(index, role);
    }
};

class MusicaTests : public QObject
{
    Q_OBJECT

    QTemporaryDir m_temp;

    static bool writeWave(const QString &path, int seconds = 3, const QString &artist = {}, const QString &album = {}, int trackNumber = -1)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        QDataStream stream(&file);
        stream.setByteOrder(QDataStream::LittleEndian);
        const quint32 dataSize = 8000 * 2 * seconds;
        stream.writeRawData("RIFF", 4);
        stream << quint32(36 + dataSize);
        stream.writeRawData("WAVEfmt ", 8);
        stream << quint32(16) << quint16(1) << quint16(1) << quint32(8000)
               << quint32(16000) << quint16(2) << quint16(16);
        stream.writeRawData("data", 4);
        stream << dataSize;
        const QByteArray samples(dataSize, '\0');
        stream.writeRawData(samples.constData(), samples.size());
        if (!artist.isEmpty() || !album.isEmpty() || trackNumber >= 0) {
            QByteArray info("INFO");
            QBuffer buffer(&info);
            buffer.open(QIODevice::WriteOnly | QIODevice::Append);
            QDataStream tags(&buffer);
            tags.setByteOrder(QDataStream::LittleEndian);
            QList<QPair<QByteArray, QString>> fields{{"IART", artist}, {"IPRD", album}};
            if (trackNumber >= 0) fields.append({"ITRK", QString::number(trackNumber)});
            for (const auto &tag : fields) {
                const QByteArray value = tag.second.toUtf8() + '\0';
                tags.writeRawData(tag.first.constData(), 4);
                tags << quint32(value.size());
                tags.writeRawData(value.constData(), value.size());
                if (value.size() % 2) tags << quint8(0);
            }
            stream.writeRawData("LIST", 4);
            stream << quint32(info.size());
            stream.writeRawData(info.constData(), info.size());
            const quint32 riffSize = file.size() - 8;
            file.seek(4);
            stream << riffSize;
        }
        return file.error() == QFile::NoError;
    }

    static QQuickItem *findItem(QQuickItem *parent, const QString &name)
    {
        if (parent->objectName() == name)
            return parent;
        for (QQuickItem *child : parent->childItems()) {
            if (auto *found = findItem(child, name))
                return found;
        }
        return nullptr;
    }

    static QVector<float> audioSignal(int seconds, int rate)
    {
        QVector<float> samples(seconds * rate);
        for (int i = 0; i < samples.size(); ++i) {
            const double time = double(i) / rate;
            samples[i] = float(0.25 * std::sin(2 * 3.14159265358979323846 * 440 * time)
                + 0.10 * std::sin(2 * 3.14159265358979323846 * 220 * time));
        }
        return samples;
    }

    static bool writeSignal(const QString &path, const QVector<float> &samples, int rate)
    {
        QDir().mkpath(QFileInfo(path).absolutePath()); QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return false;
        QDataStream stream(&file); stream.setByteOrder(QDataStream::LittleEndian);
        stream.writeRawData("RIFF", 4); stream << quint32(36 + samples.size() * 2); stream.writeRawData("WAVEfmt ", 8);
        stream << quint32(16) << quint16(1) << quint16(1) << quint32(rate) << quint32(rate * 2) << quint16(2) << quint16(16);
        stream.writeRawData("data", 4); stream << quint32(samples.size() * 2);
        for (const auto sample : samples) stream << qint16(qBound(-1.0f, sample, 1.0f) * 32767);
        return file.error() == QFile::NoError;
    }

    static std::shared_ptr<PcmTrack> constantPcm(float value, int frames, int rate = 1000)
    {
        QAudioFormat format; format.setSampleRate(rate); format.setChannelCount(2); format.setSampleFormat(QAudioFormat::Float);
        auto track = std::make_shared<PcmTrack>(QUrl::fromLocalFile(QStringLiteral("/synthetic/%1.wav").arg(value)), format);
        QVector<float> data(frames * 2, value);
        if (!track->append(reinterpret_cast<const char *>(data.constData()), data.size() * sizeof(float))) return {};
        track->complete = true; return track;
    }

    static QByteArray runMediaTool(const QString &program, const QStringList &arguments, bool *ok = nullptr)
    {
        QProcess process; process.setProgram(program); process.setArguments(arguments); process.start();
        const bool success = process.waitForStarted(3000) && process.waitForFinished(15000) && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
        if (ok) *ok = success;
        return success ? process.readAllStandardOutput() : process.readAllStandardError();
    }

    static QByteArray fileBytes(const QString &path)
    {
        QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    static QVariantMap releaseTrack(const QString &album, const QString &artist, int number,
                                     const QString &path, const QString &cover = {}, const QString &albumArtist = {})
    {
        QVariantMap track{{"album", album}, {"artist", artist}, {"title", QFileInfo(path).completeBaseName()},
            {"fileUrl", QUrl::fromLocalFile(path).toString()}, {"artworkKey", cover},
            {"tagAlbumArtist", albumArtist}, {"favorite", false}};
        if (number >= 0) track.insert("tagTrackNumber", number);
        AlbumGrouping::updateTrackOrder(track, path);
        AlbumGrouping::updateIdentity(track);
        return track;
    }

    static void click(QQuickWindow *window, const QString &name)
    {
        auto *item = findItem(window->contentItem(), name);
        QVERIFY2(item, qPrintable(name));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
    }

    static bool clickLink(QQuickWindow *window, const QString &name, const QString &target = QStringLiteral("artist:0"))
    {
        auto *item = findItem(window->contentItem(), name);
        if (!item) return false;
        const auto y = item->height() / 2;
        for (int x = 0; x < int(item->width()); ++x) {
            QString link;
            if (!QMetaObject::invokeMethod(item, "linkAt", Q_RETURN_ARG(QString, link), Q_ARG(qreal, qreal(x)), Q_ARG(qreal, y))) return false;
            if (link != target) continue;
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, item->mapToScene(QPointF(x + 1, y)).toPoint());
            return true;
        }
        return false;
    }

    static QVariantList qmlList(QObject *object, const char *property)
    {
        const auto value = object->property(property);
        return value.metaType() == QMetaType::fromType<QJSValue>() ? value.value<QJSValue>().toVariant().toList() : value.toList();
    }

    static void drag(QQuickWindow *window, QQuickItem *handle, QQuickItem *target)
    {
        QVERIFY(handle && target);
        const QPoint start = handle->mapToScene(QPointF(handle->width() / 2, handle->height() / 2)).toPoint();
        const QPoint end = target->mapToScene(QPointF(12, target->height() * 0.8)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
        for (int step = 1; step <= 8; ++step) QTest::mouseMove(window, start + (end - start) * step / 8, 20);
        QVERIFY(handle->property("dragging").toBool());
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, end);
        QTest::qWait(60);
    }

private slots:
    void genresRestoreFromTagsAndUpgradeWithoutScanning()
    {
        const auto config = m_temp.filePath("genres-library.ini");
        const QStringList tags{" Jazz ", "jazz", "R&B/Soul", ""};
        const QList<qint64> durations{120000, 180000, 7200000, 90000};
        QVariantList snapshot;
        for (int i = 0; i < tags.size(); ++i) {
            auto track = releaseTrack("Album " + QString::number(i), "Artist", i + 1,
                m_temp.filePath(QString("unavailable-genres/%1.wav").arg(i)));
            track["genre"] = tags[i]; track["duration"] = durations[i]; track["addedAt"] = (i + 1) * 100LL;
            track["artwork"] = QString(); track["artStyle"] = 0; track["probed"] = true; snapshot.append(track);
        }
        QSettings settings(config, QSettings::IniFormat); settings.setValue("library/folders", QStringList{});
        settings.setValue("library/snapshot", snapshot); settings.sync();
        {
            TrackListModel library(config, false); QCOMPARE(library.genres()->count(), 3); QVERIFY(!library.scanning());
            QCOMPARE(library.genreForTag("ＪＡＺＺ").value("id").toString(), QString("genre:jazz"));
            QCOMPARE(library.tracksForGroup("genre", "genre:jazz").size(), 2);
            QCOMPARE(library.tracksForGroup("genre", "genre:r&b/soul").size(), 1);
            QCOMPARE(library.tracksForGroup("genre", library.genreForTag("").value("id").toString()).size(), 1);
        }
        // Simulate the preceding database version: songs and their cached tags
        // are present, but genre collections, membership, and search are absent.
        const auto connection = QString("pre-genres-database");
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection); db.setDatabaseName(config + ".library.sqlite"); QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("DELETE FROM entries WHERE kind='genre'"));
            QVERIFY(query.exec("DELETE FROM membership WHERE kind='genre'"));
            QVERIFY(query.exec("DELETE FROM state WHERE key IN ('genresVersion','hash/genre')"));
            QVERIFY(query.exec("UPDATE entries SET search='' WHERE kind='song'"));
        }
        QSqlDatabase::removeDatabase(connection);
        TrackListModel library(config, false); QVERIFY(library.pagedLibrary());
        QCOMPARE(library.residentTrackCount(), 0); QCOMPARE(library.genres()->count(), 3);
        QSignalSpy scanning(&library, &TrackListModel::scanningChanged);
        auto *genres = library.genres(); genres->setSortKey("count"); genres->setSortDirection("descending");
        QCOMPARE(genres->get(0).value("name").toString(), QString("Jazz")); QCOMPARE(genres->get(0).value("trackCount").toInt(), 2);
        QCOMPARE(genres->get(0).value("duration").toLongLong(), 300000LL);
        genres->setSortKey("duration"); QCOMPARE(genres->get(0).value("name").toString(), QString("R&B/Soul"));
        genres->setSortDirection("ascending"); QCOMPARE(genres->get(0).value("name").toString(), QString("Unknown genre"));
        genres->setSortKey("added"); genres->setSortDirection("descending"); QCOMPARE(genres->get(0).value("name").toString(), QString("Unknown genre"));
        genres->setSearch("jazz"); QCOMPARE(genres->count(), 1); genres->setSearch("");
        auto *songs = library.songs(); songs->setCollectionKind("genre"); songs->setCollectionId("genre:jazz"); QCOMPARE(songs->count(), 2);
        songs->setSearch("Artist"); QCOMPARE(songs->count(), 2); songs->setSearch("R&B"); QCOMPARE(songs->count(), 0);
        songs->setCollectionId(""); QCOMPARE(songs->count(), 1); songs->setSearch("Jazz"); QCOMPARE(songs->count(), 2);
        QCOMPARE(library.tracksForGroup("genre", "genre:jazz").size(), 2);
        QCOMPARE(library.residentTrackCount(), 0); QVERIFY(genres->cachedEntryCount() <= 192);
        QTest::qWait(30); QCOMPARE(scanning.size(), 0); QVERIFY(!library.scanning());
        LibraryEntryModel source; LibraryViewModel memory;
        source.synchronize(snapshot, "fileUrl"); memory.setSourceModel(&source); memory.setCollectionKind("genre"); memory.setCollectionId("genre:jazz");
        QCOMPARE(memory.count(), 2); memory.setSearch("jazz"); QCOMPARE(memory.count(), 2);
        auto changed = snapshot[0].toMap(); changed["genre"] = "Rock"; snapshot[0] = changed; source.synchronize(snapshot, "fileUrl");
        QCOMPARE(memory.count(), 1);
    }

    void genresPageSupportsSortingMenusAndPlayback_data()
    {
        QTest::addColumn<QSize>("windowSize"); QTest::addColumn<double>("textScale");
        QTest::newRow("normal") << QSize(1280, 850) << 1.0;
        QTest::newRow("large-text") << QSize(800, 600) << 1.6;
    }

    void genresPageSupportsSortingMenusAndPlayback()
    {
        QFETCH(QSize, windowSize); QFETCH(double, textScale);
        const auto folder = m_temp.filePath("genres-ui");
        const QStringList tags{"Jazz", "jazz", "Rock", ""}; const QList<int> durations{20, 40, 120, 10};
        QVariantList snapshot; QStringList jazzUrls;
        for (int i = 0; i < tags.size(); ++i) {
            const auto path = folder + QString("/%1 Song.wav").arg(i + 1);
            QVERIFY(writeWave(path, durations[i], "Artist", "Album", i + 1));
            const auto native = FileTags::read(path, folder, true);
            const auto tagged = FileTags::write(path, {{"genre", tags[i]}}, "keep", {}, native.value("revision").toString(), folder);
            QVERIFY2(!tagged.contains("error"), qPrintable(tagged.value("error").toString()));
            auto track = releaseTrack("Album", "Artist", i + 1, path);
            track["genre"] = tags[i]; track["duration"] = durations[i] * 1000LL; track["addedAt"] = (i + 1) * 100LL;
            track["artwork"] = QString(); track["artStyle"] = 0; track["probed"] = true; snapshot.append(track);
            if (i < 2) jazzUrls.append(track.value("fileUrl").toString());
        }
        QSettings settings; settings.clear(); settings.setValue("library/folders", QStringList{}); settings.setValue("library/snapshot", snapshot); settings.sync();
        QQmlApplicationEngine qml; QSignalSpy warnings(&qml, &QQmlApplicationEngine::warnings);
        qml.load(QUrl("qrc:/main.qml")); QVERIFY(!qml.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(qml.rootObjects().first()); QVERIFY(window);
        auto *library = window->findChild<TrackListModel *>("library"); auto *player = window->findChild<PlaybackEngine *>("player"); QVERIFY(library && player);
        player->audioOutput()->setMuted(true);
        auto *display = window->property("displayPreferences").value<DisplaySettings *>(); QVERIFY(display); display->setTextScale(textScale);
        QTRY_VERIFY(window->property("sessionRestored").toBool()); QTest::qWait(20); window->resize(windowSize); QTest::qWait(30);
        click(window, "navGenres"); QTest::qWait(40); QCOMPARE(window->property("activePage").toString(), QString("Genres"));
        QCOMPARE(library->genres()->count(), 3); QCOMPARE(library->genres()->get(0).value("name").toString(), QString("Jazz"));
        click(window, "groupSortButton"); QTest::qWait(20); click(window, "groupSortOption_count"); QTest::qWait(20);
        QCOMPARE(library->genres()->get(0).value("name").toString(), QString("Jazz"));
        click(window, "groupSortDirectionButton"); QTest::qWait(20); QCOMPARE(library->genres()->get(0).value("name").toString(), QString("Rock"));
        click(window, "groupSortButton"); QTest::qWait(20); click(window, "groupSortOption_duration"); QTest::qWait(20);
        QCOMPARE(library->genres()->get(0).value("name").toString(), QString("Rock"));
        click(window, "groupSortDirectionButton"); QTest::qWait(20); QCOMPARE(library->genres()->get(0).value("name").toString(), QString("Unknown genre"));
        click(window, "groupSortButton"); QTest::qWait(20); click(window, "groupSortOption_added"); QTest::qWait(20);
        QCOMPARE(library->genres()->get(0).value("name").toString(), QString("Unknown genre"));
        click(window, "groupSortButton"); QTest::qWait(20); click(window, "groupSortOption_name"); QTest::qWait(20);
        auto *search = window->findChild<QObject *>("searchField"); QVERIFY(search); search->setProperty("text", "Jazz"); QTest::qWait(20);
        QCOMPARE(library->genres()->count(), 1); search->setProperty("text", ""); QTest::qWait(20);
        auto *card = findItem(window->contentItem(), "groupCard0"); QVERIFY(card);
        QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, card->mapToScene(QPointF(30, 30)).toPoint()); QTest::qWait(30);
        auto *menu = window->findChild<QObject *>("groupMenu"); QVERIFY(menu && menu->property("visible").toBool());
        QCOMPARE(menu->property("kind").toString(), QString("genre"));
        // Menus can scroll on small screens; activating their action checks the
        // same handler without assuming every item fits above the pointer.
        auto *queueAction = window->findChild<QObject *>("groupAddToQueue"); QVERIFY(queueAction);
        QVERIFY(QMetaObject::invokeMethod(queueAction, "triggered")); QMetaObject::invokeMethod(menu, "close"); QTest::qWait(20);
        QCOMPARE(window->property("queueCount").toInt(), 2);
        click(window, "groupCard0"); QTest::qWait(30); QCOMPARE(window->property("collectionKind").toString(), QString("genre"));
        QCOMPARE(library->songs()->count(), 2); QCOMPARE(window->property("collectionName").toString(), QString("Jazz"));
        click(window, "playAllButton"); QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState); player->pause();
        QCOMPARE(window->property("queueCount").toInt(), 2);
        const auto queues = library->savedQueues();
        const auto active = std::find_if(queues.cbegin(), queues.cend(), [&](const QVariant &entry) { return entry.toMap().value("id").toString() == library->activeQueueId(); });
        QVERIFY(active != queues.cend()); QCOMPARE(active->toMap().value("name").toString(), QString("Jazz"));
        QSignalSpy loaded(library, &TrackListModel::batchMetadataLoaded), saved(library, &TrackListModel::batchMetadataSaved);
        click(window, "selectAllSongs"); click(window, "editSelectedMetadataButton"); QTRY_COMPARE(loaded.size(), 1);
        auto *editor = window->findChild<QObject *>("batchMetadataEditor"); QVERIFY(editor); QTRY_VERIFY(editor->property("ready").toBool());
        QVERIFY(QMetaObject::invokeMethod(editor, "applyField", Q_ARG(QVariant, QString("genre")), Q_ARG(QVariant, true)));
        QVERIFY(QMetaObject::invokeMethod(editor, "edit", Q_ARG(QVariant, QString("genre")), Q_ARG(QVariant, QString("Soul"))));
        click(window, "saveBatchMetadataButton"); QTRY_COMPARE_WITH_TIMEOUT(saved.size(), 1, 10000);
        QCOMPARE(window->property("collectionName").toString(), QString("Soul")); QCOMPARE(library->songs()->count(), 2);
        QCOMPARE(library->tracksForGroup("genre", "genre:jazz").size(), 0); QCOMPARE(library->tracksForGroup("genre", "genre:soul").size(), 2);
        QCOMPARE(window->property("queueCount").toInt(), 2); QVERIFY(!library->scanning());
        QMetaObject::invokeMethod(window, "goBack"); QTest::qWait(30); QCOMPARE(window->property("activePage").toString(), QString("Genres"));
        QCOMPARE(window->property("collectionName").toString(), QString()); QCOMPARE(window->property("genreSortKey").toString(), QString("name"));
        QVERIFY(findItem(window->contentItem(), "groupSortButton")->isVisible()); QCOMPARE(warnings.size(), 0);
    }

    void collectionSortingIsNumericAndDirectional_data()
    {
        QTest::addColumn<bool>("database");
        QTest::addColumn<bool>("cLocale");
        QTest::newRow("in-memory") << false << false;
        QTest::newRow("sqlite") << true << false;
        QTest::newRow("in-memory-C-locale") << false << true;
        QTest::newRow("sqlite-C-locale") << true << true;
    }

    void collectionSortingIsNumericAndDirectional()
    {
        QFETCH(bool, database);
        QFETCH(bool, cLocale);
        const QLocale previousLocale;
        const auto restoreLocale = qScopeGuard([previousLocale] { QLocale::setDefault(previousLocale); });
        if (cLocale) QLocale::setDefault(QLocale::c());
        QVariantList rows{
            QVariantMap{{"id", "a"}, {"name", "Album 2"}, {"subtitle", "Zulu"}, {"addedAt", 100LL}, {"trackCount", 12}, {"duration", 2000000LL}},
            QVariantMap{{"id", "b"}, {"name", "Album 10"}, {"subtitle", "Alpha"}, {"addedAt", 300LL}, {"trackCount", 2}, {"duration", 12000000LL}},
            QVariantMap{{"id", "c"}, {"name", "Album 1"}, {"subtitle", "Middle"}, {"addedAt", 200LL}, {"trackCount", 2}, {"duration", 1000000LL}}
        };
        LibraryEntryModel source;
        CountingLibraryViewModel view;
        QAbstractItemModelTester modelTester(&view, QAbstractItemModelTester::FailureReportingMode::QtTest);
        LibraryDatabase store(m_temp.filePath(cLocale ? "collection-sorts-C.sqlite" : "collection-sorts.sqlite")); QVERIFY(store.valid());
        if (database) { QVERIFY(store.replace({}, rows, {}, {})); view.setDatabase(&store, "album"); }
        else { source.synchronize(rows, "id"); view.setSourceModel(&source); }
        const auto check = [&](const QString &key, const QStringList &ascending, const QStringList &descending) {
            view.setSortKey(key); view.setSortDirection("ascending"); QCOMPARE(view.fileUrls(), ascending);
            view.setSortDirection("descending"); QCOMPARE(view.fileUrls(), descending);
        };
        check("title", {"c", "a", "b"}, {"b", "a", "c"});
        check("artist", {"b", "c", "a"}, {"a", "c", "b"});
        check("added", {"a", "c", "b"}, {"b", "c", "a"});
        check("count", {"c", "b", "a"}, {"a", "c", "b"});
        check("duration", {"c", "a", "b"}, {"b", "a", "c"});
        view.setSearch("Album 1"); QCOMPARE(view.fileUrls(), QStringList({"b", "c"}));
        view.setSortDirection("ascending"); QCOMPARE(view.fileUrls(), QStringList({"c", "b"}));
        view.setSearch(""); view.setSortDirection("descending");
        auto changed = rows[2].toMap(); changed["duration"] = 25000000LL; changed["trackCount"] = 20; rows[2] = changed;
        if (database) QVERIFY(store.replace({}, rows, {}, {}));
        else source.synchronize(rows, "id");
        QCOMPARE(view.fileUrls(), QStringList({"c", "b", "a"}));
        view.setSortKey("count"); QCOMPARE(view.fileUrls(), QStringList({"c", "a", "b"}));
        changed = rows[0].toMap(); changed["subtitle"] = "Aardvark"; rows[0] = changed;
        if (database) QVERIFY(store.replace({}, rows, {}, {}));
        else source.synchronize(rows, "id");
        view.setSortKey("artist"); view.setSortDirection("ascending"); QCOMPARE(view.fileUrls(), QStringList({"a", "b", "c"}));
        if (database) QVERIFY(view.cachedEntryCount() <= 192);
        else {
            view.setSortKey("duration"); view.comparisons = 0;
            for (auto &entry : rows) { auto item = entry.toMap(); item["artwork"] = "file:///changed.jpg"; entry = item; }
            source.synchronize(rows, "id"); QCOMPARE(view.comparisons, 0);
        }
    }

    void existingDatabaseGainsCollectionSortingWithoutScanning()
    {
        const auto config = m_temp.filePath("legacy-collection-sorts.ini");
        const auto path = config + ".library.sqlite";
        const QString connection = "legacy-collection-sorts";
        const auto cleanup = qScopeGuard([&] { QSqlDatabase::removeDatabase(connection); });
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection); db.setDatabaseName(path); QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("CREATE TABLE entries(kind TEXT NOT NULL,id TEXT NOT NULL,pos INTEGER,payload BLOB NOT NULL,nrank INTEGER,trank INTEGER,arrank INTEGER,alrank INTEGER,trackrank INTEGER,plays INTEGER,added INTEGER,favorite INTEGER,search TEXT,failed INTEGER,PRIMARY KEY(kind,id))"));
            QVERIFY(query.exec("CREATE TABLE membership(kind TEXT NOT NULL,groupid TEXT NOT NULL,songid TEXT NOT NULL,ord INTEGER,PRIMARY KEY(kind,groupid,songid))"));
            QVERIFY(query.exec("CREATE TABLE state(key TEXT PRIMARY KEY,value BLOB)"));
            QVERIFY(query.exec("INSERT INTO state VALUES('snapshot','[true]')"));
            const QVariantList songs{
                QVariantMap{{"fileUrl", "file:///unavailable-one.wav"}, {"duration", 3600000LL}, {"addedAt", 100LL}},
                QVariantMap{{"fileUrl", "file:///unavailable-two.wav"}, {"duration", 1800000LL}, {"addedAt", 300LL}},
                QVariantMap{{"fileUrl", "file:///unavailable-three.wav"}, {"duration", 7200000LL}, {"addedAt", 200LL}}
            };
            const QVariantList albums{
                QVariantMap{{"id", "album-a"}, {"name", "Zulu album"}, {"subtitle", "Alpha artist"}, {"trackCount", 2}},
                QVariantMap{{"id", "album-b"}, {"name", "Alpha album"}, {"subtitle", "Zulu artist"}, {"trackCount", 1}}
            };
            const QVariantList artists{
                QVariantMap{{"id", "artist-a"}, {"name", "Alpha artist"}, {"trackCount", 2}},
                QVariantMap{{"id", "artist-b"}, {"name", "Zulu artist"}, {"trackCount", 2}}
            };
            for (const auto &dataset : {qMakePair(QString("song"), songs), qMakePair(QString("album"), albums), qMakePair(QString("artist"), artists)}) {
                for (int row = 0; row < dataset.second.size(); ++row) {
                    const auto entry = dataset.second[row].toMap();
                    query.prepare("INSERT INTO entries(kind,id,pos,payload,added) VALUES(?,?,?,?,?)");
                    query.addBindValue(dataset.first); query.addBindValue(entry.value(dataset.first == "song" ? "fileUrl" : "id"));
                    query.addBindValue(row); query.addBindValue(QJsonDocument::fromVariant(entry).toJson(QJsonDocument::Compact)); query.addBindValue(entry.value("addedAt", 0));
                    QVERIFY2(query.exec(), qPrintable(query.lastError().text()));
                }
            }
            const QStringList members{
                "('album','album-a','file:///unavailable-one.wav',0)", "('album','album-a','file:///unavailable-two.wav',0)",
                "('album','album-b','file:///unavailable-three.wav',0)", "('artist','artist-a','file:///unavailable-one.wav',0)",
                "('artist','artist-a','file:///unavailable-two.wav',0)", "('artist','artist-b','file:///unavailable-two.wav',0)",
                "('artist','artist-b','file:///unavailable-three.wav',0)"
            };
            for (const auto &member : members) QVERIFY(query.exec("INSERT INTO membership VALUES" + member));
        }
        QSettings settings(config, QSettings::IniFormat);
        settings.setValue("library/databaseReady", true); settings.setValue("library/folders", QStringList{}); settings.sync();
        {
            TrackListModel library(config, false); QVERIFY(library.pagedLibrary()); QVERIFY(!library.scanning());
            QCOMPARE(library.residentTrackCount(), 0); QCOMPARE(library.trackCount(), 3);
            auto *albums = library.albums(); albums->setSortKey("added");
            QCOMPARE(albums->get(0).value("id").toString(), QString("album-a"));
            QCOMPARE(albums->get(0).value("addedAt").toLongLong(), 300LL);
            albums->setSortDirection("ascending"); QCOMPARE(albums->get(0).value("id").toString(), QString("album-b"));
            albums->setSortKey("artist"); QCOMPARE(albums->get(0).value("id").toString(), QString("album-a"));
            albums->setSortKey("title"); QCOMPARE(albums->get(0).value("id").toString(), QString("album-b"));
            auto *artists = library.artists(); artists->setSortKey("duration"); artists->setSortDirection("descending");
            QCOMPARE(artists->get(0).value("id").toString(), QString("artist-b"));
            QCOMPARE(artists->get(0).value("duration").toLongLong(), 9000000LL);
            QCOMPARE(artists->get(1).value("duration").toLongLong(), 5400000LL);
            QCOMPARE(artists->get(0).value("trackCount").toInt(), 2);
            QCOMPARE(library.residentTrackCount(), 0); QVERIFY(!library.scanning());
        }
        LibraryDatabase reopened(path); QVERIFY2(reopened.valid(), qPrintable(reopened.error()));
        QCOMPARE(reopened.value("collectionSortingVersion").toInt(), 1);
        QCOMPARE(reopened.entry("artist", "artist-b").value("duration").toLongLong(), 9000000LL);
    }

    void collectionSortControlsKeepTheirOrderAcrossNavigation_data()
    {
        QTest::addColumn<QSize>("windowSize"); QTest::addColumn<double>("textScale");
        QTest::newRow("normal") << QSize(1280, 850) << 1.0;
        QTest::newRow("small-large-text") << QSize(800, 600) << 1.6;
    }

    void collectionSortControlsKeepTheirOrderAcrossNavigation()
    {
        QFETCH(QSize, windowSize); QFETCH(double, textScale);
        QVariantList snapshot;
        const QStringList artists{"Zulu", "Alpha", "Middle"}, albums{"Album 2", "Album 10", "Album 1"};
        const QList<int> counts{12, 2, 3}; const QList<qint64> durations{60000, 3600000, 120000}; const QList<qint64> dates{100, 300, 200};
        for (int artist = 0; artist < artists.size(); ++artist) {
            for (int song = 0; song < counts[artist]; ++song) {
                auto track = releaseTrack(albums[artist], artists[artist], song + 1,
                    m_temp.filePath(QString("collection-ui/%1/track-%2.wav").arg(artists[artist]).arg(song)), {}, artists[artist]);
                track["duration"] = durations[artist]; track["addedAt"] = dates[artist]; track["probed"] = true;
                track["artwork"] = QString(); track["artStyle"] = 0; snapshot.append(track);
            }
        }
        QSettings settings; settings.clear(); settings.setValue("library/folders", QStringList{}); settings.setValue("library/snapshot", snapshot); settings.sync();
        QQmlApplicationEngine qml; QSignalSpy warnings(&qml, &QQmlApplicationEngine::warnings);
        qml.load(QUrl("qrc:/main.qml")); QVERIFY(!qml.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(qml.rootObjects().first()); QVERIFY(window);
        auto *library = window->findChild<TrackListModel *>("library"); QVERIFY(library);
        auto *display = window->property("displayPreferences").value<DisplaySettings *>(); QVERIFY(display); display->setTextScale(textScale);
        QTRY_VERIFY(window->property("sessionRestored").toBool());
        // Startup fits the window to the offscreen display. Resize after that
        // initial event so pointer checks use the requested logical dimensions.
        QTest::qWait(20); window->resize(windowSize);
        QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, QString("Albums"))); QTest::qWait(50);
        QCOMPARE(window->size(), windowSize);
        QCOMPARE(library->albums()->get(0).value("name").toString(), QString("Album 1"));
        click(window, "groupSortButton"); QTest::qWait(30); click(window, "groupSortOption_added"); QTest::qWait(30);
        QCOMPARE(library->albums()->get(0).value("name").toString(), QString("Album 10")); QVERIFY(window->property("albumSortDescending").toBool());
        click(window, "groupSortDirectionButton"); QTest::qWait(20);
        QCOMPARE(library->albums()->get(0).value("name").toString(), QString("Album 2"));
        click(window, "groupSortButton"); QTest::qWait(30); click(window, "groupSortOption_artist"); QTest::qWait(30);
        QCOMPARE(library->albums()->get(0).value("name").toString(), QString("Album 10"));
        click(window, "groupCard0"); QTest::qWait(30);
        QVERIFY(!window->property("collectionName").toString().isEmpty()); QVERIFY(!findItem(window->contentItem(), "groupSortingToolbar")->isVisible());
        QCOMPARE(window->property("sortBy").toString(), QString("Track"));
        QMetaObject::invokeMethod(window, "goBack"); QTest::qWait(40);
        QCOMPARE(window->property("albumSortKey").toString(), QString("artist"));
        QCOMPARE(library->albums()->get(0).value("name").toString(), QString("Album 10"));
        QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, QString("Artists"))); QTest::qWait(40);
        click(window, "groupSortButton"); QTest::qWait(30); click(window, "groupSortOption_count"); QTest::qWait(30);
        QCOMPARE(library->artists()->get(0).value("name").toString(), QString("Zulu"));
        click(window, "groupSortDirectionButton"); QTest::qWait(20);
        QCOMPARE(library->artists()->get(0).value("name").toString(), QString("Alpha"));
        click(window, "groupSortButton"); QTest::qWait(30); click(window, "groupSortOption_duration"); QTest::qWait(30);
        QCOMPARE(library->artists()->get(0).value("name").toString(), QString("Alpha"));
        auto *details = findItem(window->contentItem(), "groupDetails0"); QVERIFY(details);
        QVERIFY(details->property("text").toString().contains("2 h"));
        auto *title = findItem(window->contentItem(), "groupTitle0"); QVERIFY(title);
        QVERIFY(details->y() >= title->y() + title->height());
        click(window, "groupSortButton"); QTest::qWait(30); click(window, "groupSortAscending"); QTest::qWait(30);
        QCOMPARE(library->artists()->get(0).value("name").toString(), QString("Middle"));
        auto *search = window->findChild<QObject *>("searchField"); QVERIFY(search); search->setProperty("text", "Zulu"); QTest::qWait(30);
        QCOMPARE(library->artists()->count(), 1); QCOMPARE(library->artists()->get(0).value("name").toString(), QString("Zulu"));
        search->setProperty("text", ""); QTest::qWait(30); QCOMPARE(library->artists()->count(), 3);
        auto *button = findItem(window->contentItem(), "groupSortButton"); auto *direction = findItem(window->contentItem(), "groupSortDirectionButton");
        QVERIFY(button && direction); QVERIFY(button->isVisible() && direction->isVisible());
        QVERIFY(button->mapToScene(QPointF()).x() >= 0); QVERIFY(direction->mapToScene(QPointF(direction->width(), direction->height())).x() <= window->width());
        auto *grid = findItem(window->contentItem(), "groupGrid"); QVERIFY(grid);
        QVERIFY(button->mapToScene(QPointF(0, button->height())).y() <= grid->mapToScene(QPointF()).y());
        QVERIFY(grid->height() > 0); QCOMPARE(warnings.size(), 0);
    }

    void initTestCase()
    {
        registerPerformanceTypes();
        QVERIFY(m_temp.isValid());
        qputenv("XDG_DATA_HOME", m_temp.filePath("data").toUtf8());
        QDir().mkpath(m_temp.filePath("data"));
        QCoreApplication::setOrganizationName(QStringLiteral("MusicaTests"));
        QCoreApplication::setApplicationName(QStringLiteral("MusicaTests"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_temp.filePath("config"));
        QQuickStyle::setStyle(QStringLiteral("Basic"));
        qmlRegisterType<TrackListModel>("MusicPlayer", 1, 0, "TrackListModel");
        qmlRegisterType<PlaybackEngine>("MusicPlayer", 1, 0, "PlaybackEngine");
    qmlRegisterType<DisplaySettings>("MusicPlayer", 1, 0, "DisplaySettings");
    qmlRegisterType<Sharing>("MusicPlayer", 1, 0, "Sharing");
    qmlRegisterSingletonType(QUrl("qrc:/components/Theme.qml"), "MusicPlayer", 1, 0, "Theme");
        qmlRegisterUncreatableType<LibraryViewModel>("MusicPlayer", 1, 0, "LibraryViewModel", QStringLiteral("Provided by the music library"));
    }

    void searchPublishesCompleteCounts_data()
    {
        QTest::addColumn<bool>("database");
        QTest::newRow("in-memory") << false;
        QTest::newRow("sqlite") << true;
    }

    void searchPublishesCompleteCounts()
    {
        QFETCH(bool, database);
        QVariantList rows;
        QStringList selected;
        for (int i = 0; i < 512; ++i) {
            const auto url = QString("file:///search/%1.wav").arg(i);
            rows.append(QVariantMap{{"fileUrl", url}, {"title", i % 2 ? "East" : "West"}, {"artist", "Performer"}, {"album", "Album"},
                {"albumId", "album"}, {"artistIds", QStringList{"performer"}}});
            selected.append(url);
        }
        TrackMapReadCounter source; LibraryViewModel view;
        LibraryDatabase store(m_temp.filePath("search-counts.sqlite")); QVERIFY(store.valid());
        if (database) { QVERIFY(store.replace(rows, {}, {}, {})); view.setDatabase(&store, "song"); }
        else { source.synchronize(rows, "fileUrl"); view.setSourceModel(&source); }
        source.mapReads = 0;
        int expected = 512; bool stable = true;
        QSignalSpy counts(&view, &LibraryViewModel::countChanged);
        connect(&view, &LibraryViewModel::countChanged, &view, [&] {
            const auto *entries = qobject_cast<LibraryEntryModel *>(view.sourceModel());
            stable &= !entries->synchronizing() && view.count() == expected && view.countMatchingUrls(selected) == expected;
        });
        for (const auto &search : {"East", "missing", "", "West", "East", ""}) {
            expected = QString(search).isEmpty() ? 512 : QString(search) == "missing" ? 0 : 256;
            const int before = counts.size(), oldCount = view.count();
            view.setSearch(search);
            QCOMPARE(view.count(), expected); QCOMPARE(view.countMatchingUrls(selected), expected);
            QCOMPARE(view.fileUrls().size(), expected);
            QCOMPARE(counts.size() - before, oldCount == expected ? 0 : 1);
        }
        QVERIFY(stable); QCOMPARE(source.mapReads, 0);
        if (database) QCOMPARE(view.cachedEntryCount(), 0);
        const auto chosen = view.entriesForUrls({selected[2], selected[0]}); QCOMPARE(chosen.size(), 2);
        QCOMPARE(chosen[0].toMap().value("fileUrl").toString(), selected[0]);
    }

    void searchingWithSelectionsKeepsPlaybackAndModelsStable()
    {
        const auto path = m_temp.filePath("search-playback/Playing.wav"); QVERIFY(writeWave(path, 120));
        QVariantList snapshot;
        QStringList selected;
        QVariantMap counts;
        for (int i = 0; i < 1200; ++i) {
            const auto file = i == 0 ? path : m_temp.filePath(QString("search-playback/%1.wav").arg(i));
            auto track = releaseTrack(QString("Album %1").arg(i / 10), QString("Artist %1").arg(i % 24), i % 10 + 1, file, {}, "Collective");
            track["title"] = QString(i % 2 ? "Needle %1" : "Other %1").arg(i); track["duration"] = 120000; track["probed"] = true;
            track["artwork"] = QString();
            track["comment"] = QString(400, QChar('x')); snapshot.append(track); selected.append(track.value("fileUrl").toString());
            if (i % 7 == 0) counts[selected.last()] = 1;
        }
        QSettings settings; settings.clear(); settings.setValue("library/folders", QStringList{}); settings.setValue("library/snapshot", snapshot);
        settings.setValue("library/playCounts", counts); settings.setValue("library/favorites", selected.mid(0, 30)); settings.sync();
        QQmlApplicationEngine qml; QSignalSpy warnings(&qml, &QQmlApplicationEngine::warnings);
        qml.load(QUrl("qrc:/main.qml")); QVERIFY(!qml.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(qml.rootObjects().first()); auto *library = window->findChild<TrackListModel *>("library");
        auto *player = window->findChild<PlaybackEngine *>("player"); QVERIFY(window && library && player);
        QTRY_VERIFY(window->property("sessionRestored").toBool());
        player->audioOutput()->setMuted(true);
        QVERIFY(QMetaObject::invokeMethod(window, "playUrl", Q_ARG(QVariant, selected[0])));
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
        auto *search = window->findChild<QObject *>("searchField"); QVERIFY(search);
        const auto playingId = window->property("currentQueueId");
        for (const auto &page : {"Songs", "Favorites", "Most Played", "Albums", "Artists"}) {
            QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, QString(page)));
            window->setProperty("selectedUrls", selected);
            for (const auto &query : {"N", "Ne", "Needle", "Needle 1", "missing", "", "Other", "", "Artist 2", ""}) {
                search->setProperty("text", query); QTest::qWait(5); qml.collectGarbage();
                auto *view = QString(page) == "Most Played" ? library->mostPlayed() : library->songs();
                QCOMPARE(window->property("selectedVisibleCount").toInt(), view->count());
                QVERIFY(view->cachedEntryCount() <= 192);
                QCOMPARE(window->property("currentQueueId"), playingId);
            }
        }
        QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, QString("Songs")));
        window->setProperty("selectedUrls", selected); search->setProperty("text", "Needle"); QTest::qWait(30);
        QCOMPARE(window->property("selectedVisibleCount").toInt(), 600);
        click(window, "selectAllSongs"); QCOMPARE(window->property("selectedVisibleCount").toInt(), 0);
        QCOMPARE(window->property("selectedCount").toInt(), 600);
        search->setProperty("text", ""); QTest::qWait(20);
        QCOMPARE(window->property("selectedVisibleCount").toInt(), 600);
        click(window, "selectAllSongs"); QCOMPARE(window->property("selectedCount").toInt(), 1200);
        QCOMPARE(window->property("currentUrl").toString(), selected[0]);
        QVERIFY(player->position() > 0); QCOMPARE(player->playbackState(), QMediaPlayer::PlayingState);
        QCOMPARE(warnings.size(), 0); player->stop();
    }

    void pagedLibraryRestoresSearchesAndUpdatesWithoutFullResidency()
    {
        const auto config = m_temp.filePath("paged-library.ini");
        QVariantList snapshot;
        for (int i = 0; i < 3000; ++i) {
            const auto path = m_temp.filePath(QString("paged/%1.wav").arg(i));
            auto song = releaseTrack("Shared album", i % 2 ? "Odd artist" : "Even artist", i + 1, path, {}, "Collective");
            song["title"] = QString("Title %1").arg(i, 4, 10, QChar('0')); song["duration"] = 4000; song["probed"] = true;
            snapshot.append(song);
        }
        QSettings settings(config, QSettings::IniFormat); settings.setValue("library/folders", QStringList{});
        settings.setValue("library/snapshot", snapshot); settings.sync();
        {
            TrackListModel migrated(config, false); QVERIFY(migrated.pagedLibrary()); QCOMPARE(migrated.trackCount(), 3000);
            QTRY_COMPARE_WITH_TIMEOUT(migrated.residentTrackCount(), 0, 3000);
        }
        settings.sync(); QVERIFY(!settings.contains("library/snapshot")); QVERIFY(!settings.contains("library/metadata"));
        TrackListModel library(config, false); QCOMPARE(library.residentTrackCount(), 0); QCOMPARE(library.songs()->count(), 3000);
        QCOMPARE(library.songs()->get(2999).value("title").toString(), QString("Title 2999"));
        for (int row = 0; row < 3000; row += 71) QVERIFY(!library.songs()->get(row).isEmpty());
        QVERIFY(library.songs()->cachedEntryCount() <= 192); QCOMPARE(library.residentTrackCount(), 0);
        library.songs()->setSearch("Title 2999"); QCOMPARE(library.songs()->count(), 1);
        library.songs()->setSearch("' OR 1=1 --"); QCOMPARE(library.songs()->count(), 0); library.songs()->setSearch("");
        const auto url = snapshot[2999].toMap().value("fileUrl").toString();
        library.toggleFavorite(url); QCOMPARE(library.favoriteCount(), 1); library.songs()->setFavoritesOnly(true);
        QCOMPARE(library.songs()->count(), 1); QCOMPARE(library.songs()->get(0).value("fileUrl").toString(), url);
        library.observeAudiblePlayback("paged-listen", url, 0, 4000, true, false, 0);
        library.observeAudiblePlayback("paged-listen", url, 2000, 4000, true, false, 2000);
        QCOMPARE(library.mostPlayed()->count(), 1); QCOMPARE(library.mostPlayed()->get(0).value("playCount").toInt(), 1);
        QCOMPARE(library.residentTrackCount(), 0); QVERIFY(!library.scanning());
        const auto lists = library.smartPlaylists(); QCOMPARE(lists[1].toMap().value("count").toInt(), 2999);
        QCOMPARE(library.residentTrackCount(), 0);
    }

    void shortcutPreferencesRejectConflictsAndPersist()
    {
        const auto path = m_temp.filePath("shortcuts.ini");
        {
            DisplaySettings preferences(path);
            QVERIFY(preferences.setShortcut("playPause", "Ctrl+Alt+P").isEmpty());
            QVERIFY(!preferences.setShortcut("next", "Ctrl+Alt+P").isEmpty());
            QVERIFY(!preferences.setShortcut("next", "Backspace").isEmpty());
            QVERIFY(!preferences.setShortcut("next", "N").isEmpty());
            QCOMPARE(preferences.shortcuts().value("next").toString(), QString("Ctrl+Right"));
            QVERIFY(preferences.assignShortcut("seekForward", Qt::Key_L, Qt::ControlModifier | Qt::AltModifier).isEmpty());
            preferences.setReducedMotion(true);
        }
        DisplaySettings restored(path); QVERIFY(restored.reducedMotion());
        QCOMPARE(restored.shortcuts().value("playPause").toString(), QString("Ctrl+Alt+P"));
        QCOMPARE(restored.shortcuts().value("seekForward").toString(), QString("Ctrl+Alt+L"));
        QVERIFY(restored.setShortcut("next", "").isEmpty()); QVERIFY(restored.shortcuts().value("next").toString().isEmpty());
        restored.resetShortcuts(); QCOMPARE(restored.shortcuts().value("playPause").toString(), QString("Space"));
    }

    void artworkCacheIsBoundedAndLoadsNamesWithSpaces()
    {
        auto memory = std::make_shared<ArtworkMemory>(); memory->images.setMaxCost(512);
        ArtworkProvider provider(memory);
        QImage image(600, 600, QImage::Format_RGB32); image.fill(QColor("#2255ee"));
        QString source;
        for (int i = 0; i < 12; ++i) {
            const auto path = m_temp.filePath(QString("cover ü 100% %1.png").arg(i)); QVERIFY(image.save(path));
            source = QUrl::fromLocalFile(path).toString();
            QSize size;
            const auto encoded = QString::fromLatin1(QUrl::toPercentEncoding(source));
            const auto first = provider.requestImage(encoded, &size, QSize(128, 128));
            QVERIFY(!first.isNull()); QCOMPARE(size, QSize(128, 128));
            QCOMPARE(provider.requestImage(encoded, nullptr, QSize(128, 128)).cacheKey(), first.cacheKey());
            QVERIFY(memory->images.totalCost() <= 512);
        }
        QQmlApplicationEngine qml; QSignalSpy warnings(&qml, &QQmlApplicationEngine::warnings);
        QQmlComponent component(&qml, QUrl("qrc:/components/CoverArt.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QQuickWindow window; window.resize(80, 80);
        QScopedPointer<QObject> object(component.createWithInitialProperties({{"width", 80}, {"height", 80}, {"artwork", source}}));
        auto *item = qobject_cast<QQuickItem *>(object.data()); QVERIFY(item); item->setParentItem(window.contentItem()); window.show();
        QTRY_COMPARE_WITH_TIMEOUT(window.grabWindow().pixelColor(40, 40), QColor("#2255ee"), 3000);
        QCOMPARE(warnings.size(), 0);
        CacheManager manager; manager.setMemoryLimitMiB(8); QCOMPARE(manager.memoryLimitMiB(), 8);
        QSignalSpy revisions(&manager, &CacheManager::revisionChanged);
        manager.clearThumbnails(); QTRY_VERIFY(!manager.busy()); QVERIFY(revisions.size() > 0);
        QCOMPARE(manager.memoryKiB(), 0);
    }

    void lazyScreensUnloadAndCustomShortcutsWork()
    {
        QSettings settings; settings.clear(); settings.setValue("library/folders", QStringList{}); settings.sync();
        QQmlApplicationEngine qml; QSignalSpy warnings(&qml, &QQmlApplicationEngine::warnings);
        qml.load(QUrl("qrc:/main.qml")); QVERIFY(!qml.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(qml.rootObjects().first()); window->resize(1100, 800); window->requestActivate();
        QVERIFY(!window->findChild<QObject *>("metadataEditor")); QVERIFY(!window->findChild<QObject *>("displaySettingsDialog"));
        QVERIFY(!window->findChild<QObject *>("nowPlayingPane")); QVERIFY(!window->findChild<QObject *>("listeningDashboard"));
        auto *display = qobject_cast<DisplaySettings *>(window->property("displayPreferences").value<QObject *>()); QVERIFY(display);
        display->resetShortcuts();
        const auto reset = qScopeGuard([display] { display->setCapturingShortcut(false); display->resetShortcuts(); display->setReducedMotion(false); });
        const auto path = m_temp.filePath("lazy/Shared.wav"); QVERIFY(writeWave(path, 30));
        auto *library = window->findChild<TrackListModel *>("library"); auto *player = window->findChild<PlaybackEngine *>("player");
        player->audioOutput()->setMuted(true); library->addFiles({QUrl::fromLocalFile(path)}); QTRY_VERIFY(!library->scanning());
        click(window, "navSongs"); QTest::qWait(40); click(window, "playAllButton");
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState); player->pause();
        click(window, "nowPlayingCoverButton"); QTRY_VERIFY(window->findChild<QObject *>("nowPlayingPane"));
        click(window, "closeNowPlayingButton"); QTRY_VERIFY(!window->findChild<QObject *>("nowPlayingPane"));
        QVERIFY(QMetaObject::invokeMethod(window, "openDisplaySettings"));
        auto *button = findItem(window->contentItem(), "shortcut_playPause"); QVERIFY(button); button->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Space);
        QTRY_VERIFY(display->capturingShortcut());
        QTest::keyClick(window, Qt::Key_P, Qt::ControlModifier | Qt::AltModifier);
        QTRY_VERIFY(!display->capturingShortcut()); QCOMPARE(display->shortcuts().value("playPause").toString(), QString("Ctrl+Alt+P"));
        auto *dialog = window->findChild<QObject *>("displaySettingsDialog"); QVERIFY(dialog); QMetaObject::invokeMethod(dialog, "close");
        QTRY_VERIFY(!window->findChild<QObject *>("displaySettingsDialog")); window->contentItem()->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Space); QCOMPARE(player->playbackState(), QMediaPlayer::PausedState);
        QTest::keyClick(window, Qt::Key_P, Qt::ControlModifier | Qt::AltModifier); QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
        player->pause(); display->setReducedMotion(true);
        QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, QString("Listening dashboard")));
        QTRY_VERIFY(window->findChild<QObject *>("listeningDashboard")); click(window, "navSongs");
        QTRY_VERIFY(!window->findChild<QObject *>("listeningDashboard")); QCOMPARE(warnings.size(), 0);
    }

    void displaySettingsUseSystemFontsAndPersist()
    {
        const auto original = QGuiApplication::font();
        const auto restoreFont = qScopeGuard([original] { QGuiApplication::setFont(original); });
        const auto path = m_temp.filePath("display-settings.ini");
        {
            DisplaySettings display(path);
            QSignalSpy fontChanged(&display, &DisplaySettings::systemFontChanged);
            auto enlarged = original; enlarged.setPointSizeF(17);
            QGuiApplication::setFont(enlarged);
            QTRY_COMPARE(display.fontPointSize(), 17.0);
            QCOMPARE(display.fontFamily(), QFontInfo(enlarged).family());
            QVERIFY(!fontChanged.isEmpty());
            display.setDensity("compact"); display.setTextScale(1.5);
        }
        DisplaySettings restored(path);
        QCOMPARE(restored.density(), QString("compact")); QCOMPARE(restored.textScale(), 1.5);
        QCOMPARE(restored.fontPointSize(), 17.0);
        for (const auto &available : {QRect(0, 0, 1280, 720), QRect(1920, 0, 1024, 768), QRect(-900, -200, 720, 1280)}) {
            const auto fitted = DisplaySettings::fittedGeometry(QRect(-10000, 4000, 1600, 1000), available);
            QVERIFY(available.contains(fitted));
            QCOMPARE(fitted.size(), QSize(1600, 1000).boundedTo(available.size()));
        }
        restored.reset(); QCOMPARE(restored.textScale(), 1.0); QCOMPARE(restored.density(), QString("automatic"));
    }

    void displayAdaptsToSmallWindowsAndLargeText_data()
    {
        QTest::addColumn<QSize>("size"); QTest::addColumn<double>("scale"); QTest::addColumn<QString>("density");
        QTest::newRow("laptop") << QSize(1280, 720) << 1.0 << QString("automatic");
        QTest::newRow("four-three") << QSize(1024, 768) << 1.25 << QString("comfortable");
        QTest::newRow("small") << QSize(720, 480) << 1.0 << QString("compact");
        QTest::newRow("large-text") << QSize(900, 640) << 1.75 << QString("automatic");
        QTest::newRow("maximum-text") << QSize(1280, 800) << 2.0 << QString("comfortable");
        QTest::newRow("portrait") << QSize(800, 1100) << 1.5 << QString("automatic");
        QTest::newRow("ultrawide") << QSize(1920, 800) << 1.0 << QString("comfortable");
    }

    void displayAdaptsToSmallWindowsAndLargeText()
    {
        QFETCH(QSize, size); QFETCH(double, scale); QFETCH(QString, density);
        const auto song = m_temp.filePath("display-library/Artist - A Song.wav"); QVERIFY(writeWave(song, 30));
        QSettings settings; settings.clear(); settings.setValue("library/folders", QStringList{});
        settings.setValue("library/files", QStringList{song}); settings.sync();
        const auto original = QGuiApplication::font();
        const auto restoreFont = qScopeGuard([original] { QGuiApplication::setFont(original); });
        auto desktopFont = original; desktopFont.setPointSizeF(11); QGuiApplication::setFont(desktopFont);
        QQmlApplicationEngine qml; QSignalSpy warnings(&qml, &QQmlApplicationEngine::warnings);
        qml.load(QUrl("qrc:/main.qml")); QVERIFY(!qml.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(qml.rootObjects().first());
        auto *display = qobject_cast<DisplaySettings *>(window->property("displayPreferences").value<QObject *>()); QVERIFY(display);
        const auto restoreSettings = qScopeGuard([display] { display->reset(); });
        auto *library = window->findChild<TrackListModel *>("library");
        library->rescan(); // New test fixtures are discovered by a manual scan.
        QTRY_VERIFY(library->libraryReady() && !library->scanning());
        QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, "Songs"));
        window->setProperty("currentUrl", QUrl::fromLocalFile(song).toString());
        display->setDensity(density); display->setTextScale(scale);
        window->resize(size); QTest::qWait(70);
        const auto insideWindow = [window](QQuickItem *item) {
            const QRectF bounds(item->mapToScene(QPointF()), item->size());
            return bounds.left() >= -1 && bounds.top() >= -1 && bounds.right() <= window->width() + 1
                && bounds.bottom() <= window->height() + 1 && item->width() > 0 && item->height() > 0;
        };
        for (const auto &name : {"displaySettingsButton", "nowPlayingCoverButton", "playPauseButton", "previousButton", "nextButton",
                 "queueButton", "crossfadeSettingsButton", "sleepTimerButton", "audioSettingsButton", "volumeSlider", "progressSlider"}) {
            auto *control = findItem(window->contentItem(), name); QVERIFY2(control, name);
            QVERIFY2(insideWindow(control), qPrintable(QString("%1 is outside %2×%3 at %4% text").arg(name).arg(size.width()).arg(size.height()).arg(scale * 100)));
        }
        auto *title = findItem(window->contentItem(), "footerSongTitle"); QVERIFY(title);
        if (window->property("narrowLayout").toBool())
            QVERIFY(!findItem(window->contentItem(), "appWordmark")->isVisible());
        QTRY_VERIFY(std::abs(title->property("font").value<QFont>().pointSizeF() - 11 * scale) < 0.1);
        QCOMPARE(title->property("font").value<QFont>().family(), QFontInfo(desktopFont).family());
        auto changedFont = desktopFont; changedFont.setPointSizeF(14); QGuiApplication::setFont(changedFont);
        QTRY_VERIFY(std::abs(title->property("font").value<QFont>().pointSizeF() - 14 * scale) < 0.1);
        QTest::qWait(70);
        QVERIFY(insideWindow(findItem(window->contentItem(), "displaySettingsButton")));
        auto *list = findItem(window->contentItem(), "songList"); QVERIFY(list);
        auto *row = findItem(window->contentItem(), "songRow0"); QVERIFY(row);
        QVERIFY2(list->height() >= row->height() * 0.8, "The compact layout leaves no usable song rows");
        for (const auto &name : {"songTitleLabel0", "songArtistLabel0"}) {
            auto *label = findItem(window->contentItem(), name); QVERIFY(label);
            const auto bounds = label->mapRectToItem(row, label->boundingRect());
            QVERIFY(bounds.top() >= -1 && bounds.bottom() <= row->height() + 1);
        }
        window->grabWindow().save(QCoreApplication::applicationDirPath() + "/musica-display-" + QString::fromLatin1(QTest::currentDataTag()) + ".png");
        // Settings remain reachable and scrollable even in the smallest viewport.
        if (QString::fromLatin1(QTest::currentDataTag()) == "laptop") {
            window->requestActivate(); QTest::qWait(30);
            QTest::keyClick(window, Qt::Key_Comma, Qt::ControlModifier);
        } else click(window, "displaySettingsButton");
        auto *dialog = window->findChild<QObject *>("displaySettingsDialog"); QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("visible").toBool());
        QVERIFY(dialog->property("height").toReal() <= window->height());
        QVERIFY(dialog->property("width").toReal() <= window->width());
        auto *preview = findItem(window->contentItem(), "displayFontPreview"); QVERIFY(preview);
        if (QString::fromLatin1(QTest::currentDataTag()) == "laptop") {
            auto *slider = findItem(window->contentItem(), "displayTextScaleSlider"); QVERIFY(slider);
            slider->forceActiveFocus();
            QTest::keyClick(window, Qt::Key_Home); QTRY_COMPARE(display->textScale(), 1.0);
            QTest::keyClick(window, Qt::Key_Right); QTRY_COMPARE(display->textScale(), 1.05);
        }
        display->setTextScale(1.25);
        QTRY_VERIFY(preview->property("font").value<QFont>().pointSizeF() > 14);
        QMetaObject::invokeMethod(dialog, "close");
        // The selection bar and batch editor must also fit at large font sizes.
        display->setTextScale(scale); QTest::qWait(40);
        click(window, "selectSong0"); QTest::qWait(40);
        for (const auto &name : {"clearSongSelection", "queueSelectedSongs", "editSelectedMetadataButton", "selectedSongsOptions"}) {
            auto *control = findItem(window->contentItem(), name); QVERIFY(control);
            QVERIFY2(insideWindow(control), name);
        }
        click(window, "editSelectedMetadataButton");
        auto *batch = window->findChild<QObject *>("batchMetadataEditor"); QVERIFY(batch);
        QTRY_VERIFY(batch->property("ready").toBool());
        // Tag readiness may precede the dialog's first layout polish.
        auto *batchSave = findItem(window->contentItem(), "saveBatchMetadataButton"); QVERIFY(batchSave);
        display->settleLayout(batchSave);
        QVERIFY(batch->property("height").toReal() <= window->height());
        QVERIFY(insideWindow(batchSave));
        click(window, "cancelBatchMetadataButton");
        QCOMPARE(warnings.size(), 0);
    }

    void startupLoadsSavedLibraryAndOnlyScansOnRequest()
    {
        const auto folder = m_temp.filePath("manual-library");
        const auto first = folder + "/Artist/Old album/01 First.wav";
        const auto added = folder + "/Artist/New album/02 Added.wav";
        const auto config = m_temp.filePath("manual-library.ini");
        QVERIFY(writeWave(first, 3, "Artist", "Old album", 1));
        const auto url = QUrl::fromLocalFile(first).toString();
        {
            QSettings settings(config, QSettings::IniFormat);
            settings.setValue("library/folders", QStringList{}); settings.sync();
            TrackListModel library(config, false);
            library.addFolder(QUrl::fromLocalFile(folder));
            QTRY_COMPARE(library.trackCount(), 1); QTRY_VERIFY(!library.scanning());
            QCOMPARE(library.genres()->count(), 1); QCOMPARE(library.genres()->get(0).value("name").toString(), QString("Unknown genre"));
            library.toggleFavorite(url);
        }
        QVERIFY(writeWave(added, 3, "Artist", "New album", 2));
        auto native = FileTags::read(first, folder, true);
        QVERIFY(!FileTags::write(first, {{"album", "Externally edited"}}, "keep", {}, native.value("revision").toString(), folder).contains("error"));
        {
            TrackListModel restored(config, false);
            QSignalSpy scanning(&restored, &TrackListModel::scanningChanged);
            QTest::qWait(100);
            QVERIFY(restored.libraryReady()); QVERIFY(!restored.scanning()); QCOMPARE(scanning.size(), 0);
            QCOMPARE(restored.trackCount(), 1); QCOMPARE(restored.favoriteCount(), 1);
            QCOMPARE(restored.trackForUrl(url).value("album").toString(), QStringLiteral("Old album"));
            restored.rescan();
            QTRY_COMPARE(restored.trackCount(), 2); QTRY_VERIFY(!restored.scanning());
            QCOMPARE(restored.trackForUrl(url).value("album").toString(), QStringLiteral("Externally edited"));
        }
        // Migration also restores older caches without reading modified files.
        {
            QSettings settings(config, QSettings::IniFormat); settings.remove("library/snapshot"); settings.sync();
            TrackListModel legacy(config, false);
            QSignalSpy scanning(&legacy, &TrackListModel::scanningChanged);
            QTest::qWait(70); QCOMPARE(legacy.trackCount(), 2); QCOMPARE(scanning.size(), 0);
        }
        QVERIFY(QFile::remove(added));
        {
            TrackListModel restored(config, false);
            QCOMPARE(restored.trackCount(), 2); // No implicit filesystem checks.
            restored.rescan(); QTRY_VERIFY(!restored.scanning()); QCOMPARE(restored.trackCount(), 1);
        }
    }

    void batchMetadataPreservesIndividualTagsAndReportsStaleFiles()
    {
        const auto directory = m_temp.filePath("batch-native");
        const auto config = directory + "/library.ini";
        QDir().mkpath(directory);
        QSettings settings(config, QSettings::IniFormat); settings.setValue("library/folders", QStringList{}); settings.sync();
        QStringList urls, paths;
        const auto cover = directory + "/cover.png";
        QImage image(200, 200, QImage::Format_RGB32); image.fill(Qt::darkCyan); QVERIFY(image.save(cover));
        for (int i = 0; i < 3; ++i) {
            const auto path = directory + QString("/%1 Song.wav").arg(i + 1);
            QVERIFY(writeWave(path, 3, "Artist " + QString::number(i), "Shared album", i + 1));
            const auto before = FileTags::read(path, directory, true);
            const auto result = FileTags::write(path, {{"title", "Title " + QString::number(i)}, {"comment", "Keep " + QString::number(i)}},
                i == 0 ? "replace" : "keep", QUrl::fromLocalFile(cover), before.value("revision").toString(), directory);
            QVERIFY2(!result.contains("error"), qPrintable(result.value("error").toString()));
            paths.append(path); urls.append(QUrl::fromLocalFile(path).toString());
        }
        TrackListModel library(config, false);
        QList<QUrl> files; for (const auto &path : paths) files.append(QUrl::fromLocalFile(path));
        library.addFiles(files); QTRY_VERIFY(!library.scanning()); QCOMPARE(library.trackCount(), 3);
        bool automaticScan = false;
        connect(&library, &TrackListModel::scanningChanged, &library, [&] { automaticScan |= library.scanning(); });
        QSignalSpy loaded(&library, &TrackListModel::batchMetadataLoaded), saved(&library, &TrackListModel::batchMetadataSaved);
        library.loadBatchMetadata(urls); QTRY_COMPARE(loaded.size(), 1);
        auto details = loaded.takeFirst().first().toMap();
        QCOMPARE(details.value("fields").toMap().value("album").toString(), QStringLiteral("Shared album"));
        QVERIFY(details.value("mixed").toMap().value("artist").toBool());
        QVERIFY(details.value("mixed").toMap().value("trackNumber").toBool());
        QVERIFY(details.value("editable").toBool());
        QVERIFY(library.saveBatchMetadata(urls, {{"album", "One release"}, {"albumArtist", "The Collective"}, {"genre", "Jazz"}}, "keep", {}, details.value("revisions").toMap()));
        QTRY_COMPARE_WITH_TIMEOUT(saved.size(), 1, 10000);
        auto result = saved.takeFirst().first().toMap();
        QCOMPARE(result.value("saved").toMap().size(), 3); QVERIFY(result.value("errors").toList().isEmpty());
        QCOMPARE(library.albums()->count(), 1); QVERIFY(!automaticScan);
        QCOMPARE(library.genres()->count(), 1); QCOMPARE(library.tracksForGroup("genre", "genre:jazz").size(), 3);
        for (int i = 0; i < 3; ++i) {
            const auto actual = FileTags::read(paths[i], directory + "/independent", true);
            const auto fields = actual.value("fields").toMap();
            QCOMPARE(fields.value("title").toString(), "Title " + QString::number(i));
            QCOMPARE(fields.value("artist").toString(), "Artist " + QString::number(i));
            QCOMPARE(fields.value("comment").toString(), "Keep " + QString::number(i));
            QCOMPARE(fields.value("trackNumber").toString().toInt(), i + 1);
            QCOMPARE(fields.value("album").toString(), QStringLiteral("One release"));
            QCOMPARE(actual.value("embeddedCover").toBool(), i == 0);
            QCOMPARE(library.trackForUrl(urls[i]).value("trackNumber").toInt(), i + 1);
        }
        library.loadBatchMetadata(urls); QTRY_COMPARE(loaded.size(), 1);
        details = loaded.takeFirst().first().toMap();
        const auto external = FileTags::read(paths[0], directory, true);
        QVERIFY(!FileTags::write(paths[0], {{"comment", "Changed elsewhere"}}, "keep", {}, external.value("revision").toString(), directory).contains("error"));
        const auto untouched = fileBytes(paths[0]);
        QVERIFY(library.saveBatchMetadata(urls, {{"genre", "Soul"}}, "replace", QUrl::fromLocalFile(cover), details.value("revisions").toMap()));
        QTRY_COMPARE_WITH_TIMEOUT(saved.size(), 1, 10000);
        result = saved.takeFirst().first().toMap();
        QCOMPARE(result.value("saved").toMap().size(), 2); QCOMPARE(result.value("errors").toList().size(), 1);
        QCOMPARE(fileBytes(paths[0]), untouched); QVERIFY(!automaticScan);
        QCOMPARE(library.genres()->count(), 2); QCOMPARE(library.tracksForGroup("genre", "genre:jazz").size(), 1);
        QCOMPARE(library.tracksForGroup("genre", "genre:soul").size(), 2);
        QCOMPARE(FileTags::read(paths[1], directory).value("fields").toMap().value("genre").toString(), QStringLiteral("Soul"));
        QVERIFY(FileTags::read(paths[1], directory).value("embeddedCover").toBool());
        QVERIFY(FileTags::read(paths[2], directory).value("embeddedCover").toBool());
        const auto playlist = library.createPlaylist("Batch playlist");
        QCOMPARE(library.addTracksToPlaylist(playlist, urls + urls), 3);
        QCOMPARE(library.tracksForGroup("playlist", playlist).size(), 3);
        library.setTracksFavorite(urls, true); QCOMPARE(library.favoriteCount(), 3);
        QCOMPARE(library.removeTracksFromPlaylist(playlist, {urls[0], urls[2]}), 2);
        QCOMPARE(library.tracksForGroup("playlist", playlist).size(), 1);
        QVERIFY(!automaticScan);
    }

    void bulkTrashUpdatesSavedLibraryWithoutScanning()
    {
        const auto config = m_temp.filePath("batch-trash.ini");
        QSettings settings(config, QSettings::IniFormat); settings.setValue("library/folders", QStringList{}); settings.sync();
        QStringList urls, paths;
        QList<QUrl> files;
        for (int i = 0; i < 3; ++i) {
            const auto path = m_temp.filePath(QString("batch-trash/%1.wav").arg(i)); QVERIFY(writeWave(path));
            paths.append(path); files.append(QUrl::fromLocalFile(path)); urls.append(files.last().toString());
        }
        {
            TrackListModel library(config, false); library.addFiles(files); QTRY_VERIFY(!library.scanning());
            const auto id = library.createPlaylist("Trash batch"); library.addTracksToPlaylist(id, urls); library.setTracksFavorite(urls, true);
            QSignalSpy removed(&library, &TrackListModel::trackRemoved);
            QStandardPaths::setTestModeEnabled(false); // Use this test's isolated XDG_DATA_HOME for Trash.
            const auto result = library.trashTracks({urls[0], urls[0], urls[1], "file:///missing-song.wav"});
            QStandardPaths::setTestModeEnabled(true);
            QCOMPARE(result.value("removed").toStringList().size(), 2); QCOMPARE(result.value("errors").toList().size(), 1);
            QCOMPARE(removed.size(), 2); QCOMPARE(library.trackCount(), 1); QCOMPARE(library.favoriteCount(), 1);
            QVERIFY(!library.scanning()); QCOMPARE(library.tracksForGroup("playlist", id).size(), 1);
            QVERIFY(!QFileInfo::exists(paths[0])); QVERIFY(!QFileInfo::exists(paths[1])); QVERIFY(QFileInfo::exists(paths[2]));
        }
        TrackListModel restored(config, false); QCOMPARE(restored.trackCount(), 1); QVERIFY(!restored.scanning());
        QCOMPARE(restored.tracks().first().toMap().value("fileUrl").toString(), urls[2]);
    }

    void collectionContextMenusAndBatchEditorAreUsable()
    {
        QSettings settings; settings.clear(); settings.setValue("library/folders", QStringList{}); settings.sync();
        QStringList urls, paths;
        QList<QUrl> files;
        for (int i = 0; i < 3; ++i) {
            const auto path = m_temp.filePath(QString("batch-ui/Album/%1 Song.wav").arg(i + 1));
            QVERIFY(writeWave(path, 30, i == 1 ? "B Artist" : "A Artist", "Original album", i + 1));
            const auto native = FileTags::read(path, m_temp.path(), true);
            QVERIFY(!FileTags::write(path, {{"title", "Song " + QString::number(i + 1)}, {"albumArtist", "Collective"}}, "keep", {}, native.value("revision").toString(), m_temp.path()).contains("error"));
            files.append(QUrl::fromLocalFile(path)); paths.append(path); urls.append(files.last().toString());
        }
        { TrackListModel seed(settings.fileName(), false); seed.addFiles(files); QTRY_VERIFY(!seed.scanning()); }
        QQmlApplicationEngine qml; QSignalSpy warnings(&qml, &QQmlApplicationEngine::warnings);
        qml.load(QUrl("qrc:/main.qml")); QVERIFY(!qml.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(qml.rootObjects().first());
        auto *library = window->findChild<TrackListModel *>("library");
        auto *player = window->findChild<PlaybackEngine *>("player"); player->audioOutput()->setMuted(true);
        QSignalSpy scanning(library, &TrackListModel::scanningChanged);
        QTest::qWait(80); QCOMPARE(library->trackCount(), 3); QCOMPARE(scanning.size(), 0);
        QVERIFY(!window->findChild<QObject *>("hero"));
        click(window, "navAlbums"); QTest::qWait(60);
        auto *card = findItem(window->contentItem(), "groupCard0"); QVERIFY(card);
        const auto rightClick = [&] {
            card = findItem(window->contentItem(), "groupCard0");
            QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, card->mapToScene(QPointF(40, 40)).toPoint());
            QTest::qWait(60);
        };
        rightClick(); QVERIFY(window->findChild<QObject *>("groupMenu")->property("visible").toBool());
        QVERIFY(window->property("collectionName").toString().isEmpty()); QVERIFY(window->property("currentUrl").toString().isEmpty());
        click(window, "groupAddToQueue");
        QCOMPARE(qmlList(window, "queueUrls"), QVariantList({urls[0], urls[1], urls[2]}));
        QMetaObject::invokeMethod(window, "playUrl", Q_ARG(QVariant, urls[0])); QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState); player->pause();
        rightClick(); click(window, "groupPlayNext");
        QCOMPARE(qmlList(window, "queueUrls"), QVariantList({urls[0], urls[0], urls[1], urls[2], urls[1], urls[2]}));
        click(window, "navArtists"); QTest::qWait(60); rightClick();
        click(window, "groupDelete"); auto *trash = window->findChild<QObject *>("trashSongDialog"); QTRY_VERIFY(trash->property("visible").toBool());
        QCOMPARE(qmlList(trash, "pendingTracks").size(), 2);
        QMetaObject::invokeMethod(trash, "reject"); for (const auto &path : paths) QVERIFY(QFileInfo::exists(path));
        click(window, "navAlbums"); QTest::qWait(60); click(window, "groupCard0"); QTest::qWait(60);
        const auto currentId = window->property("currentQueueId");
        click(window, "selectSong0"); QCOMPARE(window->property("selectedCount").toInt(), 1);
        QCOMPARE(window->property("currentQueueId"), currentId); QCOMPARE(player->playbackState(), QMediaPlayer::PausedState);
        QCOMPARE(findItem(window->contentItem(), "selectAllSongs")->property("checkState").toInt(), int(Qt::PartiallyChecked));
        click(window, "selectAllSongs"); QCOMPARE(window->property("selectedCount").toInt(), 3);
        click(window, "selectAllSongs"); QCOMPARE(window->property("selectedCount").toInt(), 0);
        click(window, "editAlbumMetadataButton"); QPointer<QObject> editor = window->findChild<QObject *>("batchMetadataEditor"); QVERIFY(editor);
        QTRY_VERIFY(editor->property("ready").toBool()); QCOMPARE(qmlList(editor, "urls").size(), 3);
        const auto original = fileBytes(paths[0]); click(window, "cancelBatchMetadataButton"); QCOMPARE(fileBytes(paths[0]), original);
        click(window, "selectSong0"); click(window, "selectSong1"); QTest::qWait(60);
        QVERIFY(findItem(window->contentItem(), "selectionBar")->isVisible());
        click(window, "editSelectedMetadataButton"); editor = window->findChild<QObject *>("batchMetadataEditor"); QVERIFY(editor);
        QTRY_VERIFY(editor->property("ready").toBool()); QCOMPARE(qmlList(editor, "urls").size(), 2);
        QCOMPARE(qmlList(editor, "urls"), QVariantList({urls[0], urls[1]}));
        const auto initialValue = editor->property("initial");
        const auto initial = initialValue.metaType() == QMetaType::fromType<QJSValue>() ? initialValue.value<QJSValue>().toVariant().toMap() : initialValue.toMap();
        QVERIFY(initial.value("mixed").toMap().value("artist").toBool());
        QVERIFY(!findItem(window->contentItem(), "batchMetadata_artist")->isEnabled());
        click(window, "batchApply_album");
        auto *field = findItem(window->contentItem(), "batchMetadata_album"); QVERIFY(field && field->isEnabled()); field->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        for (const auto character : QStringLiteral("Retagged album")) QTest::keyClick(window, character.toLatin1());
        window->grabWindow().save(QCoreApplication::applicationDirPath() + "/musica-batch-editor.png");
        QSignalSpy saved(library, &TrackListModel::batchMetadataSaved);
        click(window, "saveBatchMetadataButton"); QTRY_COMPARE_WITH_TIMEOUT(saved.size(), 1, 10000);
        QVERIFY(!editor || !editor->property("visible").toBool());
        QCOMPARE(library->trackForUrl(urls[0]).value("album").toString(), QStringLiteral("Retagged album"));
        QCOMPARE(library->trackForUrl(urls[1]).value("album").toString(), QStringLiteral("Retagged album"));
        QCOMPARE(library->trackForUrl(urls[2]).value("album").toString(), QStringLiteral("Original album"));
        QCOMPARE(FileTags::read(paths[0], m_temp.path()).value("fields").toMap().value("title").toString(), QStringLiteral("Song 1"));
        QCOMPARE(library->trackForUrl(urls[1]).value("trackNumber").toInt(), 2);
        QCOMPARE(window->property("currentQueueId"), currentId);
        QTest::qWait(60);
        window->grabWindow().save(QCoreApplication::applicationDirPath() + "/musica-batch-selection.png");
        click(window, "navSongs"); QTest::qWait(40); QCOMPARE(window->property("selectedCount").toInt(), 0);
        click(window, "selectSong0"); click(window, "selectSong1");
        click(window, "selectedSongsOptions"); QTest::qWait(40); click(window, "deleteSelectedSongs");
        QTRY_VERIFY(trash->property("visible").toBool());
        QStandardPaths::setTestModeEnabled(false);
        QVERIFY(QMetaObject::invokeMethod(trash, "accept"));
        QStandardPaths::setTestModeEnabled(true);
        QTRY_COMPARE(library->trackCount(), 1);
        QCOMPARE(window->property("currentUrl").toString(), urls[2]);
        for (const auto &url : qmlList(window, "queueUrls")) QCOMPARE(url.toString(), urls[2]);
        QVERIFY(!QFileInfo::exists(paths[0])); QVERIFY(!QFileInfo::exists(paths[1])); QVERIFY(QFileInfo::exists(paths[2]));
        QCOMPARE(warnings.size(), 0); player->stop();
    }

    void embeddedTagsAndCoversTravelWithTheAudio_data()
    {
        QTest::addColumn<QString>("extension"); QTest::addColumn<QString>("codec");
        QTest::newRow("MP3") << QString("mp3") << QString("libmp3lame");
        QTest::newRow("FLAC") << QString("flac") << QString("flac");
        QTest::newRow("M4A") << QString("m4a") << QString("aac");
        QTest::newRow("Vorbis") << QString("ogg") << QString("libvorbis");
        QTest::newRow("Opus") << QString("opus") << QString("libopus");
        QTest::newRow("WAV") << QString("wav") << QString("pcm_s16le");
        QTest::newRow("AIFF") << QString("aiff") << QString("pcm_s16be");
        QTest::newRow("WMA") << QString("wma") << QString("wmav2");
    }

    void embeddedTagsAndCoversTravelWithTheAudio()
    {
        QFETCH(QString, extension); QFETCH(QString, codec);
        const auto directory = m_temp.filePath("portable-tags/" + extension); QDir().mkpath(directory);
        const auto source = directory + "/source.wav", audio = directory + "/Song." + extension;
        QVERIFY(writeSignal(source, audioSignal(1, 44100), 44100));
        bool ok = false;
        const auto encoded = runMediaTool("ffmpeg", {"-nostdin", "-v", "error", "-y", "-i", source, "-map", "0:a:0", "-c:a", codec, "-metadata", "title=Before", "-metadata", "artist=Original artist", "-metadata", "copyright=Keep this unrelated tag", audio}, &ok);
        QVERIFY2(ok, encoded.constData());
        const auto hashBefore = runMediaTool("ffmpeg", {"-nostdin", "-v", "error", "-i", audio, "-map", "0:a:0", "-f", "hash", "-hash", "sha256", "-"}, &ok); QVERIFY2(ok, hashBefore.constData());
        const auto cover = directory + "/Chosen cover.png"; QImage image(640, 480, QImage::Format_RGB32); image.fill(QColor("#347a58")); QVERIFY(image.save(cover));
        auto before = FileTags::read(audio, directory + "/cache", true); QVERIFY2(!before.contains("error"), qPrintable(before.value("error").toString()));
        const QVariantMap changes{{"title", QString::fromUtf8("Été · 海")}, {"artist", "New artist"}, {"album", "Portable album"},
            {"albumArtist", "The Collective"}, {"genre", "Jazz"}, {"date", "2026"}, {"composer", "A composer"}, {"comment", "Edited entirely offline"},
            {"trackNumber", "2"}, {"trackTotal", "12"}, {"discNumber", "1"}, {"discTotal", "2"}};
        const auto saved = FileTags::write(audio, changes, "replace", QUrl::fromLocalFile(cover), before.value("revision").toString(), directory + "/cache");
        QVERIFY2(!saved.contains("error"), qPrintable(saved.value("error").toString()));
        const auto hashAfter = runMediaTool("ffmpeg", {"-nostdin", "-v", "error", "-i", audio, "-map", "0:a:0", "-f", "hash", "-hash", "sha256", "-"}, &ok); QVERIFY2(ok, hashAfter.constData()); QCOMPARE(hashAfter, hashBefore);
        const auto probeBytes = runMediaTool("ffprobe", {"-v", "error", "-show_entries", "format_tags:stream_tags:stream_disposition=attached_pic", "-of", "json", audio}, &ok); QVERIFY2(ok, probeBytes.constData());
        const auto probe = QJsonDocument::fromJson(probeBytes).object();
        QJsonObject externalTags = probe.value("format").toObject().value("tags").toObject(); bool attached = false;
        for (const auto &stream : probe.value("streams").toArray()) {
            const auto object = stream.toObject(); const bool picture = object.value("disposition").toObject().value("attached_pic").toInt() != 0;
            attached |= picture;
            if (picture) continue;
            const auto tags = object.value("tags").toObject();
            for (auto it = tags.begin(); it != tags.end(); ++it) externalTags.insert(it.key(), it.value());
        }
        QMap<QString, QString> tags;
        for (auto it = externalTags.begin(); it != externalTags.end(); ++it) tags[it.key().toLower()] = it.value().toString();
        QCOMPARE(tags.value("title"), changes.value("title").toString());
        QCOMPARE(tags.value("artist"), QStringLiteral("New artist")); QCOMPARE(tags.value("album"), QStringLiteral("Portable album"));
        QCOMPARE(tags.value("copyright"), QStringLiteral("Keep this unrelated tag"));
        QVERIFY2(attached, probeBytes.constData());
        // A copied file and a fresh settings file must work without Musica's cache.
        const auto moved = directory + "/Second machine/Song." + extension; QDir().mkpath(QFileInfo(moved).absolutePath()); QVERIFY(QFile::copy(audio, moved));
        const auto copied = FileTags::read(moved, directory + "/new-cache", true); QVERIFY(!copied.contains("error"));
        QCOMPARE(copied.value("fields").toMap().value("albumArtist").toString(), QStringLiteral("The Collective"));
        QCOMPARE(copied.value("coverHash").toString(), QString::fromLatin1(QCryptographicHash::hash(fileBytes(cover), QCryptographicHash::Sha256).toHex()));
        const auto config = directory + "/other-machine.ini"; QSettings settings(config, QSettings::IniFormat); settings.setValue("library/folders", QStringList{}); settings.sync();
        TrackListModel library(config, false); library.addFiles({QUrl::fromLocalFile(moved)}); QTRY_VERIFY(!library.scanning());
        const auto track = library.trackForUrl(QUrl::fromLocalFile(moved).toString());
        QCOMPARE(track.value("title").toString(), changes.value("title").toString());
        QCOMPARE(track.value("trackNumber").toInt(), 2); QCOMPARE(track.value("discNumber").toInt(), 1);
        QVERIFY(!track.value("artwork").toString().isEmpty());
    }

    void unsafeMetadataSavesLeaveTheFileUnchanged()
    {
        const auto directory = m_temp.filePath("safe-tags"); QDir().mkpath(directory);
        const auto audio = directory + "/Artist - Original.wav"; QVERIFY(writeWave(audio, 2, "Artist", "Album"));
        const auto beforeBytes = fileBytes(audio); const auto before = FileTags::read(audio, {}, true); QVERIFY(!before.contains("error"));
        auto result = FileTags::write(audio, {{"title", "New"}}, "keep", {}, "stale", {}); QVERIFY(result.contains("error")); QCOMPARE(fileBytes(audio), beforeBytes);
        const auto invalidCover = directory + "/broken.png"; QFile broken(invalidCover); QVERIFY(broken.open(QIODevice::WriteOnly)); broken.write("not an image"); broken.close();
        result = FileTags::write(audio, {{"title", "New"}}, "replace", QUrl::fromLocalFile(invalidCover), before.value("revision").toString(), {});
        QVERIFY(result.contains("error")); QCOMPARE(fileBytes(audio), beforeBytes);
        result = FileTags::write(audio, {{"trackNumber", "10000"}}, "keep", {}, before.value("revision").toString(), {}); QVERIFY(result.contains("error")); QCOMPARE(fileBytes(audio), beforeBytes);
        const auto permissions = QFileInfo(audio).permissions(); QVERIFY(QFile::setPermissions(audio, QFile::ReadOwner));
        result = FileTags::write(audio, {{"title", "New"}}, "keep", {}, before.value("revision").toString(), {}); QVERIFY(result.contains("error"));
        QVERIFY(QFile::setPermissions(audio, permissions)); QCOMPARE(fileBytes(audio), beforeBytes);
        const auto cover = directory + "/cover.jpg"; QImage image(300, 300, QImage::Format_RGB32); image.fill(Qt::red); QVERIFY(image.save(cover));
        result = FileTags::write(audio, {{"title", "Edited"}}, "replace", QUrl::fromLocalFile(cover), before.value("revision").toString(), directory + "/cache"); QVERIFY2(!result.contains("error"), qPrintable(result.value("error").toString()));
        const auto firstCover = result.value("coverHash");
        result = FileTags::write(audio, {{"comment", "Only text changed"}}, "keep", {}, result.value("revision").toString(), directory + "/cache"); QVERIFY(!result.contains("error")); QCOMPARE(result.value("coverHash"), firstCover);
        result = FileTags::write(audio, {}, "remove", {}, result.value("revision").toString(), directory + "/cache"); QVERIFY2(!result.contains("error"), qPrintable(result.value("error").toString())); QVERIFY(!result.value("embeddedCover").toBool());
        QCOMPARE(result.value("fields").toMap().value("title").toString(), QStringLiteral("Edited"));
        QCOMPARE(QFileInfo(audio).permissions(), permissions);
    }

    void metadataEditorSavesPortableTagsAndCoverAndCancelDiscardsChanges()
    {
        const auto directory = m_temp.filePath("metadata-ui");
        const auto audio = directory + "/Original - Song.wav"; QVERIFY(writeWave(audio, 30, "Original", "Old album"));
        const auto url = QUrl::fromLocalFile(audio).toString();
        const auto cover = directory + "/Manual cover.png"; QImage image(700, 700, QImage::Format_RGB32); image.fill(QColor("#366b94")); QVERIFY(image.save(cover));
        QSettings settings; settings.clear(); settings.setValue("library/folders", QStringList{}); settings.setValue("library/files", QStringList{audio});
        settings.setValue("library/favorites", QStringList{url}); settings.setValue("library/playCounts", QVariantMap{{url, 3}}); settings.sync();
        QQmlApplicationEngine engine; QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        engine.load(QUrl("qrc:/main.qml")); QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); auto *library = window->findChild<TrackListModel *>("library"); auto *player = window->findChild<PlaybackEngine *>("player");
        library->rescan(); // New test fixtures are discovered by a manual scan.
        QTRY_VERIFY(library->libraryReady() && !library->scanning());
        const auto playlist = library->createPlaylist("Keep my place", url); QVERIFY(!playlist.isEmpty());
        click(window, "navSongs"); QTest::qWait(60); click(window, "playAllButton"); QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
        const auto original = fileBytes(audio);
        click(window, "songOptions0"); QTest::qWait(50); click(window, "menuEditMetadata");
        QPointer<QObject> editor = window->findChild<QObject *>("metadataEditor"); QVERIFY(editor); QTRY_VERIFY(editor->property("ready").toBool());
        auto *title = findItem(window->contentItem(), "metadata_title"); QVERIFY(title); title->setProperty("text", "Discard this draft");
        click(window, "cancelMetadataButton"); QCOMPARE(fileBytes(audio), original);
        click(window, "songOptions0"); QTest::qWait(50); click(window, "menuEditMetadata");
        editor = window->findChild<QObject *>("metadataEditor"); QVERIFY(editor); QTRY_VERIFY(editor->property("ready").toBool());
        title = findItem(window->contentItem(), "metadata_title"); QVERIFY(title); QCOMPARE(title->property("text").toString(), QString());
        title->setProperty("text", QString::fromUtf8("Édition finale"));
        findItem(window->contentItem(), "metadata_artist")->setProperty("text", "Manual artist");
        findItem(window->contentItem(), "metadata_album")->setProperty("text", "New album");
        findItem(window->contentItem(), "metadata_albumArtist")->setProperty("text", "The Collective");
        findItem(window->contentItem(), "metadata_trackNumber")->setProperty("text", "3");
        findItem(window->contentItem(), "metadata_discNumber")->setProperty("text", "2");
        auto *chooser = window->findChild<QObject *>("chooseMetadataCoverDialog"); QVERIFY(chooser);
        chooser->setProperty("selectedFile", QUrl::fromLocalFile(cover)); QVERIFY(QMetaObject::invokeMethod(chooser, "accepted"));
        QCOMPARE(editor->property("coverAction").toString(), QStringLiteral("replace"));
        QTest::qWait(60); window->grabWindow().save(QCoreApplication::applicationDirPath() + "/musica-metadata-editor.png");
        window->resize(900, 640); QTest::qWait(60);
        auto *save = findItem(window->contentItem(), "saveMetadataButton"); QVERIFY(save);
        const auto savePosition = save->mapToScene(QPointF()); QVERIFY(savePosition.y() >= 0 && savePosition.y() + save->height() <= window->height());
        QSignalSpy saved(library, &TrackListModel::metadataSaved), errors(library, &TrackListModel::metadataError);
        click(window, "saveMetadataButton"); QTRY_COMPARE_WITH_TIMEOUT(saved.size(), 1, 10000); QTRY_VERIFY(!library->scanning());
        QCOMPARE(errors.size(), 0); QVERIFY(!editor || !editor->property("visible").toBool());
        const auto track = library->trackForUrl(url); QCOMPARE(track.value("title").toString(), QString::fromUtf8("Édition finale"));
        QCOMPARE(track.value("artist").toString(), QStringLiteral("Manual artist")); QCOMPARE(track.value("albumArtist").toString(), QStringLiteral("The Collective"));
        QCOMPARE(track.value("trackNumber").toInt(), 3); QCOMPARE(track.value("discNumber").toInt(), 2);
        QVERIFY(track.value("embeddedCover").toBool()); QVERIFY(!track.value("artwork").toString().isEmpty());
        QCOMPARE(library->favoriteCount(), 1); QCOMPARE(track.value("playCount").toInt(), 3);
        QCOMPARE(library->tracksForGroup("playlist", playlist).first().toMap().value("title").toString(), track.value("title").toString());
        QCOMPARE(window->property("currentUrl").toString(), url); QCOMPARE(player->playbackState(), QMediaPlayer::PlayingState);
        QVERIFY(window->title().contains(QString::fromUtf8("Édition finale")));
        const auto independent = FileTags::read(audio, directory + "/other-cache", true); QCOMPARE(independent.value("fields").toMap().value("title").toString(), track.value("title").toString());
        QCOMPARE(warnings.size(), 0); player->stop();
    }

    void pcmMixerJoinsTracksWithoutInsertingSilence()
    {
        auto first = constantPcm(0.2f, 1000), second = constantPcm(0.4f, 1000); QVERIFY(first && second);
        AudioMixer mixer(1000, 2); mixer.setCurrent(first, "one"); mixer.setNext(second, "two");
        QByteArray output; QList<MixerEvent> events;
        for (int i = 0; i < 30 && !mixer.ended(); ++i) {
            const auto block = mixer.render(137); const qint64 base = output.size() / 8;
            for (auto event : block.events) { event.offset += base; events.append(event); }
            output.append(block.pcm);
        }
        QCOMPARE(output.size(), 2000 * 8);
        const float *samples = reinterpret_cast<const float *>(output.constData());
        QCOMPARE(samples[999 * 2], 0.2f); QCOMPARE(samples[1000 * 2], 0.4f);
        for (int i = 0; i < 4000; ++i) QVERIFY(samples[i] > 0);
        const auto next = std::find_if(events.cbegin(), events.cend(), [](const auto &event) { return event.kind == MixerEvent::Started && event.entryId == "two"; });
        QVERIFY(next != events.cend()); QCOMPARE(next->offset, 1000);
        // Repeating the same queue entry still gets an independent listening token.
        mixer.setCurrent(first, "one"); mixer.setNext(first, "one");
        const auto repeated = mixer.render(1500); QStringList tokens;
        for (const auto &event : repeated.events) if (event.kind == MixerEvent::Started) tokens.append(event.token);
        QCOMPARE(tokens.size(), 2); QVERIFY(tokens[0] != tokens[1]);
    }

    void crossfadeMixesAudioAndCapsTheTransition()
    {
        auto first = constantPcm(0.2f, 5000), second = constantPcm(0.4f, 5000); QVERIFY(first && second);
        AudioMixer mixer(1000, 2); mixer.crossfadeSeconds = 2; mixer.setCurrent(first, "one"); mixer.setNext(second, "two");
        QByteArray output;
        for (int i = 0; i < 100 && !mixer.ended(); ++i) output.append(mixer.render(111).pcm);
        QCOMPARE(output.size(), 8000 * 8);
        const float *data = reinterpret_cast<const float *>(output.constData());
        QVERIFY(std::abs(data[3000 * 2] - 0.2f) < 0.001f);
        QVERIFY(std::abs(data[4000 * 2] - float(0.6 / std::sqrt(2.0))) < 0.002f);
        QVERIFY(std::abs(data[5000 * 2] - 0.4f) < 0.001f);
        first = constantPcm(0.2f, 10000); second = constantPcm(0.4f, 10000);
        const auto plan = AudioMixer::transitionPlan(*first, *second, 3.2);
        QCOMPARE(plan.first, 6800); QCOMPARE(plan.second, 0);
        QCOMPARE(AudioMixer::transitionPlan(*first, *second, 0).first, 10000);
        auto longTrack = constantPcm(0.2f, 180000), longNext = constantPcm(0.4f, 200000);
        QCOMPARE(AudioMixer::transitionPlan(*longTrack, *longNext, 100).first, 110000); // Hard limit: 70 seconds.
        auto shortNext = constantPcm(0.4f, 1000);
        QCOMPARE(AudioMixer::transitionPlan(*first, *shortNext, 70).first, 9001);
        mixer.clear(); mixer.setCurrent(first, "one"); mixer.setNext(second, "two"); mixer.stopAfterCurrent = true; mixer.crossfadeSeconds = 2;
        const auto sleeping = mixer.render(22000); QCOMPARE(sleeping.pcm.size(), 10000 * 8);
        for (const auto &event : sleeping.events) QVERIFY(event.kind != MixerEvent::Started || event.entryId == "one");
    }

    void playbackContinuesWhileTheInterfaceSavesSettings()
    {
        const auto config = m_temp.filePath("playback-continuity.ini");
        const auto song = QUrl::fromLocalFile(m_temp.filePath("continuity/Long.wav"));
        QVERIFY(writeSignal(song.toLocalFile(), audioSignal(14, 11025), 11025));
        // The real checkpoint rewrites an INI file containing the library cache.
        QVariantMap metadata;
        for (int i = 0; i < 5000; ++i)
            metadata.insert(QString("file:///fixture/%1.flac").arg(i), QVariantMap{
                {"title", QString("Library song %1").arg(i)}, {"artist", "Artist"},
                {"album", "Album"}, {"duration", 240000}, {"probed", true}});
        QSettings settings(config, QSettings::IniFormat);
        settings.setValue("library/folders", QStringList{});
        settings.setValue("library/metadata", metadata); settings.sync();
        TrackListModel library(config, false); library.addFiles({song}); QTRY_VERIFY(!library.scanning());
        PlaybackEngine engine; engine.audioOutput()->setMuted(true);
        engine.setCrossfadeEnabled(false);
        QSignalSpy errors(&engine, &PlaybackEngine::errorOccurred);
        connect(&engine, &PlaybackEngine::listeningProgress, &library,
            [&library](const QString &token, const QUrl &url, qint64 position, qint64 duration, bool playing, bool finished, qint64 timestamp) {
                library.observeAudiblePlayback(token, url.toString(), position, duration, playing, finished, timestamp);
            });
        engine.playEntry("long", song, library.trackForUrl(song.toString()));
        QTRY_VERIFY_WITH_TIMEOUT(engine.outputStatistics().processedUs > 200000, 3000);
        QElapsedTimer clock; clock.start();
        for (const int checkpoint : {4800, 9800}) {
            while (clock.elapsed() < checkpoint) QTest::qWait(20);
            const auto before = engine.outputStatistics();
            const QVariantMap entry{{"id", "long"}, {"fileUrl", song.toString()}};
            library.savePlaybackSession({{"queue", QVariantList{entry}}, {"source", QVariantList{entry}}, {"currentId", "long"}, {"position", engine.position()}});
            // Deterministic slow-storage/UI work, much longer than the output buffer.
            // No Qt events are processed on the interface thread during this time.
            QThread::msleep(600);
            const auto after = engine.outputStatistics();
            QVERIFY2(after.submittedFrames - before.submittedFrames >= before.sampleRate * 0.4,
                "Audio feeding stopped while the interface thread was busy saving settings");
            QVERIFY2(after.processedUs - before.processedUs >= 400000,
                "The audio output ran dry during a session checkpoint");
            QCOMPARE(after.underruns, before.underruns);
            QTest::qWait(30);
            QCOMPARE(engine.playbackState(), QMediaPlayer::PlayingState);
        }
        engine.pause();
        QVERIFY(library.dashboard().value("totalMs").toLongLong() > 10000);
        QCOMPARE(errors.size(), 0);
        settings.sync();
        QCOMPARE(library.playbackSession().value("currentId").toString(), QString("long"));
        engine.stop();
    }

    void transitionsSeeksSleepAndSessionSavesSurviveABusyInterface()
    {
        const auto first = QUrl::fromLocalFile(m_temp.filePath("busy-interface/First.wav"));
        const auto second = QUrl::fromLocalFile(m_temp.filePath("busy-interface/Second.wav"));
        QVERIFY(writeWave(first.toLocalFile(), 2)); QVERIFY(writeWave(second.toLocalFile(), 4));
        QSettings settings; settings.clear(); settings.setValue("library/folders", QStringList{});
        settings.setValue("library/files", QStringList{first.toLocalFile(), second.toLocalFile()}); settings.sync();
        QQmlApplicationEngine qml;
        QSignalSpy warnings(&qml, &QQmlApplicationEngine::warnings);
        qml.load(QUrl("qrc:/main.qml")); QVERIFY(!qml.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(qml.rootObjects().first());
        auto *library = window->findChild<TrackListModel *>("library");
        library->rescan(); // New test fixtures are discovered by a manual scan.
        auto *engine = window->findChild<PlaybackEngine *>("player"); QVERIFY(library && engine);
        QTRY_VERIFY(!library->scanning());
        engine->audioOutput()->setMuted(true);
        engine->setGaplessEnabled(true); engine->setCrossfadeEnabled(true); engine->setCrossfadeSeconds(0.6);
        QSignalSpy advanced(engine, &PlaybackEngine::trackAdvanced), slept(engine, &PlaybackEngine::sleepTriggered);
        QSignalSpy errors(engine, &PlaybackEngine::errorOccurred);
        click(window, "navSongs"); QTest::qWait(60); click(window, "playAllButton");
        QTRY_VERIFY(engine->position() >= 800);
        const auto queued = qmlList(window, "queueItems"); QCOMPARE(queued.size(), 2);
        const auto secondId = queued[1].toMap().value("id").toString();
        QThread::msleep(1000); // Crossfade begins on the audio thread while QML is blocked.
        QVERIFY(QMetaObject::invokeMethod(window, "persistSession"));
        QCOMPARE(advanced.size(), 1); QCOMPARE(engine->source(), second);
        QCOMPARE(window->property("currentQueueId").toString(), secondId);
        settings.sync();
        QCOMPARE(library->playbackSession().value("currentId").toString(), secondId);
        QTRY_COMPARE(library->trackForUrl(first.toString()).value("playCount").toInt(), 1);

        engine->pause(); const auto paused = engine->position();
        QTest::qWait(120); QCOMPARE(engine->position(), paused); // Queued old positions cannot undo pause.
        engine->setPosition(0); QCOMPARE(engine->position(), 0);
        QTest::qWait(60); QCOMPARE(engine->position(), 0); // Nor can they undo a seek.
        engine->play(); engine->startSleepSeconds(1);
        QThread::msleep(1300); // The sleep deadline must not depend on the UI event loop.
        engine->checkpoint();
        QCOMPARE(slept.size(), 1); QCOMPARE(engine->playbackState(), QMediaPlayer::PausedState);
        QCOMPARE(engine->sleepRemainingMs(), 0);
        const auto sleptAt = engine->position(); QTest::qWait(100); QCOMPARE(engine->position(), sleptAt);
        QCOMPARE(warnings.size(), 0); QCOMPARE(errors.size(), 0);
        engine->stop(); engine->setCrossfadeEnabled(false);
    }

    void realCrossfadeCountsBothSongsAndSleepPreventsAdvancing()
    {
        const auto config = m_temp.filePath("transitions.ini"); QSettings settings(config, QSettings::IniFormat); settings.setValue("library/folders", QStringList{}); settings.sync();
        const auto first = QUrl::fromLocalFile(m_temp.filePath("transitions/First.wav")), second = QUrl::fromLocalFile(m_temp.filePath("transitions/Second.wav"));
        QVERIFY(writeWave(first.toLocalFile(), 2)); QVERIFY(writeWave(second.toLocalFile(), 2));
        TrackListModel library(config, false); library.addFiles({first, second}); QTRY_VERIFY(!library.scanning());
        PlaybackEngine engine; engine.audioOutput()->setMuted(true);
        connect(&engine, &PlaybackEngine::listeningProgress, &library, [&library](const QString &token, const QUrl &url, qint64 position, qint64 duration, bool playing, bool finished, qint64 timestamp) { library.observeAudiblePlayback(token, url.toString(), position, duration, playing, finished, timestamp); });
        connect(&engine, &PlaybackEngine::listeningSeeked, &library, &TrackListModel::seekAudiblePlayback);
        QSignalSpy advanced(&engine, &PlaybackEngine::trackAdvanced), requests(&engine, &PlaybackEngine::advanceRequested), slept(&engine, &PlaybackEngine::sleepTriggered), errors(&engine, &PlaybackEngine::errorOccurred);
        engine.setGaplessEnabled(true); engine.setCrossfadeEnabled(true); engine.setCrossfadeSeconds(1);
        engine.playEntry("first", first, library.trackForUrl(first.toString()));
        engine.prepareNext("second", second, library.trackForUrl(second.toString()));
        QTRY_VERIFY_WITH_TIMEOUT(engine.crossfading(), 3000);
        QCOMPARE(advanced.size(), 1); QCOMPARE(engine.source(), second);
        engine.pause(); const auto pausedAt = engine.position(); QTest::qWait(120); QCOMPARE(engine.position(), pausedAt); engine.play();
        QTRY_COMPARE_WITH_TIMEOUT(engine.playbackState(), QMediaPlayer::StoppedState, 3500);
        QTRY_COMPARE(library.mostPlayed()->count(), 2);
        QCOMPARE(library.trackForUrl(first.toString()).value("playCount").toInt(), 1);
        QCOMPARE(library.trackForUrl(second.toString()).value("playCount").toInt(), 1);
        QVERIFY(library.dashboard().value("totalMs").toLongLong() >= 3800);
        advanced.clear(); requests.clear();
        engine.playEntry("first", first, library.trackForUrl(first.toString())); engine.prepareNext("second", second, library.trackForUrl(second.toString())); engine.startSleepSeconds(1);
        QTRY_COMPARE_WITH_TIMEOUT(slept.size(), 1, 2000);
        QCOMPARE(engine.playbackState(), QMediaPlayer::PausedState); QCOMPARE(engine.sleepRemainingMs(), 0);
        const auto sleepPosition = engine.position(); QTest::qWait(100); QCOMPARE(engine.position(), sleepPosition);
        engine.playEntry("first", first, library.trackForUrl(first.toString())); engine.prepareNext("second", second, library.trackForUrl(second.toString())); engine.sleepAtEndOfTrack();
        advanced.clear(); requests.clear();
        QTRY_COMPARE_WITH_TIMEOUT(slept.size(), 2, 3000);
        QCOMPARE(engine.playbackState(), QMediaPlayer::StoppedState); QCOMPARE(engine.source(), first);
        QCOMPARE(advanced.size(), 0); QCOMPARE(requests.size(), 0); QVERIFY(!engine.sleepAfterTrack());
        QCOMPARE(errors.size(), 0);
    }

    void mainPlayerTransitionControlsWork()
    {
        QSettings settings; settings.clear(); settings.setValue("library/folders", QStringList{});
        settings.setValue("library/analysis", QVariantMap{{"old-file", QVariantMap{{"bpm", 120}}}});
        settings.setValue("playback/quantize", true); settings.sync();
        QQmlApplicationEngine engine; QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        engine.load(QUrl("qrc:/main.qml")); QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        auto *library = window->findChild<TrackListModel *>("library"); auto *player = window->findChild<PlaybackEngine *>("player");
        QTRY_VERIFY(library->libraryReady() && !library->scanning());
        settings.sync(); QVERIFY(!settings.contains("library/analysis")); QVERIFY(!settings.contains("playback/quantize"));
        QCOMPARE(library->metaObject()->indexOfProperty("analyzing"), -1);
        QCOMPARE(library->songs()->metaObject()->indexOfProperty("musicMatch"), -1);
        QCOMPARE(player->metaObject()->indexOfProperty("quantizeFade"), -1);
        for (const auto &name : {"analyzeLibraryButton", "footerMusicalMatches", "musicalMatchMode", "quantizeFadeCheckBox"})
            QVERIFY(!window->findChild<QObject *>(name));

        QList<QUrl> files;
        for (int i = 0; i < 3; ++i) { const auto path = m_temp.filePath(QStringLiteral("mixing-ui/%1 Song.wav").arg(i)); QVERIFY(writeWave(path, 30)); files.append(QUrl::fromLocalFile(path)); }
        library->addFiles(files); QTRY_VERIFY(!library->scanning());
        QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, QString("Songs"))); QTest::qWait(60); click(window, "playAllButton");
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
        QTest::qWait(60); click(window, "crossfadeSettingsButton"); QTest::qWait(60);
        QVERIFY(window->findChild<QObject *>("playbackOptions")->property("visible").toBool());
        click(window, "crossfadeCheckBox"); QVERIFY(player->crossfadeEnabled());
        player->setCrossfadeSeconds(70); QTRY_COMPARE(window->findChild<QObject *>("crossfadeSecondsBox")->property("value").toInt(), 70);
        player->setCrossfadeSeconds(0); QCOMPARE(player->crossfadeSeconds(), 0.0);
        window->grabWindow().save(QCoreApplication::applicationDirPath() + "/musica-transition-controls.png");
        QMetaObject::invokeMethod(window->findChild<QObject *>("playbackOptions"), "close");
        click(window, "sleepTimerButton"); QTest::qWait(50);
        window->findChild<QObject *>("sleepMinutesBox")->setProperty("value", 1); click(window, "startSleepTimerButton");
        QVERIFY(player->sleepRemainingMs() > 58000 && player->sleepRemainingMs() <= 60000);
        click(window, "sleepTimerButton"); QTest::qWait(50); click(window, "cancelSleepTimerButton"); QCOMPARE(player->sleepRemainingMs(), 0);
        click(window, "sleepAfterTrackButton"); QVERIFY(player->sleepAfterTrack());
        QMetaObject::invokeMethod(window->findChild<QObject *>("sleepTimerPopup"), "close");
        click(window, "playPauseButton"); QTRY_COMPARE(player->playbackState(), QMediaPlayer::PausedState);
        click(window, "playPauseButton"); QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
        click(window, "footerFavoriteButton"); QCOMPARE(library->favoriteCount(), 1);
        QVERIFY(window->isVisible());
        QVERIFY(!window->findChild<QQuickWindow *>("miniPlayer"));
        player->cancelSleep(); player->setCrossfadeEnabled(false);
        window->resize(900, 640); QTest::qWait(60); click(window, "crossfadeSettingsButton"); QTest::qWait(60);
        QVERIFY(findItem(window->contentItem(), "crossfadeSecondsBox")->mapToScene(QPointF()).y() >= 0);
        for (const auto &name : {"crossfadeSettingsButton", "sleepTimerButton", "audioSettingsButton"}) {
            auto *button = findItem(window->contentItem(), name); QVERIFY(button);
            const auto point = button->mapToScene(QPointF()); QVERIFY(point.x() >= 0 && point.x() + button->width() <= window->width());
        }
        QCOMPARE(warnings.size(), 0); player->stop();
    }

    void mainPlayerStaysReachableAcrossWindowStates()
    {
        QSettings settings; settings.clear(); settings.setValue("library/folders", QStringList{}); settings.sync();
        QQmlApplicationEngine engine; QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        engine.load(QUrl("qrc:/main.qml")); QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(!window->findChild<QQuickWindow *>("miniPlayer"));
        QVERIFY(!window->findChild<QObject *>("miniPlayerButton"));
        auto *display = window->property("displayPreferences").value<DisplaySettings *>(); QVERIFY(display);
        QVERIFY(!display->shortcuts().contains("miniPlayer"));
        for (const auto &value : display->shortcutOptions())
            QVERIFY(value.toMap().value("id").toString() != QStringLiteral("miniPlayer"));
        QTRY_VERIFY(window->findChild<TrackListModel *>("library")->libraryReady());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate(); QTest::qWait(40);
        QTest::keyClick(window, Qt::Key_M, Qt::ControlModifier);
        QCOMPARE(window->visibility(), QWindow::Windowed);
        window->showMinimized();
        QTRY_COMPARE(window->visibility(), QWindow::Minimized);
        QVERIFY(QMetaObject::invokeMethod(window, "desktopCommand", Q_ARG(QVariant, "raise"), Q_ARG(QVariant, QVariant())));
        QTRY_COMPARE(window->visibility(), QWindow::Windowed);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->showMaximized(); QTRY_COMPARE(window->visibility(), QWindow::Maximized);
        QVERIFY(QMetaObject::invokeMethod(window, "desktopCommand", Q_ARG(QVariant, "raise"), Q_ARG(QVariant, QVariant())));
        QCOMPARE(window->visibility(), QWindow::Maximized);
        window->showMinimized(); QTRY_COMPARE(window->visibility(), QWindow::Minimized);
        QVERIFY(QMetaObject::invokeMethod(window, "desktopCommand", Q_ARG(QVariant, "raise"), Q_ARG(QVariant, QVariant())));
        QTRY_COMPARE(window->visibility(), QWindow::Maximized);
        QVERIFY(QMetaObject::invokeMethod(window, "toggleFullscreenNowPlaying"));
        QTRY_COMPARE(window->visibility(), QWindow::FullScreen);
        QVERIFY(QMetaObject::invokeMethod(window, "desktopCommand", Q_ARG(QVariant, "raise"), Q_ARG(QVariant, QVariant())));
        QCOMPARE(window->visibility(), QWindow::FullScreen);
        QVERIFY(window->property("fullscreenNowPlaying").toBool());
        window->showMinimized(); QTRY_COMPARE(window->visibility(), QWindow::Minimized);
        QVERIFY(QMetaObject::invokeMethod(window, "desktopCommand", Q_ARG(QVariant, "raise"), Q_ARG(QVariant, QVariant())));
        QTRY_COMPARE(window->visibility(), QWindow::FullScreen);
        QVERIFY(window->property("fullscreenNowPlaying").toBool());
        QVERIFY(QMetaObject::invokeMethod(window, "toggleFullscreenNowPlaying"));
        QTRY_COMPARE(window->visibility(), QWindow::Maximized);
        QCOMPARE(warnings.size(), 0);
    }

    void closeButtonQuitsTheWholeApplication_data()
    {
        QTest::addColumn<QString>("mode");
        for (const auto &mode : {"main-idle", "main-playing", "minimized-playing", "desktop-main", "desktop-minimized", "fullscreen-playing", "desktop-fullscreen"})
            QTest::newRow(mode) << QString(mode);
    }

    void closeButtonQuitsTheWholeApplication()
    {
        QFETCH(QString, mode);
        const auto song = m_temp.filePath("closing/Long.wav"); QVERIFY(writeWave(song, 30));
        QProcess process;
        process.start(QCoreApplication::applicationDirPath() + "/musica_close_probe", {mode, song});
        QVERIFY(process.waitForStarted(2000));
        QVERIFY2(process.waitForFinished(6500), "Closing Musica left its process running");
        const auto output = process.readAllStandardOutput() + process.readAllStandardError();
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QVERIFY2(process.exitCode() == 0, output.constData());
    }

    void nestedImportDeduplicatesAndLoadsDuration()
    {
        const QString root = m_temp.filePath("library");
        const QString first = root + "/Daylight/Arlo - First.WAV";
        const QString second = root + "/Daylight/Arlo - Second.wav";
        QVERIFY(writeWave(first));
        QVERIFY(writeWave(second));
        QFile text(root + "/notes.txt");
        QVERIFY(text.open(QIODevice::WriteOnly));
        text.write("This is not audio.");
        text.close();

        QSettings settings(m_temp.filePath("nested.ini"), QSettings::IniFormat);
        settings.setValue("library/folders", QStringList{});
        settings.sync();
        TrackListModel library(m_temp.filePath("nested.ini"), false);
        library.addFolder(QUrl::fromLocalFile(root));
        // Imports arriving during a scan must be picked up, without duplicate tracks.
        library.addFiles({QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)});
        library.rescan();
        QTRY_VERIFY(!library.scanning());
        QCOMPARE(library.tracks().size(), 2);
        const auto track = library.tracks().first().toMap();
        QCOMPARE(track.value("title").toString(), QStringLiteral("First"));
        QCOMPARE(track.value("artist").toString(), QStringLiteral("Arlo"));
        QCOMPARE(track.value("album").toString(), QStringLiteral("Daylight"));
        QCOMPARE(track.value("format").toString(), QStringLiteral("WAV"));
        QTRY_VERIFY_WITH_TIMEOUT(library.tracks().last().toMap().value("probed").toBool(), 12000);
        QCOMPARE(library.tracks().first().toMap().value("duration").toLongLong(), 3000);
        QCOMPARE(library.tracks().last().toMap().value("duration").toLongLong(), 3000);
    }

    void favoritesAndVolumeSurviveRestart()
    {
        const QString path = m_temp.filePath("saved/June - Home.wav");
        const QString config = m_temp.filePath("saved.ini");
        QVERIFY(writeWave(path));
        QSettings settings(config, QSettings::IniFormat);
        settings.setValue("library/folders", QStringList{});
        settings.sync();
        QString url;
        {
            TrackListModel library(config, false);
            library.addFiles({QUrl::fromLocalFile(path)});
            QTRY_VERIFY(!library.scanning());
            QCOMPARE(library.tracks().size(), 1);
            url = library.tracks().first().toMap().value("fileUrl").toString();
            library.toggleFavorite(url);
            QVERIFY(library.tracks().first().toMap().value("favorite").toBool());
            library.setVolume(0.23);
        }
        TrackListModel restored(config, true);
        QTRY_COMPARE(restored.tracks().size(), 1);
        QVERIFY(restored.tracks().first().toMap().value("favorite").toBool());
        QCOMPARE(restored.volume(), 0.23);
        restored.toggleFavorite(url);
        QVERIFY(!restored.tracks().first().toMap().value("favorite").toBool());
        restored.setVolume(10);
        QCOMPARE(restored.volume(), 1.0);
        restored.setVolume(-10);
        QCOMPARE(restored.volume(), 0.0);
    }

    void invalidImportsReportErrors()
    {
        TrackListModel library(m_temp.filePath("invalid.ini"), false);
        QSignalSpy errors(&library, &TrackListModel::importError);
        library.addFolder(QUrl(QStringLiteral("https://example.com/music")));
        library.addFiles({QUrl::fromLocalFile(m_temp.filePath("missing.mp3"))});
        QCOMPARE(errors.size(), 2);
        QVERIFY(library.tracks().isEmpty());
        QVERIFY(!library.scanning());
    }

    void artworkAndDurationUpdatesDoNotRecheckSortOrder()
    {
        LibraryEntryModel source;
        CountingLibraryViewModel albums;
        albums.setSourceModel(&source);
        QVariantList entries;
        for (int i = 0; i < 300; ++i)
            entries.append(QVariantMap{{"id", QString::number(i)},
                {"name", QStringLiteral("Album %1").arg(i, 3, 10, QLatin1Char('0'))},
                {"duration", 0}, {"artwork", ""}});
        source.synchronize(entries, "id");
        QCOMPARE(albums.count(), 300);
        QSignalSpy resets(&albums, &QAbstractItemModel::modelReset);
        albums.comparisons = 0;
        for (int i = 0; i < entries.size(); ++i) {
            auto item = entries[i].toMap();
            item.insert("duration", 180000);
            item.insert("artwork", "file:///cover.jpg");
            entries[i] = item;
        }
        source.synchronize(entries, "id");
        QCOMPARE(albums.comparisons, 0);
        QCOMPARE(resets.size(), 0);
        QCOMPARE(albums.get(299).value("duration").toInt(), 180000);
        auto renamed = entries[299].toMap();
        renamed.insert("name", "AAA renamed album");
        entries[299] = renamed;
        source.synchronize(entries, "id");
        QVERIFY(albums.comparisons > 0);
        QCOMPARE(albums.get(0).value("id").toString(), QStringLiteral("299"));
        QCOMPARE(resets.size(), 0);
        albums.setSearch("AAA");
        QCOMPARE(albums.count(), 1);
        renamed.insert("name", "ZZZ renamed album");
        entries[299] = renamed;
        source.synchronize(entries, "id");
        QCOMPARE(albums.count(), 0);
        albums.setSearch(QString());
        QCOMPARE(albums.count(), 300);
        QCOMPARE(albums.get(299).value("id").toString(), QStringLiteral("299"));
    }

    void albumIdentityAndFailedMetadataStayStable()
    {
        const QString root = m_temp.filePath("grouping");
        QVERIFY(writeWave(root + "/Quelle Chris/Is Men/01 Quelle Chris - Alpha.wav", 1, "Quelle Chris", "Is Men"));
        QVERIFY(writeWave(root + "/Quelle Chris/Is Men/02 Quelle Chris - Beta.wav", 1, "Quelle Chris feat. Guest", "  IS   MEN  "));
        QVERIFY(writeWave(root + "/Other Artist/Is Men/01 Other Artist - Delta.wav", 1, "Other Artist", "Is Men"));
        QFile broken(root + "/Quelle Chris/Is Men/03 Quelle Chris - Broken.wav");
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("invalid audio");
        broken.close();
        const QString config = m_temp.filePath("grouping.ini");
        QSettings settings(config, QSettings::IniFormat);
        settings.setValue("library/folders", QStringList{});
        settings.sync();
        {
            TrackListModel library(config, false);
            QAbstractItemModelTester sourceTester(library.albums()->sourceModel(), QAbstractItemModelTester::FailureReportingMode::QtTest);
            QAbstractItemModelTester proxyTester(library.albums(), QAbstractItemModelTester::FailureReportingMode::QtTest);
            QSignalSpy resets(library.albums(), &QAbstractItemModel::modelReset);
            library.addFolder(QUrl::fromLocalFile(root));
            QTRY_COMPARE(library.trackCount(), 4);
            QTRY_COMPARE(library.albums()->count(), 2);
            QTRY_VERIFY_WITH_TIMEOUT(!library.scanning(), 8000);
            library.albums()->setSearch("Quelle Chris");
            QCOMPARE(library.albums()->count(), 1);
            const auto album = library.albums()->get(0);
            QCOMPARE(album.value("trackCount").toInt(), 3);
            QCOMPARE(album.value("subtitle").toString(), QStringLiteral("Quelle Chris"));
            QCOMPARE(library.tracksForGroup("album", album.value("id").toString()).size(), 3);
            QVERIFY(library.scanStatus().contains("1 file without readable tags"));
            QCOMPARE(resets.size(), 0);
            QSignalSpy changes(library.songs(), &QAbstractItemModel::dataChanged);
            library.rescan();
            QTRY_VERIFY(!library.scanning());
            QCOMPARE(changes.size(), 0); // Cached failures and valid files are not probed repeatedly.
            QCOMPARE(resets.size(), 0);
        }
        TrackListModel restored(config, true);
        QTRY_COMPARE(restored.trackCount(), 4);
        QTRY_VERIFY(!restored.scanning());
        QCOMPARE(restored.albums()->count(), 2);
    }

    void collectiveAndCompilationGrouping()
    {
        // Downloaders place a compilation's performers into separate directories.
        QVariantList sharedCover{
            releaseTrack("Night Sessions", "Arlo", 1, "/music/Arlo/Night Sessions/01 Zulu.wav", "shared-cover"),
            releaseTrack(" night sessions ", "June", 2, "/music/June/Night Sessions/02 Amber.wav", "shared-cover"),
            releaseTrack("Night Sessions", "Vale", 1, "/music/Vale/Night Sessions/01 Different.wav", "other-cover")};
        AlbumGrouping::reconcile(sharedCover);
        const QString merged = sharedCover[0].toMap().value("albumId").toString();
        QCOMPARE(sharedCover[1].toMap().value("albumId").toString(), merged);
        QVERIFY(sharedCover[2].toMap().value("albumId").toString() != merged);
        QCOMPARE(sharedCover[0].toMap().value("albumArtist").toString(), QStringLiteral("Various artists"));
        QCOMPARE(sharedCover[1].toMap().value("artist").toString(), QStringLiteral("June"));
        auto retagged = sharedCover[2].toMap();
        const QString previousId = retagged.value("albumId").toString();
        retagged.insert("tagAlbumArtist", "New Collective");
        AlbumGrouping::updateIdentity(retagged);
        QVariantList renamed{retagged};
        const auto aliases = AlbumGrouping::reconcile(renamed);
        QCOMPARE(aliases.value(previousId), renamed.first().toMap().value("albumId").toString());

        QVariantList collective{
            releaseTrack("Collective Tape", "Arlo", 1, "/music/Arlo/Tape/01.wav", {}, "The Collective"),
            releaseTrack("Collective Tape", "June", 2, "/music/June/Tape/02.wav", {}, "the collective"),
            releaseTrack("Another Album", "The Collective", 1, "/music/Collective/Another/01.wav", {}, "The Collective")};
        AlbumGrouping::reconcile(collective);
        QCOMPARE(collective[0].toMap().value("albumId"), collective[1].toMap().value("albumId"));
        QVERIFY(collective[0].toMap().value("albumId") != collective[2].toMap().value("albumId"));

        QVariantList credits{
            releaseTrack("Sessions Vol. 5 (Compiled By Kid Fonque)", "Arlo", -1, "/music/Arlo/Sessions/unordered.wav"),
            releaseTrack("Sessions Vol 5", "June", -1, "/music/June/Sessions/other.wav")};
        AlbumGrouping::reconcile(credits);
        QCOMPARE(credits[0].toMap().value("albumId"), credits[1].toMap().value("albumId"));

        QVariantList numbered{
            releaseTrack("Shared Release", "Arlo", 1, "/music/Arlo/Release/a.wav"),
            releaseTrack("Shared Release", "June", 2, "/music/June/Release/b.wav"),
            releaseTrack("Shared Release", "Vale", 10, "/music/Vale/Release/c.wav")};
        AlbumGrouping::reconcile(numbered);
        QCOMPARE(numbered[0].toMap().value("albumId"), numbered[2].toMap().value("albumId"));

        QVariantList ambiguous{
            releaseTrack("Greatest Hits", "Arlo", 1, "/music/Arlo/Hits/a.wav"),
            releaseTrack("Greatest Hits", "June", 2, "/music/June/Hits/b.wav"),
            releaseTrack("Same Title", "Arlo", 1, "/music/Arlo/Same Title/a.wav"),
            releaseTrack("Same Title", "June", 1, "/music/June/Same Title/b.wav")};
        AlbumGrouping::reconcile(ambiguous);
        QVERIFY(ambiguous[0].toMap().value("albumId") != ambiguous[1].toMap().value("albumId"));
        QVERIFY(ambiguous[2].toMap().value("albumId") != ambiguous[3].toMap().value("albumId"));
    }

    void albumSequenceUsesTaggedNumbersAndDiscFolders()
    {
        QVariantList tracks{
            releaseTrack("Compilation", "June", 1, "/music/Collective/Compilation/CD2/01 Second disc.wav"),
            releaseTrack("Compilation", "Arlo", 10, "/music/Collective/Compilation/CD1/02 Ten.wav"),
            releaseTrack("Compilation", "Vale", 2, "/music/Collective/Compilation/CD1/10 Two.wav"),
            releaseTrack("Compilation", "Arlo", 1, "/music/Collective/Compilation/CD1/20 One.wav"),
            releaseTrack("Compilation", "Arlo", -1, "/music/Collective/Compilation/CD1/Unnumbered.wav")};
        AlbumGrouping::reconcile(tracks);
        const auto id = tracks.first().toMap().value("albumId");
        for (const auto &track : tracks) QCOMPARE(track.toMap().value("albumId"), id);
        LibraryEntryModel source;
        LibraryViewModel view;
        view.setSourceModel(&source);
        view.setSortKey("Track");
        source.synchronize(tracks, "fileUrl");
        QCOMPARE(view.get(0).value("trackNumber").toInt(), 1);
        QCOMPARE(view.get(1).value("trackNumber").toInt(), 2);
        QCOMPARE(view.get(2).value("trackNumber").toInt(), 10);
        QCOMPARE(view.get(3).value("trackNumber").toInt(), -1);
        QCOMPARE(view.get(4).value("discNumber").toInt(), 2);
        std::sort(tracks.begin(), tracks.end(), AlbumGrouping::trackLessThan);
        QCOMPARE(tracks[0].toMap().value("fileUrl"), view.get(0).value("fileUrl"));
        QCOMPARE(tracks[4].toMap().value("fileUrl"), view.get(4).value("fileUrl"));
    }

    void scannerReadsTaggedTrackNumbers()
    {
        const QString root = m_temp.filePath("numbered");
        const QString zulu = root + "/Collective/Night Mix/10 Arlo - Zulu.wav";
        const QString amber = root + "/Collective/Night Mix/02 June - Amber.wav";
        QVERIFY(writeWave(zulu, 1, "Arlo", "Night Mix", 2));
        QVERIFY(writeWave(amber, 1, "June", "Night Mix", 10));
        const QString config = m_temp.filePath("numbered.ini");
        QSettings settings(config, QSettings::IniFormat);
        settings.setValue("library/folders", QStringList{});
        settings.sync();
        {
            TrackListModel library(config, false);
            library.addFolder(QUrl::fromLocalFile(root));
            QTRY_COMPARE(library.trackCount(), 2);
            QTRY_VERIFY(!library.scanning());
            QCOMPARE(library.albums()->count(), 1);
            const auto album = library.albums()->get(0);
            QCOMPARE(album.value("trackCount").toInt(), 2);
            QCOMPARE(album.value("subtitle").toString(), QStringLiteral("Collective"));
            const auto queue = library.tracksForGroup("album", album.value("id").toString());
            QCOMPARE(queue[0].toMap().value("trackNumber").toInt(), 2);
            QCOMPARE(queue[0].toMap().value("title").toString(), QStringLiteral("Zulu"));
            QCOMPARE(queue[1].toMap().value("trackNumber").toInt(), 10);
            library.songs()->setCollectionKind("album");
            library.songs()->setCollectionId(album.value("id").toString());
            library.songs()->setSortKey("Track");
            QCOMPARE(library.songs()->get(0).value("fileUrl"), queue[0].toMap().value("fileUrl"));
        }
        TrackListModel restored(config, true);
        QTRY_COMPARE(restored.trackCount(), 2);
        QTRY_VERIFY(!restored.scanning());
        const auto queue = restored.tracksForGroup("album", restored.albums()->get(0).value("id").toString());
        QCOMPARE(queue[0].toMap().value("trackNumber").toInt(), 2);
        QCOMPARE(queue[1].toMap().value("trackNumber").toInt(), 10);
    }

    void albumPageQueuesTracksInReleaseOrder()
    {
        const QString root = m_temp.filePath("album-playback");
        const QString intro = root + "/Collective/Night Mix/20 Vale - Intro.wav";
        const QString zulu = root + "/Collective/Night Mix/10 Arlo - Zulu.wav";
        const QString amber = root + "/Collective/Night Mix/02 June - Amber.wav";
        QVERIFY(writeWave(intro, 15, "Vale", "Night Mix", 1));
        QVERIFY(writeWave(zulu, 15, "Arlo", "Night Mix", 2));
        QVERIFY(writeWave(amber, 15, "June", "Night Mix", 10));
        QSettings settings;
        settings.clear();
        settings.setValue("library/folders", QStringList{root});
        settings.sync();
        QQmlApplicationEngine engine;
        QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        engine.load(QUrl(QStringLiteral("qrc:/main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        auto *library = window->findChild<TrackListModel *>("library");
        library->rescan(); // New test fixtures are discovered by a manual scan.
        auto *player = window->findChild<PlaybackEngine *>("player");
        QVERIFY(library && player);
        QTRY_COMPARE(library->trackCount(), 3);
        QTRY_VERIFY(!library->scanning());
        QCOMPARE(library->albums()->count(), 1);
        click(window, "navAlbums");
        QTest::qWait(80);
        click(window, "groupCard0");
        QTest::qWait(80);
        QCOMPARE(window->property("sortBy").toString(), QStringLiteral("Track"));
        QCOMPARE(library->songs()->get(0).value("title").toString(), QStringLiteral("Intro"));
        QCOMPARE(library->songs()->get(1).value("title").toString(), QStringLiteral("Zulu"));
        QCOMPARE(library->songs()->get(2).value("title").toString(), QStringLiteral("Amber"));
        click(window, "playAllButton");
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
        const auto queue = window->property("queueUrls").value<QJSValue>().toVariant().toList();
        QCOMPARE(queue.size(), 3);
        QCOMPARE(QUrl(queue[0].toString()), QUrl::fromLocalFile(intro));
        QCOMPARE(QUrl(queue[1].toString()), QUrl::fromLocalFile(zulu));
        QCOMPARE(QUrl(queue[2].toString()), QUrl::fromLocalFile(amber));
        QCOMPARE(warnings.size(), 0);
        player->stop();
    }

    void largeAlbumGridKeepsPositionDuringMetadataUpdates()
    {
        constexpr int albumCount = 160;
        constexpr int tracksPerAlbum = 10;
        const QString root = m_temp.filePath("large-library");
        QVariantMap cache;
        for (int album = 0; album < albumCount; ++album) {
            const QString albumName = QStringLiteral("Album %1").arg(album, 3, 10, QLatin1Char('0'));
            for (int song = 0; song < tracksPerAlbum; ++song) {
                const QString title = QStringLiteral("Song %1").arg(song, 2, 10, QLatin1Char('0'));
                const QString path = root + "/The Artist/" + albumName + QStringLiteral("/%1 The Artist - ").arg(song + 1, 2, 10, QLatin1Char('0')) + title + ".wav";
                QVERIFY(writeWave(path, 1));
                // Leave 40 files uncached so metadata changes arrive while scrolling.
                if (album % 4 == 0 && song == 0) continue;
                const QFileInfo file(path);
                const QString url = QUrl::fromLocalFile(path).toString();
                cache.insert(url, QVariantMap{{"fileUrl", url}, {"title", title}, {"artist", "The Artist"},
                    {"album", albumName}, {"genre", "Other"}, {"duration", 1000}, {"artwork", ""},
                    {"probed", true}, {"metadataVersion", TrackListModel::MetadataCacheVersion},
                    {"modified", file.lastModified().toMSecsSinceEpoch()}, {"size", file.size()}});
            }
        }
        QSettings settings;
        settings.clear();
        settings.setValue("library/folders", QStringList{root});
        settings.setValue("library/metadata", cache);
        settings.sync();
        QQmlApplicationEngine engine;
        QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        engine.load(QUrl(QStringLiteral("qrc:/main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        auto *library = window->findChild<TrackListModel *>("library");
        library->rescan(); // New test fixtures are discovered by a manual scan.
        QVERIFY(library);
        QTRY_COMPARE_WITH_TIMEOUT(library->trackCount(), albumCount * tracksPerAlbum, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(library->albums()->count(), albumCount, 10000);
        QCOMPARE(library->trackCount(), albumCount * tracksPerAlbum);
        QSignalSpy resets(library->albums(), &QAbstractItemModel::modelReset);
        click(window, "navAlbums");
        QTest::qWait(80);
        QCOMPARE(window->property("activePage").toString(), QStringLiteral("Albums"));
        auto *grid = findItem(window->contentItem(), "groupGrid");
        auto *outer = findItem(window->contentItem(), "contentScroll");
        QVERIFY(grid && outer);
        QVERIFY(!outer->property("interactive").toBool());
        QVERIFY(grid->property("interactive").toBool());
        QVERIFY(grid->height() > 300);
        const QPointF pos = grid->mapToScene(QPointF(grid->width() / 2, grid->height() / 2));
        QPointingDevice touchpad(QStringLiteral("Test touchpad"), 9876, QInputDevice::DeviceType::TouchPad,
            QPointingDevice::PointerType::Finger, QInputDevice::Capability::Position | QInputDevice::Capability::PixelScroll | QInputDevice::Capability::Scroll, 2, 0);
        ulong wheelTimestamp = 100;
        const auto wheel = [&](int pixels, Qt::ScrollPhase phase) {
            QWheelEvent event(pos, window->mapToGlobal(pos.toPoint()), QPoint(0, pixels), {},
                Qt::NoButton, Qt::NoModifier, phase, false, Qt::MouseEventSynthesizedBySystem, &touchpad);
            event.setTimestamp(wheelTimestamp += 16);
            QSpontaneKeyEvent::setSpontaneous(&event);
            QCoreApplication::sendEvent(window, &event);
        };
        wheel(0, Qt::ScrollBegin);
        QElapsedTimer scrollResponse; scrollResponse.start();
        wheel(-240, Qt::ScrollUpdate);
        QTRY_VERIFY_WITH_TIMEOUT(grid->property("contentY").toReal() >= 400, 150);
        QVERIFY(scrollResponse.elapsed() < 200);
        const qreal position = grid->property("contentY").toReal();
        QTest::qWait(500);
        QVERIFY(grid->property("contentY").toReal() >= position - 1);
        for (int n = 0; n < 60; ++n) {
            wheel(-600, Qt::ScrollUpdate);
            QTest::qWait(2);
        }
        wheel(0, Qt::ScrollEnd);
        QTRY_VERIFY(grid->property("atYEnd").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!library->scanning(), 12000);
        QVERIFY(grid->property("atYEnd").toBool());
        QCOMPARE(resets.size(), 0);
        QVERIFY(findItem(window->contentItem(), "groupCard159"));
        const qreal endPosition = grid->property("contentY").toReal();
        QWheelEvent mouseWheel(pos, window->mapToGlobal(pos.toPoint()), {}, QPoint(0, 120),
            Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        mouseWheel.setTimestamp(wheelTimestamp += 16);
        QSpontaneKeyEvent::setSpontaneous(&mouseWheel);
        QCoreApplication::sendEvent(window, &mouseWheel);
        QTRY_VERIFY(grid->property("contentY").toReal() <= endPosition - 140);
        // Search and navigation must retain the complete, correctly grouped library.
        window->findChild<QObject *>("searchField")->setProperty("text", QStringLiteral("Album 159"));
        QTRY_COMPARE(library->albums()->count(), 1);
        QCOMPARE(library->albums()->get(0).value("trackCount").toInt(), tracksPerAlbum);
        window->findChild<QObject *>("searchField")->setProperty("text", QString());
        window->resize(900, 640);
        QTest::qWait(80);
        QMetaObject::invokeMethod(grid, "positionViewAtEnd");
        QTRY_VERIFY(grid->property("atYEnd").toBool());
        QCOMPARE(warnings.size(), 0);
    }

    void listeningRequiresHalfOfTheSong()
    {
        ListeningSession session;
        session.begin("song");
        QVERIFY(!session.observe(0, 10000, true, 0));
        QVERIFY(!session.observe(4999, 10000, true, 4999));
        QVERIFY(session.observe(5000, 10000, true, 5000));
        QVERIFY(!session.observe(10000, 10000, false, 10000));
        QCOMPARE(session.listened(), 5000);
        QCOMPARE(session.lastAudibleMs(), 5000); // Time continues accumulating after a qualified play.
        session.begin("song");
        QVERIFY(!session.observe(0, 10000, true, 0));
        QVERIFY(session.observe(5000, 10000, true, 5000));
        session.begin("song");
        QVERIFY(!session.observe(0, 10000, true, 0));
        QVERIFY(!session.observe(2000, 10000, false, 2000));
        QVERIFY(!session.observe(2000, 10000, false, 20000));
        QCOMPARE(session.lastAudibleMs(), 0); // A pause contributes no listening time.
        session.seek(9000, 20001);
        QVERIFY(!session.observe(9000, 10000, true, 20001));
        QVERIFY(!session.observe(10000, 10000, false, 21001));
        QCOMPARE(session.listened(), 3000);
        // Replaying an already heard passage in the same play does not double-count it.
        session.seek(0, 21002);
        QVERIFY(!session.observe(0, 10000, true, 21002));
        QVERIFY(!session.observe(2000, 10000, true, 23002));
        QCOMPARE(session.listened(), 3000);
        QVERIFY(session.observe(4000, 10000, true, 25002));
        session.begin("song");
        QVERIFY(!session.observe(0, 10000, true, 0));
        QVERIFY(!session.observe(9000, 10000, true, 20));
        QCOMPARE(session.listened(), 0);
        QCOMPARE(session.lastAudibleMs(), 0);
    }

    void playlistOrderingAndM3uRoundTrip()
    {
        const auto folder = m_temp.filePath("playlist-management");
        const QStringList paths{folder + "/Zulu.wav", folder + "/#Intro.wav", folder + "/Été.wav"};
        QList<QUrl> files;
        for (const auto &path : paths) { QVERIFY(writeWave(path)); files.append(QUrl::fromLocalFile(path)); }
        const auto config = m_temp.filePath("playlist-management.ini");
        QSettings settings(config, QSettings::IniFormat);
        settings.setValue("library/folders", QStringList{});
        settings.sync();
        const auto a = files[0].toString(), b = files[1].toString(), c = files[2].toString();
        QString id;
        {
            TrackListModel library(config, false);
            library.addFiles(files);
            QTRY_VERIFY(!library.scanning());
            id = library.createPlaylist("Drive", a);
            QVERIFY(library.addToPlaylist(id, b));
            QVERIFY(library.addToPlaylist(id, c));
            auto *songs = library.songs();
            songs->setCollectionKind("playlist"); songs->setCollectionId(id); songs->setSortKey("Order");
            QCOMPARE(songs->get(0).value("fileUrl").toString(), a);
            QVERIFY(library.movePlaylistTrack(id, c, a));
            QCOMPARE(songs->get(0).value("fileUrl").toString(), c);
            QCOMPARE(songs->get(1).value("fileUrl").toString(), a);
            QVERIFY(library.movePlaylistTrack(id, a, ""));
            QCOMPARE(songs->get(2).value("fileUrl").toString(), a);
            QVERIFY(!library.movePlaylistTrack(id, a, "unknown"));
            QVERIFY(library.renamePlaylist(id, "Evening drive"));
            const auto other = library.createPlaylist("Other", a);
            QVERIFY(!library.renamePlaylist(other, "EVENING DRIVE"));
            const auto exported = QUrl::fromLocalFile(folder + "/Evening drive.m3u8");
            QVERIFY(library.exportPlaylist(id, exported));
            QFile output(exported.toLocalFile()); QVERIFY(output.open(QIODevice::ReadOnly));
            const auto content = QString::fromUtf8(output.readAll());
            QVERIFY(content.contains("./#Intro.wav"));
            QVERIFY(content.indexOf("Été.wav") < content.indexOf("./#Intro.wav"));
            QVERIFY(content.indexOf("./#Intro.wav") < content.indexOf("Zulu.wav"));
            const auto imported = library.importPlaylist(exported);
            QVERIFY(!imported.isEmpty());
            const auto copy = library.tracksForGroup("playlist", imported);
            QCOMPARE(copy.size(), 3);
            QCOMPARE(copy[0].toMap().value("fileUrl").toString(), c);
            QCOMPARE(copy[1].toMap().value("fileUrl").toString(), b);
            const auto size = QFileInfo(paths[0]).size();
            QVERIFY(!library.exportPlaylist(id, files[0]));
            QCOMPARE(QFileInfo(paths[0]).size(), size);
            const auto external = folder + "/Elsewhere/New.wav";
            QVERIFY(writeWave(external));
            QFile incoming(folder + "/incoming.m3u"); QVERIFY(incoming.open(QIODevice::WriteOnly));
            incoming.write("\xef\xbb\xbf#EXTM3U\r\nElsewhere/New.wav\r\nhttps://example.invalid/stream.mp3\r\nmissing.wav\r\n"); incoming.close();
            QSignalSpy errors(&library, &TrackListModel::importError);
            const auto newList = library.importPlaylist(QUrl::fromLocalFile(incoming.fileName()));
            QVERIFY(!newList.isEmpty());
            QTRY_VERIFY(!library.scanning());
            QCOMPARE(library.trackCount(), 4);
            QCOMPARE(library.tracksForGroup("playlist", newList).size(), 1);
            QCOMPARE(errors.size(), 1);
        }
        TrackListModel restored(config, true);
        QTRY_VERIFY(!restored.scanning() && restored.libraryReady());
        const auto tracks = restored.tracksForGroup("playlist", id);
        QCOMPARE(tracks.size(), 3);
        QCOMPARE(tracks[0].toMap().value("fileUrl").toString(), c);
        restored.songs()->setCollectionKind("playlist"); restored.songs()->setCollectionId(id); restored.songs()->setSortKey("Order");
        QCOMPARE(restored.songs()->get(1).value("fileUrl").toString(), b);
        for (const auto &path : paths) QVERIFY(QFileInfo::exists(path));
    }

    void manualAlbumChangesPersistThroughScansAndUndo()
    {
        const QString config = m_temp.filePath("album-edits.ini");
        const QString root = m_temp.filePath("album-edits");
        const QStringList paths{root + "/First/03 Finish.wav", root + "/First/01 Start.wav", root + "/Second/02 Middle.wav"};
        QVERIFY(writeWave(paths[0], 3, "Artist A", "First release", 3));
        QVERIFY(writeWave(paths[1], 3, "Artist A", "First release", 1));
        QVERIFY(writeWave(paths[2], 3, "Artist B", "Second release", 2));
        QSettings settings(config, QSettings::IniFormat); settings.setValue("library/folders", QStringList{}); settings.sync();
        QString mergedId;
        {
            TrackListModel library(config, false);
            QList<QUrl> files; for (const auto &path : paths) files.append(QUrl::fromLocalFile(path));
            library.addFiles(files); QTRY_VERIFY(!library.scanning());
            QCOMPARE(library.albums()->count(), 2);
            const QStringList ids{library.albums()->get(0).value("id").toString(), library.albums()->get(1).value("id").toString()};
            const auto preview = library.previewAlbumEdit("merge", ids, {}, "Together", "The collective", "");
            QVERIFY(!preview.contains("error"));
            QCOMPARE(library.albums()->count(), 2); // Preview is read-only.
            const auto rows = preview.value("groups").toList().first().toMap().value("tracks").toList();
            QCOMPARE(rows[0].toMap().value("trackNumber").toInt(), 1);
            QCOMPARE(rows[2].toMap().value("trackNumber").toInt(), 3);
            QVERIFY(library.applyAlbumEdit(preview));
            QCOMPARE(library.albums()->count(), 1);
            QVERIFY(library.canUndoAlbumEdit());
            mergedId = library.albums()->get(0).value("id").toString();
            QCOMPARE(library.albums()->get(0).value("subtitle").toString(), QStringLiteral("The collective"));
            QCOMPARE(library.trackForUrl(files[2].toString()).value("artist").toString(), QStringLiteral("Artist B"));
            library.rescan(); QTRY_VERIFY(!library.scanning());
            QCOMPARE(library.albums()->count(), 1);
            QCOMPARE(library.tracksForGroup("album", mergedId).size(), 3);
            QVERIFY(!library.applyAlbumEdit(preview)); // Reject a stale preview.
            const auto split = library.previewAlbumEdit("split", {mergedId}, {files[2].toString()}, "Side project", "Artist B", "selected");
            QVERIFY(!split.contains("error"));
            QVERIFY(library.applyAlbumEdit(split));
            QCOMPARE(library.albums()->count(), 2);
            library.rescan(); QTRY_VERIFY(!library.scanning());
            QCOMPARE(library.albums()->count(), 2);
        }
        TrackListModel restored(config, true);
        QTRY_VERIFY(restored.libraryReady() && !restored.scanning());
        QCOMPARE(restored.albums()->count(), 2);
        QVERIFY(restored.undoAlbumEdit());
        QCOMPARE(restored.albums()->count(), 1);
        QCOMPARE(restored.albums()->get(0).value("id").toString(), mergedId);
        QVERIFY(restored.undoAlbumEdit());
        QCOMPARE(restored.albums()->count(), 2);
        QVERIFY(!restored.canUndoAlbumEdit());
        for (const auto &entry : restored.tracks()) {
            const auto track = entry.toMap();
            QVERIFY((QStringList{"First release", "Second release"}.contains(track.value("album").toString())));
        }
        for (const auto &path : paths) QVERIFY(QFileInfo::exists(path));
    }

    void smartPlaylistsAndDashboardUseDatedHistory()
    {
        const QString config = m_temp.filePath("smart-history.ini");
        QStringList paths, urls;
        for (const auto &name : {"A - New", "B - Rare", "C - Popular"}) {
            const auto path = m_temp.filePath(QStringLiteral("smart-history/%1.wav").arg(name));
            QVERIFY(writeWave(path)); paths.append(path); urls.append(QUrl::fromLocalFile(path).toString());
        }
        QSettings settings(config, QSettings::IniFormat);
        settings.setValue("library/folders", QStringList{}); settings.setValue("library/files", paths);
        const auto now = QDateTime::currentMSecsSinceEpoch();
        settings.setValue("library/addedAt", QVariantMap{{urls[0], now}, {urls[1], now - 86400000LL}, {urls[2], now - 31LL * 86400000}});
        settings.setValue("library/playCounts", QVariantMap{{urls[1], 2}, {urls[2], 9}});
        settings.setValue("history/totals", QVariantMap{{urls[1], 120000LL}, {urls[2], 300000LL}});
        settings.setValue("history/days", QVariantMap{
            {QDate::currentDate().toString(Qt::ISODate), QVariantMap{{urls[1], QVariantMap{{"ms", 120000LL}, {"plays", 2}}}}},
            {QDate::currentDate().addDays(-8).toString(Qt::ISODate), QVariantMap{{urls[2], QVariantMap{{"ms", 300000LL}, {"plays", 9}}}}}});
        settings.setValue("history/recent", QVariantList{QVariantMap{{"fileUrl", urls[1]}, {"at", now}, {"ms", 120000LL}, {"qualified", true}}});
        settings.sync();
        TrackListModel library(config, true); QTRY_VERIFY(library.libraryReady() && !library.scanning());
        auto *songs = library.songs(); songs->setCollectionKind("smart"); songs->setCollectionId("recent"); songs->setSortKey("Added");
        QCOMPARE(songs->count(), 2); QCOMPARE(songs->get(0).value("fileUrl").toString(), urls[0]);
        songs->setCollectionId("never"); QCOMPARE(songs->count(), 1); QCOMPARE(songs->get(0).value("fileUrl").toString(), urls[0]);
        songs->setCollectionId("rare"); songs->setSortKey("Rare"); QCOMPARE(songs->count(), 1); QCOMPARE(songs->get(0).value("fileUrl").toString(), urls[1]);
        const auto stats = library.dashboard();
        QCOMPARE(stats.value("weeklyMs").toLongLong(), 120000);
        QCOMPARE(stats.value("totalMs").toLongLong(), 420000);
        QCOMPARE(stats.value("weeklyPlays").toInt(), 2);
        QCOMPARE(stats.value("weeklyTracks").toList().size(), 1);
        QCOMPARE(stats.value("topArtists").toList().first().toMap().value("name").toString(), QStringLiteral("C"));
        QCOMPARE(stats.value("recent").toList().first().toMap().value("fileUrl").toString(), urls[1]);
        library.rescan(); QTRY_VERIFY(!library.scanning());
        QCOMPARE(library.trackForUrl(urls[0]).value("addedAt").toLongLong(), now);
    }

    void mostPlayedIsRankedByQualifiedListenCount()
    {
        LibraryEntryModel source;
        LibraryViewModel mostPlayed;
        mostPlayed.setSourceModel(&source);
        mostPlayed.setPlayedOnly(true);
        mostPlayed.setSortKey("Plays");
        QVariantList tracks{
            QVariantMap{{"fileUrl", "a"}, {"title", "Zulu"}, {"playCount", 2}},
            QVariantMap{{"fileUrl", "b"}, {"title", "Beta"}, {"playCount", 10}},
            QVariantMap{{"fileUrl", "c"}, {"title", "New song"}, {"playCount", 0}},
            QVariantMap{{"fileUrl", "d"}, {"title", "Amber"}, {"playCount", 2}}};
        source.synchronize(tracks, "fileUrl");
        QCOMPARE(mostPlayed.count(), 3);
        QCOMPARE(mostPlayed.get(0).value("fileUrl").toString(), QStringLiteral("b"));
        QCOMPARE(mostPlayed.get(1).value("fileUrl").toString(), QStringLiteral("d"));
        QSignalSpy resets(&mostPlayed, &QAbstractItemModel::modelReset);
        auto promoted = tracks[3].toMap();
        promoted.insert("playCount", 20);
        tracks[3] = promoted;
        source.synchronize(tracks, "fileUrl");
        QCOMPARE(mostPlayed.get(0).value("fileUrl").toString(), QStringLiteral("d"));
        QCOMPARE(resets.size(), 0);
    }

    void mostPlayedRefiltersAndResortsExistingRowsImmediately()
    {
        LibraryEntryModel source;
        LibraryViewModel played, rare;
        played.setSourceModel(&source); played.setPlayedOnly(true); played.setSortKey("Plays");
        rare.setSourceModel(&source); rare.setCollectionKind("smart"); rare.setCollectionId("rare"); rare.setSortKey("Rare");
        QVariantList entries;
        for (int i = 0; i < 120; ++i)
            entries.append(QVariantMap{{"fileUrl", QString("file:///ranked/%1.wav").arg(i)},
                {"title", QString("Song %1").arg(i, 3, 10, QLatin1Char('0'))}, {"playCount", i % 6}});
        source.synchronize(entries, "fileUrl");
        QSignalSpy resets(&played, &QAbstractItemModel::modelReset);
        for (int pass = 0; pass < 8; ++pass) {
            for (int i = 0; i < entries.size(); ++i) {
                auto entry = entries[i].toMap(); entry["playCount"] = (i + pass * 7) % 9; entries[i] = entry;
            }
            source.synchronize(entries, "fileUrl");
            const auto expectedCount = std::count_if(entries.cbegin(), entries.cend(), [](const auto &entry) { return entry.toMap().value("playCount").toInt() > 0; });
            QCOMPARE(played.count(), expectedCount);
            qint64 previous = std::numeric_limits<qint64>::max();
            for (int row = 0; row < played.count(); ++row) {
                const auto count = played.get(row).value("playCount").toLongLong();
                QVERIFY(count > 0); QVERIFY2(count <= previous, qPrintable(QString("Pass %1, row %2: %3 follows %4").arg(pass).arg(row).arg(count).arg(previous)));
                previous = count;
            }
            previous = 0;
            for (int row = 0; row < rare.count(); ++row) {
                const auto count = rare.get(row).value("playCount").toLongLong();
                QVERIFY(count >= 1 && count <= 4); QVERIFY(count >= previous); previous = count;
            }
        }
        QCOMPARE(resets.size(), 0);
    }

    void mostPlayedVisibleRowsStayRankedWhenAListenIsAdded_data()
    {
        QTest::addColumn<bool>("legacySwitch");
        QTest::newRow("current") << false;
        QTest::newRow("legacy-page-switch") << true;
    }

    void mostPlayedVisibleRowsStayRankedWhenAListenIsAdded()
    {
        QFETCH(bool, legacySwitch);
        QSettings settings; settings.clear(); settings.setValue("library/folders", QStringList{});
        QVariantList tracks;
        QVariantMap counts;
        for (int i = 0; i < 1000; ++i) {
            const auto path = m_temp.filePath(QString("most-played-live/%1 - Song.wav").arg(i, 4, 10, QLatin1Char('0')));
            auto track = releaseTrack("A library", "Artist", i + 1, path);
            track["title"] = QString("Song %1").arg(i, 4, 10, QLatin1Char('0'));
            track["format"] = "WAV"; track["artwork"] = ""; track["duration"] = 1000; track["probed"] = true;
            tracks.append(track);
            if (i >= 900) counts[track.value("fileUrl").toString()] = (i - 900) / 20 + 1;
        }
        settings.setValue("library/snapshot", tracks); settings.setValue("library/playCounts", counts); settings.sync();
        QQmlApplicationEngine qml; QSignalSpy warnings(&qml, &QQmlApplicationEngine::warnings);
        QFile viewSource(":/main.qml"); QVERIFY(viewSource.open(QIODevice::ReadOnly));
        auto view = viewSource.readAll();
        if (legacySwitch) view.replace("model: win.restoringNavigation ? null : win.visibleTracks", "model: win.visibleTracks");
        qml.loadData(view, QUrl("qrc:/main.qml")); QVERIFY(!qml.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(qml.rootObjects().first());
        auto *library = window->findChild<TrackListModel *>("library"); QVERIFY(library);
        auto *display = qobject_cast<DisplaySettings *>(window->property("displayPreferences").value<QObject *>());
        display->reset(); const auto restoreDisplay = qScopeGuard([display] { display->reset(); });
        window->resize(1280, 850); QTest::qWait(50);
        click(window, "navSongs"); QTest::qWait(40);
        click(window, "navMostPlayed"); QTest::qWait(60);
        auto *list = findItem(window->contentItem(), "songList"); QVERIFY(list);
        auto *content = list->property("contentItem").value<QQuickItem *>(); QVERIFY(content);
        auto *ranking = library->mostPlayed(); QCOMPARE(ranking->count(), 100);
        const auto displayError = [&]() -> QString {
            int checked = 0;
            for (auto *row : content->childItems()) {
                if (!row->objectName().startsWith("songRow")) continue;
                const int index = row->property("index").toInt(); if (index < 0 || index >= ranking->count()) continue;
                const auto bounds = row->mapRectToItem(list, row->boundingRect());
                if (bounds.bottom() <= 0 || bounds.top() >= list->height()) continue;
                const auto value = row->property("track");
                const auto shown = value.metaType() == QMetaType::fromType<QJSValue>() ? value.value<QJSValue>().toVariant().toMap() : value.toMap();
                const auto expected = ranking->get(index);
                if (shown.value("fileUrl") != expected.value("fileUrl") || shown.value("playCount").toInt() <= 0 || shown.value("playCount") != expected.value("playCount"))
                    return QString("Row %1 shows %2 (%3 plays), expected %4 (%5 plays)").arg(index).arg(shown.value("title").toString()).arg(shown.value("playCount").toLongLong()).arg(expected.value("title").toString()).arg(expected.value("playCount").toLongLong());
                auto *label = findItem(row, "songTitleLabel" + QString::number(index));
                if (!label || label->property("text").toString() != expected.value("title").toString()) return QString("Stale title at row %1").arg(index);
                auto *playCount = findItem(row, "songPlayCountLabel" + QString::number(index));
                if (!playCount || playCount->property("text").toString().toLongLong() != expected.value("playCount").toLongLong()) return QString("Stale count at row %1").arg(index);
                ++checked;
            }
            return checked > 0 ? QString() : QString("No visible rows");
        };
        QTRY_COMPARE_WITH_TIMEOUT(displayError(), QString(), 1000);
        const auto promoted = tracks[0].toMap().value("fileUrl").toString();
        for (int play = 0; play < 6; ++play) {
            const auto token = "qualified-" + QString::number(play);
            library->observeAudiblePlayback(token, promoted, 0, 1000, true, false, 0);
            library->observeAudiblePlayback(token, promoted, 500, 1000, true, true, 500);
            QCOMPARE(library->trackForUrl(promoted).value("playCount").toInt(), play + 1);
            QMetaObject::invokeMethod(list, "positionViewAtBeginning"); QMetaObject::invokeMethod(list, "forceLayout");
            QCOMPARE(displayError(), QString());
            qint64 previous = std::numeric_limits<qint64>::max();
            for (const auto &entry : ranking->entries()) {
                const auto count = entry.toMap().value("playCount").toLongLong();
                QVERIFY(count > 0 && count <= previous); previous = count;
            }
            QMetaObject::invokeMethod(list, "positionViewAtEnd"); QMetaObject::invokeMethod(list, "forceLayout");
            QCOMPARE(displayError(), QString());
        }
        QCOMPARE(ranking->get(0).value("fileUrl").toString(), promoted);
        for (int repeat = 0; repeat < 3; ++repeat) {
            click(window, "navSongs"); QTest::qWait(25);
            click(window, "navMostPlayed"); QTest::qWait(40); QTRY_COMPARE_WITH_TIMEOUT(displayError(), QString(), 1000);
        }
        QCOMPARE(warnings.size(), 0);
    }

    void playlistsPersistAndRemovingSongsKeepsTheirFiles()
    {
        const QString first = m_temp.filePath("playlists/Arlo - First.wav");
        const QString second = m_temp.filePath("playlists/June - Second.wav");
        const QString config = m_temp.filePath("playlists.ini");
        QVERIFY(writeWave(first));
        QVERIFY(writeWave(second));
        QSettings settings(config, QSettings::IniFormat);
        settings.setValue("library/folders", QStringList{});
        settings.sync();
        QString id;
        const auto firstUrl = QUrl::fromLocalFile(first).toString();
        const auto secondUrl = QUrl::fromLocalFile(second).toString();
        {
            TrackListModel library(config, false);
            library.addFiles({QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)});
            QTRY_VERIFY(!library.scanning());
            id = library.createPlaylist("Night Drive", firstUrl);
            QVERIFY(!id.isEmpty());
            QVERIFY(library.addToPlaylist(id, secondUrl));
            QVERIFY(library.addToPlaylist(id, firstUrl));
            QCOMPARE(library.playlists()->count(), 1);
            QCOMPARE(library.playlists()->get(0).value("trackCount").toInt(), 2);
            library.songs()->setCollectionKind("playlist");
            library.songs()->setCollectionId(id);
            QCOMPARE(library.songs()->count(), 2);
            QVERIFY(library.removeFromPlaylist(id, firstUrl));
            QCOMPARE(library.songs()->count(), 1);
            QCOMPARE(library.trackCount(), 2);
            QVERIFY(QFileInfo::exists(first));
            QVERIFY(library.createPlaylist("night drive").isEmpty());
        }
        TrackListModel restored(config, true);
        QTRY_COMPARE(restored.trackCount(), 2);
        QTRY_VERIFY(!restored.scanning());
        QCOMPARE(restored.playlists()->count(), 1);
        const auto playlist = restored.tracksForGroup("playlist", id);
        QCOMPARE(playlist.size(), 1);
        QCOMPARE(playlist.first().toMap().value("fileUrl").toString(), secondUrl);
        QVERIFY(restored.deletePlaylist(id));
        QCOMPARE(restored.playlists()->count(), 0);
        QVERIFY(QFileInfo::exists(second));
    }

    void trashRemovesOnlyTheSelectedLibraryFile()
    {
        const QString removed = m_temp.filePath("trash-test/Owner - Removed.wav");
        const QString kept = m_temp.filePath("trash-test/Owner - Kept.wav");
        const QString unrelated = m_temp.filePath("trash-test/Not imported.wav");
        QVERIFY(writeWave(removed));
        QVERIFY(writeWave(kept));
        QVERIFY(writeWave(unrelated));
        const QString config = m_temp.filePath("trash.ini");
        QSettings settings(config, QSettings::IniFormat);
        settings.setValue("library/folders", QStringList{});
        settings.sync();
        TrackListModel library(config, false);
        library.addFiles({QUrl::fromLocalFile(removed), QUrl::fromLocalFile(kept)});
        QTRY_VERIFY(!library.scanning());
        const QString url = QUrl::fromLocalFile(removed).toString();
        const QString playlist = library.createPlaylist("Trash test", url);
        QVERIFY(!playlist.isEmpty());
        library.toggleFavorite(url);
        QVERIFY(!library.trashTrack(QUrl::fromLocalFile(unrelated).toString()));
        QVERIFY(QFileInfo::exists(unrelated));
        QSignalSpy errors(&library, &TrackListModel::importError);
        QSignalSpy removedSignal(&library, &TrackListModel::trackRemoved);
        // Test mode redirects GenericDataLocation to ~/.qttest. Use our isolated
        // XDG_DATA_HOME instead so this integration check never touches user Trash.
        QStandardPaths::setTestModeEnabled(false);
        const bool trashed = library.trashTrack(url);
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY2(trashed, errors.isEmpty() ? "Could not trash the temporary fixture" : qPrintable(errors.last().first().toString()));
        QVERIFY(!QFileInfo::exists(removed));
        QVERIFY(QFileInfo::exists(kept));
        QTRY_VERIFY(!library.scanning());
        QCOMPARE(library.trackCount(), 1);
        QCOMPARE(library.favoriteCount(), 0);
        QCOMPARE(library.tracksForGroup("playlist", playlist).size(), 0);
        QCOMPARE(removedSignal.size(), 1);
    }

    void nowPlayingBlocksInputToCoveredTracks_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::newRow("album") << QStringLiteral("album");
        QTest::newRow("playlist") << QStringLiteral("playlist");
    }

    void nowPlayingBlocksInputToCoveredTracks()
    {
        QFETCH(QString, kind);
        QStringList paths;
        for (int i = 1; i <= 8; ++i) {
            const auto path = m_temp.filePath(QStringLiteral("now-playing-input/%1/%2 Song.wav").arg(kind).arg(i, 2, 10, QLatin1Char('0')));
            QVERIFY(writeWave(path, 30, "Background artist", "Background album", i)); paths.append(path);
        }
        QSettings settings; settings.clear(); settings.setValue("library/folders", QStringList{}); settings.setValue("library/files", paths); settings.sync();
        QQmlApplicationEngine engine; QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        engine.load(QUrl("qrc:/main.qml")); QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        auto *library = window->findChild<TrackListModel *>("library"); auto *player = window->findChild<PlaybackEngine *>("player"); QVERIFY(library && player);
        library->rescan(); // New test fixtures are discovered by a manual scan.
        QTRY_VERIFY(library->libraryReady() && !library->scanning());
        if (kind == "playlist") {
            const auto id = library->createPlaylist("Behind Now Playing"); QVERIFY(!id.isEmpty());
            for (const auto &path : paths) QVERIFY(library->addToPlaylist(id, QUrl::fromLocalFile(path).toString()));
            click(window, "navPlaylists");
        } else click(window, "navAlbums");
        QTest::qWait(60); click(window, "groupCard0"); QTest::qWait(60);
        QCOMPARE(window->property("collectionKind").toString(), kind);
        click(window, "playAllButton"); QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
        const auto currentUrl = window->property("currentUrl").toString(), currentId = window->property("currentQueueId").toString();
        QVERIFY(library->songs()->get(1).value("fileUrl").toString() != currentUrl);
        const auto queue = qmlList(window, "queueItems");
        click(window, "nowPlayingCoverButton");
        auto *pane = findItem(window->contentItem(), "nowPlayingPane"), *list = findItem(window->contentItem(), "songList"); QVERIFY(pane && list);
        for (const auto &size : {QSize(1280, 850), QSize(900, 640)}) {
            window->resize(size); QTest::qWait(60); QVERIFY(pane->isVisible());
            auto *row = findItem(window->contentItem(), "songRow1"); QVERIFY(row);
            // This lies over a different playable row, on the artwork/background
            // side of Now Playing, away from its buttons and links.
            const auto point = row->mapToScene(QPointF(4, row->height() / 2)).toPoint();
            QVERIFY(pane->contains(pane->mapFromScene(point)));
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
            QCOMPARE(window->property("currentUrl").toString(), currentUrl);
            QCOMPARE(window->property("currentQueueId").toString(), currentId);
            QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier, point);
            QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, point);
            QVERIFY(!window->findChild<QObject *>("trackMenu")->property("visible").toBool());
            const auto scrollPosition = list->property("contentY").toReal();
            QWheelEvent wheel(point, window->mapToGlobal(point), QPoint(0, -240), {}, Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
            QSpontaneKeyEvent::setSpontaneous(&wheel); QCoreApplication::sendEvent(window, &wheel);
            QCOMPARE(list->property("contentY").toReal(), scrollPosition);
            QCOMPARE(window->property("currentUrl").toString(), currentUrl); QCOMPARE(qmlList(window, "queueItems"), queue);
            QCOMPARE(player->playbackState(), QMediaPlayer::PlayingState);
            click(window, "nowPlayingPlayPauseButton"); QTRY_COMPARE(player->playbackState(), QMediaPlayer::PausedState);
            click(window, "nowPlayingPlayPauseButton"); QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
        }
        click(window, "closeNowPlayingButton"); QVERIFY(!window->property("nowPlayingOpen").toBool()); QTest::qWait(40);
        click(window, "songRow1"); QTRY_COMPARE(window->property("currentUrl").toString(), library->songs()->get(1).value("fileUrl").toString());
        QCOMPARE(warnings.size(), 0); player->stop();
    }

    void nowPlayingLinksMenusAndQueueRemoval()
    {
        QSettings settings;
        settings.clear();
        settings.setValue("library/folders", QStringList{});
        settings.sync();
        QQmlApplicationEngine engine;
        QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        engine.load(QUrl(QStringLiteral("qrc:/main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        auto *library = window->findChild<TrackListModel *>("library");
        auto *player = window->findChild<PlaybackEngine *>("player");
        QVERIFY(library && player);
        QTRY_VERIFY(!library->scanning());
        const QString first = m_temp.filePath("new-ui/Album/Alpha - First.wav");
        const QString second = m_temp.filePath("new-ui/Album/Beta - Second.wav");
        const QString third = m_temp.filePath("new-ui/Album/Gamma - Third.wav");
        QVERIFY(writeWave(first, 30));
        QVERIFY(writeWave(second, 30));
        QVERIFY(writeWave(third, 30));
        library->addFiles({QUrl::fromLocalFile(first), QUrl::fromLocalFile(second), QUrl::fromLocalFile(third)});
        QTRY_COMPARE(library->trackCount(), 3);
        QTRY_VERIFY(!library->scanning());
        click(window, "navSongs");
        QTest::qWait(80);
        click(window, "playAllButton");
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
        click(window, "nowPlayingCoverButton");
        QVERIFY(window->property("nowPlayingOpen").toBool());
        click(window, "nowPlayingFavoriteButton");
        QCOMPARE(library->favoriteCount(), 1);
        QTRY_COMPARE(findItem(window->contentItem(), "footerFavoriteButton")->property("glyphColor").value<QColor>(), QColor("#ef4444"));
        QVERIFY(findItem(window->contentItem(), "nowPlayingFavoriteButton")->property("filled").toBool());
        click(window, "nowPlayingPlayPauseButton");
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PausedState);
        click(window, "nowPlayingPlayPauseButton");
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
        QVERIFY(clickLink(window, "nowPlayingArtistLink"));
        QCOMPARE(window->property("activePage").toString(), QStringLiteral("Artists"));
        QCOMPARE(library->songs()->count(), 1);
        QVERIFY(!window->property("nowPlayingOpen").toBool());
        click(window, "nowPlayingCoverButton");
        click(window, "nowPlayingAlbumLink");
        QCOMPARE(window->property("activePage").toString(), QStringLiteral("Albums"));
        QCOMPARE(library->songs()->count(), 3);
        click(window, "nowPlayingCoverButton");
        QTest::qWait(60);
        window->grabWindow().save(QCoreApplication::applicationDirPath() + "/musica-now-playing.png");
        const QString currentId = window->property("currentQueueId").toString();
        click(window, "nowPlayingMoreButton");
        QTest::qWait(40);
        click(window, "menuAddToQueue");
        QCOMPARE(window->property("queueUrls").value<QJSValue>().toVariant().toList().size(), 4);
        click(window, "nowPlayingMoreButton");
        QTest::qWait(40);
        click(window, "menuPlayNext");
        QCOMPARE(window->property("queueUrls").value<QJSValue>().toVariant().toList().size(), 5);
        click(window, "nowPlayingQueueButton");
        QTest::qWait(80);
        click(window, "removeQueue1");
        QCOMPARE(window->property("queueUrls").value<QJSValue>().toVariant().toList().size(), 4);
        QCOMPARE(window->property("currentQueueId").toString(), currentId);
        QTest::qWait(40);
        click(window, "removeQueue3");
        QCOMPARE(window->property("queueUrls").value<QJSValue>().toVariant().toList().size(), 3);
        QCOMPARE(window->property("currentQueueId").toString(), currentId);
        window->setProperty("queueOpen", false);
        QTest::qWait(40);
        click(window, "nowPlayingMoreButton");
        QTest::qWait(40);
        click(window, "menuAddToPlaylist");
        QTest::qWait(80);
        window->findChild<QObject *>("playlistNameField")->setProperty("text", QStringLiteral("Night Drive"));
        click(window, "createPlaylistButton");
        QCOMPARE(library->playlists()->count(), 1);
        QCOMPARE(library->playlists()->get(0).value("trackCount").toInt(), 1);
        click(window, "nowPlayingMoreButton");
        QTest::qWait(40);
        click(window, "menuSongInfo");
        QTest::qWait(60);
        auto *info = window->findChild<QObject *>("songInfoDialog");
        QVERIFY(info->property("visible").toBool());
        QCOMPARE(info->property("details").toMap().value("path").toString(), first);
        QMetaObject::invokeMethod(info, "close");
        click(window, "navPlaylists");
        QTest::qWait(80);
        click(window, "groupCard0");
        QCOMPARE(window->property("collectionKind").toString(), QStringLiteral("playlist"));
        QCOMPARE(library->songs()->count(), 1);
        click(window, "navSongs");
        QTest::qWait(80);
        click(window, "songOptions1");
        QTest::qWait(40);
        click(window, "menuAddToPlaylist");
        QTest::qWait(80);
        click(window, "choosePlaylist0");
        QCOMPARE(library->playlists()->get(0).value("trackCount").toInt(), 2);
        click(window, "songOptions2");
        QTest::qWait(40);
        click(window, "menuTrashSong");
        QTest::qWait(60);
        auto *trash = window->findChild<QObject *>("trashSongDialog");
        QVERIFY(trash->property("visible").toBool());
        QMetaObject::invokeMethod(trash, "reject");
        QVERIFY(QFileInfo::exists(third));
        window->resize(900, 640);
        QTest::qWait(80);
        click(window, "nowPlayingCoverButton");
        QTest::qWait(80);
        QVERIFY(window->property("nowPlayingOpen").toBool());
        QCOMPARE(warnings.size(), 0);
        player->stop();
    }

    void realPlaybackQualifiesAtHalfAndSurvivesRestart()
    {
        QSettings settings;
        settings.clear();
        settings.setValue("library/folders", QStringList{});
        settings.sync();
        const QString path = m_temp.filePath("history/Sol - Halfway.wav");
        QVERIFY(writeWave(path, 4));
        const auto url = QUrl::fromLocalFile(path).toString();
        {
            QQmlApplicationEngine engine;
            QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
            engine.load(QUrl(QStringLiteral("qrc:/main.qml")));
            QVERIFY(!engine.rootObjects().isEmpty());
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            auto *library = window->findChild<TrackListModel *>("library");
            auto *player = window->findChild<PlaybackEngine *>("player");
            QVERIFY(library && player);
            QTRY_VERIFY(!library->scanning());
            library->addFiles({QUrl::fromLocalFile(path)});
            QTRY_COMPARE(library->trackCount(), 1);
            QTRY_VERIFY(!library->scanning());
            click(window, "navSongs");
            QTest::qWait(80);
            click(window, "playAllButton");
            QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
            QTest::qWait(700);
            QCOMPARE(library->mostPlayed()->count(), 0);
            QTRY_COMPARE_WITH_TIMEOUT(library->mostPlayed()->count(), 1, 4000);
            QCOMPARE(library->trackForUrl(url).value("playCount").toInt(), 1);
            QCOMPARE(library->dashboard().value("weeklyPlays").toInt(), 1);
            QVERIFY(library->dashboard().value("totalMs").toLongLong() >= 2000);
            click(window, "navMostPlayed");
            QTest::qWait(80);
            QCOMPARE(window->property("activePage").toString(), QStringLiteral("Most Played"));
            QCOMPARE(library->mostPlayed()->get(0).value("fileUrl").toString(), url);
            player->stop();
            QCOMPARE(warnings.size(), 0);
        }
        TrackListModel restored;
        QTRY_COMPARE(restored.trackCount(), 1);
        QTRY_VERIFY(!restored.scanning());
        QCOMPARE(restored.mostPlayed()->count(), 1);
        QCOMPARE(restored.mostPlayed()->get(0).value("playCount").toInt(), 1);
        QCOMPARE(restored.dashboard().value("weeklyPlays").toInt(), 1);
        QVERIFY(restored.dashboard().value("totalMs").toLongLong() >= 2000);
    }

    void automaticQueuesReplaceTheOldestAndKeepSessions()
    {
        const auto file = m_temp.filePath("automatic-queues.json");
        auto session = [](const QString &label, int position) {
            const QVariantMap entry{{"id", label}, {"fileUrl", "file:///music/" + label + ".wav"}};
            return QVariantMap{{"queue", QVariantList{entry}}, {"source", QVariantList{entry}},
                {"currentId", label}, {"position", position}, {"shuffle", true}, {"repeat", 2}};
        };
        QString second, newest;
        {
            QueueStore store(file, {}, true);
            const auto initial = store.activeId();
            const auto first = store.startPlaybackQueue("Album A", session("A", 1234));
            QCOMPARE(first, initial); QCOMPARE(store.queues().size(), 1);
            second = store.startPlaybackQueue("Playlist B", session("B", 5678));
            QVERIFY(!store.startPlaybackQueue("Artist C", session("C", 9000)).isEmpty());
            QVERIFY(!store.startPlaybackQueue("Most Played", session("D", 1000)).isEmpty());
            QCOMPARE(store.queues().size(), 4); QVERIFY(store.activate(first));
            const auto before = store.queues();
            QVERIFY(store.startPlaybackQueue("", session("invalid", 0)).isEmpty());
            QVERIFY(store.startPlaybackQueue("Empty", {}).isEmpty()); QCOMPARE(store.queues(), before);
            newest = store.startPlaybackQueue("Search: music", session("E", 2000));
            QCOMPARE(store.queues().size(), 4); QCOMPARE(store.queues().first().toMap().value("id").toString(), second);
            QVERIFY(!store.activate(first)); QVERIFY(store.activate(second)); QCOMPARE(store.session(), session("B", 5678));
            QVERIFY(store.activate(newest)); QCOMPARE(store.session(), session("E", 2000));
        }
        QueueStore restored(file); QCOMPARE(restored.queues().size(), 4); QCOMPARE(restored.activeId(), newest);
        QCOMPARE(restored.queues().first().toMap().value("id").toString(), second);
        QVERIFY(restored.activate(second)); QCOMPARE(restored.session().value("position").toInt(), 5678);
        QVERIFY(restored.session().value("shuffle").toBool()); QCOMPARE(restored.session().value("repeat").toInt(), 2);
    }

    void libraryPlaybackCreatesNamedQueuesAndSavesPlaylists()
    {
        const auto first = m_temp.filePath("automatic-queue-ui/A/01 First.wav");
        const auto second = m_temp.filePath("automatic-queue-ui/A/02 Second.wav");
        const auto third = m_temp.filePath("automatic-queue-ui/B/01 Third.wav");
        QVERIFY(writeWave(first, 120, "Artist A", "Album A", 1));
        QVERIFY(writeWave(second, 120, "Artist A", "Album A", 2));
        QVERIFY(writeWave(third, 120, "Artist B", "Album B", 1));
        const QStringList urls{QUrl::fromLocalFile(first).toString(), QUrl::fromLocalFile(second).toString(), QUrl::fromLocalFile(third).toString()};
        QSettings settings; settings.clear(); settings.setValue("library/folders", QStringList{});
        settings.setValue("library/playCounts", QVariantMap{{urls[0], 3}, {urls[2], 2}}); settings.sync();
        QString artistQueue;
        {
            QQmlApplicationEngine qml; QSignalSpy warnings(&qml, &QQmlApplicationEngine::warnings);
            qml.load(QUrl("qrc:/main.qml")); QVERIFY(!qml.rootObjects().isEmpty());
            auto *window = qobject_cast<QQuickWindow *>(qml.rootObjects().first()); window->resize(1200, 800);
            auto *library = window->findChild<TrackListModel *>("library"); auto *player = window->findChild<PlaybackEngine *>("player");
            QVERIFY(library && player); player->audioOutput()->setMuted(true);
            library->addFiles({QUrl::fromLocalFile(first), QUrl::fromLocalFile(second), QUrl::fromLocalFile(third)});
            QTRY_VERIFY(!library->scanning()); QTRY_VERIFY(window->property("sessionRestored").toBool());
            QCOMPARE(window->property("pageTitle").toString(), QString("Overview"));
            const auto track = library->trackForUrl(urls[0]);
            auto open = [&](const QString &page, const QVariantMap &group) {
                return QMetaObject::invokeMethod(window, "openCollection", Q_ARG(QVariant, page), Q_ARG(QVariant, group));
            };
            auto play = [&] { return QMetaObject::invokeMethod(window, "playAll", Q_ARG(QVariant, false)); };
            auto activeName = [&] {
                for (const auto &value : library->savedQueues()) if (value.toMap().value("active").toBool()) return value.toMap().value("name").toString();
                return QString();
            };
            QVERIFY(open("Albums", {{"id", track.value("albumId")}, {"name", "Album A"}})); QVERIFY(play());
            QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState); player->pause(); player->setPosition(5000);
            const auto albumQueue = library->activeQueueId(); QCOMPARE(activeName(), QString("Album A")); QCOMPARE(library->savedQueues().size(), 1);
            window->setProperty("shuffleEnabled", true); window->setProperty("repeatMode", 1);
            QVERIFY(open("Artists", {{"id", track.value("artistId")}, {"name", "Artist A"}})); QVERIFY(play());
            QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState); player->pause(); player->setPosition(3000);
            artistQueue = library->activeQueueId(); QCOMPARE(activeName(), QString("Artist A")); QVERIFY(artistQueue != albumQueue);
            const auto playlist = library->createPlaylistFromUrls("Mix", {urls[2], urls[0]}); QVERIFY(!playlist.isEmpty());
            QVERIFY(open("Playlists", {{"id", playlist}, {"name", "Mix"}})); QVERIFY(play());
            QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState); player->pause(); QCOMPARE(activeName(), QString("Mix"));
            QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, QString("Most Played"))); QVERIFY(play());
            QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState); player->pause(); QCOMPARE(activeName(), QString("Most Played"));
            QCOMPARE(library->savedQueues().size(), 4); const auto mostPlayedQueue = library->activeQueueId();
            QMetaObject::invokeMethod(window, "activateSavedQueue", Q_ARG(QVariant, albumQueue)); QTRY_COMPARE(player->position(), 5000);
            QVERIFY(window->property("shuffleEnabled").toBool()); QCOMPARE(window->property("repeatMode").toInt(), 1);
            QMetaObject::invokeMethod(window, "activateSavedQueue", Q_ARG(QVariant, mostPlayedQueue));
            QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, QString("Songs")));
            auto *search = window->findChild<QObject *>("searchField"); QVERIFY(search); search->setProperty("text", "First");
            QCOMPARE(library->songs()->count(), 1); QVERIFY(play());
            QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState); player->pause(); QCOMPARE(activeName(), QString("Search: First"));
            QCOMPARE(library->savedQueues().size(), 4); QCOMPARE(library->savedQueues().first().toMap().value("id").toString(), artistQueue);
            for (const auto &value : library->savedQueues()) QVERIFY(value.toMap().value("id").toString() != albumQueue);
            QMetaObject::invokeMethod(window, "activateSavedQueue", Q_ARG(QVariant, artistQueue)); QTRY_COMPARE(player->position(), 3000);
            const auto bankCount = library->savedQueues().size();
            QMetaObject::invokeMethod(window, "nextTrack", Q_ARG(QVariant, false)); QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState); player->pause();
            QMetaObject::invokeMethod(window, "enqueueTrack", Q_ARG(QVariant, library->trackForUrl(urls[2])), Q_ARG(QVariant, false));
            QMetaObject::invokeMethod(window, "enqueueTrack", Q_ARG(QVariant, library->trackForUrl(urls[1])), Q_ARG(QVariant, false));
            const auto entries = qmlList(window, "queueItems"); QCOMPARE(entries.size(), 4);
            QMetaObject::invokeMethod(window, "moveQueueItem", Q_ARG(QVariant, entries[2].toMap().value("id")), Q_ARG(QVariant, entries[0].toMap().value("id")));
            QCOMPARE(library->savedQueues().size(), bankCount); QCOMPARE(library->activeQueueId(), artistQueue);
            player->setPosition(7000); click(window, "queueButton"); QTest::qWait(30); click(window, "saveQueuePlaylistButton");
            auto *dialog = window->findChild<QObject *>("queuePlaylistDialog"); QVERIFY(dialog && dialog->property("visible").toBool());
            window->findChild<QObject *>("queuePlaylistNameField")->setProperty("text", "Saved queue");
            click(window, "confirmSaveQueuePlaylist"); QTest::qWait(30); search->setProperty("text", "");
            QString savedId;
            for (const auto &value : library->playlists()->entries()) if (value.toMap().value("name").toString() == "Saved queue") savedId = value.toMap().value("id").toString();
            QVERIFY(!savedId.isEmpty()); const auto saved = library->tracksForGroup("playlist", savedId); QCOMPARE(saved.size(), 3);
            QCOMPARE(saved[0].toMap().value("fileUrl").toString(), urls[2]); QCOMPARE(saved[1].toMap().value("fileUrl").toString(), urls[0]);
            QCOMPARE(saved[2].toMap().value("fileUrl").toString(), urls[1]); QCOMPARE(library->activeQueueId(), artistQueue);
            QCOMPARE(player->position(), 7000); QCOMPARE(player->playbackState(), QMediaPlayer::PausedState);
            QSignalSpy errors(library, &TrackListModel::importError);
            QVERIFY(library->createPlaylistFromUrls("Invalid", {urls[0], "file:///missing-song.wav"}).isEmpty());
            QCOMPARE(errors.size(), 1); QCOMPARE(library->playlists()->count(), 2);
            QMetaObject::invokeMethod(window, "persistSession"); QCOMPARE(warnings.size(), 0);
        }
        TrackListModel restored(settings.fileName(), false); QCOMPARE(restored.savedQueues().size(), 4); QCOMPARE(restored.activeQueueId(), artistQueue);
        QCOMPARE(restored.playbackSession().value("position").toInt(), 7000);
        QCOMPARE(restored.playbackSession().value("queue").toList().size(), 4); QCOMPARE(restored.playlists()->count(), 2);
    }

    void queueBanksPersistFourSessionsAndWriteWithoutBlocking()
    {
        const auto file = m_temp.filePath("four-queues.json");
        const QVariantMap entry{{"id", "first"}, {"fileUrl", "file:///music/first.wav"}};
        const QVariantMap first{{"queue", QVariantList{entry}}, {"source", QVariantList{entry}}, {"currentId", "first"}, {"position", 7654}, {"shuffle", true}, {"repeat", 2}};
        QString firstId, secondId;
        {
            QueueStore store(file, first, true);
            firstId = store.activeId(); QCOMPARE(store.session(), first);
            secondId = store.createQueue("Evening"); QVERIFY(!secondId.isEmpty());
            const QVariantMap second{{"currentId", "second"}, {"position", 1234}};
            store.saveSession(second);
            QVERIFY(store.activate(firstId)); QCOMPARE(store.session(), first);
            QVERIFY(store.renameQueue(firstId, "Morning"));
            QVERIFY(!store.createQueue("Travel").isEmpty());
            QVERIFY(!store.createQueue("Focus").isEmpty()); QCOMPARE(store.queues().size(), 4);
            QSignalSpy errors(&store, &QueueStore::error);
            QVERIFY(store.createQueue("Fifth").isEmpty()); QCOMPARE(errors.size(), 1);
            QVERIFY(store.activate(secondId)); QCOMPARE(store.session(), second);
            store.saveHistory({{"playCounts", QVariantMap{{"file:///music/first.wav", 7}}}});
        }
        {
            QueueStore restored(file); QCOMPARE(restored.queues().size(), 4); QCOMPARE(restored.activeId(), secondId);
            QCOMPARE(restored.session().value("position").toInt(), 1234);
            QCOMPARE(restored.history().value("playCounts").toMap().value("file:///music/first.wav").toInt(), 7);
            QVERIFY(restored.activate(firstId)); QCOMPARE(restored.session(), first);
            QVERIFY(restored.removeQueue(secondId)); QCOMPARE(restored.queues().size(), 3);
            restored.removeOtherQueues(); QCOMPARE(restored.queues().size(), 1); QVERIFY(!restored.removeQueue(firstId));
            QVariantList large;
            for (int i = 0; i < 10000; ++i) large.append(QVariantMap{{"id", QString::number(i)}, {"fileUrl", QString("file:///music/%1 track.flac").arg(i)}});
            QElapsedTimer elapsed; elapsed.start();
            restored.saveSession({{"queue", large}, {"source", large}, {"currentId", "9000"}, {"position", 4000}});
            QVERIFY2(elapsed.elapsed() < 100, "Scheduling playback persistence blocked the interface");
        }
        QueueStore finalStore(file); QCOMPARE(finalStore.session().value("queue").toList().size(), 10000);
    }

    void displayThemeAndSeekPreferencesPersistAndArtworkIsOptional()
    {
        const auto config = m_temp.filePath("theme-preferences.ini");
        const auto cover = m_temp.filePath("theme-cover.png");
        QImage image(300, 300, QImage::Format_RGB32); image.fill(QColor("#d03f58")); QVERIFY(image.save(cover));
        {
            DisplaySettings settings(config);
            QCOMPARE(settings.theme(), QString("dark")); QCOMPARE(settings.seekSeconds(), 10);
            const auto teal = settings.accentColor();
            settings.setArtwork(QUrl::fromLocalFile(cover)); QTest::qWait(40); QCOMPARE(settings.accentColor(), teal);
            settings.setArtworkColors(true); QTRY_VERIFY(settings.accentColor() != teal);
            const auto dark = settings.accentColor();
            settings.setTheme("light"); QVERIFY(settings.accentColor() != dark);
            settings.setArtwork(QUrl()); QCOMPARE(settings.accentColor(), QColor("#006c67"));
            settings.setArtworkColors(false); settings.setSeekSeconds(2); QCOMPARE(settings.seekSeconds(), 5);
            settings.setSeekSeconds(80); QCOMPARE(settings.seekSeconds(), 30); settings.setSeekSeconds(7);
            QVERIFY(!settings.splitArtists()); QCOMPARE(settings.artistSeparators(), QString(",;|"));
            settings.setSplitArtists(true); settings.setArtistSeparators("|;|unexpected");
            QCOMPARE(settings.artistSeparators(), QString(";|"));
        }
        DisplaySettings restored(config);
        QCOMPARE(restored.theme(), QString("light")); QCOMPARE(restored.seekSeconds(), 7); QVERIFY(!restored.artworkColors());
        QVERIFY(restored.splitArtists()); QCOMPARE(restored.artistSeparators(), QString(";|"));
    }

    void artistCreditsAreOptionalAndSharedAcrossArtistPages()
    {
        const auto first = m_temp.filePath("artist-credits/Shared.wav");
        const auto second = m_temp.filePath("artist-credits/Solo.wav");
        const auto third = m_temp.filePath("artist-credits/Band.wav");
        const auto fourth = m_temp.filePath("artist-credits/Ampersand.wav");
        QVERIFY(writeWave(first)); QVERIFY(writeWave(second)); QVERIFY(writeWave(third)); QVERIFY(writeWave(fourth));
        const auto original = fileBytes(first);
        const auto sharedCredit = QStringLiteral("Artist A, Artist B; Artist C | artist a");
        QVariantList snapshot{
            releaseTrack("Collective", sharedCredit, 2, first, {}, "Collective"),
            releaseTrack("Collective", "Artist B", 1, second, {}, "Collective"),
            releaseTrack("Collective", "AC, DC", 3, third, {}, "Collective"),
            releaseTrack("Collective", "Artist A & Artist D", 4, fourth, {}, "Collective")};
        QSettings settings(m_temp.filePath("artist-credits.ini"), QSettings::IniFormat);
        settings.setValue("library/folders", QStringList{}); settings.setValue("library/snapshot", snapshot); settings.sync();
        TrackListModel library(settings.fileName(), false);
        QCOMPARE(library.artists()->count(), 4);
        QCOMPARE(library.trackForUrl(QUrl::fromLocalFile(first).toString()).value("artistCredits").toList().size(), 1);
        const auto albumId = library.albums()->get(0).value("id"); QCOMPARE(library.albums()->count(), 1);
        QSignalSpy scans(&library, &TrackListModel::scanningChanged);
        library.setArtistSeparation(true, ",;|");
        QCOMPARE(library.artists()->count(), 6);
        const auto shared = library.trackForUrl(QUrl::fromLocalFile(first).toString());
        QCOMPARE(shared.value("artist").toString(), sharedCredit);
        const auto credits = shared.value("artistCredits").toList(); QCOMPARE(credits.size(), 3);
        QCOMPARE(credits[0].toMap().value("name").toString(), QString("Artist A"));
        const auto artistB = credits[1].toMap().value("id").toString();
        const auto songs = library.tracksForGroup("artist", artistB); QCOMPARE(songs.size(), 2);
        QCOMPARE(songs[0].toMap().value("trackNumber").toInt(), 1); QCOMPARE(songs[1].toMap().value("trackNumber").toInt(), 2);
        library.songs()->setCollectionKind("artist"); library.songs()->setCollectionId(artistB);
        QCOMPARE(library.songs()->count(), 2);
        QCOMPARE(library.albums()->count(), 1); QCOMPARE(library.albums()->get(0).value("id"), albumId);
        library.setArtistSeparation(true, ";|");
        const auto band = library.trackForUrl(QUrl::fromLocalFile(third).toString()).value("artistCredits").toList();
        QCOMPARE(band.size(), 1); QCOMPARE(band[0].toMap().value("name").toString(), QString("AC, DC"));
        library.setArtistSeparation(false, ",;|");
        QCOMPARE(library.artists()->count(), 4); QCOMPARE(library.tracksForGroup("artist", artistB).size(), 1);
        QCOMPARE(fileBytes(first), original); QCOMPARE(scans.size(), 0);
    }

    void artistLinksNavigateToTheArtistActuallyClicked()
    {
        QSettings settings; settings.clear(); settings.setValue("library/folders", QStringList{}); settings.sync();
        const auto path = m_temp.filePath("artist-links/Shared.wav");
        QVERIFY(writeWave(path, 30, "Artist A, Artist B; Artist C | Artist D", "Shared album"));
        const auto original = fileBytes(path);
        QQmlApplicationEngine qml; QSignalSpy warnings(&qml, &QQmlApplicationEngine::warnings);
        qml.load(QUrl("qrc:/main.qml")); QVERIFY(!qml.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(qml.rootObjects().first()); window->resize(1200, 800); window->requestActivate();
        auto *display = qobject_cast<DisplaySettings *>(window->property("displayPreferences").value<QObject *>());
        QVERIFY(display); display->reset(); display->setSplitArtists(false); display->setArtistSeparators(",;|");
        const auto restore = qScopeGuard([display] { display->setSplitArtists(false); display->setArtistSeparators(",;|"); display->reset(); });
        auto *library = window->findChild<TrackListModel *>("library");
        auto *player = window->findChild<PlaybackEngine *>("player"); QVERIFY(library && player); player->audioOutput()->setMuted(true);
        library->addFiles({QUrl::fromLocalFile(path)}); QTRY_VERIFY(!library->scanning()); QCOMPARE(library->artists()->count(), 1);
        QVERIFY(QMetaObject::invokeMethod(window, "openDisplaySettings"));
        auto *splitBox = findItem(window->contentItem(), "splitArtistsCheckBox"); QVERIFY(splitBox);
        splitBox->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
        QTRY_VERIFY(display->splitArtists()); QCOMPARE(library->artists()->count(), 4);
        QMetaObject::invokeMethod(window->findChild<QObject *>("displaySettingsDialog"), "close");
        click(window, "navSongs"); QTest::qWait(50); click(window, "playAllButton");
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState); player->pause();
        const auto url = window->property("currentUrl").toString();
        const auto credits = library->trackForUrl(url).value("artistCredits").toList(); QCOMPARE(credits.size(), 4);
        const auto artistB = credits[1].toMap().value("id").toString();
        auto clickSecondArtist = [window](QQuickItem *links) {
            if (!links) return false;
            for (int x = 0; x < int(links->width()); ++x) {
                QString href;
                if (!QMetaObject::invokeMethod(links, "linkAt", Q_RETURN_ARG(QString, href), Q_ARG(qreal, qreal(x)), Q_ARG(qreal, links->height() / 2))) return false;
                if (href != "artist:1") continue;
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, links->mapToScene(QPointF(x + 1, links->height() / 2)).toPoint());
                return true;
            }
            return false;
        };
        click(window, "nowPlayingCoverButton"); QTest::qWait(50);
        QVERIFY(clickSecondArtist(findItem(window->contentItem(), "nowPlayingArtistLink")));
        QTRY_COMPARE(window->property("collectionId").toString(), artistB);
        QCOMPARE(window->property("collectionName").toString(), QString("Artist B")); QCOMPARE(library->songs()->count(), 1);
        click(window, "navSongs"); QTest::qWait(50);
        QVERIFY(clickSecondArtist(findItem(window->contentItem(), "songArtistLabel0")));
        QTRY_COMPARE(window->property("collectionId").toString(), artistB);
        click(window, "navSongs"); QTest::qWait(50);
        QVERIFY(clickSecondArtist(findItem(window->contentItem(), "footerArtistLink")));
        QTRY_COMPARE(window->property("collectionId").toString(), artistB);
        QVERIFY(QMetaObject::invokeMethod(window, "openArtist", Q_ARG(QVariant, library->trackForUrl(url))));
        auto *picker = window->findChild<QObject *>("artistPicker"); QVERIFY(picker); QTRY_VERIFY(picker->property("visible").toBool());
        QTest::qWait(50); click(window, "chooseArtist2");
        QTRY_COMPARE(window->property("collectionId").toString(), credits[2].toMap().value("id").toString());
        QCOMPARE(window->property("currentUrl").toString(), url); QCOMPARE(fileBytes(path), original);
        // The About button is the final setting, and its dialog contains the requested credit.
        QVERIFY(QMetaObject::invokeMethod(window, "openDisplaySettings"));
        auto *aboutButton = findItem(window->contentItem(), "aboutMusicaButton"); QVERIFY(aboutButton);
        aboutButton->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
        auto *about = window->findChild<QObject *>("aboutMusicaDialog"); QVERIFY(about); QTRY_VERIFY(about->property("visible").toBool());
        auto *creator = findItem(window->contentItem(), "musicaCreatorCredit"); QVERIFY(creator);
        QVERIFY(creator->property("text").toString().contains("weo sikho fuzile"));
        QMetaObject::invokeMethod(about, "close");
        QMetaObject::invokeMethod(window->findChild<QObject *>("displaySettingsDialog"), "close");
        QCOMPARE(warnings.size(), 0);
    }

    void modelBatchesRefreshDelegatesOnce()
    {
        LibraryEntryModel source; LibraryViewModel view;
        view.setSourceModel(&source); view.setSortKey("Title");
        QVariantList entries;
        for (int i = 0; i < 1500; ++i) entries.append(QVariantMap{{"fileUrl", QString::number(i)}, {"title", QString::number(i)}, {"duration", 0}, {"playCount", 0}});
        source.synchronize(entries, "fileUrl");
        QSignalSpy updates(&view, &LibraryViewModel::entriesChanged), resets(&view, &QAbstractItemModel::modelReset);
        for (auto &entry : entries) { auto fields = entry.toMap(); fields["duration"] = 200000; fields["artwork"] = "file:///cover.png"; entry = fields; }
        source.synchronize(entries, "fileUrl");
        QCOMPARE(updates.size(), 1); QCOMPARE(resets.size(), 0);
        source.setPlayCount("10", 1); QVERIFY(updates.size() > 1);
    }

    void sharingCopiesAudioAndCapturesNowPlayingWithoutChangingTheFile()
    {
        const auto path = m_temp.filePath("share/Artist - Song.wav"); QVERIFY(writeWave(path, 30, "Artist", "Album"));
        const auto original = fileBytes(path);
        Sharing sharing; QSignalSpy ready(&sharing, &Sharing::ready), copied(&sharing, &Sharing::copied), exported(&sharing, &Sharing::exported), errors(&sharing, &Sharing::error);
        sharing.prepareSong({{"fileUrl", QUrl::fromLocalFile(path)}, {"artist", "Artist"}, {"title", "Song"}});
        QCOMPARE(ready.size(), 1); QVERIFY(sharing.copyToClipboard()); QCOMPARE(copied.size(), 1);
        QCOMPARE(QGuiApplication::clipboard()->mimeData()->urls(), QList<QUrl>{QUrl::fromLocalFile(path)});
        const auto copy = m_temp.filePath("shared-copy.wav");
        QVERIFY(sharing.exportTo(QUrl::fromLocalFile(copy))); QTRY_COMPARE(exported.size(), 1);
        QCOMPARE(fileBytes(copy), original); QCOMPARE(fileBytes(path), original);
        QSettings settings; settings.clear(); settings.setValue("library/folders", QStringList{}); settings.sync();
        QQmlApplicationEngine qml; QSignalSpy warnings(&qml, &QQmlApplicationEngine::warnings);
        qml.load(QUrl("qrc:/main.qml")); QVERIFY(!qml.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(qml.rootObjects().first());
        auto *library = window->findChild<TrackListModel *>("library"); library->addFiles({QUrl::fromLocalFile(path)}); QTRY_VERIFY(!library->scanning());
        QMetaObject::invokeMethod(window, "playUrl", Q_ARG(QVariant, QUrl::fromLocalFile(path).toString()));
        auto *player = window->findChild<PlaybackEngine *>("player"); QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState); player->pause();
        click(window, "nowPlayingCoverButton"); QTest::qWait(60);
        auto *pane = findItem(window->contentItem(), "nowPlayingPane"); QVERIFY(pane && pane->isVisible());
        sharing.captureNowPlaying(pane); QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 2, 5000);
        const auto details = ready.last().first().toMap(); QVERIFY(details.value("image").toBool());
        const auto screenshot = details.value("fileUrl").toUrl().toLocalFile();
        const QImage capture(screenshot); QVERIFY(!capture.isNull()); QVERIFY(capture.width() <= 2400);
        QVERIFY(sharing.copyToClipboard()); QVERIFY(QGuiApplication::clipboard()->mimeData()->hasImage());
        const auto png = m_temp.filePath("shared-now-playing.png");
        QVERIFY(sharing.exportTo(QUrl::fromLocalFile(png))); QTRY_COMPARE(exported.size(), 2);
        QVERIFY(!QImage(png).isNull()); QCOMPARE(fileBytes(path), original); QCOMPARE(errors.size(), 0); QCOMPARE(warnings.size(), 0);
        QGuiApplication::clipboard()->clear();
    }

    void fourQueuesAndSeekControlsWorkInTheInterface()
    {
        QSettings settings; settings.clear(); settings.setValue("library/folders", QStringList{}); settings.sync();
        const auto first = m_temp.filePath("queue-bank-ui/First.wav"), second = m_temp.filePath("queue-bank-ui/Second.wav");
        QVERIFY(writeWave(first, 120)); QVERIFY(writeWave(second, 120));
        QString firstId, secondId;
        {
            QQmlApplicationEngine qml; QSignalSpy warnings(&qml, &QQmlApplicationEngine::warnings);
            qml.load(QUrl("qrc:/main.qml")); QVERIFY(!qml.rootObjects().isEmpty());
            auto *window = qobject_cast<QQuickWindow *>(qml.rootObjects().first());
            auto *library = window->findChild<TrackListModel *>("library"); library->addFiles({QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)}); QTRY_VERIFY(!library->scanning());
            auto *player = window->findChild<PlaybackEngine *>("player"); player->audioOutput()->setMuted(true);
            auto *display = qobject_cast<DisplaySettings *>(window->property("displayPreferences").value<QObject *>());
            display->reset(); display->setTheme("dark"); display->setArtworkColors(false); display->setSeekSeconds(7);
            const auto defaults = qScopeGuard([display] { display->setTheme("dark"); display->setSeekSeconds(10); });
            window->requestActivate(); QTest::qWait(30);
            QMetaObject::invokeMethod(window, "playUrl", Q_ARG(QVariant, QUrl::fromLocalFile(first).toString()));
            QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState); player->pause(); player->setPosition(10000);
            QTest::keyClick(window, Qt::Key_Right); QTRY_COMPARE(player->position(), 17000);
            QTest::keyClick(window, Qt::Key_Left); QTRY_COMPARE(player->position(), 10000);
            firstId = library->activeQueueId();
            window->setProperty("shuffleEnabled", true); window->setProperty("repeatMode", 2);
            QMetaObject::invokeMethod(window, "createSavedQueue", Q_ARG(QVariant, QString("Evening")));
            secondId = library->activeQueueId(); QVERIFY(secondId != firstId); QCOMPARE(qmlList(window, "queueItems").size(), 0);
            QMetaObject::invokeMethod(window, "playUrl", Q_ARG(QVariant, QUrl::fromLocalFile(second).toString()));
            QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState); player->pause(); player->setPosition(3000);
            QMetaObject::invokeMethod(window, "activateSavedQueue", Q_ARG(QVariant, firstId)); QTRY_COMPARE(player->position(), 10000);
            QCOMPARE(window->property("currentUrl").toString(), QUrl::fromLocalFile(first).toString()); QVERIFY(window->property("shuffleEnabled").toBool()); QCOMPARE(window->property("repeatMode").toInt(), 2);
            QMetaObject::invokeMethod(window, "activateSavedQueue", Q_ARG(QVariant, secondId)); QTRY_COMPARE(player->position(), 3000);
            display->setTheme("light"); QTest::qWait(70);
            window->grabWindow().save(QCoreApplication::applicationDirPath() + "/musica-teal-light.png");
            click(window, "queueButton"); QTest::qWait(60); click(window, "manageQueuesButton"); QTest::qWait(60);
            auto *manager = window->findChild<QObject *>("queueManagerDialog"); QVERIFY(manager && manager->property("visible").toBool());
            click(window, "newSavedQueueButton"); QTest::qWait(40);
            window->findChild<QObject *>("queueNameField")->setProperty("text", "Third");
            QMetaObject::invokeMethod(window->findChild<QObject *>("queueNameDialog"), "accept"); QCOMPARE(library->savedQueues().size(), 3);
            QMetaObject::invokeMethod(window, "createSavedQueue", Q_ARG(QVariant, QString("Fourth"))); QCOMPARE(library->savedQueues().size(), 4);
            QVERIFY(!findItem(window->contentItem(), "newSavedQueueButton")->isEnabled());
            QMetaObject::invokeMethod(manager, "close"); QMetaObject::invokeMethod(window, "activateSavedQueue", Q_ARG(QVariant, secondId));
            QTRY_COMPARE(player->position(), 3000); QMetaObject::invokeMethod(window, "persistSession"); QCOMPARE(warnings.size(), 0);
        }
        TrackListModel restored(settings.fileName(), false); QCOMPARE(restored.savedQueues().size(), 4); QCOMPARE(restored.activeQueueId(), secondId);
        QCOMPARE(restored.playbackSession().value("position").toInt(), 3000);
    }

    void backNavigationRestoresCollectionsSearchAndScroll()
    {
        QSettings settings; settings.clear(); settings.setValue("library/folders", QStringList{});
        QVariantList snapshot;
        for (int album = 0; album < 60; ++album) for (int song = 0; song < 10; ++song) {
            const auto albumName = QString("Release %1").arg(album, 2, 10, QLatin1Char('0'));
            const auto path = m_temp.filePath(QString("navigation/%1/%2 Song.wav").arg(albumName).arg(song + 1, 2, 10, QLatin1Char('0')));
            auto track = releaseTrack(albumName, "Artist " + QString::number(album % 3), song + 1, path);
            track["artwork"] = ""; track["format"] = "WAV"; track["duration"] = 30000;
            track["genre"] = "Jazz"; track["probed"] = true; track["metadataVersion"] = TrackListModel::MetadataCacheVersion;
            snapshot.append(track);
        }
        settings.setValue("library/snapshot", snapshot);
        QStringList playlistUrls; for (int i = 0; i < 10; ++i) playlistUrls.append(snapshot[i].toMap().value("fileUrl").toString());
        settings.setValue("library/playlists", QVariantList{QVariantMap{{"id", "navigation-playlist"}, {"name", "My playlist"}, {"urls", playlistUrls}}});
        settings.sync();
        QQmlApplicationEngine qml; QSignalSpy warnings(&qml, &QQmlApplicationEngine::warnings);
        qml.load(QUrl("qrc:/main.qml")); QVERIFY(!qml.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(qml.rootObjects().first());
        auto *library = window->findChild<TrackListModel *>("library"); QVERIFY(library);
        auto *display = qobject_cast<DisplaySettings *>(window->property("displayPreferences").value<QObject *>());
        display->reset(); const auto restoreDisplay = qScopeGuard([display] { display->reset(); });
        window->resize(900, 640); QTest::qWait(60); window->requestActivate();
        auto *back = findItem(window->contentItem(), "backButton"); QVERIFY(back && back->isVisible()); QVERIFY(!back->isEnabled());
        // The persistent button remains available when the old page heading is hidden.
        for (const auto &page : {"Albums", "Artists", "Playlists"}) {
            click(window, "nav" + QString(page)); QTest::qWait(50);
            QVERIFY(back->isVisible() && back->isEnabled());
            click(window, "groupCard0"); QTest::qWait(50); QVERIFY(!window->property("collectionName").toString().isEmpty());
            click(window, "backButton"); QTRY_VERIFY(window->property("collectionName").toString().isEmpty());
            QCOMPARE(window->property("activePage").toString(), QString(page));
        }
        while (window->property("canGoBack").toBool()) {
            QTest::keyClick(window, Qt::Key_Left, Qt::AltModifier); QTest::qWait(30);
        }
        QCOMPARE(window->property("activePage").toString(), QStringLiteral("Overview")); QVERIFY(!back->isEnabled());

        click(window, "navAlbums");
        auto *search = window->findChild<QObject *>("searchField"); search->setProperty("text", "Release"); QTest::qWait(70);
        auto *grid = findItem(window->contentItem(), "groupGrid"); QVERIFY(grid);
        QMetaObject::invokeMethod(grid, "positionViewAtEnd"); QTRY_VERIFY(grid->property("atYEnd").toBool());
        const auto offset = grid->property("contentY").toReal() - grid->property("originY").toReal();
        QVERIFY(offset > grid->height());
        click(window, "groupCard59"); QTest::qWait(50);
        QCOMPARE(window->property("collectionName").toString(), QStringLiteral("Release 59"));
        click(window, "backButton"); QTRY_VERIFY(window->property("collectionName").toString().isEmpty());
        QCOMPARE(window->property("searchQuery").toString(), QStringLiteral("Release"));
        QTRY_VERIFY(std::abs(grid->property("contentY").toReal() - grid->property("originY").toReal() - offset) < 1);
        QVERIFY(grid->property("atYEnd").toBool());
        click(window, "groupCard59"); QTest::qWait(50);
        const auto albumId = window->property("collectionId").toString();
        search->setProperty("text", "Song"); window->setProperty("sortBy", "Artist"); QTest::qWait(50);
        auto *songs = findItem(window->contentItem(), "songList"); QVERIFY(songs);
        QMetaObject::invokeMethod(songs, "positionViewAtEnd"); QTest::qWait(40);
        const auto songOffset = songs->property("contentY").toReal() - songs->property("originY").toReal();
        const auto track = library->songs()->get(0);
        QVERIFY(QMetaObject::invokeMethod(window, "openArtist", Q_ARG(QVariant, track))); QTest::qWait(40);
        QCOMPARE(window->property("collectionKind").toString(), QStringLiteral("artist"));
        QTest::keyClick(window, Qt::Key_Left, Qt::AltModifier);
        QTRY_COMPARE(window->property("collectionId").toString(), albumId);
        QCOMPARE(window->property("searchQuery").toString(), QStringLiteral("Song"));
        QCOMPARE(window->property("sortBy").toString(), QStringLiteral("Artist"));
        QCOMPARE(library->songs()->count(), 10);
        // Allow fractional item/layout rounding while retaining the same songs.
        QTRY_VERIFY_WITH_TIMEOUT(std::abs(songs->property("contentY").toReal() - songs->property("originY").toReal() -
            std::min(songOffset, songs->property("contentHeight").toReal() - songs->height())) <= 3, 1000);

        const auto historySize = qmlList(window, "navigationHistory").size();
        click(window, "navSongs"); QTest::qWait(40);
        click(window, "navSongs"); QCOMPARE(qmlList(window, "navigationHistory").size(), historySize + 1);
        // Backspace edits text normally while a search field has focus.
        auto *searchItem = qobject_cast<QQuickItem *>(search); QVERIFY(searchItem);
        search->setProperty("text", "Songs"); searchItem->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_End); QTest::keyClick(window, Qt::Key_Backspace);
        QCOMPARE(window->property("searchQuery").toString(), QStringLiteral("Song"));
        QCOMPARE(window->property("activePage").toString(), QStringLiteral("Songs"));
        QCOMPARE(warnings.size(), 0);
    }

    void popupButtonsToggleAndFullscreenNowPlayingRestoresTheWindow()
    {
        QSettings settings; settings.clear(); settings.setValue("library/folders", QStringList{}); settings.sync();
        const auto song = m_temp.filePath("navigation-playing/Artist - Song.wav"); QVERIFY(writeWave(song, 120, "Artist", "Album", 1));
        QQmlApplicationEngine qml; QSignalSpy warnings(&qml, &QQmlApplicationEngine::warnings);
        qml.load(QUrl("qrc:/main.qml")); QVERIFY(!qml.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(qml.rootObjects().first());
        auto *library = window->findChild<TrackListModel *>("library");
        auto *player = window->findChild<PlaybackEngine *>("player"); QVERIFY(library && player);
        auto *display = qobject_cast<DisplaySettings *>(window->property("displayPreferences").value<QObject *>());
        display->reset(); const auto restoreDisplay = qScopeGuard([display] { display->reset(); });
        window->resize(900, 640); window->requestActivate();
        player->audioOutput()->setMuted(true);
        library->addFiles({QUrl::fromLocalFile(song)}); QTRY_VERIFY(!library->scanning());
        click(window, "navSongs"); QTest::qWait(50); click(window, "playAllButton");
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState); player->pause(); player->setPosition(5000);
        for (const auto &pair : {qMakePair("queueButton", "queuePopup"), qMakePair("crossfadeSettingsButton", "playbackOptions"), qMakePair("sleepTimerButton", "sleepTimerPopup")}) {
            auto *popup = window->findChild<QObject *>(pair.second); QVERIFY(popup);
            for (int repeat = 0; repeat < 2; ++repeat) {
                click(window, pair.first); QTRY_VERIFY(popup->property("visible").toBool());
                click(window, pair.first); QTRY_VERIFY(!popup->property("visible").toBool());
                QTest::qWait(50); QVERIFY(!popup->property("visible").toBool());
            }
        }
        click(window, "nowPlayingCoverButton"); QTRY_VERIFY(window->property("nowPlayingOpen").toBool());
        for (int repeat = 0; repeat < 2; ++repeat) {
            click(window, "nowPlayingQueueButton"); QTRY_VERIFY(window->property("queueOpen").toBool());
            click(window, "nowPlayingQueueButton"); QTRY_VERIFY(!window->property("queueOpen").toBool());
        }
        // An artist link is one navigation step; Back returns to Now Playing.
        const auto historySize = qmlList(window, "navigationHistory").size();
        QVERIFY(clickLink(window, "nowPlayingArtistLink")); QTRY_COMPARE(window->property("collectionKind").toString(), QStringLiteral("artist"));
        QCOMPARE(qmlList(window, "navigationHistory").size(), historySize + 1);
        click(window, "backButton"); QTRY_VERIFY(window->property("nowPlayingOpen").toBool());
        click(window, "closeNowPlayingButton"); QTRY_VERIFY(!window->property("nowPlayingOpen").toBool());
        QCOMPARE(window->property("activePage").toString(), QStringLiteral("Songs"));

        QPointer<QQuickItem> pane;
        auto *bar = findItem(window->contentItem(), "playerBar");
        auto *sidebar = findItem(window->contentItem(), "sidebar"); QVERIFY(bar && sidebar);
        const auto queue = qmlList(window, "queueItems");
        const auto id = window->property("currentQueueId");
        const auto position = player->position();
        window->requestActivate(); QTest::qWait(30); QTest::keyClick(window, Qt::Key_F11);
        QTRY_VERIFY(window->property("fullscreenNowPlaying").toBool()); QTRY_COMPARE(window->visibility(), QWindow::FullScreen);
        pane = findItem(window->contentItem(), "nowPlayingPane"); QVERIFY(pane);
        QVERIFY(window->property("nowPlayingOpen").toBool()); QVERIFY(!bar->isVisible() && !sidebar->isVisible());
        QTRY_COMPARE(pane->width(), qreal(window->width())); QTRY_COMPARE(pane->height(), qreal(window->height()));
        QCOMPARE(pane->mapToScene(QPointF()), QPointF());
        QCOMPARE(qmlList(window, "queueItems"), queue); QCOMPARE(window->property("currentQueueId"), id);
        QCOMPARE(player->position(), position); QCOMPARE(player->playbackState(), QMediaPlayer::PausedState);
        click(window, "nowPlayingQueueButton"); QTRY_VERIFY(window->property("queueOpen").toBool());
        auto *popup = window->findChild<QObject *>("queuePopup"); QVERIFY(popup);
        QVERIFY(popup->property("y").toReal() >= 0 && popup->property("y").toReal() + popup->property("height").toReal() <= window->height());
        QTest::keyClick(window, Qt::Key_Escape); QTRY_VERIFY(!window->property("queueOpen").toBool());
        QVERIFY(window->property("fullscreenNowPlaying").toBool());
        window->grabWindow().save(QCoreApplication::applicationDirPath() + "/musica-fullscreen-now-playing.png");
        click(window, "nowPlayingPlayPauseButton"); QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
        click(window, "nowPlayingPlayPauseButton"); QTRY_COMPARE(player->playbackState(), QMediaPlayer::PausedState);
        QTest::keyClick(window, Qt::Key_F11);
        QTRY_COMPARE(window->visibility(), QWindow::Windowed); QVERIFY(!window->property("fullscreenNowPlaying").toBool());
        QVERIFY(window->property("nowPlayingOpen").toBool() && bar->isVisible() && sidebar->isVisible());
        click(window, "nowPlayingFullscreenButton"); QTRY_COMPARE(window->visibility(), QWindow::FullScreen);
        QTest::keyClick(window, Qt::Key_Escape); QTRY_COMPARE(window->visibility(), QWindow::Windowed);
        QVERIFY(window->property("nowPlayingOpen").toBool());
        QTest::keyClick(window, Qt::Key_Escape); QTRY_VERIFY(!window->property("nowPlayingOpen").toBool());

        window->showMaximized(); QTRY_COMPARE(window->visibility(), QWindow::Maximized);
        QTest::keyClick(window, Qt::Key_F11); QTRY_COMPARE(window->visibility(), QWindow::FullScreen);
        QTest::qWait(60); // Let the fullscreen header finish its layout before the pointer click.
        QVERIFY(window->property("fullscreenNowPlaying").toBool());
        QCOMPARE(window->property("fullscreenReturnVisibility").toInt(), int(QWindow::Maximized));
        pane = findItem(window->contentItem(), "nowPlayingPane"); QVERIFY(pane);
        QSignalSpy closed(pane, SIGNAL(closeRequested()));
        click(window, "closeNowPlayingButton"); QTest::qWait(40);
        QCOMPARE(closed.size(), 1);
        QVERIFY(!window->property("fullscreenNowPlaying").toBool());
        QVERIFY(!window->property("nowPlayingOpen").toBool());
        QTRY_COMPARE_WITH_TIMEOUT(window->visibility(), QWindow::Maximized, 1000);
        QVERIFY(!window->property("nowPlayingOpen").toBool() && bar->isVisible());
        window->showNormal(); player->stop(); QCOMPARE(warnings.size(), 0);
    }

    void queueBrowsingSurvivesRemovalAndCanJumpToCurrentSong_data()
    {
        QTest::addColumn<double>("textScale");
        QTest::addColumn<QSize>("windowSize");
        QTest::newRow("normal") << 1.0 << QSize(1280, 850);
        QTest::newRow("compact-large-text") << 2.0 << QSize(720, 480);
    }

    void queueBrowsingSurvivesRemovalAndCanJumpToCurrentSong()
    {
        QFETCH(double, textScale); QFETCH(QSize, windowSize);
        QSettings settings; settings.clear(); settings.setValue("library/folders", QStringList{}); settings.sync();
        const auto first = m_temp.filePath("queue-browsing/First.wav");
        const auto second = m_temp.filePath("queue-browsing/Second.wav");
        QVERIFY(writeWave(first, 120)); QVERIFY(writeWave(second, 120));
        QQmlApplicationEngine qml; QSignalSpy warnings(&qml, &QQmlApplicationEngine::warnings);
        qml.load(QUrl("qrc:/main.qml")); QVERIFY(!qml.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(qml.rootObjects().first());
        auto *library = window->findChild<TrackListModel *>("library");
        auto *player = window->findChild<PlaybackEngine *>("player");
        auto *display = qobject_cast<DisplaySettings *>(window->property("displayPreferences").value<QObject *>());
        QVERIFY(library && player && display);
        display->setDensity("compact"); display->setTextScale(textScale);
        const auto resetDisplay = qScopeGuard([display] { display->reset(); });
        window->resize(windowSize);
        player->audioOutput()->setMuted(true);
        library->addFiles({QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)}); QTRY_VERIFY(!library->scanning());
        QVariantList entries;
        // The same files appear repeatedly: scrolling must target the playing
        // queue entry, not the first matching URL.
        for (int i = 0; i < 120; ++i)
            entries.append(QVariantMap{{"id", QString::number(i + 1)},
                {"fileUrl", QUrl::fromLocalFile(i % 2 ? second : first).toString()}});
        window->setProperty("queueSerial", 120);
        window->setProperty("queueSource", entries); window->setProperty("queueItems", entries);
        auto *model = window->findChild<QAbstractItemModel *>("queueEntries"); QVERIFY(model);
        QCOMPARE(model->rowCount(), 120);
        QSignalSpy resets(model, &QAbstractItemModel::modelReset);
        const auto playEntry = [window](const QString &id) {
            return QMetaObject::invokeMethod(window, "playQueueItem", Q_ARG(QVariant, id));
        };
        QVERIFY(playEntry("81")); QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState); player->pause();
        click(window, "queueButton"); QTRY_VERIFY(window->property("queueOpen").toBool());
        auto *list = findItem(window->contentItem(), "queueList"); QVERIFY(list);
        auto *popup = window->findChild<QObject *>("queuePopup"); QVERIFY(popup);
        auto *content = list->property("contentItem").value<QQuickItem *>(); QVERIFY(content);
        const auto rowForId = [content](const QString &id) -> QQuickItem * {
            for (auto *item : content->childItems())
                if (item->objectName().startsWith("queueRow") && item->property("entryId").toString() == id) return item;
            return nullptr;
        };
        const auto visibleId = [&](const QString &id) {
            auto *row = rowForId(id); if (!row) return false;
            const auto bounds = row->mapRectToItem(list, row->boundingRect());
            return bounds.top() >= -1 && bounds.bottom() <= list->height() + 1;
        };
        const auto firstVisibleRow = [&]() -> QQuickItem * {
            QQuickItem *firstVisible = nullptr;
            qreal firstY = list->height();
            for (auto *row : content->childItems()) {
                if (!row->objectName().startsWith("queueRow")) continue;
                const auto bounds = row->mapRectToItem(list, row->boundingRect());
                if (bounds.bottom() > 0 && bounds.top() < firstY) { firstVisible = row; firstY = bounds.top(); }
            }
            return firstVisible;
        };
        const auto rowY = [list](QQuickItem *row) { return row->mapToItem(list, QPointF()).y(); };
        const auto positionAt = [list](int index, int mode) {
            return QMetaObject::invokeMethod(list, "positionViewAtIndex", Q_ARG(int, index), Q_ARG(int, mode));
        };
        // Opening starts at the currently playing duplicate, far down the queue.
        window->grabWindow().save(QCoreApplication::applicationDirPath() + "/musica-queue-layout-" + QString::fromLatin1(QTest::currentDataTag()) + ".png");
        QTRY_VERIFY2(visibleId("81"), qPrintable(QString("Queue viewport %1 high, current row %2 high, popup %3 high").arg(list->height()).arg(rowForId("81") ? rowForId("81")->height() : -1).arg(popup->property("height").toReal())));
        auto *jump = findItem(window->contentItem(), "queueCurrentSongButton"); QVERIFY(jump && jump->isEnabled());
        const auto jumpBounds = jump->mapRectToItem(window->contentItem(), jump->boundingRect());
        QVERIFY(jumpBounds.left() >= 0 && jumpBounds.right() <= window->width());
        QVERIFY(jumpBounds.top() >= 0 && jumpBounds.bottom() <= window->height());

        QMetaObject::invokeMethod(list, "positionViewAtBeginning"); QTest::qWait(60);
        QVERIFY(visibleId("1"));
        const auto startY = rowY(rowForId("1"));
        // Give the remove button keyboard focus too: deleting its delegate
        // must not make ListView follow a different current/focused row.
        auto *remove = findItem(window->contentItem(), "removeQueue1"); QVERIFY(remove); remove->forceActiveFocus();
        click(window, "removeQueue1"); QTest::qWait(60);
        QCOMPARE(model->rowCount(), 119); QCOMPARE(window->property("currentQueueId").toString(), QStringLiteral("81"));
        QVERIFY(rowForId("1")); QVERIFY(std::abs(rowY(rowForId("1")) - startY) < 1);
        QVERIFY(list->property("atYBeginning").toBool()); QVERIFY(!visibleId("81"));

        QMetaObject::invokeMethod(list, "positionViewAtEnd"); QTest::qWait(60);
        // Consecutive removals at the bottom may clamp by one row, never jump
        // away to the playing song or back to the start.
        for (int removed = 0; removed < 4; ++removed) {
            auto *anchor = firstVisibleRow(); QVERIFY(anchor);
            const auto anchorId = anchor->property("entryId").toString();
            const auto before = rowY(anchor), rowHeight = anchor->height();
            const int last = model->rowCount() - 1;
            click(window, "removeQueue" + QString::number(last)); QTest::qWait(50);
            QCOMPARE(window->property("currentQueueId").toString(), QStringLiteral("81"));
            QVERIFY(rowForId(anchorId)); QVERIFY(std::abs(rowY(rowForId(anchorId)) - before) <= rowHeight + 1);
            QVERIFY(list->property("atYEnd").toBool());
        }
        // Removing an entry above the viewport keeps the same visible songs
        // anchored, even though all subsequent indexes change.
        auto *anchor = firstVisibleRow(); QVERIFY(anchor);
        auto anchorId = anchor->property("entryId").toString();
        auto beforeY = rowY(anchor);
        QVERIFY(QMetaObject::invokeMethod(window, "removeQueueItem", Q_ARG(QVariant, QString("1")))); QTest::qWait(60);
        QVERIFY(rowForId(anchorId)); QVERIFY(std::abs(rowY(rowForId(anchorId)) - beforeY) < 1);

        QVERIFY(positionAt(45, 0)); QTest::qWait(60); // ListView.Beginning
        anchor = firstVisibleRow(); QVERIFY(anchor);
        anchorId = anchor->property("entryId").toString(); beforeY = rowY(anchor);
        QVERIFY(playEntry("101")); QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState); player->pause();
        QTest::qWait(60);
        QVERIFY(rowForId(anchorId)); QVERIFY(std::abs(rowY(rowForId(anchorId)) - beforeY) < 1);
        QVERIFY(!visibleId("101")); // Advancing playback does not steal browsing.
        QVERIFY(QMetaObject::invokeMethod(window, "removeQueueItem", Q_ARG(QVariant, QString("101"))));
        QTRY_COMPARE(window->property("currentQueueId").toString(), QStringLiteral("102"));
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState); player->pause(); QTest::qWait(40);
        QVERIFY(rowForId(anchorId)); QVERIFY(std::abs(rowY(rowForId(anchorId)) - beforeY) < 1);
        QVERIFY(!visibleId("102"));
        const auto queueBeforeJump = qmlList(window, "queueItems");
        const auto positionBeforeJump = player->position();
        click(window, "queueCurrentSongButton"); QTRY_VERIFY(visibleId("102"));
        QCOMPARE(qmlList(window, "queueItems"), queueBeforeJump);
        QCOMPARE(player->position(), positionBeforeJump); QCOMPARE(player->playbackState(), QMediaPlayer::PausedState);

        // Closing and reopening must re-center again; both footer and Now
        // Playing queue buttons use the same behavior.
        QMetaObject::invokeMethod(list, "positionViewAtBeginning"); QTest::qWait(40);
        window->setProperty("queueOpen", false); QTest::qWait(40);
        QVERIFY(playEntry("61")); QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState); player->pause();
        click(window, "nowPlayingCoverButton"); QTRY_VERIFY(window->property("nowPlayingOpen").toBool());
        click(window, "nowPlayingQueueButton"); QTRY_VERIFY(visibleId("61"));
        window->grabWindow().save(QCoreApplication::applicationDirPath() + "/musica-queue-" + QString::fromLatin1(QTest::currentDataTag()) + ".png");
        for (const auto &entry : {queueBeforeJump.first(), queueBeforeJump.last()}) {
            const auto id = entry.toMap().value("id").toString();
            QVERIFY(playEntry(id)); QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState); player->pause();
            click(window, "queueCurrentSongButton"); QTRY_VERIFY(visibleId(id));
        }
        QCOMPARE(resets.size(), 0); QCOMPARE(warnings.size(), 0);
        QVERIFY(QMetaObject::invokeMethod(window, "stopAndClearTrack"));
        window->setProperty("queueSource", QVariantList{}); window->setProperty("queueItems", QVariantList{}); QTest::qWait(30);
        QCOMPARE(model->rowCount(), 0); QVERIFY(!jump->isEnabled());
        window->setProperty("queueOpen", false); QTest::qWait(30);
        window->setProperty("queueOpen", true); QTest::qWait(40);
        QVERIFY(popup->property("visible").toBool()); QCOMPARE(warnings.size(), 0);
    }

    void shuffledQueueDragAndResumePreserveEntryIdentity()
    {
        QSettings settings; settings.clear(); settings.setValue("library/folders", QStringList{}); settings.sync();
        QStringList paths;
        for (const auto &name : {"Alpha", "Beta", "Gamma"}) {
            const auto path = m_temp.filePath(QStringLiteral("resume/%1.wav").arg(name));
            QVERIFY(writeWave(path, 30)); paths.append(path);
        }
        QVariantList savedQueue, savedSource;
        QString currentId, currentUrl;
        {
            QQmlApplicationEngine engine; QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
            engine.load(QUrl("qrc:/main.qml")); QVERIFY(!engine.rootObjects().isEmpty());
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            auto *library = window->findChild<TrackListModel *>("library");
            auto *player = window->findChild<PlaybackEngine *>("player");
            QTRY_VERIFY(library->libraryReady() && !library->scanning());
            QList<QUrl> files; for (const auto &path : paths) files.append(QUrl::fromLocalFile(path));
            library->addFiles(files); QTRY_VERIFY(!library->scanning());
            QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, QString("Songs")));
            QMetaObject::invokeMethod(window, "playAll", Q_ARG(QVariant, false));
            QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
            const auto duplicate = library->trackForUrl(QUrl::fromLocalFile(paths[0]).toString());
            QMetaObject::invokeMethod(window, "enqueueTrack", Q_ARG(QVariant, duplicate), Q_ARG(QVariant, false));
            auto items = qmlList(window, "queueItems"); QCOMPARE(items.size(), 4);
            const auto duplicateId = items.last().toMap().value("id").toString();
            QMetaObject::invokeMethod(window, "playQueueItem", Q_ARG(QVariant, duplicateId));
            QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
            player->pause(); window->setProperty("shuffleEnabled", true); window->setProperty("repeatMode", 2);
            QMetaObject::invokeMethod(window, "seekPlayback", Q_ARG(QVariant, 6000));
            QTRY_COMPARE(player->position(), 6000);
            currentId = window->property("currentQueueId").toString(); currentUrl = window->property("currentUrl").toString();
            window->setProperty("queueOpen", true); QTest::qWait(100);
            items = qmlList(window, "queueItems");
            const auto movedId = items[1].toMap().value("id").toString();
            auto *queue = findItem(window->contentItem(), "queueList"); QVERIFY(queue);
            auto *lastHandle = findItem(window->contentItem(), "queueDrag3"); QVERIFY(lastHandle);
            auto *target = lastHandle->parentItem()->parentItem();
            drag(window, findItem(window->contentItem(), "queueDrag1"), target);
            QTRY_COMPARE_WITH_TIMEOUT(qmlList(window, "queueItems").last().toMap().value("id").toString(), movedId, 1000);
            QCOMPARE(window->property("currentQueueId").toString(), currentId);
            QCOMPARE(player->position(), 6000);
            // Keep the drag alive while its source row scrolls out of view.
            for (int i = 0; i < 10; ++i) QMetaObject::invokeMethod(window, "enqueueTrack", Q_ARG(QVariant, duplicate), Q_ARG(QVariant, false));
            QTest::qWait(80);
            const auto firstId = qmlList(window, "queueItems").first().toMap().value("id").toString();
            auto *handle = findItem(window->contentItem(), "queueDrag0"); QVERIFY(handle);
            const auto start = handle->mapToScene(QPointF(10, 15)).toPoint();
            const auto bottom = queue->mapToScene(QPointF(22, queue->height() - 6)).toPoint();
            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
            for (int step = 1; step <= 6; ++step) QTest::mouseMove(window, start + (bottom - start) * step / 6, 20);
            QTRY_VERIFY_WITH_TIMEOUT(queue->property("atYEnd").toBool(), 2500);
            auto *last = findItem(window->contentItem(), "queueDrag13"); QVERIFY(last);
            auto *lastRow = last->parentItem()->parentItem();
            const auto end = lastRow->mapToScene(QPointF(12, lastRow->height() * 0.85)).toPoint();
            QTest::mouseMove(window, end, 30); QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, end);
            QTRY_COMPARE_WITH_TIMEOUT(qmlList(window, "queueItems").last().toMap().value("id").toString(), firstId, 1000);
            QCOMPARE(window->property("currentQueueId").toString(), currentId); QCOMPARE(player->position(), 6000);
            savedQueue = qmlList(window, "queueItems"); savedSource = qmlList(window, "queueSource");
            QMetaObject::invokeMethod(window, "persistSession");
            QCOMPARE(warnings.size(), 0);
        }
        settings.sync();
        {
            TrackListModel saved(settings.fileName(), false);
            auto session = saved.playbackSession();
            auto queue = session.value("queue").toList();
            queue.append(QVariantMap{{"id", "999"}, {"fileUrl", QUrl::fromLocalFile(m_temp.filePath("missing.wav")).toString()}});
            session["queue"] = queue; saved.savePlaybackSession(session);
        }
        QQmlApplicationEngine engine; QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        engine.load(QUrl("qrc:/main.qml")); QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        auto *library = window->findChild<TrackListModel *>("library"); auto *player = window->findChild<PlaybackEngine *>("player");
        QTRY_VERIFY(library->libraryReady() && !library->scanning());
        QTRY_COMPARE(player->position(), 6000);
        QCOMPARE(qmlList(window, "queueItems"), savedQueue); QCOMPARE(qmlList(window, "queueSource"), savedSource);
        QCOMPARE(window->property("currentQueueId").toString(), currentId); QCOMPARE(window->property("currentUrl").toString(), currentUrl);
        QVERIFY(window->property("shuffleEnabled").toBool()); QCOMPARE(window->property("repeatMode").toInt(), 2);
        QVERIFY(player->playbackState() != QMediaPlayer::PlayingState);
        QCOMPARE(library->mostPlayed()->count(), 0);
        QCOMPARE(warnings.size(), 0);
    }

    void newLibraryControlsAreUsable()
    {
        QSettings settings; settings.clear(); settings.setValue("library/folders", QStringList{});
        QStringList paths, urls;
        for (int i = 0; i < 3; ++i) {
            const auto path = m_temp.filePath(QStringLiteral("management-ui/%1/0%2 Song.wav").arg(i == 2 ? "Other" : "Original").arg(i + 1));
            QVERIFY(writeWave(path, 30, i == 2 ? "B" : "A", i == 2 ? "Other" : "Original", i + 1));
            paths.append(path); urls.append(QUrl::fromLocalFile(path).toString());
        }
        settings.setValue("library/files", paths);
        settings.setValue("library/playCounts", QVariantMap{{urls[1], 2}});
        settings.setValue("history/totals", QVariantMap{{urls[1], 240000LL}, {urls[0], 150000LL}});
        settings.setValue("history/days", QVariantMap{{QDate::currentDate().toString(Qt::ISODate), QVariantMap{{urls[1], QVariantMap{{"ms", 240000LL}, {"plays", 2}}}}}});
        settings.setValue("history/recent", QVariantList{QVariantMap{{"fileUrl", urls[1]}, {"at", QDateTime::currentMSecsSinceEpoch()}, {"ms", 240000LL}, {"qualified", true}}});
        settings.sync();
        QQmlApplicationEngine engine; QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        engine.load(QUrl("qrc:/main.qml")); QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); auto *library = window->findChild<TrackListModel *>("library");
        library->rescan(); // New test fixtures are discovered by a manual scan.
        QTRY_VERIFY(library->libraryReady() && !library->scanning());
        click(window, "navSmart playlists"); QTest::qWait(60); click(window, "smartPlaylist1");
        QCOMPARE(library->songs()->count(), 2); QCOMPARE(window->property("collectionId").toString(), QStringLiteral("never"));
        click(window, "navListening dashboard"); QTest::qWait(100);
        auto *dashboard = findItem(window->contentItem(), "listeningDashboard"); QVERIFY(dashboard && dashboard->isVisible());
        window->grabWindow().save(QCoreApplication::applicationDirPath() + "/musica-dashboard.png");
        click(window, "navAlbums"); QTest::qWait(60); click(window, "manageAlbumsButton"); QTest::qWait(60);
        click(window, "albumEditChoice0"); click(window, "albumEditChoice1");
        window->findChild<QObject *>("albumEditName")->setProperty("text", "Collective release");
        window->findChild<QObject *>("albumEditArtist")->setProperty("text", "Various artists");
        click(window, "previewAlbumEditButton"); QTest::qWait(80);
        auto *editor = window->findChild<QObject *>("albumEditor"); QVERIFY(editor);
        QVERIFY(!editor->property("preview").value<QJSValue>().toVariant().toMap().contains("error"));
        window->grabWindow().save(QCoreApplication::applicationDirPath() + "/musica-album-editor.png");
        click(window, "applyAlbumEditButton"); QCOMPARE(library->albums()->count(), 1);
        QTest::qWait(60); click(window, "manageAlbumsButton"); QTest::qWait(60); click(window, "undoAlbumEditButton"); QCOMPARE(library->albums()->count(), 2);
        const auto id = library->createPlaylist("Mixed", urls[0]); QVERIFY(library->addToPlaylist(id, urls[1])); QVERIFY(library->addToPlaylist(id, urls[2]));
        click(window, "navPlaylists"); QTest::qWait(80); click(window, "playlistOptions0"); QTest::qWait(40); click(window, "renamePlaylistAction"); QTest::qWait(60);
        window->findChild<QObject *>("playlistRenameField")->setProperty("text", "Renamed mix");
        QMetaObject::invokeMethod(window->findChild<QObject *>("renamePlaylistDialog"), "accept");
        QCOMPARE(library->playlists()->get(0).value("name").toString(), QStringLiteral("Renamed mix"));
        QTest::qWait(60); click(window, "groupCard0"); QTest::qWait(80);
        QCOMPARE(window->property("sortBy").toString(), QStringLiteral("Order"));
        auto *lastRow = findItem(window->contentItem(), "songRow2"); QVERIFY(lastRow);
        drag(window, findItem(window->contentItem(), "playlistDrag0"), lastRow);
        QTRY_COMPARE_WITH_TIMEOUT(library->songs()->get(2).value("fileUrl").toString(), urls[0], 1000);
        window->resize(900, 640); QTest::qWait(80);
        click(window, "collectionPlaylistOptions"); QTest::qWait(40); click(window, "deletePlaylistAction"); QTest::qWait(60); click(window, "confirmDeletePlaylist");
        QCOMPARE(library->playlists()->count(), 0);
        for (const auto &path : paths) QVERIFY(QFileInfo::exists(path));
        QCOMPARE(warnings.size(), 0);
    }

    void linuxDesktopControlsUseMpris()
    {
#if MUSICA_HAS_DBUS
        const auto daemon = QStandardPaths::findExecutable("dbus-daemon");
        QVERIFY2(!daemon.isEmpty(), "dbus-daemon is required for the isolated MPRIS integration check");
        QProcess bus; bus.setProgram(daemon); bus.setArguments({"--session", "--nofork", "--print-address=1"}); bus.start();
        QVERIFY(bus.waitForStarted()); QVERIFY(bus.waitForReadyRead());
        const QString address = QString::fromUtf8(bus.readLine()).trimmed(); QVERIFY(!address.isEmpty());
        const auto serviceBus = QDBusConnection::connectToBus(address, "musica-test-service");
        auto client = QDBusConnection::connectToBus(address, "musica-test-client"); QVERIFY(serviceBus.isConnected() && client.isConnected());
        QSettings settings; settings.clear(); settings.setValue("library/folders", QStringList{}); settings.sync();
        const auto audio = m_temp.filePath("mpris/Signal - Desktop.wav"); QVERIFY(writeWave(audio, 30, "Signal", "Desktop", 1));
        QQmlApplicationEngine engine; QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        engine.load(QUrl("qrc:/main.qml")); QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); auto *library = window->findChild<TrackListModel *>("library"); auto *player = window->findChild<PlaybackEngine *>("player");
        QTRY_VERIFY(library->libraryReady() && !library->scanning()); library->addFiles({QUrl::fromLocalFile(audio)}); QTRY_VERIFY(!library->scanning());
        MprisService desktop(window, library, player, serviceBus, "org.mpris.MediaPlayer2.MusicaTest"); QVERIFY(desktop.available());
        const QString path = "/org/mpris/MediaPlayer2", iface = "org.mpris.MediaPlayer2.Player", props = "org.freedesktop.DBus.Properties";
        DesktopSignalRecorder notifications;
        QVERIFY(client.connect(desktop.serviceName(), path, props, "PropertiesChanged", &notifications, SLOT(changed(QString,QVariantMap,QStringList))));
        QVERIFY(client.connect(desktop.serviceName(), path, iface, "Seeked", &notifications, SLOT(seeked(qlonglong))));
        auto call = [&client, &desktop, &path](const QString &interface, const QString &method, const QVariantList &args = {}) {
            auto message = QDBusMessage::createMethodCall(desktop.serviceName(), path, interface, method); message.setArguments(args);
            return client.asyncCall(message, 3000);
        };
        QDBusPendingCallWatcher introspection(call("org.freedesktop.DBus.Introspectable", "Introspect")); QTRY_VERIFY(introspection.isFinished());
        const QDBusPendingReply<QString> xml(introspection); QVERIFY2(!xml.isError(), qPrintable(xml.error().message())); QVERIFY(xml.value().contains(iface));
        QDBusPendingCallWatcher play(call(iface, "Play")); QTRY_VERIFY(play.isFinished()); QVERIFY(!play.isError()); QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
        QDBusPendingCallWatcher get(call(props, "GetAll", {iface})); QTRY_VERIFY(get.isFinished()); const QDBusPendingReply<QVariantMap> properties(get); QVERIFY2(!properties.isError(), qPrintable(properties.error().message()));
        QCOMPARE(properties.value().value("PlaybackStatus").toString(), QStringLiteral("Playing"));
        const auto metadata = qdbus_cast<QVariantMap>(properties.value().value("Metadata"));
        QCOMPARE(metadata.value("xesam:title").toString(), QStringLiteral("Desktop"));
        QCOMPARE(metadata.value("mpris:length").toLongLong(), 30000000LL);
        const auto trackId = qvariant_cast<QDBusObjectPath>(metadata.value("mpris:trackid")); QVERIFY(trackId.path().startsWith("/com/musica/track/"));
        QDBusPendingCallWatcher pause(call(iface, "Pause")); QTRY_VERIFY(pause.isFinished()); QTRY_COMPARE(player->playbackState(), QMediaPlayer::PausedState);
        QTRY_COMPARE(notifications.values.value("PlaybackStatus").toString(), QStringLiteral("Paused"));
        QDBusPendingCallWatcher seek(call(iface, "SetPosition", {QVariant::fromValue(trackId), QVariant::fromValue<qlonglong>(7000000)})); QTRY_VERIFY(seek.isFinished()); QVERIFY(!seek.isError()); QTRY_COMPARE(player->position(), 7000);
        QTRY_COMPARE(notifications.position, 7000000LL);
        QDBusPendingCallWatcher stale(call(iface, "SetPosition", {QVariant::fromValue(QDBusObjectPath("/com/musica/track/stale")), QVariant::fromValue<qlonglong>(1000000)})); QTRY_VERIFY(stale.isFinished()); QCOMPARE(player->position(), 7000);
        QDBusPendingCallWatcher volume(call(props, "Set", {iface, QString("Volume"), QVariant::fromValue(QDBusVariant(0.32))})); QTRY_VERIFY(volume.isFinished()); QVERIFY(!volume.isError()); QCOMPARE(library->volume(), 0.32);
        QDBusPendingCallWatcher shuffle(call(props, "Set", {iface, QString("Shuffle"), QVariant::fromValue(QDBusVariant(true))})); QTRY_VERIFY(shuffle.isFinished()); QVERIFY(window->property("shuffleEnabled").toBool());
        QDBusPendingCallWatcher repeat(call(props, "Set", {iface, QString("LoopStatus"), QVariant::fromValue(QDBusVariant(QString("Track")))})); QTRY_VERIFY(repeat.isFinished()); QCOMPARE(window->property("repeatMode").toInt(), 2);
        QDBusPendingCallWatcher bad(call(props, "Set", {iface, QString("LoopStatus"), QVariant::fromValue(QDBusVariant(QString("bad")))})); QTRY_VERIFY(bad.isFinished()); QVERIFY(bad.isError());
        QVERIFY(!notifications.changedKeys.contains("Position"));
        QCOMPARE(warnings.size(), 0);
        bus.terminate(); QVERIFY(bus.waitForFinished());
        QDBusConnection::disconnectFromBus("musica-test-client"); QDBusConnection::disconnectFromBus("musica-test-service");
#else
        QSKIP("MPRIS is a Linux desktop integration; native playback and window lifecycle are tested separately.");
#endif
    }

    void interfaceSearchPlaybackAndQueue()
    {
        QSettings settings;
        settings.clear();
        settings.setValue("library/folders", QStringList{});
        settings.setValue("library/files", QStringList{});
        settings.sync();
        QQmlApplicationEngine engine;
        QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        engine.load(QUrl(QStringLiteral("qrc:/main.qml")));
        QVERIFY2(!engine.rootObjects().isEmpty(), "The complete QML interface must load.");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        auto *library = window->findChild<TrackListModel *>("library");
        auto *player = window->findChild<PlaybackEngine *>("player");
        QVERIFY(library);
        QVERIFY(player);
        QTRY_VERIFY(!library->scanning());
        QVERIFY(library->tracks().isEmpty());
        QVERIFY(!window->findChild<QObject *>("playPauseButton")->property("enabled").toBool());

        const QString first = m_temp.filePath("ui/Sol - Amber.wav");
        const QString second = m_temp.filePath("ui/Sol - Blue.wav");
        QVERIFY(writeWave(first, 15));
        QVERIFY(writeWave(second, 15));
        library->addFiles({QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)});
        QTRY_COMPARE(library->tracks().size(), 2);
        QTRY_VERIFY(!library->scanning());
        QTest::qWait(100);

        click(window, "navSongs");
        QCOMPARE(window->property("activePage").toString(), QStringLiteral("Songs"));
        QObject *search = window->findChild<QObject *>("searchField");
        QVERIFY(search);
        search->setProperty("text", QStringLiteral("amber"));
        QTRY_COMPARE(library->songs()->count(), 1);
        search->setProperty("text", QString());
        QTest::qWait(100);
        click(window, "playAllButton");
        QTRY_COMPARE_WITH_TIMEOUT(player->playbackState(), QMediaPlayer::PlayingState, 8000);
        QCOMPARE(QUrl(window->property("currentUrl").toString()), QUrl::fromLocalFile(first));
        QVERIFY(player->audioOutput());
        click(window, "playPauseButton");
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PausedState);
        click(window, "nextButton");
        QTRY_COMPARE(QUrl(window->property("currentUrl").toString()), QUrl::fromLocalFile(second));
        QTRY_COMPARE(player->duration(), 15000);
        player->setPosition(5000);
        QTRY_VERIFY(player->position() >= 4900);
        click(window, "previousButton");
        QTRY_VERIFY(player->position() < 1000);
        QCOMPARE(QUrl(window->property("currentUrl").toString()), QUrl::fromLocalFile(second));
        library->setVolume(0.2);
        QTRY_VERIFY(qAbs(player->audioOutput()->volume() - 0.2f) < 0.001f);
        click(window, "shuffleButton");
        QVERIFY(window->property("shuffleEnabled").toBool());
        const auto queue = window->property("queueUrls").value<QJSValue>().toVariant().toList();
        QCOMPARE(queue.size(), 2);
        QVERIFY(queue.first() != queue.last());
        QCOMPARE(QUrl(queue.first().toString()), QUrl::fromLocalFile(second));
        click(window, "repeatButton");
        QCOMPARE(window->property("repeatMode").toInt(), 1);
        click(window, "repeatButton");
        QCOMPARE(window->property("repeatMode").toInt(), 2);
        player->setPosition(14950);
        QTest::qWait(250);
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
        QCOMPARE(QUrl(window->property("currentUrl").toString()), QUrl::fromLocalFile(second));
        QTRY_VERIFY(player->position() < 2000);
        // Turning repeat off stops at the final track, while repeat-all wraps.
        click(window, "shuffleButton");
        click(window, "repeatButton");
        QCOMPARE(window->property("repeatMode").toInt(), 0);
        player->setPosition(14950);
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::StoppedState);
        QCOMPARE(QUrl(window->property("currentUrl").toString()), QUrl::fromLocalFile(second));
        click(window, "repeatButton");
        click(window, "playPauseButton");
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
        player->setPosition(14950);
        QTRY_COMPARE(QUrl(window->property("currentUrl").toString()), QUrl::fromLocalFile(first));
        click(window, "queueButton");
        QVERIFY(window->property("queueOpen").toBool());
        window->setProperty("queueOpen", false);

        click(window, "favorite0");
        click(window, "navFavorites");
        QCOMPARE(window->property("favoriteCount").toInt(), 1);
        QCOMPARE(window->property("activePage").toString(), QStringLiteral("Favorites"));
        // Equal album titles belonging to different artists must stay separate.
        const QString third = m_temp.filePath("other-artist/ui/Vale - Coast.wav");
        QVERIFY(writeWave(third));
        library->addFiles({QUrl::fromLocalFile(third)});
        QTRY_COMPARE(library->tracks().size(), 3);
        click(window, "navAlbums");
        QTest::qWait(100);
        click(window, "groupCard0");
        QCOMPARE(window->property("collectionName").toString(), QStringLiteral("ui"));
        QCOMPARE(library->songs()->count(), 2);
        click(window, "navArtists");
        // Exercise the compact layout, empty search results, and the overview again.
        window->resize(900, 640);
        search->setProperty("text", QStringLiteral("no-such-song"));
        QTest::qWait(100);
        click(window, "navOverview");
        QTest::qWait(100);
        QCOMPARE(warnings.size(), 0);
        player->stop();
    }
};

QTEST_MAIN(MusicaTests)
#include "tst_musica.moc"
