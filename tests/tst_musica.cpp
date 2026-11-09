#include <QAudioOutput>
#include <QAbstractItemModelTester>
#include <QBuffer>
#include <QDataStream>
#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
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
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QWheelEvent>
#include <QtTest>

#include "tracklibrary.h"
#include "albumgrouping.h"
#include "mprisservice.h"
#include "audiomixer.h"
#include "filetags.h"
#include <cmath>

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

    static QVector<float> musicalSignal(int seconds, int rate, double bpm = 120)
    {
        QVector<float> samples(seconds * rate);
        constexpr double pi = 3.14159265358979323846;
        for (int i = 0; i < samples.size(); ++i) {
            const double time = double(i) / rate;
            const double phase = std::fmod(time - 0.125 + 60 / bpm, 60 / bpm);
            const double click = phase < 0.025 ? 0.5 * std::exp(-phase * 180) * std::sin(2 * pi * 1900 * phase) : 0;
            // Root-weighted C major chord with harmonics and a stable pulse.
            const double chord = 0.12 * std::sin(2 * pi * 261.625565 * time) + 0.06 * std::sin(2 * pi * 329.627557 * time)
                + 0.06 * std::sin(2 * pi * 391.995436 * time) + 0.04 * std::sin(2 * pi * 523.25113 * time);
            samples[i] = float(chord + click);
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
    void initTestCase()
    {
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
        qmlRegisterUncreatableType<LibraryViewModel>("MusicPlayer", 1, 0, "LibraryViewModel", QStringLiteral("Provided by the music library"));
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
        QVERIFY(writeSignal(source, musicalSignal(1, 44100), 44100));
        bool ok = false;
        const auto encoded = runMediaTool("ffmpeg", {"-nostdin", "-v", "error", "-y", "-i", source, "-map", "0:a:0", "-c:a", codec, "-metadata", "title=Before", "-metadata", "artist=Original artist", "-metadata", "copyright=Keep this unrelated tag", audio}, &ok);
        QVERIFY2(ok, encoded.constData());
        const auto hashBefore = runMediaTool("ffmpeg", {"-nostdin", "-v", "error", "-i", audio, "-map", "0:a:0", "-f", "hash", "-hash", "sha256", "-"}, &ok); QVERIFY2(ok, hashBefore.constData());
        const auto cover = directory + "/Chosen cover.png"; QImage image(640, 480, QImage::Format_RGB32); image.fill(QColor("#347a58")); QVERIFY(image.save(cover));
        auto before = FileTags::read(audio, directory + "/cache", true); QVERIFY2(!before.contains("error"), qPrintable(before.value("error").toString()));
        const QVariantMap changes{{"title", QString::fromUtf8("Été · 海")}, {"artist", "New artist"}, {"album", "Portable album"},
            {"albumArtist", "The Collective"}, {"genre", "Jazz"}, {"date", "2026"}, {"composer", "A composer"}, {"comment", "Edited entirely offline"},
            {"trackNumber", "2"}, {"trackTotal", "12"}, {"discNumber", "1"}, {"discTotal", "2"}, {"bpm", "126"}, {"initialKey", "Am"}};
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
        QCOMPARE(track.value("title").toString(), changes.value("title").toString()); QCOMPARE(track.value("bpm").toInt(), 126);
        QCOMPARE(track.value("keyId").toInt(), 21); QCOMPARE(track.value("trackNumber").toInt(), 2); QCOMPARE(track.value("discNumber").toInt(), 1);
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
        result = FileTags::write(audio, {{"bpm", "999"}}, "keep", {}, before.value("revision").toString(), {}); QVERIFY(result.contains("error")); QCOMPARE(fileBytes(audio), beforeBytes);
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
        QTRY_VERIFY(library->libraryReady() && !library->scanning());
        const auto playlist = library->createPlaylist("Keep my place", url); QVERIFY(!playlist.isEmpty());
        click(window, "navSongs"); QTest::qWait(60); click(window, "playAllButton"); QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
        const auto original = fileBytes(audio);
        click(window, "songOptions0"); QTest::qWait(50); click(window, "menuEditMetadata");
        auto *editor = window->findChild<QObject *>("metadataEditor"); QVERIFY(editor); QTRY_VERIFY(editor->property("ready").toBool());
        auto *title = findItem(window->contentItem(), "metadata_title"); QVERIFY(title); title->setProperty("text", "Discard this draft");
        click(window, "cancelMetadataButton"); QCOMPARE(fileBytes(audio), original);
        click(window, "songOptions0"); QTest::qWait(50); click(window, "menuEditMetadata"); QTRY_VERIFY(editor->property("ready").toBool());
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
        QCOMPARE(errors.size(), 0); QVERIFY(!editor->property("visible").toBool());
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

    void crossfadeMixesAudioAndQuantizesTheTransition()
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
        first->analysis = {{"bpm", 120}, {"beatOffset", 0.1}, {"bpmConfidence", 1}};
        second->analysis = {{"bpm", 100}, {"beatOffset", 0.08}, {"bpmConfidence", 1}};
        const auto plan = AudioMixer::transitionPlan(*first, *second, 3.2, true);
        QCOMPARE(plan.first, 6600); QCOMPARE(plan.second, 80);
        QCOMPARE(AudioMixer::transitionPlan(*first, *second, 3.2, false).first, 6800);
        auto longTrack = constantPcm(0.2f, 180000), longNext = constantPcm(0.4f, 200000);
        QCOMPARE(AudioMixer::transitionPlan(*longTrack, *longNext, 100, false).first, 110000); // Hard limit: 70 seconds.
        longTrack->analysis = {{"bpm", 120}, {"beatOffset", 0.4}, {"bpmConfidence", 1}};
        const auto limited = AudioMixer::transitionPlan(*longTrack, *longNext, 70, true);
        QVERIFY(limited.first >= 110000); QCOMPARE(limited.first % 500, 400); // Stay on the grid without exceeding 70 seconds.
        mixer.clear(); mixer.setCurrent(first, "one"); mixer.setNext(second, "two"); mixer.stopAfterCurrent = true; mixer.crossfadeSeconds = 2;
        const auto sleeping = mixer.render(22000); QCOMPARE(sleeping.pcm.size(), 10000 * 8);
        for (const auto &event : sleeping.events) QVERIFY(event.kind != MixerEvent::Started || event.entryId == "one");
    }

    void audioAnalysisFindsTempoKeyAndCompatibleKeys()
    {
        const auto result = MusicAnalysis::analyze(musicalSignal(16, 11025), 11025);
        QVERIFY2(std::abs(result.value("bpm").toDouble() - 120) < 1.5, qPrintable(QString::number(result.value("bpm").toDouble())));
        QCOMPARE(result.value("keyId").toInt(), 0);
        QVERIFY(std::abs(result.value("beatOffset").toDouble() - 0.125) < 0.035);
        const auto silence = MusicAnalysis::analyze(QVector<float>(11025 * 4, 0), 11025);
        QCOMPARE(silence.value("bpm").toInt(), 0); QCOMPARE(silence.value("keyId").toInt(), -1);
        QCOMPARE(MusicAnalysis::parseKey("C major"), 0); QCOMPARE(MusicAnalysis::parseKey("8B"), 0);
        QCOMPARE(MusicAnalysis::parseKey("Am"), 21); QCOMPARE(MusicAnalysis::parseKey("C# minor"), 13);
        QVERIFY(MusicAnalysis::relatedKeys(0, 7)); // C major / G major: 8B / 9B.
        QVERIFY(MusicAnalysis::relatedKeys(0, 21)); // Relative minor, 8B / 8A.
        QVERIFY(!MusicAnalysis::relatedKeys(0, 1));
        QVERIFY(!MusicAnalysis::relatedKeys(-1, 0));
    }

    void musicalDetailsPersistAndMatchingCombinesTempoAndKey()
    {
        const auto config = m_temp.filePath("musical-details.ini");
        QSettings settings(config, QSettings::IniFormat); settings.setValue("library/folders", QStringList{}); settings.sync();
        QStringList urls; QList<QUrl> files;
        for (int i = 0; i < 4; ++i) { const auto path = m_temp.filePath(QStringLiteral("musical-details/%1 Song.wav").arg(i)); QVERIFY(writeWave(path, 4)); files.append(QUrl::fromLocalFile(path)); urls.append(files.last().toString()); }
        {
            TrackListModel library(config, false); library.addFiles(files); QTRY_VERIFY(!library.scanning());
            QVERIFY(library.setMusicalDetails(urls[0], 120, 0, 0.125));
            QVERIFY(library.setMusicalDetails(urls[1], 121, 7, 0));
            QVERIFY(library.setMusicalDetails(urls[2], 120, 1, 0));
            QVERIFY(library.setMusicalDetails(urls[3], 130, 21, 0));
            QVariantMap match{{"fileUrl", urls[0]}, {"bpm", 120}, {"keyId", 0}, {"related", true}, {"mode", "both"}, {"tolerance", 1}};
            library.songs()->setMusicMatch(match); QCOMPARE(library.songs()->count(), 1); QCOMPARE(library.songs()->get(0).value("fileUrl").toString(), urls[1]);
            match["mode"] = "either"; library.songs()->setMusicMatch(match); QCOMPARE(library.songs()->count(), 3);
            match["mode"] = "bpm"; match["tolerance"] = 0; library.songs()->setMusicMatch(match); QCOMPARE(library.songs()->count(), 1); QCOMPARE(library.songs()->get(0).value("fileUrl").toString(), urls[2]);
            match["mode"] = "key"; match["related"] = false; library.songs()->setMusicMatch(match); QCOMPARE(library.songs()->count(), 0);
            library.rescan(); QTRY_VERIFY(!library.scanning());
            QCOMPARE(library.trackForUrl(urls[0]).value("bpm").toInt(), 120);
        }
        TrackListModel restored(config, true); QTRY_VERIFY(restored.libraryReady() && !restored.scanning());
        QCOMPARE(restored.trackForUrl(urls[0]).value("camelot").toString(), QStringLiteral("8B"));
        QCOMPARE(restored.trackForUrl(urls[0]).value("beatOffset").toDouble(), 0.125);
        const auto signalPath = m_temp.filePath("musical-details/Analyzed.wav"); QVERIFY(writeSignal(signalPath, musicalSignal(12, 11025), 11025));
        const auto signalUrl = QUrl::fromLocalFile(signalPath).toString(); restored.addFiles({QUrl(signalUrl)}); QTRY_VERIFY(!restored.scanning());
        restored.analyzeTracks({signalUrl});
        QTRY_VERIFY_WITH_TIMEOUT(!restored.analyzing() && restored.trackForUrl(signalUrl).value("analyzed").toBool(), 15000);
        QVERIFY(std::abs(restored.trackForUrl(signalUrl).value("bpm").toDouble() - 120) < 1.5);
        restored.analyzeTracks({signalUrl}); QVERIFY(!restored.analyzing()); // Cached analysis doesn't restart.
    }

    void playbackContinuesWhileTheInterfaceSavesSettings()
    {
        const auto config = m_temp.filePath("playback-continuity.ini");
        const auto song = QUrl::fromLocalFile(m_temp.filePath("continuity/Long.wav"));
        QVERIFY(writeSignal(song.toLocalFile(), musicalSignal(14, 11025), 11025));
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
            library.savePlaybackSession({{"currentId", "long"}, {"position", engine.position()}});
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
        QCOMPARE(settings.value("player/session").toMap().value("currentId").toString(), QString("long"));
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
        QCOMPARE(settings.value("player/session").toMap().value("currentId").toString(), secondId);
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
        engine.setGaplessEnabled(true); engine.setCrossfadeEnabled(true); engine.setCrossfadeSeconds(1); engine.setQuantizeFade(false);
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

    void miniPlayerTransitionControlsAndMusicalMatchingWork()
    {
        QSettings settings; settings.clear(); settings.setValue("library/folders", QStringList{}); settings.sync();
        QQmlApplicationEngine engine; QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        engine.load(QUrl("qrc:/main.qml")); QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        auto *library = window->findChild<TrackListModel *>("library"); auto *player = window->findChild<PlaybackEngine *>("player");
        QTRY_VERIFY(library->libraryReady() && !library->scanning());
        QList<QUrl> files;
        for (int i = 0; i < 3; ++i) { const auto path = m_temp.filePath(QStringLiteral("mixing-ui/%1 Song.wav").arg(i)); QVERIFY(writeWave(path, 30)); files.append(QUrl::fromLocalFile(path)); }
        library->addFiles(files); QTRY_VERIFY(!library->scanning());
        QVERIFY(library->setMusicalDetails(files[0].toString(), 120, 0, 0.125));
        QVERIFY(library->setMusicalDetails(files[1].toString(), 121, 7, 0));
        QVERIFY(library->setMusicalDetails(files[2].toString(), 140, 1, 0));
        QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, QString("Songs"))); QTest::qWait(60); click(window, "playAllButton");
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
        click(window, "footerMusicalMatches");
        QCOMPARE(window->property("activePage").toString(), QStringLiteral("Matching songs"));
        QTRY_COMPARE(library->songs()->count(), 1);
        QTest::qWait(60); click(window, "editMatchReferenceButton"); QTest::qWait(60);
        auto *editor = window->findChild<QObject *>("metadataEditor"); QVERIFY(editor);
        QTRY_VERIFY(editor->property("ready").toBool());
        auto *bpmField = findItem(window->contentItem(), "metadata_bpm"); QVERIFY(bpmField);
        bpmField->setProperty("text", "126"); click(window, "saveMetadataButton");
        QTRY_VERIFY(!library->metadataBusy());
        QTRY_COMPARE(library->trackForUrl(files[0].toString()).value("bpm").toInt(), 126); QTRY_COMPARE(library->songs()->count(), 0);
        QTest::qWait(60); click(window, "crossfadeSettingsButton"); QTest::qWait(60);
        QVERIFY(window->findChild<QObject *>("playbackOptions")->property("visible").toBool());
        click(window, "crossfadeCheckBox"); QVERIFY(player->crossfadeEnabled());
        player->setCrossfadeSeconds(70); QTRY_COMPARE(window->findChild<QObject *>("crossfadeSecondsBox")->property("value").toInt(), 70);
        click(window, "quantizeFadeCheckBox"); QVERIFY(player->quantizeFade());
        player->setCrossfadeSeconds(0); QCOMPARE(player->crossfadeSeconds(), 0.0);
        window->grabWindow().save(QCoreApplication::applicationDirPath() + "/musica-transition-controls.png");
        QMetaObject::invokeMethod(window->findChild<QObject *>("playbackOptions"), "close");
        click(window, "sleepTimerButton"); QTest::qWait(50);
        window->findChild<QObject *>("sleepMinutesBox")->setProperty("value", 1); click(window, "startSleepTimerButton");
        QVERIFY(player->sleepRemainingMs() > 58000 && player->sleepRemainingMs() <= 60000);
        click(window, "sleepTimerButton"); QTest::qWait(50); click(window, "cancelSleepTimerButton"); QCOMPARE(player->sleepRemainingMs(), 0);
        click(window, "sleepAfterTrackButton"); QVERIFY(player->sleepAfterTrack());
        QMetaObject::invokeMethod(window->findChild<QObject *>("sleepTimerPopup"), "close");
        click(window, "miniPlayerButton"); auto *mini = window->findChild<QQuickWindow *>("miniPlayer"); QVERIFY(mini);
        QTRY_VERIFY(mini->isVisible() && window->visibility() == QWindow::Minimized); QVERIFY(mini->flags().testFlag(Qt::WindowStaysOnTopHint));
        click(mini, "miniPlayPauseButton"); QTRY_COMPARE(player->playbackState(), QMediaPlayer::PausedState);
        click(mini, "miniPlayPauseButton"); QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
        click(mini, "miniFavoriteButton"); QCOMPARE(library->favoriteCount(), 1);
        mini->grabWindow().save(QCoreApplication::applicationDirPath() + "/musica-mini-player.png");
        mini->close(); QTRY_VERIFY(window->isVisible() && !mini->isVisible());
        player->cancelSleep(); player->setCrossfadeEnabled(false);
        window->resize(900, 640); QTest::qWait(60); click(window, "crossfadeSettingsButton"); QTest::qWait(60);
        QVERIFY(findItem(window->contentItem(), "crossfadeSecondsBox")->mapToScene(QPointF()).y() >= 0);
        for (const auto &name : {"crossfadeSettingsButton", "sleepTimerButton", "miniPlayerButton"}) {
            auto *button = findItem(window->contentItem(), name); QVERIFY(button);
            const auto point = button->mapToScene(QPointF()); QVERIFY(point.x() >= 0 && point.x() + button->width() <= window->width());
        }
        QCOMPARE(warnings.size(), 0); player->stop();
    }

    void miniPlayerCanAlwaysReturnToTheFullPlayer()
    {
        QSettings settings; settings.clear(); settings.setValue("library/folders", QStringList{}); settings.sync();
        QQmlApplicationEngine engine; QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        engine.load(QUrl("qrc:/main.qml")); QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        auto *mini = window->findChild<QQuickWindow *>("miniPlayer"); QVERIFY(mini);
        QTRY_VERIFY(window->findChild<TrackListModel *>("library")->libraryReady());
        click(window, "miniPlayerButton");
        QTRY_VERIFY(mini->isVisible());
        // Tool/transient windows can disappear with their owner on Linux.
        QCOMPARE(mini->transientParent(), nullptr);
        QCOMPARE(mini->flags() & Qt::WindowType_Mask, Qt::Window);
        QVERIFY(mini->flags().testFlag(Qt::WindowMinimizeButtonHint));
        QCOMPARE(window->visibility(), QWindow::Minimized);
        QVERIFY(window->isVisible()); // The full player's taskbar entry remains available.

        click(mini, "restoreMainPlayerButton");
        QTRY_VERIFY(!mini->isVisible() && window->visibility() == QWindow::Windowed);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTest::qWait(60); // Let native window-state events settle before the next user action.
        click(window, "miniPlayerButton");
        QTRY_VERIFY(mini->isVisible() && window->visibility() == QWindow::Minimized);
        window->showNormal(); // Restore the full player from its taskbar entry.
        QTRY_VERIFY(!mini->isVisible() && window->visibility() == QWindow::Windowed);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTest::qWait(60);

        click(window, "miniPlayerButton");
        QTRY_VERIFY(mini->isVisible()); mini->hide(); // Unexpected mini-window loss must recover the app.
        QTRY_COMPARE(window->visibility(), QWindow::Windowed);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTest::qWait(60);
        mini->setPosition(-100000, -100000);
        click(window, "miniPlayerButton");
        QTRY_VERIFY(mini->isVisible());
        QVERIFY(mini->screen()->geometry().contains(mini->geometry()));
        mini->showMinimized();
        QTRY_COMPARE(mini->visibility(), QWindow::Minimized);
        QVERIFY(QMetaObject::invokeMethod(window, "desktopCommand", Q_ARG(QVariant, "raise"), Q_ARG(QVariant, QVariant())));
        QTRY_VERIFY(!mini->isVisible() && window->visibility() == QWindow::Windowed);

        window->requestActivate(); QTest::qWait(40);
        QTest::keyClick(window, Qt::Key_M, Qt::ControlModifier);
        QTRY_VERIFY(mini->isVisible() && window->visibility() == QWindow::Minimized);
        mini->requestActivate(); QTest::qWait(40);
        QTest::keyClick(mini, Qt::Key_M, Qt::ControlModifier);
        QTRY_VERIFY(!mini->isVisible() && window->visibility() == QWindow::Windowed);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTest::qWait(60);
        click(window, "miniPlayerButton"); QTRY_VERIFY(mini->isVisible());
        mini->close(); QTRY_VERIFY(!mini->isVisible() && window->visibility() == QWindow::Windowed);
        QCOMPARE(warnings.size(), 0);
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
        QVERIFY(library);
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
        click(window, "nowPlayingArtistLink");
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
        auto session = settings.value("player/session").toMap();
        auto queue = session.value("queue").toList();
        queue.append(QVariantMap{{"id", "999"}, {"fileUrl", QUrl::fromLocalFile(m_temp.filePath("missing.wav")).toString()}});
        session["queue"] = queue; settings.setValue("player/session", session); settings.sync();
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
