#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include "tracklibrary.h"
#include "mprisservice.h"

// Run real window-close/quit events in a separate process: a successful quit
// must not terminate the QtTest process or be masked by forced app.exit().
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid() || argc != 3) return 2;
    QCoreApplication::setOrganizationName("MusicaTests");
    QCoreApplication::setApplicationName("MusicaCloseProbe");
    QStandardPaths::setTestModeEnabled(true);
    qputenv("XDG_DATA_HOME", directory.filePath("data").toUtf8());
    qputenv("XDG_CACHE_HOME", directory.filePath("cache").toUtf8());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    QSettings settings; settings.setValue("library/folders", QStringList{});
    settings.setValue("library/files", QStringList{QString::fromLocal8Bit(argv[2])}); settings.sync();
    QQuickStyle::setStyle("Basic");
    qmlRegisterType<TrackListModel>("MusicPlayer", 1, 0, "TrackListModel");
    qmlRegisterType<PlaybackEngine>("MusicPlayer", 1, 0, "PlaybackEngine");
    qmlRegisterUncreatableType<LibraryViewModel>("MusicPlayer", 1, 0, "LibraryViewModel", "Provided by the music library");
    QQmlApplicationEngine qml;
    qml.load(QUrl("qrc:/main.qml"));
    if (qml.rootObjects().isEmpty()) return 3;
    auto *window = qobject_cast<QQuickWindow *>(qml.rootObjects().first());
    auto *library = window->findChild<TrackListModel *>("library");
    auto *player = window->findChild<PlaybackEngine *>("player");
    auto *mini = window->findChild<QQuickWindow *>("miniPlayer");
    MprisService desktop(window, library, player);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, window, [window] { QMetaObject::invokeMethod(window, "persistSession"); });
    const QString mode = QString::fromLocal8Bit(argv[1]);
    const QString url = QUrl::fromLocalFile(QString::fromLocal8Bit(argv[2])).toString();
    player->audioOutput()->setMuted(true);
    bool started = false;
    QTimer ready;
    ready.setInterval(20);
    QObject::connect(&ready, &QTimer::timeout, window, [&] {
        if (!library->libraryReady() || library->scanning()) return;
        if (mode != "main-idle" && !started) {
            started = true;
            QMetaObject::invokeMethod(window, "playUrl", Q_ARG(QVariant, url));
        }
        if (mode != "main-idle" && player->position() < 100) return;
        ready.stop();
        if (mode.contains("mini")) QMetaObject::invokeMethod(window, "openMiniPlayer");
        QTimer::singleShot(60, window, [&, mode] {
            if (mode.startsWith("desktop"))
                QMetaObject::invokeMethod(window, "desktopCommand", Q_ARG(QVariant, "quit"), Q_ARG(QVariant, QVariant()));
            else if (mode == "mini-playing") mini->close();
            else window->close();
        });
    });
    ready.start();
    QTimer::singleShot(4000, &app, [&] { qWarning("The close request did not terminate Musica."); app.exit(42); });
    const int result = app.exec();
    if (result != 0) return result;
    const auto saved = library->playbackSession();
    if (mode != "main-idle" && (saved.value("currentId").toString().isEmpty() || saved.value("position").toLongLong() < 100)) {
        qWarning("Quitting lost the active song or its listening position.");
        return 43;
    }
    return 0;
}
