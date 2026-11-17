#include <QtTest>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include "applicationrequests.h"
#include "filetags.h"
#include "mprisservice.h"
#include "sharing.h"

class PlatformPortabilityTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("MusicaPlatformTests");
        QCoreApplication::setApplicationName("Platform");
    }
    void unicodePathsRetainTagsAndAudio()
    {
        QTemporaryDir temporary; QVERIFY(temporary.isValid());
        const auto folder = temporary.filePath(QString::fromUtf8("Música 漢字"));
        QVERIFY(QDir().mkpath(folder));
        const auto path = QDir(folder).filePath(QString::fromUtf8("café 音楽.wav"));
        {
            QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
            QDataStream out(&file); out.setByteOrder(QDataStream::LittleEndian);
            out.writeRawData("RIFF", 4); out << quint32(16036); out.writeRawData("WAVEfmt ", 8);
            out << quint32(16) << quint16(1) << quint16(1) << quint32(8000) << quint32(16000) << quint16(2) << quint16(16);
            out.writeRawData("data", 4); out << quint32(16000); out.writeRawData(QByteArray(16000, '\0').constData(), 16000);
        }
        const auto before = FileTags::read(path, {}, true);
        QVERIFY2(!before.contains("error"), qPrintable(before.value("error").toString()));
        const QString title = QString::fromUtf8("Chanson – 音楽");
        const auto after = FileTags::write(path, {{"title", title}, {"artist", QString::fromUtf8("Beyoncé")}},
                                           "keep", {}, before.value("revision").toString(), {});
        QVERIFY2(!after.contains("error"), qPrintable(after.value("error").toString()));
        QCOMPARE(after.value("fields").toMap().value("title").toString(), title);
        QCOMPARE(after.value("duration"), before.value("duration"));
        const auto reopened = FileTags::read(path, {});
        QCOMPARE(reopened.value("fields").toMap().value("artist").toString(), QString::fromUtf8("Beyoncé"));
        const auto urls = ApplicationRequests::urlsFromArguments({path, QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded)});
        QCOMPARE(urls.size(), 2); QCOMPARE(urls[0], urls[1]);
    }
    void desktopIntegrationHasAnExplicitPlatformBoundary()
    {
        Sharing sharing;
#if MUSICA_HAS_DBUS
        QVERIFY(sharing.emailAttachmentsAvailable());
#else
        QObject window;
        MprisService desktop(&window, nullptr, nullptr);
        QVERIFY(!desktop.available()); QVERIFY(desktop.playerProperties().isEmpty());
        QVERIFY(!sharing.emailAttachmentsAvailable());
        QSignalSpy errors(&sharing, &Sharing::error);
        sharing.composeEmail(); QCOMPARE(errors.size(), 1);
#endif
#ifdef Q_OS_WIN
        const auto endpoint = ApplicationRequests::defaultServerName();
        QVERIFY(endpoint.startsWith("musica-open-"));
        QVERIFY(!endpoint.contains('/') && !endpoint.contains('\\') && !endpoint.contains(':'));
#endif
    }
    void openWithEndpointDoesNotDependOnTheLaunchDirectory()
    {
        const auto endpoint = ApplicationRequests::defaultServerName();
        const auto previous = QDir::currentPath();
        QTemporaryDir folder; QVERIFY(folder.isValid());
        const auto restoreDirectory = qScopeGuard([previous] { QDir::setCurrent(previous); });
        QVERIFY(QDir::setCurrent(folder.path()));
        QCOMPARE(ApplicationRequests::defaultServerName(), endpoint);
    }
};

QTEST_GUILESS_MAIN(PlatformPortabilityTests)
#include "tst_platformportability.moc"
