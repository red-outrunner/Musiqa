#include "albumgrouping.h"
#include "artworkcache.h"
#include "displaysettings.h"
#include "playbackengine.h"
#include "sharing.h"
#include "tracklibrary.h"
#include "librarymaintenance.h"
#include "lyricsdocument.h"
#include "audiotestrequirements.h"
#include "playlistmigration.h"
#include <QAccessible>

#include <QAudioOutput>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QWheelEvent>
#include <QtTest>
#include <algorithm>

class PracticalUiTests : public QObject
{
    Q_OBJECT
    QTemporaryDir m_temp;
    QString m_musicRoot;
    int m_fixture = 0;

    static QQuickItem *findItem(QQuickItem *parent, const QString &name)
    {
        if (parent->objectName() == name) return parent;
        for (auto *child : parent->childItems()) if (auto *found = findItem(child, name)) return found;
        return nullptr;
    }
    static bool click(QQuickWindow *window, const QString &name, QQuickItem *parent = nullptr)
    {
        auto *item = findItem(parent ? parent : window->contentItem(), name);
        if (!item || !item->isVisible() || !item->isEnabled() || item->width() <= 0 || item->height() <= 0) return false;
        const auto point = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
        if (!QRectF(0, 0, window->width(), window->height()).contains(point)) return false;
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point.toPoint()); QTest::qWait(25); return true;
    }
    static QString errors(const QSignalSpy &warnings)
    {
        QStringList text;
        for (const auto &warning : warnings) for (const auto &error : warning.first().value<QList<QQmlError>>()) text.append(error.toString());
        return text.join('\n');
    }
    static QVariantList values(QObject *object, const char *property)
    {
        const auto value = object->property(property);
        return value.metaType() == QMetaType::fromType<QJSValue>() ? value.value<QJSValue>().toVariant().toList() : value.toList();
    }
    static bool revealInScroll(QQuickWindow *window, const QString &name, const QString &scrollName)
    {
        auto *item = findItem(window->contentItem(), name); auto *scroll = window->findChild<QObject *>(scrollName);
        auto *view = scroll ? scroll->property("contentItem").value<QQuickItem *>() : nullptr;
        if (!item || !view) return false;
        for (auto *ancestor = item; ancestor; ancestor = ancestor->parentItem()) ancestor->ensurePolished();
        QTest::qWait(25);
        const auto target = view->property("contentY").toReal() + item->mapToItem(view, QPointF()).y() - 8;
        view->setProperty("contentY", std::max(qreal(0), std::min(target, view->property("contentHeight").toReal() - view->height())));
        QTest::qWait(25); return insideWindow(window, item);
    }
    static bool insideWindow(QQuickWindow *window, QQuickItem *item)
    {
        if (!item || !item->isVisible() || item->width() <= 0 || item->height() <= 0) return false;
        const auto bounds = item->mapRectToScene(item->boundingRect());
        return bounds.left() >= -1 && bounds.top() >= -1 && bounds.right() <= window->width() + 1
            && bounds.bottom() <= window->height() + 1;
    }
    static QQuickItem *revealSetting(QQuickWindow *window, const QString &name)
    {
        auto *item = findItem(window->contentItem(), name);
        auto *view = window->findChild<QObject *>("displaySettingsScroll");
        if (!item || !view) return nullptr;
        auto *flickable = view->property("contentItem").value<QQuickItem *>();
        if (!flickable) return nullptr;
        for (int pass = 0; pass < 3; ++pass) {
            // New band delegates precede Row/ScrollView polish. Derive the
            // scroll range from the completed layout, as a user sees it.
            for (auto *ancestor = item; ancestor; ancestor = ancestor->parentItem()) ancestor->ensurePolished();
            QTest::qWait(20);
            if (name.startsWith("equalizerGainSlider")) {
                auto *bandsView = window->findChild<QObject *>("equalizerBandsScroll");
                auto *bandFlickable = bandsView ? bandsView->property("contentItem").value<QQuickItem *>() : nullptr;
                if (!bandFlickable) return nullptr;
                const qreal currentX = bandFlickable->property("contentX").toReal();
                const qreal maximumX = std::max(qreal(0), bandFlickable->property("contentWidth").toReal() - bandFlickable->width());
                const qreal targetX = currentX + item->mapToItem(bandFlickable, QPointF()).x() - (bandFlickable->width() - item->width()) / 2;
                bandFlickable->setProperty("contentX", std::max(qreal(0), std::min(targetX, maximumX)));
            }
            const qreal current = flickable->property("contentY").toReal();
            const qreal maximum = std::max(qreal(0), flickable->property("contentHeight").toReal() - flickable->height());
            auto *visibleBlock = name.startsWith("equalizerGainSlider") ? item->parentItem() : item;
            const qreal target = current + visibleBlock->mapToItem(flickable, QPointF()).y() - 8;
            flickable->setProperty("contentY", std::max(qreal(0), std::min(target, maximum)));
            QTest::qWait(20);
            if (insideWindow(window, item)) break;
        }
        return item;
    }
    static bool writeWave(const QString &path)
    {
        QDir().mkpath(QFileInfo(path).absolutePath()); QFile file(path); if (!file.open(QIODevice::WriteOnly)) return false;
        QDataStream stream(&file); stream.setByteOrder(QDataStream::LittleEndian);
        stream.writeRawData("RIFF", 4); stream << quint32(36 + 16000); stream.writeRawData("WAVEfmt ", 8);
        stream << quint32(16) << quint16(1) << quint16(1) << quint32(8000) << quint32(16000) << quint16(2) << quint16(16);
        stream.writeRawData("data", 4); stream << quint32(16000); stream.writeRawData(QByteArray(16000, '\0').constData(), 16000);
        return file.error() == QFile::NoError;
    }
    bool seedLibrary()
    {
        QCoreApplication::setApplicationName(QStringLiteral("PracticalUiTests%1").arg(++m_fixture));
        m_musicRoot = m_temp.filePath(QStringLiteral("fixture%1/Music").arg(m_fixture));
        QVariantList tracks; QStringList favorites;
        const QStringList relative{"Artist/Album/First.wav", "Other/Second.wav"};
        for (int i = 0; i < relative.size(); ++i) {
            const auto path = QDir(m_musicRoot).filePath(relative[i]); if (!writeWave(path)) return false;
            QVariantMap track{{"fileUrl", QUrl::fromLocalFile(path).toString()}, {"title", i == 0 ? "First song" : "Second song"},
                {"artist", "Artist"}, {"albumArtist", "Artist"}, {"album", "First album"}, {"genre", i == 0 ? "Jazz" : "Rock"},
                {"year", i == 0 ? "2012" : "2020"}, {"duration", 120000LL}, {"favorite", i == 0}, {"probed", true},
                {"artwork", QString()}, {"artStyle", i}, {"metadataVersion", TrackListModel::MetadataCacheVersion}};
            AlbumGrouping::updateTrackOrder(track, path); AlbumGrouping::updateIdentity(track);
            if (i == 0) favorites.append(track.value("fileUrl").toString()); tracks.append(track);
        }
        QSettings settings; settings.clear(); settings.setValue("library/folders", QStringList{m_musicRoot});
        settings.setValue("library/files", QStringList{}); settings.setValue("library/snapshot", tracks); settings.setValue("library/favorites", favorites); settings.sync();
        return settings.status() == QSettings::NoError;
    }
    static QQuickWindow *loadWindow(QQmlApplicationEngine &engine)
    {
        engine.load(QUrl("qrc:/main.qml"));
        if (engine.rootObjects().isEmpty()) return nullptr;
        return qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    }
private slots:
    void initTestCase()
    {
        QVERIFY(m_temp.isValid());
        qputenv("XDG_DATA_HOME", m_temp.filePath("data").toUtf8()); QDir().mkpath(m_temp.filePath("data"));
        QCoreApplication::setOrganizationName("MusicaPracticalUiTests"); QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_temp.filePath("config"));
        QQuickStyle::setStyle("Basic"); registerPerformanceTypes();
        qmlRegisterType<TrackListModel>("MusicPlayer", 1, 0, "TrackListModel");
        qmlRegisterType<PlaybackEngine>("MusicPlayer", 1, 0, "PlaybackEngine");
        qmlRegisterType<DisplaySettings>("MusicPlayer", 1, 0, "DisplaySettings"); qmlRegisterType<Sharing>("MusicPlayer", 1, 0, "Sharing");
        qmlRegisterSingletonType(QUrl("qrc:/components/Theme.qml"), "MusicPlayer", 1, 0, "Theme");
        qmlRegisterUncreatableType<LibraryViewModel>("MusicPlayer", 1, 0, "LibraryViewModel", "Provided by the music library");
    }

    void foldersAndPinnedAlbumNavigateAndPersist()
    {
        QVERIFY(seedLibrary()); QString pinnedAlbum;
        {
            QQmlApplicationEngine engine; QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
            auto *window = loadWindow(engine); QVERIFY2(window, qPrintable(errors(warnings)));
            auto *library = window->findChild<TrackListModel *>("library"); QVERIFY(library);
            QTRY_VERIFY(window->property("sessionRestored").toBool()); window->resize(1280, 1000); QTest::qWait(40);
            QVERIFY(click(window, "navFolders")); QCOMPARE(window->property("activePage").toString(), QString("Folders"));
            QTRY_VERIFY(findItem(window->contentItem(), "folderEntry0"));
            QVERIFY(click(window, "folderEntry0")); QCOMPARE(window->property("folderPath").toString(), m_musicRoot);
            QCOMPARE(library->songs()->count(), 2); QCOMPARE(window->property("collectionKind").toString(), QString("folder"));
            QVERIFY(click(window, "folderEntry0")); QCOMPARE(window->property("folderPath").toString(), m_musicRoot + "/Artist");
            QCOMPARE(library->songs()->count(), 1); QVERIFY(click(window, "folderUpButton"));
            QCOMPARE(window->property("folderPath").toString(), m_musicRoot); QVERIFY(click(window, "folderUpButton"));
            QVERIFY(window->property("folderPath").toString().isEmpty());
            QVERIFY(click(window, "navAlbums")); QTRY_VERIFY(findItem(window->contentItem(), "groupCard0")); QVERIFY(click(window, "groupCard0"));
            pinnedAlbum = window->property("collectionId").toString(); QVERIFY(!pinnedAlbum.isEmpty());
            QVERIFY(click(window, "pinCollectionButton")); QVERIFY(library->collectionPinned("album", pinnedAlbum));
            QVERIFY(click(window, "navArtists")); QTRY_VERIFY(findItem(window->contentItem(), "pinnedCollection0"));
            QVERIFY(click(window, "pinnedCollection0")); QCOMPARE(window->property("collectionId").toString(), pinnedAlbum);
            QCOMPARE(window->property("collectionKind").toString(), QString("album")); QCOMPARE(library->songs()->count(), 2);
            QCOMPARE(warnings.size(), 0);
        }
        TrackListModel restored(QSettings().fileName(), false); QVERIFY(restored.collectionPinned("album", pinnedAlbum)); QCOMPARE(restored.pinnedCollections().size(), 1);
    }

    void pressingEnterSearchesTheNewestTextBeforePlaying()
    {
        QVERIFY(seedLibrary()); QQmlApplicationEngine engine;
        auto *window = loadWindow(engine); QVERIFY(window);
        auto *library = window->findChild<TrackListModel *>("library"); QVERIFY(library);
        auto *player = window->findChild<PlaybackEngine *>("player"); QVERIFY(player); player->audioOutput()->setMuted(true);
        QTRY_VERIFY(window->property("sessionRestored").toBool());
        auto *search = window->findChild<QQuickItem *>("searchField"); QVERIFY(search);
        search->setProperty("text", "First");
        QTRY_COMPARE(window->property("appliedSearchQuery").toString(), QString("First"));
        const auto secondUrl = QUrl::fromLocalFile(QDir(m_musicRoot).filePath("Other/Second.wav")).toString();
        // Submit while the previous query's results are still displayed.
        search->setProperty("text", "Second"); search->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Return);
        QCOMPARE(window->property("currentUrl").toString(), secondUrl);
        QCOMPARE(values(window, "queueUrls"), QVariantList{secondUrl});
        search->setProperty("text", "No song matches this query"); QTest::keyClick(window, Qt::Key_Return);
        QCOMPARE(window->property("currentUrl").toString(), secondUrl);
        QCOMPARE(values(window, "queueUrls"), QVariantList{secondUrl}); player->stop();
    }

    void updatingArtistFavoritesKeepsExistingRows()
    {
        QVERIFY(seedLibrary());
        QSettings settings; auto tracks = settings.value("library/snapshot").toList();
        const auto initial = tracks.first().toMap();
        for (int index = 0; index < 60; ++index) {
            auto extra = initial;
            extra["fileUrl"] = QUrl::fromLocalFile(QDir(m_musicRoot).filePath(QString("Extra %1/Song.wav").arg(index))).toString();
            extra["album"] = QString("ZZZ Album %1").arg(index); extra["favorite"] = false;
            AlbumGrouping::updateIdentity(extra); tracks.append(extra);
        }
        settings.setValue("library/snapshot", tracks); settings.sync();
        QQmlApplicationEngine engine;
        auto *window = loadWindow(engine); QVERIFY(window);
        auto *library = window->findChild<TrackListModel *>("library"); QVERIFY(library);
        QTRY_VERIFY(window->property("sessionRestored").toBool()); window->resize(1200, 850);
        QVERIFY(click(window, "navArtists")); QTRY_VERIFY(findItem(window->contentItem(), "groupCard0"));
        QVERIFY(click(window, "groupCard0")); QTRY_VERIFY(findItem(window->contentItem(), "artistAlbumHeader0"));
        QVERIFY(click(window, "artistAlbumHeader0")); QTRY_VERIFY(findItem(window->contentItem(), "artistAlbumSong1"));
        auto *list = findItem(window->contentItem(), "artistAlbumList"); QVERIFY(list);
        auto *model = qobject_cast<QAbstractItemModel *>(list->property("model").value<QObject *>()); QVERIFY(model);
        QSignalSpy removed(model, &QAbstractItemModel::rowsRemoved), reset(model, &QAbstractItemModel::modelReset);
        const auto url = QUrl::fromLocalFile(QDir(m_musicRoot).filePath("Artist/Album/First.wav")).toString();
        library->toggleFavorite(url); QTest::qWait(100);
        QCOMPARE(removed.size(), 0); QCOMPARE(reset.size(), 0);
        auto *row = findItem(window->contentItem(), "artistAlbumSong1"); QVERIFY(row);
        const auto value = row->property("track");
        const auto track = value.metaType() == QMetaType::fromType<QJSValue>() ? value.value<QJSValue>().toVariant().toMap() : value.toMap();
        QVERIFY(!track.value("favorite").toBool());
        QVERIFY(list->property("contentHeight").toReal() > 1600);
        list->setProperty("contentY", list->property("originY").toReal() + 1000); QTest::qWait(30);
        auto *pane = window->findChild<QObject *>("artistAlbumsPane"); QVERIFY(pane);
        const auto offset = pane->property("scrollPosition").toReal();
        library->toggleFavorite(url); QTest::qWait(100);
        QVERIFY(qAbs(pane->property("scrollPosition").toReal() - offset) < 2);
        QVERIFY(click(window, "navSongs")); QVERIFY(QMetaObject::invokeMethod(window, "goBack"));
        QTRY_VERIFY(findItem(window->contentItem(), "artistAlbumList"));
        list = findItem(window->contentItem(), "artistAlbumList");
        pane = window->findChild<QObject *>("artistAlbumsPane"); QVERIFY(pane);
        QTRY_VERIFY2(qAbs(pane->property("scrollPosition").toReal() - offset) < 2,
            qPrintable(QString("Restored artist offset %1, expected %2; pending %3")
                .arg(pane->property("scrollPosition").toReal()).arg(offset).arg(window->property("pendingArtistOffset").toReal())));
    }

    void practicalLayoutSearchArtistAlbumsAndQueueTabsWorkTogether()
    {
        QVERIFY(seedLibrary());
        QQmlApplicationEngine engine; QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        auto *window = loadWindow(engine); QVERIFY2(window, qPrintable(errors(warnings)));
        auto *library = window->findChild<TrackListModel *>("library"); QVERIFY(library);
        auto *display = window->property("displayPreferences").value<DisplaySettings *>(); QVERIFY(display);
        const auto restore = qScopeGuard([display] { display->reset(); }); display->reset();
        QTRY_VERIFY(window->property("sessionRestored").toBool());
        auto *theme = engine.singletonInstance<QObject *>(qmlTypeId("MusicPlayer", 1, 0, "Theme")); QVERIFY(theme);
        const auto scale = theme->property("unitScale").toReal();
        window->resize(qRound(1400 * scale), qRound(900 * scale)); QTest::qWait(50);
        auto *sidebar = findItem(window->contentItem(), "sidebar"); QVERIFY(sidebar);
        const auto initialWidth = sidebar->width();
        QVERIFY(click(window, "sidebarToggleButton")); QVERIFY(sidebar->width() < initialWidth);
        QVERIFY(!findItem(window->contentItem(), "appWordmark")->isVisible());
        QVERIFY(click(window, "sidebarToggleButton")); QCOMPARE(sidebar->width(), initialWidth);
        auto *handle = findItem(window->contentItem(), "sidebarResizeHandle"); QVERIFY(handle);
        const auto point = handle->mapToScene(QPointF(handle->width() / 2, 180)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, point);
        QTest::mouseMove(window, point + QPoint(60, 0), 20);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, point + QPoint(60, 0));
        QVERIFY(sidebar->width() > initialWidth + 40);
        QVERIFY(click(window, "navAlbums")); QTRY_VERIFY(findItem(window->contentItem(), "groupCard0"));
        auto *grid = findItem(window->contentItem(), "groupGrid"); QVERIFY(grid);
        QVERIFY(click(window, "artworkSizeButton")); QTest::qWait(30);
        auto *sizing = findItem(window->contentItem(), "artworkSizeSlider"); QVERIFY(sizing);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, sizing->mapToScene(QPointF(sizing->width() - 1, sizing->height() / 2)).toPoint());
        QTRY_COMPARE(window->property("artworkSize").toInt(), 320);
        const auto larger = grid->property("cellWidth").toReal();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, sizing->mapToScene(QPointF(1, sizing->height() / 2)).toPoint());
        QTRY_COMPARE(window->property("artworkSize").toInt(), 150);
        QVERIFY(grid->property("cellWidth").toReal() < larger); QTest::keyClick(window, Qt::Key_Escape);

        QVERIFY(click(window, "navArtists")); QTRY_VERIFY(findItem(window->contentItem(), "groupCard0"));
        QVERIFY(click(window, "groupCard0"));
        QTRY_VERIFY(findItem(window->contentItem(), "artistAlbumHeader0"));
        QVERIFY(click(window, "artistAlbumHeader0")); QTRY_VERIFY(findItem(window->contentItem(), "artistAlbumSong1"));
        QVERIFY(click(window, "artistAlbumSong1Select")); QCOMPARE(window->property("selectedCount").toInt(), 1);
        QVERIFY(click(window, "clearSongSelection"));
        window->grabWindow().save(QCoreApplication::applicationDirPath() + "/musica-artist-albums.png");
        QVERIFY(click(window, "showArtistSongsButton")); QTRY_VERIFY(findItem(window->contentItem(), "songRow0")->isVisible());
        const auto tracks = library->tracks(); QCOMPARE(tracks.size(), 2);
        window->setProperty("currentUrl", tracks[0].toMap().value("fileUrl"));
        window->setProperty("selectedUrls", QVariantList{tracks[1].toMap().value("fileUrl")});
        auto *first = findItem(window->contentItem(), "songRow0"), *second = findItem(window->contentItem(), "songRow1"); QVERIFY(first && second);
        QVERIFY(first->property("color") != second->property("color"));
        QVERIFY(findItem(window->contentItem(), "playingIndicator0")->isVisible());

        QVERIFY(click(window, "navFavorites"));
        auto *search = window->findChild<QObject *>("searchField"); QVERIFY(search); search->setProperty("text", "a");
        QTRY_VERIFY(findItem(window->contentItem(), "searchResultsList"));
        auto *results = findItem(window->contentItem(), "searchResultsList");
        QTRY_COMPARE(results->property("count").toInt(), 2); QCOMPARE(library->songs()->count(), 1);
        QVERIFY(click(window, "searchCategory_artist")); QTRY_COMPARE(results->property("count").toInt(), 1);
        QTRY_VERIFY(findItem(window->contentItem(), "searchGroup0")); QVERIFY(click(window, "searchGroup0"));
        QTRY_COMPARE(window->property("collectionKind").toString(), QString("artist"));
        QVERIFY(!window->property("searching").toBool());
        QVERIFY(QMetaObject::invokeMethod(window, "goBack")); QTRY_VERIFY(window->property("searching").toBool());
        QCOMPARE(window->property("activePage").toString(), QString("Favorites")); QCOMPARE(window->property("searchCategory").toString(), QString("artist"));
        QVERIFY(click(window, "searchCategory_album")); QTest::qWait(30);
        QTRY_COMPARE(window->property("appliedSearchQuery").toString(), QString("a"));
        window->grabWindow().save(QCoreApplication::applicationDirPath() + "/musica-unified-search.png");
        search->setProperty("text", "");
        QVERIFY(click(window, "navSongs")); QTest::qWait(30);
        QVERIFY(findItem(window->contentItem(), "playerMoreButton")->isVisible());
        QVERIFY(click(window, "playerMoreButton")); QTest::qWait(30);
        QVERIFY(click(window, "crossfadeSettingsButton"));
        auto *options = window->findChild<QObject *>("playbackOptions"); QVERIFY(options && options->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(options, "close"));
        for (int index = library->savedQueues().size(); index < 4; ++index)
            QVERIFY(QMetaObject::invokeMethod(window, "createSavedQueue", Q_ARG(QVariant, QString("Queue %1").arg(index + 1))));
        const QVariantList queuedSongs{tracks[0], tracks[1], tracks[0]};
        QVERIFY(QMetaObject::invokeMethod(window, "handleBatchAction", Q_ARG(QVariant, QString("queue")), Q_ARG(QVariant, queuedSongs)));
        const auto entries = values(window, "queueItems"); QCOMPARE(entries.size(), 3);
        window->setProperty("currentQueueId", entries[1].toMap().value("id"));
        QVERIFY(click(window, "queueButton")); QTest::qWait(40);
        for (int index = 0; index < 4; ++index) QVERIFY(insideWindow(window, findItem(window->contentItem(), "queueTab" + QString::number(index))));
        auto *model = qobject_cast<QAbstractItemModel *>(window->findChild<QObject *>("queueEntries")); QVERIFY(model);
        const auto role = model->roleNames().key("queueSection", -1); QVERIFY(role >= 0);
        QCOMPARE(model->data(model->index(0, 0), role).toString(), QString("Previously played"));
        QCOMPARE(model->data(model->index(1, 0), role).toString(), QString("Now playing"));
        QCOMPARE(model->data(model->index(2, 0), role).toString(), QString("Up next"));
        window->grabWindow().save(QCoreApplication::applicationDirPath() + "/musica-queue-tabs.png");
        QVERIFY(click(window, "queueTab0")); QVERIFY(library->activeQueueId() == library->savedQueues()[0].toMap().value("id").toString());
        window->setProperty("queueOpen", false);
        QVERIFY(QMetaObject::invokeMethod(window, "savePagePreferences"));
        QCOMPARE(library->savedViewState().value("artworkSize").toInt(), 150);
        QVERIFY(library->savedViewState().value("sidebarSize").toInt() > 250);
        QCOMPARE(warnings.size(), 0);
    }

    void completedQueueAndPlaylistActionsDeselectTheirSongs()
    {
        QVERIFY(seedLibrary());
        QQmlApplicationEngine engine; QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        auto *window = loadWindow(engine); QVERIFY2(window, qPrintable(errors(warnings)));
        auto *library = window->findChild<TrackListModel *>("library"); QVERIFY(library);
        QTRY_VERIFY(window->property("sessionRestored").toBool()); window->resize(1200, 800);
        QVERIFY(click(window, "navSongs"));
        const auto tracks = library->tracks(); QCOMPARE(tracks.size(), 2);
        const auto firstUrl = tracks[0].toMap().value("fileUrl");
        const auto secondUrl = tracks[1].toMap().value("fileUrl");
        const QVariantList urls{firstUrl, secondUrl};
        const auto action = [window](const QString &name, const QVariantList &songs) {
            return QMetaObject::invokeMethod(window, "handleBatchAction", Q_ARG(QVariant, name), Q_ARG(QVariant, songs));
        };
        window->setProperty("selectedUrls", urls); QVERIFY(click(window, "queueSelectedSongs"));
        QCOMPARE(window->property("queueCount").toInt(), 2); QCOMPARE(window->property("selectedCount").toInt(), 0);
        QVERIFY(!findItem(window->contentItem(), "selectionBar")->isVisible());
        window->setProperty("selectedUrls", urls); QVERIFY(action("next", {tracks[0]}));
        QCOMPARE(values(window, "selectedUrls"), QVariantList{secondUrl});
        QVERIFY(action("favorite", {tracks[1]})); QCOMPARE(window->property("selectedCount").toInt(), 0);

        const auto id = library->createPlaylist("Existing playlist"); QVERIFY(!id.isEmpty());
        window->setProperty("selectedUrls", urls); QVERIFY(action("playlist", tracks));
        auto *dialog = window->findChild<QObject *>("playlistDialog"); QVERIFY(dialog);
        QCOMPARE(window->property("selectedCount").toInt(), 2);
        QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
        QTRY_VERIFY(!window->findChild<QObject *>("playlistDialog")); QCOMPARE(window->property("selectedCount").toInt(), 2);

        QVERIFY(action("playlist", tracks));
        auto *name = findItem(window->contentItem(), "playlistNameField"); QVERIFY(name);
        name->setProperty("text", "Existing playlist"); QVERIFY(click(window, "createPlaylistButton"));
        QCOMPARE(window->property("selectedCount").toInt(), 2); // Failed creation keeps the selection.
        name->setProperty("text", "New playlist"); QVERIFY(click(window, "createPlaylistButton"));
        QTRY_VERIFY(!window->findChild<QObject *>("playlistDialog")); QCOMPARE(window->property("selectedCount").toInt(), 0);
        QCOMPARE(library->playlists()->count(), 2);

        // Choosing an existing playlist also clears the selection, including songs already in it.
        for (int pass = 0; pass < 2; ++pass) {
            window->setProperty("selectedUrls", urls); QVERIFY(action("playlist", tracks));
            dialog = window->findChild<QObject *>("playlistDialog"); QVERIFY(dialog);
            auto *content = dialog->property("contentItem").value<QQuickItem *>(); QVERIFY(content);
            QTRY_VERIFY(findItem(content, "choosePlaylist0"));
            QTRY_VERIFY(insideWindow(window, findItem(content, "choosePlaylist0")));
            QVERIFY(click(window, "choosePlaylist0", content));
            QTRY_VERIFY(!window->findChild<QObject *>("playlistDialog")); QCOMPARE(window->property("selectedCount").toInt(), 0);
        }
        QCOMPARE(library->tracksForGroup("playlist", id).size(), 2);
        QCOMPARE(warnings.size(), 0);
    }

    void metadataDeselectsOnlySongsThatWereSaved_data()
    {
        QTest::addColumn<bool>("partialFailure");
        QTest::newRow("all-saved") << false;
        QTest::newRow("one-stale-file") << true;
    }

    void metadataDeselectsOnlySongsThatWereSaved()
    {
        QFETCH(bool, partialFailure);
        QVERIFY(seedLibrary());
        QQmlApplicationEngine engine; QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        auto *window = loadWindow(engine); QVERIFY2(window, qPrintable(errors(warnings)));
        auto *library = window->findChild<TrackListModel *>("library"); QVERIFY(library);
        QTRY_VERIFY(window->property("sessionRestored").toBool()); window->resize(1200, 800);
        const auto tracks = library->tracks(); QCOMPARE(tracks.size(), 2);
        const auto firstUrl = tracks[0].toMap().value("fileUrl");
        const auto secondUrl = tracks[1].toMap().value("fileUrl");
        window->setProperty("selectedUrls", QVariantList{firstUrl, secondUrl});
        const auto openEditor = [window, &tracks] {
            return QMetaObject::invokeMethod(window, "handleBatchAction", Q_ARG(QVariant, QString("metadata")), Q_ARG(QVariant, tracks));
        };
        QVERIFY(openEditor());
        auto *editor = window->findChild<QObject *>("batchMetadataEditor"); QVERIFY(editor);
        QTRY_VERIFY(editor->property("ready").toBool());
        QVERIFY(click(window, "cancelBatchMetadataButton"));
        QTRY_VERIFY(!window->findChild<QObject *>("batchMetadataEditor")); QCOMPARE(window->property("selectedCount").toInt(), 2);
        QVERIFY(openEditor()); editor = window->findChild<QObject *>("batchMetadataEditor"); QVERIFY(editor);
        QTRY_VERIFY(editor->property("ready").toBool());
        QVERIFY(QMetaObject::invokeMethod(editor, "applyField", Q_ARG(QVariant, QString("genre")), Q_ARG(QVariant, true)));
        QVERIFY(QMetaObject::invokeMethod(editor, "edit", Q_ARG(QVariant, QString("genre")), Q_ARG(QVariant, QString("Soul"))));
        if (partialFailure) {
            // An external audio edit after opening makes this file's revision stale.
            QFile file(QUrl(secondUrl.toString()).toLocalFile()); QVERIFY(file.open(QIODevice::ReadWrite));
            QVERIFY(file.seek(44)); QCOMPARE(file.write("\x01", 1), qint64(1)); file.close();
        }
        QSignalSpy saved(library, &TrackListModel::batchMetadataSaved);
        QVERIFY(click(window, "saveBatchMetadataButton")); QTRY_COMPARE_WITH_TIMEOUT(saved.size(), 1, 10000);
        QTRY_COMPARE(window->property("selectedCount").toInt(), partialFailure ? 1 : 0);
        QCOMPARE(library->trackForUrl(firstUrl.toString()).value("genre").toString(), QString("Soul"));
        if (partialFailure) {
            QCOMPARE(values(window, "selectedUrls"), QVariantList{secondUrl});
            QVERIFY(editor->property("visible").toBool()); QVERIFY(editor->property("finished").toBool());
            QCOMPARE(values(editor, "urls"), QVariantList{secondUrl});
        } else {
            QTRY_VERIFY(!window->findChild<QObject *>("batchMetadataEditor"));
            QVERIFY(!findItem(window->contentItem(), "selectionBar")->isVisible());
        }
        QCOMPARE(warnings.size(), 0);
    }

    void metadataEditorsSuggestExistingNames_data()
    {
        QTest::addColumn<bool>("batch"); QTest::addColumn<QString>("key");
        QTest::addColumn<QString>("query"); QTest::addColumn<QString>("expected"); QTest::addColumn<bool>("compact");
        for (bool batch : {false, true}) {
            const auto prefix = batch ? "batch-" : "single-";
            QTest::newRow(qPrintable(QString(prefix) + "artist")) << batch << QString("artist") << QString("rti") << QString("Artist") << false;
            QTest::newRow(qPrintable(QString(prefix) + "album-artist")) << batch << QString("albumArtist") << QString("rti") << QString("Artist") << false;
            QTest::newRow(qPrintable(QString(prefix) + "album")) << batch << QString("album") << QString("irst") << QString("First album") << false;
            QTest::newRow(qPrintable(QString(prefix) + "genre")) << batch << QString("genre") << QString("oc") << QString("Rock") << false;
        }
        QTest::newRow("compact-large-text") << true << QString("genre") << QString("oc") << QString("Rock") << true;
    }

    void metadataEditorsSuggestExistingNames()
    {
        QFETCH(bool, batch); QFETCH(QString, key); QFETCH(QString, query); QFETCH(QString, expected); QFETCH(bool, compact);
        QVERIFY(seedLibrary());
        QQmlApplicationEngine engine; QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        auto *window = loadWindow(engine); QVERIFY2(window, qPrintable(errors(warnings)));
        auto *library = window->findChild<TrackListModel *>("library"); QVERIFY(library);
        auto *display = window->property("displayPreferences").value<DisplaySettings *>(); QVERIFY(display);
        const auto restore = qScopeGuard([display] { display->reset(); });
        display->reset(); display->setTextScale(compact ? 1.5 : 1.0); window->resize(compact ? QSize(720, 480) : QSize(1200, 900));
        QTRY_VERIFY(window->property("sessionRestored").toBool());
        const auto tracks = library->tracks(); QCOMPARE(tracks.size(), 2);
        auto *loader = window->findChild<QObject *>(batch ? "batchMetadataEditorLoader" : "metadataEditorLoader"); QVERIFY(loader);
        QVERIFY(QMetaObject::invokeMethod(loader, "begin", Q_ARG(QVariant, batch ? QVariant(tracks) : tracks[0].toMap().value("fileUrl"))));
        auto *editor = window->findChild<QObject *>(batch ? "batchMetadataEditor" : "metadataEditor"); QVERIFY(editor);
        QTRY_VERIFY(editor->property("ready").toBool());
        if (batch) QVERIFY(QMetaObject::invokeMethod(editor, "applyField", Q_ARG(QVariant, key), Q_ARG(QVariant, true)));
        const auto fieldName = (batch ? "batchMetadata_" : "metadata_") + key;
        QVERIFY(revealInScroll(window, fieldName, batch ? "batchMetadataForm" : "metadataForm"));
        auto *field = findItem(window->contentItem(), fieldName); QVERIFY(field && field->isEnabled());
        QVERIFY(!field->property("suggestionsOpen").toBool());
        const auto typeQuery = [&] {
            field->forceActiveFocus(); QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
            for (const auto character : query) QTest::keyClick(window, character.toLatin1());
        };
        typeQuery(); QTRY_VERIFY(field->property("suggestionsOpen").toBool());
        QVERIFY(values(field, "suggestions").contains(expected)); QCOMPARE(field->property("text").toString(), query);
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!field->property("suggestionsOpen").toBool()); QVERIFY(editor->property("visible").toBool());
        typeQuery(); QTRY_VERIFY(field->property("suggestionsOpen").toBool());
        if (batch) {
            QTRY_VERIFY(findItem(window->contentItem(), fieldName + "Suggestion0"));
            QVERIFY(click(window, fieldName + "Suggestion0"));
        } else {
            QTest::keyClick(window, Qt::Key_Down); QTest::keyClick(window, Qt::Key_Return);
        }
        QCOMPARE(field->property("text").toString(), expected); QVERIFY(!field->property("suggestionsOpen").toBool());
        const auto draftValue = editor->property("draft");
        const auto draft = draftValue.metaType() == QMetaType::fromType<QJSValue>() ? draftValue.value<QJSValue>().toVariant().toMap() : draftValue.toMap();
        QCOMPARE(draft.value(key).toString(), expected);
        QVERIFY(editor->property("visible").toBool()); QVERIFY(!library->scanning());
        QVERIFY(click(window, batch ? "cancelBatchMetadataButton" : "cancelMetadataButton"));
        QCOMPARE(warnings.size(), 0);
    }

    void nowPlayingControlsStayPutWhenTitlesWrap_data()
    {
        QTest::addColumn<QSize>("size"); QTest::addColumn<double>("scale");
        QTest::newRow("normal") << QSize(1000, 650) << 1.0;
        QTest::newRow("compact-large-text") << QSize(600, 380) << 1.5;
        QTest::newRow("fullscreen") << QSize(1920, 1080) << 1.0;
    }

    void nowPlayingControlsStayPutWhenTitlesWrap()
    {
        QFETCH(QSize, size); QFETCH(double, scale);
        QQmlApplicationEngine engine; QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        auto *theme = engine.singletonInstance<QObject *>(qmlTypeId("MusicPlayer", 1, 0, "Theme")); QVERIFY(theme);
        auto *display = theme->property("preferences").value<DisplaySettings *>(); QVERIFY(display);
        const auto restore = qScopeGuard([display] { display->reset(); });
        display->reset(); display->setTextScale(scale);
        QQmlComponent component(&engine, QUrl("qrc:/components/NowPlayingPane.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QVariantMap track{{"title", "Hi"}, {"artist", "Artist"}, {"album", "Album"}, {"format", "FLAC"},
            {"artwork", QString()}, {"artStyle", 0}, {"favorite", false}};
        QQuickWindow window; window.resize(size);
        QScopedPointer<QObject> object(component.createWithInitialProperties({{"width", size.width()}, {"height", size.height()}, {"track", track}}));
        auto *pane = qobject_cast<QQuickItem *>(object.data()); QVERIFY2(pane, qPrintable(component.errorString()));
        pane->setParentItem(window.contentItem()); window.show(); QTest::qWait(50);
        auto *title = findItem(pane, "nowPlayingTitle");
        auto *play = findItem(pane, "nowPlayingPlayPauseButton");
        auto *seek = findItem(pane, "nowPlayingSeekSlider");
        auto *scroll = findItem(pane, "nowPlayingScroll"); QVERIFY(title && play && seek && scroll);
        QCOMPARE(title->property("lineCount").toInt(), 1);
        // Preserve a user's scroll position too when controls overflow a small window.
        scroll->setProperty("contentY", std::max(qreal(0), scroll->property("contentHeight").toReal() - scroll->height()));
        QTest::qWait(30);
        const auto playPosition = play->mapToScene(QPointF());
        const auto seekPosition = seek->mapToScene(QPointF());
        const auto scrollPosition = scroll->property("contentY").toReal();
        const QString longTitle = QString("A much longer song title with enough words to wrap across the available space ").repeated(4);
        for (const auto &text : {longTitle, QString("Hi"), longTitle}) {
            track["title"] = text; QVERIFY(pane->setProperty("track", track)); QTest::qWait(40);
            QCOMPARE(title->property("lineCount").toInt(), text == longTitle ? 2 : 1);
            QVERIFY2(qAbs(play->mapToScene(QPointF()).y() - playPosition.y()) < 0.5, "Changing the title moved the playback buttons");
            QVERIFY2(qAbs(seek->mapToScene(QPointF()).y() - seekPosition.y()) < 0.5, "Changing the title moved the seek slider");
            QCOMPARE(scroll->property("contentY").toReal(), scrollPosition);
        }
        QCOMPARE(warnings.size(), 0);
    }

    void overviewAndItsSearchScrollWithMouseAndGestures_data()
    {
        QTest::addColumn<QSize>("size"); QTest::addColumn<double>("scale");
        QTest::newRow("normal") << QSize(1200, 800) << 1.0;
        QTest::newRow("small-large-text") << QSize(720, 480) << 1.5;
    }

    void overviewAndItsSearchScrollWithMouseAndGestures()
    {
        QFETCH(QSize, size); QFETCH(double, scale);
        QVERIFY(seedLibrary());
        QVariantList tracks;
        for (int i = 0; i < 180; ++i) {
            const auto path = QDir(m_musicRoot).filePath(QString("Album %1/Search track %2.wav").arg(i / 15).arg(i, 3, 10, QLatin1Char('0')));
            QVERIFY(writeWave(path));
            QVariantMap track{{"fileUrl", QUrl::fromLocalFile(path).toString()}, {"title", QString("Search track %1").arg(i, 3, 10, QLatin1Char('0'))},
                {"artist", "Solo Artist"}, {"albumArtist", "Solo Artist"}, {"album", QString("Album %1").arg(i / 15)}, {"genre", "Jazz"},
                {"duration", 120000LL}, {"probed", true}, {"artwork", QString()}, {"artStyle", i % 4},
                {"metadataVersion", TrackListModel::MetadataCacheVersion}};
            AlbumGrouping::updateTrackOrder(track, path); AlbumGrouping::updateIdentity(track); tracks.append(track);
        }
        QSettings settings; settings.setValue("library/snapshot", tracks); settings.sync();
        QQmlApplicationEngine engine; QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        auto *window = loadWindow(engine); QVERIFY2(window, qPrintable(errors(warnings)));
        auto *display = window->property("displayPreferences").value<DisplaySettings *>(); QVERIFY(display);
        const auto restore = qScopeGuard([display] { display->reset(); });
        display->reset(); display->setTextScale(scale); window->resize(size);
        auto *library = window->findChild<TrackListModel *>("library"); QVERIFY(library);
        QTRY_VERIFY(window->property("sessionRestored").toBool()); QTRY_COMPARE(library->trackCount(), 180);
        QTest::qWait(70);
        auto *overview = findItem(window->contentItem(), "overviewPane");
        auto *outer = findItem(window->contentItem(), "contentScroll");
        auto *songs = findItem(window->contentItem(), "songList"); QVERIFY(overview && outer && songs);
        QVERIFY(overview->isVisible()); QVERIFY(!songs->isVisible());
        QVERIFY(outer->property("interactive").toBool());
        // A summary that fits needs no scrolling. Reduce the available height
        // to exercise overflow without requiring artificial empty space.
        if (outer->property("contentHeight").toReal() <= outer->height() + 20) {
            window->resize(window->width(), 520); QTest::qWait(60);
        }
        QTRY_VERIFY(outer->property("contentHeight").toReal() > outer->height() + 20);
        QPointingDevice touchpad("Overview touchpad", 9877, QInputDevice::DeviceType::TouchPad,
            QPointingDevice::PointerType::Finger, QInputDevice::Capability::Position | QInputDevice::Capability::PixelScroll | QInputDevice::Capability::Scroll, 2, 0);
        ulong timestamp = 100;
        const auto wheel = [&](QQuickItem *view, QPoint pixels, QPoint angle, Qt::ScrollPhase phase) {
            const auto point = view->mapToScene(QPointF(view->width() / 2, view->height() / 2));
            QWheelEvent event(point, window->mapToGlobal(point.toPoint()), pixels, angle, Qt::NoButton, Qt::NoModifier, phase, false,
                Qt::MouseEventSynthesizedBySystem, pixels.isNull() ? QPointingDevice::primaryPointingDevice() : &touchpad);
            event.setTimestamp(timestamp += 16); QSpontaneKeyEvent::setSpontaneous(&event); QCoreApplication::sendEvent(window, &event);
        };
        wheel(outer, QPoint(0, -120), {}, Qt::ScrollUpdate);
        QTRY_VERIFY(outer->property("contentY").toReal() > 20);
        const auto outerPosition = outer->property("contentY").toReal();
        wheel(outer, {}, QPoint(0, 120), Qt::NoScrollPhase);
        QTRY_VERIFY(outer->property("contentY").toReal() < outerPosition - 10);
        wheel(outer, {}, QPoint(0, 120), Qt::NoScrollPhase); QTest::qWait(130);
        outer->setProperty("contentY", 0); QTest::qWait(20);
        window->grabWindow().save(QCoreApplication::applicationDirPath() + "/musica-overview-" + QString::fromLatin1(QTest::currentDataTag()) + ".png");
        QVERIFY(click(window, "overviewSongs")); QTRY_COMPARE(window->property("activePage").toString(), QString("Songs"));
        QVERIFY(QMetaObject::invokeMethod(window, "goBack"));
        QTRY_COMPARE(window->property("activePage").toString(), QString("Overview"));
        auto *search = window->findChild<QObject *>("searchField"); QVERIFY(search);
        search->setProperty("text", "Search track");
        QTRY_VERIFY(findItem(window->contentItem(), "searchResultsList"));
        songs = findItem(window->contentItem(), "searchResultsList");
        QTRY_COMPARE(songs->property("count").toInt(), 180); QTRY_VERIFY(!overview->isVisible() && songs->isVisible());
        QVERIFY(!outer->property("interactive").toBool()); QVERIFY(songs->property("interactive").toBool());
        QTRY_VERIFY(songs->height() > 60); QTest::qWait(50);
        wheel(songs, QPoint(0, -180), {}, Qt::ScrollUpdate);
        QTRY_VERIFY_WITH_TIMEOUT(songs->property("contentY").toReal() >= 250, 300);
        const auto gesturePosition = songs->property("contentY").toReal();
        wheel(songs, {}, QPoint(0, -120), Qt::NoScrollPhase);
        QTRY_VERIFY(songs->property("contentY").toReal() > gesturePosition + 100);
        QTest::qWait(130);
        for (int i = 0; i < 25; ++i) { wheel(songs, QPoint(0, -1000), {}, Qt::ScrollUpdate); QTest::qWait(5); }
        QTRY_VERIFY(songs->property("atYEnd").toBool());
        QTRY_VERIFY(findItem(window->contentItem(), "searchSong179"));
        window->grabWindow().save(QCoreApplication::applicationDirPath() + "/musica-overview-search-" + QString::fromLatin1(QTest::currentDataTag()) + ".png");
        search->setProperty("text", "no matching title"); QTRY_COMPARE(library->songs()->count(), 0);
        search->setProperty("text", ""); QTRY_VERIFY(overview->isVisible());
        QVERIFY(outer->property("interactive").toBool());
        QCOMPARE(warnings.size(), 0);
    }

    void artistNavigationOnlyAcceptsVisibleNames_data()
    {
        QTest::addColumn<bool>("shared");
        QTest::newRow("single-artist") << false;
        QTest::newRow("shared-artists") << true;
    }

    void artistNavigationOnlyAcceptsVisibleNames()
    {
        QFETCH(bool, shared);
        QVERIFY(seedLibrary());
        QSettings settings; auto tracks = settings.value("library/snapshot").toList();
        auto track = tracks[0].toMap(); track["artist"] = shared ? "Artist A, Artist B" : "Solo Artist";
        tracks[0] = track; settings.setValue("library/snapshot", tracks); settings.sync();
        QQmlApplicationEngine engine; QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        auto *window = loadWindow(engine); QVERIFY2(window, qPrintable(errors(warnings)));
        auto *display = window->property("displayPreferences").value<DisplaySettings *>(); QVERIFY(display);
        const auto previousSplit = display->splitArtists(); const auto previousSeparators = display->artistSeparators();
        const auto restore = qScopeGuard([display, previousSplit, previousSeparators] { display->setSplitArtists(previousSplit); display->setArtistSeparators(previousSeparators); display->reset(); });
        display->reset(); display->setArtistSeparators(",;|"); display->setSplitArtists(shared);
        auto *library = window->findChild<TrackListModel *>("library");
        auto *player = window->findChild<PlaybackEngine *>("player"); QVERIFY(library && player); player->audioOutput()->setMuted(true);
        QTRY_VERIFY(window->property("sessionRestored").toBool());
        window->resize(1200, 800); window->setProperty("currentUrl", track.value("fileUrl")); QTest::qWait(50);
        track = library->trackForUrl(track.value("fileUrl").toString());
        const auto credits = track.value("artistCredits").toList(); QCOMPARE(credits.size(), shared ? 2 : 1);
        const int selected = shared ? 1 : 0;
        const auto artistId = credits[selected].toMap().value("id").toString();
        const auto artistName = credits[selected].toMap().value("name").toString();
        const auto hit = [window](QQuickItem *links, const QString &href, bool blank) {
            if (!links) return false;
            for (int y = int(links->height() / 2); y < int(links->height()); ++y) {
                for (int x = blank ? int(links->width()) - 3 : 0; x >= 0 && x < int(links->width()); x += blank ? -1 : 1) {
                    QString link;
                    if (!QMetaObject::invokeMethod(links, "linkAt", Q_RETURN_ARG(QString, link), Q_ARG(qreal, qreal(x)), Q_ARG(qreal, qreal(y)))) return false;
                    if (blank ? !link.isEmpty() : link != href) continue;
                    const auto point = links->mapToScene(QPointF(x, y));
                    if (!QRectF(0, 0, window->width(), window->height()).contains(point)) return false;
                    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point.toPoint()); QTest::qWait(30); return true;
                }
            }
            return false;
        };
        const auto selectedLink = QString("artist:%1").arg(selected);
        QVERIFY(click(window, "nowPlayingCoverButton")); QTest::qWait(40);
        auto *links = findItem(window->contentItem(), "nowPlayingArtistLink"); QVERIFY(links);
        QSignalSpy chosen(links, SIGNAL(artistChosen(QVariant))); QVERIFY(chosen.isValid());
        QVERIFY(hit(links, {}, true)); QCOMPARE(chosen.size(), 0); QVERIFY(window->property("nowPlayingOpen").toBool());
        QCOMPARE(window->property("activePage").toString(), QString("Overview"));
        QVERIFY(hit(links, selectedLink, false));
        QTRY_COMPARE(window->property("collectionId").toString(), artistId);
        QCOMPARE(window->property("collectionName").toString(), artistName);

        QVERIFY(QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, "Songs"))); QTest::qWait(40);
        links = findItem(window->contentItem(), "footerArtistLink"); QVERIFY(links);
        QVERIFY(hit(links, {}, true)); QCOMPARE(window->property("activePage").toString(), QString("Songs"));
        QVERIFY(hit(links, selectedLink, false)); QTRY_COMPARE(window->property("collectionId").toString(), artistId);

        QVERIFY(QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, "Songs"))); QTest::qWait(40);
        links = findItem(window->contentItem(), "songArtistLabel0"); QVERIFY(links);
        QVERIFY(hit(links, {}, true)); QCOMPARE(window->property("activePage").toString(), QString("Songs")); player->pause();
        QVERIFY(hit(links, selectedLink, false)); QTRY_COMPARE(window->property("collectionId").toString(), artistId);

        auto *menu = window->findChild<QObject *>("trackMenu"); QVERIFY(menu); menu->setProperty("track", track);
        QVERIFY(QMetaObject::invokeMethod(window, "handleTrackAction", Q_ARG(QVariant, "info"))); QTest::qWait(40);
        links = findItem(window->contentItem(), "songInfoArtistLink"); QVERIFY(links && links->isVisible());
        const auto collectionBefore = window->property("collectionId").toString();
        QVERIFY(hit(links, {}, true)); QCOMPARE(window->property("collectionId").toString(), collectionBefore);
        QVERIFY(window->findChild<QObject *>("songInfoDialog"));
        QVERIFY(hit(links, selectedLink, false)); QTRY_VERIFY(!window->findChild<QObject *>("songInfoDialog"));
        QCOMPARE(window->property("collectionId").toString(), artistId);
        QCOMPARE(warnings.size(), 0);
    }

    void undoQueueSearchAndAdvancedFiltersWorkTogether()
    {
        MUSICA_REQUIRE_AUDIO_OUTPUT();
        QVERIFY(seedLibrary()); QQmlApplicationEngine engine; QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        auto *window = loadWindow(engine); QVERIFY2(window, qPrintable(errors(warnings)));
        auto *library = window->findChild<TrackListModel *>("library"); auto *player = window->findChild<PlaybackEngine *>("player"); QVERIFY(library && player);
        player->audioOutput()->setMuted(true); QTRY_VERIFY(window->property("sessionRestored").toBool());
        const auto tracks = library->songs()->entries(); QCOMPARE(tracks.size(), 2);
        QVERIFY(QMetaObject::invokeMethod(window, "playTrack", Q_ARG(QVariant, tracks[0]), Q_ARG(QVariant, tracks)));
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState); player->pause(); player->setPosition(250);
        const auto current = window->property("currentQueueId").toString();
        QVERIFY(QMetaObject::invokeMethod(window, "enqueueTrack", Q_ARG(QVariant, tracks[1]), Q_ARG(QVariant, false)));
        auto queue = values(window, "queueItems"); QCOMPARE(queue.size(), 3);
        QVERIFY(QMetaObject::invokeMethod(window, "removeQueueItem", Q_ARG(QVariant, queue[1].toMap().value("id")))); QCOMPARE(window->property("queueCount").toInt(), 2);
        QVERIFY(click(window, "undoActionButton")); QCOMPARE(window->property("queueCount").toInt(), 3);
        QCOMPARE(window->property("currentQueueId").toString(), current); QCOMPARE(player->playbackState(), QMediaPlayer::PausedState); QCOMPARE(player->position(), 250LL);
        const auto ordered = values(window, "queueItems");
        QVERIFY(click(window, "shuffleButton")); QVERIFY(window->property("shuffleEnabled").toBool());
        QVERIFY(click(window, "undoActionButton")); QVERIFY(!window->property("shuffleEnabled").toBool()); QCOMPARE(values(window, "queueItems"), ordered);
        QVERIFY(click(window, "queueButton"));
        auto *search = findItem(window->contentItem(), "queueSearchField"); QVERIFY(search); search->setProperty("text", "Second song");
        auto *entries = window->findChild<QObject *>("queueEntries"); QVERIFY(entries); QTRY_COMPARE(entries->property("count").toInt(), 2);
        QVERIFY(click(window, "removeQueue1")); QTRY_COMPARE(entries->property("count").toInt(), 1); QCOMPARE(window->property("queueCount").toInt(), 2);
        QVERIFY(click(window, "queueUndoButton")); QTRY_COMPARE(entries->property("count").toInt(), 2); QCOMPARE(window->property("queueCount").toInt(), 3);
        QVERIFY(click(window, "queueCurrentSongButton")); QTRY_VERIFY(window->property("queueSearch").toString().isEmpty()); QTRY_COMPARE(entries->property("count").toInt(), 3);
        window->setProperty("queueOpen", false); QTest::qWait(30);
        const auto id = library->createPlaylistFromUrls("Original", {tracks[0].toMap().value("fileUrl").toString()}); QVERIFY(!id.isEmpty());
        QVERIFY(library->renamePlaylist(id, "Renamed")); QVERIFY(click(window, "undoActionButton")); QCOMPARE(library->playlists()->get(0).value("name").toString(), QString("Original"));
        QVERIFY(click(window, "advancedSearchButton"));
        auto *dialog = window->findChild<QObject *>("advancedSearchDialog"); QVERIFY(dialog);
        findItem(window->contentItem(), "filterGenre")->setProperty("text", "jazz");
        findItem(window->contentItem(), "filterYearMin")->setProperty("text", "2020");
        findItem(window->contentItem(), "filterYearMax")->setProperty("text", "2015");
        QVERIFY(click(window, "applyAdvancedFilters")); QVERIFY(dialog->property("visible").toBool()); QVERIFY(!dialog->property("errorMessage").toString().isEmpty());
        QTRY_VERIFY(insideWindow(window, findItem(window->contentItem(), "advancedFilterError")));
        findItem(window->contentItem(), "filterYearMin")->setProperty("text", "2010");
        findItem(window->contentItem(), "filterFavorite")->setProperty("currentIndex", 1);
        QVERIFY(click(window, "applyAdvancedFilters")); QTRY_COMPARE(library->songs()->count(), 1);
        QCOMPARE(library->songs()->get(0).value("title").toString(), QString("First song")); QVERIFY(!window->property("overviewSummary").toBool());
        QVERIFY(click(window, "advancedSearchButton")); QVERIFY(click(window, "clearAdvancedFilters")); QTRY_COMPARE(library->songs()->count(), 2);
        QVERIFY(window->property("overviewSummary").toBool()); QCOMPARE(warnings.size(), 0);
    }

    void collectionViewsColumnsAndPagePreferencesSurviveReopening()
    {
        QVERIFY(seedLibrary());
        QSettings settings; auto tracks = settings.value("library/snapshot").toList();
        for (int i = 0; i < 118; ++i) {
            const auto path = QDir(m_musicRoot).filePath(QString("Extra/Track %1.flac").arg(i));
            QVariantMap track{{"fileUrl", QUrl::fromLocalFile(path).toString()}, {"title", QString("Extra %1").arg(i)}, {"artist", "Artist"},
                {"albumArtist", "Artist"}, {"album", "Extra album"}, {"year", "2014"}, {"genre", "Jazz"}, {"duration", 600000LL}, {"probed", true},
                {"artwork", QString()}, {"artStyle", 0}, {"metadataVersion", TrackListModel::MetadataCacheVersion}};
            AlbumGrouping::updateTrackOrder(track, path); AlbumGrouping::updateIdentity(track); tracks.append(track);
        }
        settings.setValue("library/snapshot", tracks); settings.sync();
        const auto restore = qScopeGuard([] { DisplaySettings display; display.resetSongColumns(); display.setAvoidRecentShuffle(false); display.setShuffleRecentDays(7); display.reset(); });
        qreal savedY = 0, savedX = 0; int savedDurationWidth = 0;
        {
            QQmlApplicationEngine engine; QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
            auto *window = loadWindow(engine); QVERIFY2(window, qPrintable(errors(warnings)));
            auto *display = window->property("displayPreferences").value<DisplaySettings *>(); QVERIFY(display);
            display->reset(); display->resetSongColumns(); display->setTextScale(1.5); window->resize(800, 600);
            QTRY_VERIFY(window->property("sessionRestored").toBool());
            QVERIFY(click(window, "navAlbums")); QVERIFY(click(window, "collectionViewToggle"));
            QVERIFY(window->property("collectionListView").toBool());
            auto *grid = findItem(window->contentItem(), "groupGrid"); QVERIFY(grid); QTRY_COMPARE(grid->property("cellWidth").toReal(), grid->width());
            QVERIFY(click(window, "navArtists")); QVERIFY(click(window, "collectionViewToggle")); QVERIFY(window->property("collectionListView").toBool());
            QVERIFY(click(window, "navSongs")); QVERIFY(click(window, "songColumnsButton"));
            QVERIFY(revealInScroll(window, "columnVisible_year", "songColumnsScroll")); QVERIFY(click(window, "columnVisible_year"));
            QVERIFY(revealInScroll(window, "columnWidth_year", "songColumnsScroll"));
            auto *yearWidth = findItem(window->contentItem(), "columnWidth_year"); QVERIFY(yearWidth); yearWidth->setProperty("value", 300); QVERIFY(QMetaObject::invokeMethod(yearWidth, "valueModified"));
            QVERIFY(revealInScroll(window, "columnUp_year", "songColumnsScroll")); QVERIFY(click(window, "columnUp_year")); QVERIFY(click(window, "columnUp_year"));
            QVERIFY(revealInScroll(window, "columnVisible_album", "songColumnsScroll")); QVERIFY(click(window, "columnVisible_album"));
            QVERIFY(click(window, "applySongColumns")); QTest::qWait(40);
            QCOMPARE(display->songColumns().first().toMap().value("key").toString(), QString("year"));
            QVERIFY(findItem(window->contentItem(), "songColumn_year_0")); QVERIFY(!findItem(window->contentItem(), "songColumn_album_0"));
            auto *songs = findItem(window->contentItem(), "songList"); QVERIFY(songs); QTRY_VERIFY(songs->property("contentWidth").toReal() > songs->width());
            const auto point = songs->mapToScene(QPointF(songs->width() / 2, songs->height() / 2));
            QWheelEvent horizontal(point, window->mapToGlobal(point.toPoint()), {}, QPoint(0, -120), Qt::NoButton, Qt::ShiftModifier, Qt::NoScrollPhase, false);
            QSpontaneKeyEvent::setSpontaneous(&horizontal); QCoreApplication::sendEvent(window, &horizontal); QTRY_VERIFY(songs->property("contentX").toReal() > 20);
            auto *handle = findItem(window->contentItem(), "columnResize_duration"); QVERIFY(handle);
            auto resizePoint = handle->mapToScene(QPointF(handle->width() / 2, handle->height() / 2));
            if (resizePoint.x() >= window->width() - 10) {
                songs->setProperty("contentX", songs->property("contentWidth").toReal() - songs->width()); QTest::qWait(30);
                resizePoint = handle->mapToScene(QPointF(handle->width() / 2, handle->height() / 2));
            }
            const auto columnsBefore = display->songColumns(); int oldWidth = 0;
            for (const auto &entry : columnsBefore) if (entry.toMap().value("key") == "duration") oldWidth = entry.toMap().value("width").toInt();
            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, resizePoint.toPoint());
            for (int step = 1; step <= 4; ++step) QTest::mouseMove(window, resizePoint.toPoint() + QPoint(step * 10, 0), 20);
            QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, resizePoint.toPoint() + QPoint(40, 0)); QTest::qWait(50);
            for (const auto &entry : display->songColumns()) if (entry.toMap().value("key") == "duration") savedDurationWidth = entry.toMap().value("width").toInt();
            QVERIFY(savedDurationWidth > oldWidth + 20);
            savedX = songs->property("contentX").toReal();
            window->findChild<QObject *>("searchField")->setProperty("text", "Extra");
            window->setProperty("songFilters", QVariantMap{{"genre", "Jazz"}}); window->setProperty("sortBy", "Artist");
            auto *library = window->findChild<TrackListModel *>("library"); QTRY_COMPARE(library->songs()->count(), 118);
            QTRY_VERIFY(findItem(window->contentItem(), "searchResultsList"));
            auto *results = findItem(window->contentItem(), "searchResultsList");
            QTRY_COMPARE(results->property("count").toInt(), 118); QTest::qWait(40);
            results->setProperty("contentY", 1000); QTest::qWait(40);
            savedY = results->property("contentY").toReal();
            QVERIFY(QMetaObject::invokeMethod(window, "savePagePreferences"));
            window->grabWindow().save(QCoreApplication::applicationDirPath() + "/musica-custom-columns.png");
            QCOMPARE(warnings.size(), 0);
        }
        {
            QQmlApplicationEngine engine; QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
            auto *window = loadWindow(engine); QVERIFY2(window, qPrintable(errors(warnings))); window->resize(800, 600);
            QTRY_VERIFY(window->property("sessionRestored").toBool());
            QTRY_COMPARE(window->property("activePage").toString(), QString("Songs"));
            QCOMPARE(window->property("sortBy").toString(), QString("Artist")); QCOMPARE(window->property("albumViewStyle").toString(), QString("list"));
            QCOMPARE(window->property("artistViewStyle").toString(), QString("list"));
            QCOMPARE(window->findChild<QObject *>("searchField")->property("text").toString(), QString("Extra"));
            auto *display = window->property("displayPreferences").value<DisplaySettings *>(); QVERIFY(display);
            QCOMPARE(display->songColumns().first().toMap().value("key").toString(), QString("year"));
            QTRY_VERIFY(findItem(window->contentItem(), "searchResultsList"));
            auto *results = findItem(window->contentItem(), "searchResultsList");
            QTRY_VERIFY(std::abs(results->property("contentY").toReal() - savedY) < 5);
            window->findChild<QObject *>("searchField")->setProperty("text", "");
            auto *songs = findItem(window->contentItem(), "songList"); QVERIFY(songs);
            QTRY_VERIFY(std::abs(songs->property("contentX").toReal() - savedX) < 5);
            QVERIFY(click(window, "navAlbums")); QVERIFY(window->property("collectionListView").toBool());
            window->grabWindow().save(QCoreApplication::applicationDirPath() + "/musica-albums-list.png");
            QCOMPARE(warnings.size(), 0);
        }
    }

    void shufflePrefersUnheardSongsAndKeepsAllEntries()
    {
        QVERIFY(seedLibrary()); QQmlApplicationEngine engine; QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        auto *window = loadWindow(engine); QVERIFY2(window, qPrintable(errors(warnings)));
        auto *library = window->findChild<TrackListModel *>("library"); auto *player = window->findChild<PlaybackEngine *>("player"); QVERIFY(library && player);
        auto *display = window->property("displayPreferences").value<DisplaySettings *>(); QVERIFY(display);
        const auto restore = qScopeGuard([display] { display->setAvoidRecentShuffle(false); display->setShuffleRecentDays(7); });
        player->audioOutput()->setMuted(true); QTRY_VERIFY(window->property("sessionRestored").toBool());
        const auto tracks = library->songs()->entries(); QCOMPARE(tracks.size(), 2);
        const auto heard = tracks[0].toMap().value("fileUrl").toString(), unheard = tracks[1].toMap().value("fileUrl").toString();
        for (int step = 0; step <= 24; ++step) library->observeAudiblePlayback("history", heard, step * 500, 120000, true, step == 24, step * 500);
        display->setAvoidRecentShuffle(true);
        QVERIFY(QMetaObject::invokeMethod(window, "playAll", Q_ARG(QVariant, true)));
        QTRY_COMPARE(window->property("currentUrl").toString(), unheard); player->pause();
        const auto queue = values(window, "queueItems"); QCOMPARE(queue.size(), 2); QCOMPARE(queue.last().toMap().value("fileUrl").toString(), heard);
        QCOMPARE(warnings.size(), 0);
    }
    void undoingTheOnlyQueuedSongStopsAndClearsPlayback()
    {
        MUSICA_REQUIRE_AUDIO_OUTPUT();
        QVERIFY(seedLibrary()); QQmlApplicationEngine engine; QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        auto *window = loadWindow(engine); QVERIFY2(window, qPrintable(errors(warnings)));
        auto *library = window->findChild<TrackListModel *>("library"); auto *player = window->findChild<PlaybackEngine *>("player"); QVERIFY(library && player);
        player->audioOutput()->setMuted(true); QTRY_VERIFY(window->property("sessionRestored").toBool());
        const auto track = library->songs()->get(0);
        QVERIFY(QMetaObject::invokeMethod(window, "enqueueTrack", Q_ARG(QVariant, track), Q_ARG(QVariant, false)));
        const auto queue = values(window, "queueItems"); QCOMPARE(queue.size(), 1);
        QVERIFY(QMetaObject::invokeMethod(window, "playQueueItem", Q_ARG(QVariant, queue.first().toMap().value("id"))));
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
        QVERIFY(click(window, "undoActionButton")); QCOMPARE(window->property("queueCount").toInt(), 0);
        QVERIFY(window->property("currentUrl").toString().isEmpty()); QVERIFY(player->source().isEmpty());
        QVERIFY(player->playbackState() != QMediaPlayer::PlayingState); QCOMPARE(warnings.size(), 0);
    }

    void customSmartPlaylistEditorCombinesRules()
    {
        QVERIFY(seedLibrary());
        QQmlApplicationEngine engine; QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        auto *window = loadWindow(engine); QVERIFY2(window, qPrintable(errors(warnings)));
        auto *library = window->findChild<TrackListModel *>("library"); QVERIFY(library);
        QTRY_VERIFY(window->property("sessionRestored").toBool()); window->resize(1280, 1000); QTest::qWait(40);
        QVERIFY(click(window, "navSmart playlists")); QVERIFY(click(window, "newSmartPlaylistButton"));
        QTRY_VERIFY(findItem(window->contentItem(), "smartPlaylistName"));
        auto *name = findItem(window->contentItem(), "smartPlaylistName"); auto *genre = findItem(window->contentItem(), "smartPlaylistGenre");
        auto *yearFrom = findItem(window->contentItem(), "smartPlaylistYearFrom"); auto *yearTo = findItem(window->contentItem(), "smartPlaylistYearTo");
        auto *favorites = findItem(window->contentItem(), "smartPlaylistFavoriteRule"); auto *maxPlays = findItem(window->contentItem(), "smartPlaylistMaxPlays");
        QVERIFY(name && genre && yearFrom && yearTo && favorites && maxPlays);
        name->setProperty("text", "Jazz favorites"); genre->setProperty("text", "jazz"); yearFrom->setProperty("text", "2010"); yearTo->setProperty("text", "2015");
        favorites->setProperty("currentIndex", 1); maxPlays->setProperty("text", "0");
        QVERIFY(click(window, "saveSmartPlaylistButton")); QCOMPARE(library->customSmartPlaylists().size(), 1);
        const auto playlist = library->customSmartPlaylists().first().toMap(); QCOMPARE(playlist.value("count").toInt(), 1);
        QTRY_VERIFY(findItem(window->contentItem(), "customSmartCard0"));
        QVERIFY(click(window, "customSmartCard0"));
        QTRY_COMPARE(window->property("collectionKind").toString(), QString("customSmart")); QCOMPARE(library->songs()->count(), 1);
        QCOMPARE(library->songs()->get(0).value("title").toString(), QString("First song"));
        QVERIFY(QMetaObject::invokeMethod(window, "openSmartEditor", Q_ARG(QVariant, playlist))); QTest::qWait(30);
        name = findItem(window->contentItem(), "smartPlaylistName"); QVERIFY(name); name->setProperty("text", "Renamed jazz");
        QVERIFY(click(window, "saveSmartPlaylistButton")); QCOMPARE(window->property("collectionName").toString(), QString("Renamed jazz"));
        QCOMPARE(warnings.size(), 0);
    }

    void bookmarksCanBeAddedEditedAndRemovedWithLargeFonts()
    {
        QVERIFY(seedLibrary());
        QQmlApplicationEngine engine; QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        auto *window = loadWindow(engine); QVERIFY2(window, qPrintable(errors(warnings)));
        auto *library = window->findChild<TrackListModel *>("library"); QVERIFY(library);
        auto *display = window->property("displayPreferences").value<DisplaySettings *>(); QVERIFY(display);
        QTRY_VERIFY(window->property("sessionRestored").toBool()); window->resize(800, 600); display->setTextScale(1.5); QTest::qWait(40);
        const auto track = library->songs()->get(0);
        const auto url = track.value("fileUrl").toString();
        QVERIFY(QMetaObject::invokeMethod(window, "openTrackBookmarks", Q_ARG(QVariant, track))); QTest::qWait(30);
        auto *timestamp = findItem(window->contentItem(), "bookmarkTimestampField"); auto *name = findItem(window->contentItem(), "bookmarkNameField");
        QVERIFY(timestamp && name); timestamp->setProperty("text", "1:05"); name->setProperty("text", "Favorite passage");
        QVERIFY(click(window, "saveTrackBookmarkButton")); QCOMPARE(library->trackBookmarks(url).size(), 1);
        const auto first = library->trackBookmarks(url).first().toMap(); QCOMPARE(first.value("position").toLongLong(), 65000LL);
        QCOMPARE(first.value("name").toString(), QString("Favorite passage"));
        QTRY_VERIFY(findItem(window->contentItem(), "editTrackBookmark0")); QVERIFY(click(window, "editTrackBookmark0"));
        timestamp->setProperty("text", "0:30"); name->setProperty("text", "Earlier passage"); QVERIFY(click(window, "saveTrackBookmarkButton"));
        QCOMPARE(library->trackBookmarks(url).size(), 1); QCOMPARE(library->trackBookmarks(url).first().toMap().value("id"), first.value("id"));
        QCOMPARE(library->trackBookmarks(url).first().toMap().value("position").toLongLong(), 30000LL);
        QVERIFY(click(window, "removeTrackBookmark0")); QVERIFY(library->trackBookmarks(url).isEmpty());
        QCOMPARE(warnings.size(), 0);
    }

    void offlineLyricsOpenForAnotherSongAndCanSeek()
    {
        MUSICA_REQUIRE_AUDIO_OUTPUT();
        QVERIFY(seedLibrary());
        const auto song = m_musicRoot + "/Artist/Album/First.wav";
        QFile lyrics(m_musicRoot + "/Artist/Album/First.lrc"); QVERIFY(lyrics.open(QIODevice::WriteOnly));
        lyrics.write("[00:00.00]First lyric\n[00:00.40]Second lyric\n"); lyrics.close();
        QQmlApplicationEngine engine; QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        auto *window = loadWindow(engine); QVERIFY2(window, qPrintable(errors(warnings)));
        auto *library = window->findChild<TrackListModel *>("library"); auto *player = window->findChild<PlaybackEngine *>("player"); QVERIFY(library && player);
        player->audioOutput()->setMuted(true);
        QTRY_VERIFY(window->property("sessionRestored").toBool()); window->resize(1280, 1000); QTest::qWait(40);
        const auto track = library->trackForUrl(QUrl::fromLocalFile(song).toString());
        QVERIFY(QMetaObject::invokeMethod(window, "openLyrics", Q_ARG(QVariant, track)));
        auto *document = qobject_cast<LyricsDocument *>(player->lyrics()); QVERIFY(document);
        QTRY_VERIFY(!document->loading()); QTRY_COMPARE(document->lines().size(), 2); QVERIFY(document->synchronized());
        auto *dialog = window->findChild<QObject *>("lyricsDialog"); QVERIFY(dialog && dialog->property("visible").toBool());
        QCOMPARE(document->source(), QUrl::fromLocalFile(song));
        QVERIFY(QMetaObject::invokeMethod(dialog, "seekRequested", Q_ARG(QVariant, track), Q_ARG(qreal, 400)));
        QTRY_COMPARE(player->source(), QUrl::fromLocalFile(song)); QTRY_VERIFY(player->position() >= 350); player->pause();
        QMetaObject::invokeMethod(dialog, "close"); QCOMPARE(warnings.size(), 0);
    }
    void externalFilesPlayOrAppendWithoutRescanning()
    {
        MUSICA_REQUIRE_AUDIO_OUTPUT();
        QVERIFY(seedLibrary()); QQmlApplicationEngine engine; QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        auto *window = loadWindow(engine); QVERIFY2(window, qPrintable(errors(warnings)));
        auto *library = window->findChild<TrackListModel *>("library"); auto *player = window->findChild<PlaybackEngine *>("player"); QVERIFY(library && player);
        player->audioOutput()->setMuted(true); QTRY_VERIFY(window->property("sessionRestored").toBool());
        const auto track = library->songs()->get(0); const QVariantList urls{track.value("fileUrl")};
        QSignalSpy scanning(library, &TrackListModel::scanningChanged);
        QVERIFY(QMetaObject::invokeMethod(window, "openExternalFiles", Q_ARG(QVariant, urls), Q_ARG(QVariant, true)));
        QCOMPARE(window->property("queueCount").toInt(), 1); QVERIFY(player->playbackState() != QMediaPlayer::PlayingState);
        QVERIFY(QMetaObject::invokeMethod(window, "openExternalFiles", Q_ARG(QVariant, urls), Q_ARG(QVariant, false)));
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState); player->pause();
        QCOMPARE(window->property("currentUrl").toString(), track.value("fileUrl").toString()); QCOMPARE(scanning.size(), 0);
        QCOMPARE(warnings.size(), 0);
    }

    void backupRestoresLiveDisplayAndAudioPreferences()
    {
        QVERIFY(seedLibrary()); QQmlApplicationEngine engine; QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        auto *window = loadWindow(engine); QVERIFY2(window, qPrintable(errors(warnings)));
        auto *library = window->findChild<TrackListModel *>("library"); auto *player = window->findChild<PlaybackEngine *>("player"); QVERIFY(library && player);
        auto *display = window->property("displayPreferences").value<DisplaySettings *>(); QVERIFY(display);
        QTRY_VERIFY(window->property("sessionRestored").toBool());
        display->setTheme("light"); display->setTextScale(1.25); player->setEqualizerPreset("bass"); player->setEqualizerEnabled(true);
        QVariant extra;
        QVERIFY(QMetaObject::invokeMethod(window, "prepareLibraryBackup", Q_RETURN_ARG(QVariant, extra)));
        auto *maintenance = qobject_cast<LibraryMaintenance *>(library->maintenance()); QVERIFY(maintenance);
        const auto backup = QUrl::fromLocalFile(m_temp.filePath("ui-settings.musica-backup"));
        QVERIFY(maintenance->createBackup(backup, extra.toMap()));
        display->setTheme("dark"); display->setTextScale(1); player->setEqualizerEnabled(false); player->setEqualizerPreset("flat");
        QVERIFY(QMetaObject::invokeMethod(window, "prepareLibraryRestore")); QVERIFY(maintenance->restoreBackup(backup));
        QCOMPARE(display->theme(), QString("light")); QCOMPARE(display->textScale(), 1.25);
        QVERIFY(player->equalizerEnabled()); QCOMPARE(player->equalizerPreset(), QString("bass"));
        QVERIFY(!library->scanning()); QCOMPARE(warnings.size(), 0);
    }

    void audioControlsStayUsableWithLargeFonts_data()
    {
        QTest::addColumn<QSize>("size"); QTest::addColumn<qreal>("textScale");
        QTest::newRow("desktop") << QSize(1280, 850) << qreal(1);
        QTest::newRow("small-window-large-text") << QSize(800, 600) << qreal(1.5);
    }

    void desktopPreferencesAndPlaylistMigrationAreKeyboardAccessible_data()
    {
        QTest::addColumn<QSize>("windowSize"); QTest::addColumn<double>("scale");
        QTest::newRow("normal") << QSize(1280, 850) << 1.0;
        QTest::newRow("large-text") << QSize(900, 640) << 1.6;
    }
    void desktopPreferencesAndPlaylistMigrationAreKeyboardAccessible()
    {
        QFETCH(QSize, windowSize); QFETCH(double, scale);
        QVERIFY(seedLibrary()); QQmlApplicationEngine engine; QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        auto *window = loadWindow(engine); QVERIFY2(window, qPrintable(errors(warnings)));
        auto *library = window->findChild<TrackListModel *>("library"); auto *player = window->findChild<PlaybackEngine *>("player");
        auto *preferences = window->property("displayPreferences").value<DisplaySettings *>(); QVERIFY(library && player && preferences);
        const auto previous = preferences->exportPreferences();
        const auto restore = qScopeGuard([preferences, previous] { preferences->importPreferences(previous); });
        preferences->setTextScale(scale); preferences->setHighContrast(false); preferences->setLargeControls(false);
        QTRY_VERIFY(window->property("sessionRestored").toBool()); window->resize(windowSize); QTest::qWait(30);
        QVERIFY(QMetaObject::invokeMethod(window, "openDisplaySettings"));
        QTRY_VERIFY(window->findChild<QObject *>("displaySettingsDialog"));
        auto *dialog = window->findChild<QObject *>("displaySettingsDialog");
        QVERIFY(QMetaObject::invokeMethod(dialog, "goToSection", Q_ARG(QVariant, QString("Laptop and performance"))));
        auto *battery = revealSetting(window, "batterySaverModeBox"); QVERIFY(insideWindow(window, battery));
        battery->forceActiveFocus(); QTest::keyClick(window, Qt::Key_End);
        QTRY_COMPARE(preferences->batterySaverMode(), QString("always")); QTRY_VERIFY(player->economyMode()); QVERIFY(preferences->reduceEffects());
        QVERIFY(insideWindow(window, revealSetting(window, "pauseOnDisconnectCheckBox")));
        QVERIFY(insideWindow(window, revealSetting(window, "resumeAfterSleepCheckBox")));
        QVERIFY(insideWindow(window, revealSetting(window, "highContrastCheckBox"))); QVERIFY(click(window, "highContrastCheckBox")); QVERIFY(preferences->highContrast());
        QVERIFY(insideWindow(window, revealSetting(window, "largeControlsCheckBox"))); QVERIFY(click(window, "largeControlsCheckBox")); QVERIFY(preferences->largeControls());
        QVERIFY(click(window, "closeSettingsButton")); QTest::qWait(30);
        QVERIFY(click(window, "navSongs")); QTRY_VERIFY(findItem(window->contentItem(), "songRow0"));
        auto *first = findItem(window->contentItem(), "songRow0"); first->forceActiveFocus();
        auto *accessible = QAccessible::queryAccessibleInterface(first); QVERIFY(accessible); QVERIFY(accessible->text(QAccessible::Name).contains("First song"));
        QTest::keyClick(window, Qt::Key_Down); QTRY_VERIFY(findItem(window->contentItem(), "songRow1")->hasActiveFocus());
        QVERIFY(click(window, "navAlbums")); QTRY_VERIFY(findItem(window->contentItem(), "groupCard0"));
        findItem(window->contentItem(), "groupCard0")->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(window->property("collectionKind").toString(), QString("album"));

        const auto playlist = m_temp.filePath(QString("Migration-%1.m3u8").arg(m_fixture));
        QFile source(playlist); QVERIFY(source.open(QIODevice::WriteOnly));
        source.write(QUrl::fromLocalFile(m_musicRoot + "/Artist/Album/First.wav").toString().toUtf8() + '\n');
        source.write(QUrl::fromLocalFile(m_musicRoot + "/Other/Second.wav").toString().toUtf8() + '\n'); source.close();
        QVERIFY(QMetaObject::invokeMethod(window, "openPlaylistMigration")); QTRY_VERIFY(window->findChild<QObject *>("playlistMigrationDialog"));
        auto *migrationDialog = window->findChild<QObject *>("playlistMigrationDialog"); migrationDialog->setProperty("playlistFile", QUrl::fromLocalFile(playlist));
        QVERIFY(insideWindow(window, findItem(window->contentItem(), "migrationPreviewButton")));
        QVERIFY(click(window, "migrationPreviewButton"));
        auto *migration = qobject_cast<PlaylistMigration *>(library->playlistMigration()); QVERIFY(migration);
        QTRY_VERIFY(!migration->busy()); QCOMPARE(migration->selectedCount(), 2);
        QTRY_COMPARE(findItem(window->contentItem(), "migrationPreviewList")->property("count").toInt(), 2);
        QVERIFY(insideWindow(window, findItem(window->contentItem(), "migrationImportButton"))); QVERIFY(click(window, "migrationImportButton"));
        QTRY_COMPARE(window->property("collectionKind").toString(), QString("playlist")); QCOMPARE(library->songs()->count(), 2);
        QCOMPARE(warnings.size(), 0);
    }

    void audioControlsStayUsableWithLargeFonts()
    {
        QFETCH(QSize, size); QFETCH(qreal, textScale);
        QVERIFY(seedLibrary()); QQmlApplicationEngine engine; QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        auto *window = loadWindow(engine); QVERIFY2(window, qPrintable(errors(warnings)));
        auto *player = window->findChild<PlaybackEngine *>("player"); QVERIFY(player);
        auto *display = window->property("displayPreferences").value<DisplaySettings *>(); QVERIFY(display);
        const auto restoreDisplay = qScopeGuard([display] { display->reset(); });
        display->reset(); display->setTextScale(textScale); window->resize(size);
        QTRY_VERIFY(window->property("sessionRestored").toBool()); QTest::qWait(50);
        QVERIFY(!window->findChild<QQuickWindow *>("miniPlayer"));
        QVERIFY(!window->findChild<QObject *>("miniPlayerButton"));
        QVERIFY(!display->shortcuts().contains("miniPlayer"));
        for (const auto &value : display->shortcutOptions())
            QVERIFY(value.toMap().value("id").toString() != QStringLiteral("miniPlayer"));
        QVERIFY(click(window, "playerMoreButton"));
        QTest::qWait(40);
        auto *audioButton = findItem(window->contentItem(), "audioSettingsButton");
        QVERIFY(insideWindow(window, audioButton)); QVERIFY(click(window, "audioSettingsButton"));
        auto *dialog = window->findChild<QObject *>("displaySettingsDialog"); QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("visible").toBool());
        auto *sections = findItem(window->contentItem(), "settingsSectionSelector"); QVERIFY(sections);
        QTRY_COMPARE(sections->property("currentText").toString(), QString("Audio output"));
        QVERIFY(insideWindow(window, sections));
        QVERIFY(insideWindow(window, findItem(window->contentItem(), "closeSettingsButton")));
        auto *device = revealSetting(window, "outputDeviceBox"); QVERIFY(insideWindow(window, device));
        QVERIFY(device->property("count").toInt() >= 1); // System default is always available, including headless hosts.
        auto *mode = revealSetting(window, "outputModeBox"); QVERIFY(insideWindow(window, mode));
        mode->forceActiveFocus(); QTest::keyClick(window, Qt::Key_End);
        QTRY_COMPARE(player->outputMode(), QString("mono"));
        QTest::keyClick(window, Qt::Key_Home); QTRY_COMPARE(player->outputMode(), QString("stereo"));
        auto *replay = revealSetting(window, "replayGainModeBox"); QVERIFY(insideWindow(window, replay));
        replay->forceActiveFocus(); QTest::keyClick(window, Qt::Key_End); QTRY_COMPARE(player->replayGainMode(), QString("album"));
        QTest::keyClick(window, Qt::Key_Home); QTRY_COMPARE(player->replayGainMode(), QString("off"));

        QVERIFY(QMetaObject::invokeMethod(dialog, "goToSection", Q_ARG(QVariant, QString("Equalizer"))));
        QTRY_COMPARE(sections->property("currentText").toString(), QString("Equalizer")); QTest::qWait(40);
        auto *enabled = revealSetting(window, "equalizerEnabledCheckBox"); QVERIFY(insideWindow(window, enabled));
        if (!player->equalizerEnabled()) QVERIFY(click(window, "equalizerEnabledCheckBox"));
        QTRY_VERIFY(player->equalizerEnabled());
        auto *bands = revealSetting(window, "equalizerBandCountBox"); QVERIFY(insideWindow(window, bands));
        bands->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Home);
        QTRY_COMPARE(player->equalizerBandCount(), 5);
        QTRY_VERIFY(findItem(window->contentItem(), "equalizerGainSlider4"));
        QVERIFY(!findItem(window->contentItem(), "equalizerGainSlider5"));
        QTest::keyClick(window, Qt::Key_End);
        QTRY_COMPARE(player->equalizerBandCount(), 15);
        QTRY_VERIFY(findItem(window->contentItem(), "equalizerGainSlider14"));
        sections->forceActiveFocus();
        QVERIFY(QMetaObject::invokeMethod(dialog, "goToSection", Q_ARG(QVariant, QString("Equalizer")))); QTest::qWait(40);
        window->grabWindow().save(QCoreApplication::applicationDirPath() + "/musica-audio-editor-" + QString::fromLatin1(QTest::currentDataTag()) + ".png");
        auto *gain = revealSetting(window, "equalizerGainSlider14");
        window->grabWindow().save(QCoreApplication::applicationDirPath() + "/musica-audio-layout-" + QString::fromLatin1(QTest::currentDataTag()) + ".png");
        const auto gainBounds = gain ? gain->mapRectToScene(gain->boundingRect()) : QRectF();
        QVERIFY2(insideWindow(window, gain), qPrintable(QString("Band 15 bounds %1,%2 %3x%4 in %5x%6 window")
            .arg(gainBounds.x()).arg(gainBounds.y()).arg(gainBounds.width()).arg(gainBounds.height()).arg(window->width()).arg(window->height())));
        gain->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Up);
        QTRY_VERIFY(player->equalizerBands().last().toMap().value("gain").toDouble() > 0);
        QTRY_COMPARE(player->equalizerPreset(), QString("custom"));
        window->grabWindow().save(QCoreApplication::applicationDirPath() + "/musica-audio-" + QString::fromLatin1(QTest::currentDataTag()) + ".png");
        auto *reset = revealSetting(window, "equalizerResetButton"); QVERIFY(insideWindow(window, reset));
        QVERIFY(click(window, "equalizerResetButton"));
        for (const auto &value : player->equalizerBands()) QCOMPARE(value.toMap().value("gain").toDouble(), 0.0);
        QCOMPARE(player->equalizerBandCount(), 15);
        QVERIFY(QMetaObject::invokeMethod(dialog, "goToSection", Q_ARG(QVariant, QString("Audio output"))));
        QTRY_COMPARE(sections->property("currentText").toString(), QString("Audio output"));
        QVERIFY(insideWindow(window, findItem(window->contentItem(), "closeSettingsButton")));
        QVERIFY(click(window, "closeSettingsButton"));
        QTRY_VERIFY(!window->findChild<QObject *>("displaySettingsDialog"));
        QCOMPARE(warnings.size(), 0);
    }
};

QTEST_MAIN(PracticalUiTests)
#include "tst_practicalui.moc"
