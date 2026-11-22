#include <QCommandLineParser>
#include <QDebug>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <memory>

#include "tracklibrary.h"
#include "artworkcache.h"
#include "displaysettings.h"
#include "sharing.h"
#include "mprisservice.h"
#include "applicationrequests.h"
#include "desktopenvironment.h"
#include "windowsmediacontrols.h"
#include "appdiagnostics.h"

int main(int argc, char *argv[])
{
    QElapsedTimer startup; startup.start();
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Musica"));
    QCoreApplication::setApplicationName(QStringLiteral("Musica"));
    QCoreApplication::setApplicationVersion(QStringLiteral(MUSICA_VERSION));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Musica"));
    QGuiApplication::setDesktopFileName(QStringLiteral("musica"));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/assets/musica.png")));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("A local music player."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("files"), QStringLiteral("Audio files or music folders to open and play."), QStringLiteral("[files…]"));
    QCommandLineOption screenshot(QStringLiteral("screenshot"), QStringLiteral("Save a window preview and exit."), QStringLiteral("path"));
    QCommandLineOption smokeTest(QStringLiteral("smoke-test"), QStringLiteral("Load the interface, check QML warnings, and exit."));
    QCommandLineOption benchmark(QStringLiteral("benchmark"), QStringLiteral("Write local startup and resource measurements as JSON, then exit."), QStringLiteral("output.json"));
    parser.addOption(screenshot);
    parser.addOption(smokeTest);
    parser.addOption(benchmark);
    parser.process(app);
    std::unique_ptr<QTemporaryDir> benchmarkProfile;
    if (parser.isSet(benchmark)) {
        // Benchmarking must never read or modify a listener's real settings,
        // including on Windows, where XDG_* environment variables are ignored.
        benchmarkProfile = std::make_unique<QTemporaryDir>();
        if (!benchmarkProfile->isValid()) return 1;
        QCoreApplication::setOrganizationName(QStringLiteral("MusicaBenchmarks"));
        QCoreApplication::setApplicationName(QStringLiteral("MusicaBenchmark"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, benchmarkProfile->path());
    }

    const bool preview = parser.isSet(screenshot) || parser.isSet(smokeTest) || parser.isSet(benchmark);
    const auto files = ApplicationRequests::urlsFromArguments(parser.positionalArguments());
    // Preview/test invocations must remain independent of a running player.
    std::unique_ptr<ApplicationRequests> requests;
    if (!preview) {
        requests = std::make_unique<ApplicationRequests>(&app);
        const auto result = requests->startOrForward(files);
        if (result == ApplicationRequests::Forwarded) return 0;
        if (result == ApplicationRequests::Failed) { qCritical().noquote() << requests->errorString(); return 1; }
    }

    qmlRegisterType<TrackListModel>("MusicPlayer", 1, 0, "TrackListModel");
    qmlRegisterType<PlaybackEngine>("MusicPlayer", 1, 0, "PlaybackEngine");
    qmlRegisterType<DisplaySettings>("MusicPlayer", 1, 0, "DisplaySettings");
    qmlRegisterType<Sharing>("MusicPlayer", 1, 0, "Sharing");
    qmlRegisterSingletonType(QUrl("qrc:/components/Theme.qml"), "MusicPlayer", 1, 0, "Theme");
    qmlRegisterUncreatableType<LibraryViewModel>("MusicPlayer", 1, 0, "LibraryViewModel", QStringLiteral("Provided by the music library"));

    registerPerformanceTypes();
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
    WindowsMediaControls windowsControls(window, library, player);
    window->setProperty("desktopControlsAvailable", desktopControls.available() || windowsControls.available());
    window->setProperty("desktopControlsName", windowsControls.available() ? QStringLiteral("Windows media controls")
        : desktopControls.available() ? QStringLiteral("Linux MPRIS") : QStringLiteral("Unavailable in this session"));
    auto *preferences = window->property("displayPreferences").value<DisplaySettings *>();
    DesktopEnvironment environment(window, preferences, player);
    window->setProperty("desktopEnvironment", QVariant::fromValue<QObject *>(&environment));
    AppDiagnostics diagnostics(window);
    window->setProperty("appDiagnostics", QVariant::fromValue<QObject *>(&diagnostics));
    QObject::connect(window, &QQuickWindow::frameSwapped, &diagnostics, [&diagnostics, startup] {
        diagnostics.setStartupMs(startup.elapsed());
        diagnostics.refresh();
    }, Qt::ConnectionType(Qt::QueuedConnection | Qt::SingleShotConnection));
    QObject::connect(&app, &QCoreApplication::aboutToQuit, window, [window] {
        QMetaObject::invokeMethod(window, "persistSession");
    });
    const auto openFiles = [window](const QList<QUrl> &urls) {
        QVariantList arguments;
        for (const auto &url : urls) arguments.append(url.toString(QUrl::FullyEncoded));
        QMetaObject::invokeMethod(window, "openExternalFiles", Qt::QueuedConnection, Q_ARG(QVariant, arguments), Q_ARG(QVariant, false));
    };
    if (requests) {
        QObject::connect(requests.get(), &ApplicationRequests::urlsRequested, window, openFiles);
        QObject::connect(requests.get(), &ApplicationRequests::activateRequested, window, [window] {
            QMetaObject::invokeMethod(window, "restoreMainPlayer", Qt::QueuedConnection);
        });
        requests->setReady();
    } else if (!files.isEmpty()) openFiles(files);

    if (preview) {
        QTimer::singleShot(parser.isSet(benchmark) ? 3000 : 1800, &app, [&] {
            bool saved = true;
            if (parser.isSet(benchmark)) {
                diagnostics.refresh(); auto report = diagnostics.snapshot();
                report["profile"] = "isolated empty library";
                report["libraryTracks"] = library->trackCount(); report["residentTrackRecords"] = library->residentTrackCount();
                report["batterySaver"] = preferences && preferences->batterySaverActive();
                report["onBattery"] = preferences && preferences->onBattery();
                QSaveFile output(parser.value(benchmark));
                const auto bytes = QJsonDocument::fromVariant(report).toJson();
                saved = output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size() && output.commit();
            }
            if (parser.isSet(screenshot))
                saved = window->grabWindow().save(parser.value(screenshot));
            app.exit(saved && !qmlWarnings ? 0 : 1);
        });
    }
    return app.exec();
}
