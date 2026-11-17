#include <QtTest>
#include <QFileOpenEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>

#include "applicationrequests.h"

class ApplicationRequestTests : public QObject
{
    Q_OBJECT
    QTemporaryDir m_settings;
    static void runClient(QProcess &client, const QString &server, const QList<QUrl> &urls)
    {
        QStringList args{"--forward-request", server};
        for (const auto &url : urls) args.append(url.toString(QUrl::FullyEncoded));
        client.start(QCoreApplication::applicationFilePath(), args);
    }
    static QByteArray request(const QJsonArray &urls, int version = 1)
    {
        return QJsonDocument(QJsonObject{{"version", version}, {"urls", urls}}).toJson(QJsonDocument::Compact) + '\n';
    }
private slots:
    void initTestCase()
    {
        QVERIFY(m_settings.isValid());
        qRegisterMetaType<QList<QUrl>>();
        QCoreApplication::setOrganizationName("MusicaApplicationRequestTests");
        QCoreApplication::setApplicationName("Requests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
    }
    void argumentsPreserveFilenamesAndEncodedUris()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const QString name = QString::fromUtf8("song 100% #1 café|mix.wav");
        const auto plain = QUrl::fromLocalFile(directory.filePath(name));
        const auto encoded = QUrl::fromLocalFile(directory.filePath(QString::fromUtf8("encoded ü %20.wav")));
        const auto urls = ApplicationRequests::urlsFromArguments({name, encoded.toString(QUrl::FullyEncoded), "https://example.invalid/song.wav", "./next.wav"}, directory.path());
        QCOMPARE(urls.size(), 3); QCOMPARE(urls[0], plain); QCOMPARE(urls[1], encoded);
        QCOMPARE(urls[2].toLocalFile(), directory.filePath("next.wav"));
        const auto firstEndpoint = ApplicationRequests::defaultServerName();
        QCoreApplication::setApplicationName("AnotherRequestsProfile");
        QVERIFY(ApplicationRequests::defaultServerName() != firstEndpoint);
        QCoreApplication::setApplicationName("Requests");
    }
    void firstAndForwardedLaunchesWaitUntilWindowIsReady()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        ApplicationRequests owner(nullptr, directory.filePath("socket"));
        QSignalSpy urls(&owner, &ApplicationRequests::urlsRequested);
        QSignalSpy activation(&owner, &ApplicationRequests::activateRequested);
        const auto first = QUrl::fromLocalFile(directory.filePath(QString::fromUtf8("first %20 é.wav")));
        const auto second = QUrl::fromLocalFile(directory.filePath(QString::fromUtf8("second #漢字.wav")));
        QCOMPARE(owner.startOrForward({first}), ApplicationRequests::Listening);
        QTest::qWait(10); QCOMPARE(urls.size(), 0); QCOMPARE(activation.size(), 0);
        QProcess client; runClient(client, owner.serverName(), {second, first});
        QTRY_COMPARE_WITH_TIMEOUT(client.state(), QProcess::NotRunning, 5000);
        QCOMPARE(client.exitCode(), 0); QCOMPARE(urls.size(), 0);
        owner.setReady(); QTRY_COMPARE(urls.size(), 2); QCOMPARE(activation.size(), 1);
        QCOMPARE(qvariant_cast<QList<QUrl>>(urls[0][0]), QList<QUrl>{first});
        QCOMPARE(qvariant_cast<QList<QUrl>>(urls[1][0]), (QList<QUrl>{second, first}));
        QProcess raise; runClient(raise, owner.serverName(), {});
        QTRY_COMPARE_WITH_TIMEOUT(raise.state(), QProcess::NotRunning, 5000); QCOMPARE(raise.exitCode(), 0);
        QTRY_COMPARE(activation.size(), 2); QCOMPARE(urls.size(), 2);
    }
    void requestsHandleFragmentationAndRejectMalformedOrRemoteUrls()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        ApplicationRequests owner(nullptr, directory.filePath("socket"));
        QSignalSpy urls(&owner, &ApplicationRequests::urlsRequested);
        QCOMPARE(owner.startOrForward(), ApplicationRequests::Listening); owner.setReady();
        QTest::qWait(10);
        const auto file = QUrl::fromLocalFile(directory.filePath("song.wav"));
        QLocalSocket partial; partial.connectToServer(owner.serverName()); QVERIFY(partial.waitForConnected(1000));
        const auto data = request(QJsonArray{file.toString(QUrl::FullyEncoded)});
        partial.write(data.left(10)); partial.flush(); QTest::qWait(10); QCOMPARE(urls.size(), 0);
        partial.write(data.mid(10)); partial.flush(); QTRY_COMPARE(urls.size(), 1);
        QTRY_VERIFY(partial.bytesAvailable() > 0); QVERIFY(QJsonDocument::fromJson(partial.readAll()).object().value("ok").toBool());
        const QList<QByteArray> bad{
            "{not json}\n", request(QJsonArray{"https://example.invalid/song.wav"}), request(QJsonArray{false}),
            request(QJsonArray{file.toString()}, 2), request(QJsonArray{"file:relative.wav"}),
            request(QJsonArray{file.toString()}) + request(QJsonArray{file.toString()})};
        for (const auto &invalid : bad) {
            QLocalSocket socket; socket.connectToServer(owner.serverName()); QVERIFY(socket.waitForConnected(1000));
            socket.write(invalid); socket.flush(); QTRY_VERIFY(socket.bytesAvailable() > 0);
            QVERIFY(!QJsonDocument::fromJson(socket.readAll()).object().value("ok").toBool());
            QCOMPARE(urls.size(), 1);
        }
    }
    void oversizedRequestsAreBoundedAndEndpointCanBeReused()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto name = directory.filePath("socket");
        {
            ApplicationRequests owner(nullptr, name);
            QList<QUrl> tooMany;
            for (int i = 0; i < 2049; ++i) tooMany.append(QUrl::fromLocalFile(directory.filePath(QString::number(i) + ".wav")));
            QCOMPARE(owner.startOrForward(tooMany), ApplicationRequests::Failed);
            QCOMPARE(owner.startOrForward(), ApplicationRequests::Listening);
            QSignalSpy received(&owner, &ApplicationRequests::urlsRequested); owner.setReady();
            QLocalSocket huge; huge.connectToServer(name); QVERIFY(huge.waitForConnected(1000));
            huge.write(QByteArray(1024 * 1024 + 1, ' ')); huge.flush();
            QTRY_VERIFY_WITH_TIMEOUT(huge.bytesAvailable() > 0, 5000);
            QVERIFY(!QJsonDocument::fromJson(huge.readAll()).object().value("ok").toBool()); QCOMPARE(received.size(), 0);
        }
        ApplicationRequests reopened(nullptr, name); QCOMPARE(reopened.startOrForward(), ApplicationRequests::Listening);
    }
    void nativeFileOpenEventsAreBatchedAndDeduplicated()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        ApplicationRequests owner(nullptr, directory.filePath("socket"));
        QSignalSpy received(&owner, &ApplicationRequests::urlsRequested);
        QCOMPARE(owner.startOrForward(), ApplicationRequests::Listening); owner.setReady(); QTest::qWait(10);
        const auto first = QUrl::fromLocalFile(directory.filePath("first %20.wav"));
        const auto second = QUrl::fromLocalFile(directory.filePath(QString::fromUtf8("second é.wav")));
        QFileOpenEvent openFirst(first), openSecond(second), again(first);
        QVERIFY(QCoreApplication::sendEvent(QCoreApplication::instance(), &openFirst));
        QVERIFY(QCoreApplication::sendEvent(QCoreApplication::instance(), &openSecond));
        QVERIFY(QCoreApplication::sendEvent(QCoreApplication::instance(), &again));
        QTRY_COMPARE(received.size(), 1);
        QCOMPARE(qvariant_cast<QList<QUrl>>(received[0][0]), (QList<QUrl>{first, second}));
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto arguments = app.arguments();
    if (arguments.size() >= 3 && arguments[1] == "--forward-request") {
        ApplicationRequests client(nullptr, arguments[2]);
        QList<QUrl> urls;
        for (int i = 3; i < arguments.size(); ++i) urls.append(QUrl::fromEncoded(arguments[i].toUtf8(), QUrl::StrictMode));
        const auto result = client.startOrForward(urls);
        if (result == ApplicationRequests::Forwarded) return 0;
        qWarning().noquote() << client.errorString(); return result == ApplicationRequests::Listening ? 2 : 3;
    }
    ApplicationRequestTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "tst_applicationrequests.moc"
