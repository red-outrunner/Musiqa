#include <QCommandLineParser>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

#include "tracklibrary.h"
#include "mprisservice.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Musica"));
    QCoreApplication::setApplicationName(QStringLiteral("Musica"));
    QCoreApplication::setApplicationVersion(QStringLiteral(MUSICA_VERSION));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Musica"));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/assets/musica.svg")));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Your music. Your space. A local music player."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("files"), QStringLiteral("Audio files or music folders to import."), QStringLiteral("[files…]"));
    QCommandLineOption screenshot(QStringLiteral("screenshot"), QStringLiteral("Save a window preview and exit."), QStringLiteral("path"));
    QCommandLineOption smokeTest(QStringLiteral("smoke-test"), QStringLiteral("Load the interface, check QML warnings, and exit."));
    parser.addOption(screenshot);
    parser.addOption(smokeTest);
    parser.process(app);

    qmlRegisterType<TrackListModel>("MusicPlayer", 1, 0, "TrackListModel");
    qmlRegisterType<PlaybackEngine>("MusicPlayer", 1, 0, "PlaybackEngine");
    qmlRegisterUncreatableType<LibraryViewModel>("MusicPlayer", 1, 0, "LibraryViewModel", QStringLiteral("Provided by the music library"));

    QQmlApplicationEngine engine;
    bool qmlWarnings = false;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app,
                     [&qmlWarnings](const QList<QQmlError> &) { qmlWarnings = true; });
    engine.load(QUrl(QStringLiteral("qrc:/main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    auto *library = window->findChild<TrackListModel *>(QStringLiteral("library"));
    auto *player = window->findChild<PlaybackEngine *>(QStringLiteral("player"));
    MprisService desktopControls(window, library, player);
    window->setProperty("desktopControlsAvailable", desktopControls.available());
    QObject::connect(&app, &QCoreApplication::aboutToQuit, window, [window] {
        QMetaObject::invokeMethod(window, "persistSession");
    });
    QList<QUrl> files;
    for (const QString &path : parser.positionalArguments())
        files.append(QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()));
    if (!files.isEmpty())
        library->addFiles(files);

    if (parser.isSet(screenshot) || parser.isSet(smokeTest)) {
        QTimer::singleShot(1800, &app, [&] {
            bool saved = true;
            if (parser.isSet(screenshot))
                saved = window->grabWindow().save(parser.value(screenshot));
            app.exit(saved && !qmlWarnings ? 0 : 1);
        });
    }
    return app.exec();
}
