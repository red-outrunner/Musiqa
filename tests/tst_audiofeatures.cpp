#include <QtTest>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <fileref.h>
#include <tpropertymap.h>

#include "equalizer.h"
#include "lyricsdocument.h"
#include "playbackengine.h"
#include "playbackworker.h"
#include "replaygain.h"
#include "taglibpath.h"
#include <cmath>
#include <limits>

namespace {
constexpr double Pi = 3.14159265358979323846;
std::vector<float> tone(double frequency, float amplitude = 0.25f)
{
    std::vector<float> samples(48000 * 2);
    for (int i = 0; i < 48000; ++i) samples[size_t(i) * 2] = amplitude * float(std::sin(2 * Pi * frequency * i / 48000));
    return samples;
}
double response(const QString &preset, double frequency)
{
    AudioEqualizer eq; eq.setPreset(preset); eq.setEnabled(true); eq.reset();
    auto samples = tone(frequency); eq.process(samples.data(), 48000);
    double energy = 0;
    for (int i = 24000; i < 48000; ++i) energy += samples[size_t(i) * 2] * samples[size_t(i) * 2];
    return std::sqrt(energy / 24000) / (0.25 / std::sqrt(2.0));
}
bool write(const QString &path, const QByteArray &bytes)
{
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
bool makeWave(const QString &path)
{
    QFile file(path); if (!file.open(QIODevice::WriteOnly)) return false;
    QDataStream stream(&file); stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF", 4); stream << quint32(36 + 96000);
    stream.writeRawData("WAVEfmt ", 8); stream << quint32(16) << quint16(1) << quint16(1) << quint32(48000) << quint32(96000) << quint16(2) << quint16(16);
    stream.writeRawData("data", 4); stream << quint32(96000); stream.writeRawData(QByteArray(96000, 0).constData(), 96000);
    return stream.status() == QDataStream::Ok;
}
std::shared_ptr<PcmTrack> constantPcm(const QAudioFormat &format, const QUrl &url, float value, int frames)
{
    auto track = std::make_shared<PcmTrack>(url, format);
    const std::vector<float> pcm(size_t(frames) * format.channelCount(), value);
    if (!track->append(reinterpret_cast<const char *>(pcm.data()), qint64(pcm.size() * sizeof(float)))) return {};
    track->complete = true;
    return track;
}
}

class AudioFeatureTests : public QObject
{
    Q_OBJECT
    QTemporaryDir m_settings;
private slots:
    void initTestCase()
    {
        QVERIFY(m_settings.isValid());
        QCoreApplication::setOrganizationName("MusicaAudioFeatureTests");
        QCoreApplication::setApplicationName("MusicaAudioFeatureTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
    }
    void init() { QSettings().clear(); }
    void skipCrossfadeMixesImmediatelyAndFinishesBeforeTheOutgoingSongEnds()
    {
        QAudioFormat format; format.setSampleRate(1000); format.setChannelCount(2); format.setSampleFormat(QAudioFormat::Float);
        auto first = constantPcm(format, QUrl::fromLocalFile("/first.wav"), 0.2f, 10000);
        auto second = constantPcm(format, QUrl::fromLocalFile("/second.wav"), 0.4f, 10000); QVERIFY(first && second);
        AudioMixer mixer(1000, 2); mixer.setCurrent(first, "first"); mixer.render(1000);
        const auto originalToken = mixer.current().token;
        QVERIFY(mixer.crossfadeTo(second, "second", 2));
        const auto block = mixer.render(2500); QCOMPARE(block.pcm.size(), 2500 * 8);
        const auto *pcm = reinterpret_cast<const float *>(block.pcm.constData());
        QVERIFY(std::abs(pcm[0] - 0.2f) < 0.0001f);
        QVERIFY(std::abs(pcm[1000 * 2] - 0.6f / std::sqrt(2.0f)) < 0.0001f);
        QVERIFY(std::abs(pcm[2000 * 2] - 0.4f) < 0.0001f);
        QVERIFY(!mixer.outgoing().track); QCOMPARE(mixer.current().frame, 2500);
        bool finished = false, started = false;
        for (const auto &event : block.events) {
            if (event.kind == MixerEvent::Started) { QCOMPARE(event.entryId, QString("second")); QCOMPARE(event.offset, 0); started = true; }
            if (event.kind == MixerEvent::Finished && event.entryId == "first") {
                QCOMPARE(event.token, originalToken); QCOMPARE(event.offset, 2000); QCOMPARE(event.position, 3000); finished = true;
            }
            QVERIFY(event.kind != MixerEvent::QueueEnded);
        }
        QVERIFY(started && finished);
    }
    void skipCrossfadeCapsOverlapAndAcceptsBufferedTracksWithoutFullDecoding()
    {
        QAudioFormat format; format.setSampleRate(1000); format.setChannelCount(2); format.setSampleFormat(QAudioFormat::Float);
        auto first = constantPcm(format, QUrl::fromLocalFile("/first.wav"), 0.2f, 100000);
        auto second = constantPcm(format, QUrl::fromLocalFile("/second.wav"), 0.4f, 100000); QVERIFY(first && second);
        AudioMixer mixer(1000, 2); mixer.setCurrent(first, "first");
        for (double seconds : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN()}) {
            QVERIFY(!mixer.crossfadeTo(second, "second", seconds)); QCOMPARE(mixer.current().entryId, QString("first"));
        }
        QVERIFY(mixer.crossfadeTo(second, "second", 90)); mixer.render(70001); QVERIFY(!mixer.outgoing().track);
        auto shortTrack = constantPcm(format, QUrl::fromLocalFile("/short.wav"), 0.3f, 100); QVERIFY(shortTrack);
        first->complete = false; second->complete = false;
        mixer.setCurrent(first, "first", 99950);
        QVERIFY(mixer.crossfadeTo(shortTrack, "short", 70));
        const auto block = mixer.render(51); QVERIFY(!mixer.outgoing().track);
        bool finished = false;
        for (const auto &event : block.events)
            if (event.kind == MixerEvent::Finished && event.entryId == "first") { QCOMPARE(event.offset, 50); finished = true; }
        QVERIFY(finished);
        mixer.setCurrent(first, "first"); QVERIFY(mixer.crossfadeTo(second, "second", 1));
        QCOMPARE(mixer.render(1001).pcm.size(), 1001 * 8); QVERIFY(!mixer.outgoing().track);
    }
    void skipCrossfadeOptionPersistsAndOldBackupsKeepImmediateSkipping()
    {
        QVariantMap saved;
        {
            PlaybackEngine engine; QVERIFY(!engine.crossfadeOnSkip());
            engine.setCrossfadeOnSkip(true); engine.setCrossfadeEnabled(true); engine.setCrossfadeSeconds(7);
            saved = engine.exportOptions(); QVERIFY(saved.value("crossfadeOnSkip").toBool());
        }
        PlaybackEngine reopened; QVERIFY(reopened.crossfadeOnSkip()); QCOMPARE(reopened.crossfadeSeconds(), 7.0);
        auto invalid = saved; invalid["crossfadeOnSkip"] = "true"; invalid["crossfadeSeconds"] = 25;
        QVERIFY(!reopened.importOptions(invalid)); QCOMPARE(reopened.exportOptions(), saved);
        auto legacy = saved; legacy.remove("crossfadeOnSkip");
        QVERIFY(reopened.importOptions(legacy)); QVERIFY(!reopened.crossfadeOnSkip());
        QVERIFY(reopened.importOptions(saved)); QVERIFY(reopened.crossfadeOnSkip());
    }
    void skipCrossfadeUsesAudiblePositionAndRapidSkipsKeepDistinctVoices()
    {
        PlaybackWorker worker;
        const auto originalUrl = QUrl::fromLocalFile("/original.wav"), nextUrl = QUrl::fromLocalFile("/next.wav"), thirdUrl = QUrl::fromLocalFile("/third.wav");
        const int rate = worker.m_format.sampleRate();
        auto first = constantPcm(worker.m_format, originalUrl, 0.2f, 10 * rate);
        auto next = constantPcm(worker.m_format, nextUrl, 0.3f, 10 * rate);
        auto third = constantPcm(worker.m_format, thirdUrl, 0.4f, 10 * rate); QVERIFY(first && next && third);
        worker.m_buffers.insert(originalUrl.toString(), first); worker.m_buffers.insert(nextUrl.toString(), next); worker.m_buffers.insert(thirdUrl.toString(), third);
        worker.m_source = originalUrl; worker.m_entryId = "original"; worker.m_position = 1000; worker.m_duration = 10000;
        worker.m_state = QMediaPlayer::PlayingState;
        worker.m_mixer.setCurrent(first, "original", rate + rate / 5); // Generated PCM is ahead of what was heard.
        const auto token = worker.m_mixer.current().token; worker.m_displayToken = token;
        MixerEvent audible{MixerEvent::Progress, 0, "original", token, originalUrl, rate, 10 * rate, first};
        worker.m_liveVoices.insert(token, audible);
        worker.setCrossfadeEnabled(true);
        QVERIFY(!worker.crossfadeToEntry("next", nextUrl)); // Opt in is required.
        worker.setCrossfadeOnSkip(true); worker.setCrossfadeEnabled(false);
        QVERIFY(!worker.crossfadeToEntry("next", nextUrl));
        worker.setCrossfadeEnabled(true); worker.setCrossfadeSeconds(0);
        QVERIFY(!worker.crossfadeToEntry("next", nextUrl));
        worker.setCrossfadeSeconds(2); worker.m_state = QMediaPlayer::PausedState;
        QVERIFY(!worker.crossfadeToEntry("next", nextUrl));
        worker.m_state = QMediaPlayer::PlayingState;
        QVERIFY(!worker.crossfadeToEntry("missing", QUrl::fromLocalFile("/unbuffered.wav")));
        QVERIFY(worker.crossfadeToEntry("next", nextUrl)); worker.m_pump.stop();
        QCOMPARE(worker.source(), nextUrl); QCOMPARE(worker.entryId(), QString("next")); QCOMPARE(worker.position(), 0);
        QCOMPARE(worker.m_mixer.outgoing().frame, qint64(rate)); QCOMPARE(worker.m_mixer.outgoing().token, token);
        const auto nextToken = worker.m_mixer.current().token; QVERIFY(nextToken != token);
        QSignalSpy listening(&worker, &PlaybackWorker::listeningProgress);
        QVERIFY(worker.crossfadeToEntry("third", thirdUrl)); worker.m_pump.stop();
        QCOMPARE(worker.m_mixer.outgoing().entryId, QString("next")); QCOMPARE(worker.m_mixer.outgoing().token, nextToken);
        QVERIFY(worker.m_mixer.current().token != nextToken); QCOMPARE(worker.source(), thirdUrl);
        bool finished = false;
        for (const auto &sample : listening)
            if (sample[0].toString() == token && sample[5].toBool()) finished = true;
        QVERIFY(finished);
        worker.playEntry("original", originalUrl); worker.m_pump.stop();
        QVERIFY(!worker.m_mixer.outgoing().track); QCOMPARE(worker.source(), originalUrl);
    }
    void replayGainReadsExistingTagsAndPreservesTheFile()
    {
        const auto file = m_settings.filePath(QString::fromUtf8("ReplayGain été 海.wav")); QVERIFY(makeWave(file));
        {
            const auto path = TagLibPath::encode(file); TagLib::FileRef ref(TagLibPath::data(path), false); QVERIFY(!ref.isNull());
            auto properties = ref.file()->properties();
            properties.replace("REPLAYGAIN_TRACK_GAIN", TagLib::StringList("-6.0206 dB"));
            properties.replace("REPLAYGAIN_ALBUM_GAIN", TagLib::StringList("-3.0 dB"));
            properties.replace("REPLAYGAIN_TRACK_PEAK", TagLib::StringList("0.8"));
            QVERIFY(ref.file()->setProperties(properties).isEmpty()); QVERIFY(ref.file()->save());
        }
        QFile original(file); QVERIFY(original.open(QIODevice::ReadOnly)); const auto bytes = original.readAll(); original.close();
        const auto tags = ReplayGain::read(file); QCOMPARE(tags.value("trackGain").toDouble(), -6.0206); QCOMPARE(tags.value("trackPeak").toDouble(), 0.8);
        QVERIFY(std::abs(ReplayGain::scale(tags, "track") - 0.5) < 0.00001);
        QVERIFY(std::abs(ReplayGain::scale(tags, "album") - std::pow(10, -3.0 / 20)) < 0.00001);
        QCOMPARE(ReplayGain::scale(tags, "off"), 1.0); QCOMPARE(ReplayGain::scale({}, "track"), 1.0);
        QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(), bytes);
    }
    void replayGainChangesSamplesPerVoiceAndRampsWithoutRestarting()
    {
        QAudioFormat format; format.setSampleRate(48000); format.setChannelCount(2); format.setSampleFormat(QAudioFormat::Float);
        const auto pcm = [&](const QString &name, const QVariantMap &tags, float amplitude, int frames) {
            auto track = std::make_shared<PcmTrack>(QUrl::fromLocalFile(m_settings.filePath(name)), format);
            std::vector<float> samples(size_t(frames) * 2, amplitude);
            if (!track->append(reinterpret_cast<const char *>(samples.data()), qint64(samples.size() * sizeof(float)))) return std::shared_ptr<PcmTrack>();
            track->setInfo({{"replayGain", tags}}); track->complete = true; return track;
        };
        auto tagged = pcm("tagged.wav", {{"trackGain", -6.020599913279624}, {"albumGain", 0}}, 0.25f, 48000); QVERIFY(tagged);
        AudioMixer mixer; mixer.replayGainMode = "track"; mixer.setCurrent(tagged, "tagged");
        auto quiet = mixer.render(2000); QVERIFY(!quiet.pcm.isEmpty());
        QVERIFY(std::abs(reinterpret_cast<const float *>(quiet.pcm.constData())[0] - 0.125f) < 0.00001f);
        mixer.replayGainMode = "off"; auto ramp = mixer.render(8000); const auto *samples = reinterpret_cast<const float *>(ramp.pcm.constData());
        QVERIFY(samples[0] < 0.126f); QVERIFY(samples[15999] > 0.249f);
        for (int i = 2; i < 16000; i += 2) QVERIFY(std::abs(samples[i] - samples[i - 2]) < 0.001f);
        mixer.replayGainMode = "album"; mixer.setCurrent(tagged, "album"); auto album = mixer.render(100); QCOMPARE(reinterpret_cast<const float *>(album.pcm.constData())[0], 0.25f);
        auto clipped = pcm("peak.wav", {{"trackGain", 12}, {"trackPeak", 0.8}}, 0.8f, 1000); QVERIFY(clipped);
        mixer.replayGainMode = "track"; mixer.setCurrent(clipped, "peak"); auto peak = mixer.render(100);
        for (int i = 0; i < peak.pcm.size() / int(sizeof(float)); ++i) QVERIFY(std::abs(reinterpret_cast<const float *>(peak.pcm.constData())[i]) <= 1);
        auto first = pcm("first.wav", {{"trackGain", -6.020599913279624}}, 0.25f, 24000);
        auto second = pcm("second.wav", {{"trackGain", -12.041199826559248}}, 0.25f, 24000); QVERIFY(first && second);
        mixer.clear(); mixer.crossfadeSeconds = 0.25; mixer.setCurrent(first, "first"); mixer.setNext(second, "second"); mixer.render(12000);
        const auto mixed = mixer.render(6001); const auto actual = reinterpret_cast<const float *>(mixed.pcm.constData())[12000];
        QVERIFY(std::abs(actual - (0.125 + 0.0625) * std::sqrt(0.5)) < 0.0001);
    }
    void replayGainPreferencesPersistAndInvalidBackupIsAtomic()
    {
        QVariantMap saved;
        { PlaybackEngine engine; QCOMPARE(engine.replayGainMode(), QString("off")); engine.setReplayGainMode("album"); saved = engine.exportOptions(); }
        PlaybackEngine restored; QCOMPARE(restored.replayGainMode(), QString("album"));
        auto invalid = saved; invalid["replayGainMode"] = "analysis"; invalid["crossfade"] = !saved.value("crossfade").toBool();
        QVERIFY(!restored.importOptions(invalid)); QCOMPARE(restored.exportOptions(), saved);
        restored.setReplayGainMode("track"); QVERIFY(restored.importOptions(saved)); QCOMPARE(restored.replayGainMode(), QString("album"));
    }
    void equalizerBypassPresetsAndClipping()
    {
        AudioEqualizer eq;
        auto samples = tone(1000); const auto original = samples;
        eq.process(samples.data(), 48000); QCOMPARE(samples, original);
        QVERIFY(!eq.setPreset("missing")); QCOMPARE(eq.preset(), QString("bass"));
        QVERIFY(response("bass", 50) > response("bass", 1000) * 1.4);
        QVERIFY(response("vocal", 1800) > response("vocal", 80) * 1.5);
        QVERIFY(response("headphones", 50) > response("headphones", 300) * 1.3);
        QVERIFY(std::abs(response("flat", 1000) - 1) < 0.0001);
        eq.setEnabled(true);
        samples = tone(200, 1); eq.process(samples.data(), 48000);
        for (size_t i = 0; i < samples.size(); ++i) {
            QVERIFY(std::isfinite(samples[i])); QVERIFY(std::abs(samples[i]) <= 1);
            if (i % 2 == 1) QCOMPARE(samples[i], 0.0f);
        }
    }
    void equalizerChangesRampAndSeekResetsState()
    {
        AudioEqualizer eq; eq.setEnabled(true);
        std::vector<float> signal(2000 * 2, 0.5f);
        eq.process(signal.data(), 2000);
        QVERIFY(std::abs(signal.front() - 0.5f) < 0.005f);
        eq.setPreset("vocal");
        std::vector<float> changed(2000 * 2, 0.5f); eq.process(changed.data(), 2000);
        QVERIFY(std::abs(changed.front() - signal.back()) < 0.02f);
        for (size_t i = 2; i < changed.size(); i += 2) QVERIFY(std::abs(changed[i] - changed[i - 2]) < 0.02f);
        eq.setEnabled(false);
        std::vector<float> bypass(2000 * 2, 0.5f); eq.process(bypass.data(), 2000);
        QCOMPARE(bypass.back(), 0.5f);
        eq.setEnabled(true); eq.reset();
        AudioEqualizer fresh; fresh.setPreset("vocal"); fresh.setEnabled(true); fresh.reset();
        auto actual = tone(123); auto expected = actual;
        eq.process(actual.data(), 48000); fresh.process(expected.data(), 48000);
        QCOMPARE(actual, expected);
    }
    void customEqualizerBandsResampleAndShapeTheSound()
    {
        AudioEqualizer eq;
        QVERIFY(!eq.setBandCount(4)); QVERIFY(!eq.setBandCount(16)); QCOMPARE(eq.bandCount(), 10);
        QVERIFY(eq.setBandCount(5)); QCOMPARE(eq.frequencies().size(), 5);
        QCOMPARE(eq.frequencies().first(), 31.25); QVERIFY(std::abs(eq.frequencies().last() - 16000) < 0.001);
        eq.resetCurve(); QVERIFY(eq.setBandGain(2, 12)); QCOMPARE(eq.preset(), QString("custom"));
        QVERIFY(!eq.setBandGain(5, 0)); QVERIFY(!eq.setBandGain(-1, 0));
        QVERIFY(!eq.setBandGain(2, 12.01)); QVERIFY(!eq.setBandGain(2, std::numeric_limits<double>::quiet_NaN()));
        QCOMPARE(eq.gains()[2], 12.0);
        eq.setEnabled(true); eq.reset();
        auto peak = tone(eq.frequencies()[2]); eq.process(peak.data(), 48000);
        eq.reset(); auto outside = tone(80); eq.process(outside.data(), 48000);
        double peakEnergy = 0, outsideEnergy = 0;
        for (int i = 24000; i < 48000; ++i) {
            peakEnergy += peak[size_t(i) * 2] * peak[size_t(i) * 2];
            outsideEnergy += outside[size_t(i) * 2] * outside[size_t(i) * 2];
        }
        QVERIFY(peakEnergy > outsideEnergy * 5);
        const QVector<double> curve{-12, -6, 0, 6, 12}; QVERIFY(eq.setCustomGains(curve));
        QVERIFY(eq.setPreset("vocal")); QVERIFY(eq.setPreset("custom")); QCOMPARE(eq.gains(), curve);
        QVERIFY(eq.setBandCount(15)); QCOMPARE(eq.customGains().size(), 15);
        QCOMPARE(eq.customGains().first(), -12.0); QCOMPARE(eq.customGains()[7], 0.0); QCOMPARE(eq.customGains().last(), 12.0);
        QVERIFY(!eq.setCustomGains(QVector<double>(14, 0)));
        eq.resetCurve(); QCOMPARE(eq.gains(), QVector<double>(15, 0));
        for (int count = 5; count <= 15; ++count) {
            AudioEqualizer lowRate(8000, 2); QVERIFY(lowRate.setBandCount(count));
            for (int band = 0; band < count; ++band) QVERIFY(lowRate.setBandGain(band, band % 2 ? -12 : 12));
            lowRate.setEnabled(true); lowRate.reset();
            std::vector<float> impulse(8000 * 2, 0); impulse[0] = 1;
            lowRate.process(impulse.data(), 8000);
            for (float sample : impulse) { QVERIFY(std::isfinite(sample)); QVERIFY(std::abs(sample) <= 1); }
        }
    }
    void monoAveragesBothChannelsAndStereoRestoresThem()
    {
        AudioEqualizer eq;
        std::vector<float> original(2000 * 2);
        for (int frame = 0; frame < 2000; ++frame) { original[size_t(frame) * 2] = 0.75f; original[size_t(frame) * 2 + 1] = -0.25f; }
        auto stereo = original; eq.process(stereo.data(), 2000); QCOMPARE(stereo, original);
        eq.setMono(true); auto mono = original; eq.process(mono.data(), 2000);
        QVERIFY(std::abs(mono[0] - 0.75f) < 0.001f);
        for (int frame = 960; frame < 2000; ++frame) {
            QCOMPARE(mono[size_t(frame) * 2], 0.25f); QCOMPARE(mono[size_t(frame) * 2 + 1], 0.25f);
        }
        eq.setMono(false); auto restored = original; eq.process(restored.data(), 2000);
        for (int frame = 960; frame < 2000; ++frame) {
            QCOMPARE(restored[size_t(frame) * 2], 0.75f); QCOMPARE(restored[size_t(frame) * 2 + 1], -0.25f);
        }
        eq.setMono(true); eq.setPreset("vocal"); eq.setEnabled(true); eq.reset();
        auto filtered = original; eq.process(filtered.data(), 2000);
        for (int frame = 0; frame < 2000; ++frame) QCOMPARE(filtered[size_t(frame) * 2], filtered[size_t(frame) * 2 + 1]);
    }
    void lrcOffsetsRepeatsAndPlainLyrics()
    {
        const auto result = LyricsDocument::parseText("[ar:Artist]\n[offset:200]\n[00:02.50][00:03.125]Repeat\n[00:01.2]First\n[00:02.50]Translation\n[01:02:03.04]Long mix");
        QVERIFY(result.value("synchronized").toBool());
        const auto lines = result.value("lines").toList(); QCOMPARE(lines.size(), 4);
        QCOMPARE(lines[0].toMap().value("time").toLongLong(), 1000);
        QCOMPARE(lines[0].toMap().value("text").toString(), QString("First"));
        QCOMPARE(lines[1].toMap().value("time").toLongLong(), 2300);
        QCOMPARE(lines[1].toMap().value("text").toString(), QString("Repeat\nTranslation"));
        QCOMPARE(lines[2].toMap().value("time").toLongLong(), 2925);
        QCOMPARE(lines[3].toMap().value("time").toLongLong(), 3722840);
        const auto plain = LyricsDocument::parseText("\n[ti:Song]\nVerse\n\nChorus\n");
        QVERIFY(!plain.value("synchronized").toBool()); QCOMPARE(plain.value("lines").toList().size(), 3);
    }
    void embeddedSidecarsAndAttachedLyricsAreOffline()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const QString song = directory.filePath("Song.wav"); QVERIFY(makeWave(song));
        {
            const auto path = QFile::encodeName(song); TagLib::FileRef file(path.constData(), false); QVERIFY(!file.isNull());
            auto properties = file.properties(); properties.replace("LYRICS", TagLib::StringList(TagLib::String("Embedded verse", TagLib::String::UTF8)));
            QVERIFY(file.setProperties(properties).isEmpty()); QVERIFY(file.save());
        }
        const auto songUrl = QUrl::fromLocalFile(song);
        const auto embedded = LyricsDocument::readLyrics(songUrl);
        QCOMPARE(embedded.value("sourceLabel").toString(), QString("Embedded lyrics"));
        QCOMPARE(embedded.value("lines").toList().first().toMap().value("text").toString(), QString("Embedded verse"));
        QVERIFY(write(directory.filePath("Song.lrc"), "[00:01.00]Sidecar verse\n"));
        const auto sidecar = LyricsDocument::readLyrics(songUrl);
        QVERIFY(sidecar.value("synchronized").toBool());
        QCOMPARE(sidecar.value("sourceLabel").toString(), QString("Song.lrc"));
        const auto attachment = QUrl::fromLocalFile(directory.filePath("My lyrics.txt"));
        QVERIFY(write(attachment.toLocalFile(), "Manual verse\n"));
        {
            LyricsDocument doc; doc.load(songUrl); QTRY_VERIFY_WITH_TIMEOUT(!doc.loading(), 5000);
            QVERIFY(doc.attachFile(attachment)); QTRY_VERIFY_WITH_TIMEOUT(!doc.loading(), 5000);
            QVERIFY(doc.attached()); QCOMPARE(doc.lines().first().toMap().value("text").toString(), QString("Manual verse"));
            QVERIFY(!doc.importAttachments({{"https://example.invalid/song", attachment.toString()}}));
        }
        LyricsDocument reopened; reopened.load(songUrl); QTRY_VERIFY_WITH_TIMEOUT(!reopened.loading(), 5000);
        QVERIFY(reopened.attached()); QCOMPARE(reopened.lines().first().toMap().value("text").toString(), QString("Manual verse"));
        const auto backup = reopened.exportAttachments(); QVERIFY(!backup.isEmpty());
        reopened.clearAttachment(); QTRY_VERIFY_WITH_TIMEOUT(!reopened.loading(), 5000);
        QVERIFY(!reopened.attached()); QCOMPARE(reopened.sourceLabel(), QString("Song.lrc"));
        QVERIFY(reopened.importAttachments(backup)); QTRY_VERIFY_WITH_TIMEOUT(!reopened.loading(), 5000);
        QCOMPARE(reopened.sourceLabel(), QString("My lyrics.txt"));
        QVERIFY(QDir().mkpath(directory.filePath("Moved")));
        const auto movedSong = QUrl::fromLocalFile(directory.filePath("Moved/Song.wav"));
        QVERIFY(QFile::copy(song, movedSong.toLocalFile()));
        QVERIFY(QFile::copy(attachment.toLocalFile(), directory.filePath("Moved/My lyrics.txt")));
        reopened.remapSources({{songUrl.toString(), movedSong.toString()}});
        QTRY_VERIFY_WITH_TIMEOUT(!reopened.loading(), 5000);
        QCOMPARE(reopened.source(), movedSong); QVERIFY(reopened.attached());
        QCOMPARE(reopened.exportAttachments().value(movedSong.toString()).toString(), QUrl::fromLocalFile(directory.filePath("Moved/My lyrics.txt")).toString());
    }
    void equalizerOptionsPersistAndValidateBackup()
    {
        QVariantMap saved;
        {
            PlaybackEngine engine; QVERIFY(!engine.equalizerEnabled());
            engine.setEqualizerPreset("headphones"); engine.setEqualizerEnabled(true);
            engine.setCrossfadeSeconds(14); engine.setCrossfadeEnabled(true);
            saved = engine.exportOptions();
            auto bad = saved; bad["equalizerPreset"] = "missing";
            QVERIFY(!engine.importOptions(bad)); QCOMPARE(engine.exportOptions(), saved);
        }
        PlaybackEngine reopened; QVERIFY(reopened.equalizerEnabled()); QCOMPARE(reopened.equalizerPreset(), QString("headphones"));
        QCOMPARE(reopened.exportOptions(), saved);
        reopened.setEqualizerEnabled(false); QVERIFY(reopened.importOptions(saved)); QVERIFY(reopened.equalizerEnabled());
    }
    void customCurveChannelsAndDeviceOptionsPersistAtomically()
    {
        QVariantMap saved;
        {
            PlaybackEngine engine;
            QCOMPARE(engine.outputMode(), QString("stereo")); QVERIFY(engine.outputDeviceId().isEmpty());
            QVERIFY(!engine.outputDevices().isEmpty()); QCOMPARE(engine.outputDevices().first().toMap().value("id").toString(), QString());
            engine.setEqualizerBandCount(15); engine.resetEqualizer();
            engine.setEqualizerBandGain(0, -7.5); engine.setEqualizerBandGain(7, 3.5); engine.setEqualizerBandGain(14, 9);
            engine.setEqualizerEnabled(true); engine.setOutputMode("mono");
            engine.setOutputDeviceId(QStringLiteral("bWlzc2luZw")); // Stable ID for a disconnected device.
            QCOMPARE(engine.outputMode(), QString("mono")); QCOMPARE(engine.equalizerPreset(), QString("custom"));
            QCOMPARE(engine.equalizerBands().size(), 15); QCOMPARE(engine.equalizerBands()[7].toMap().value("gain").toDouble(), 3.5);
            QVERIFY(!engine.outputDeviceName().isEmpty());
            saved = engine.exportOptions();
            QSignalSpy eqChanges(&engine, &PlaybackEngine::equalizerChanged);
            QSignalSpy outputChanges(&engine, &PlaybackEngine::audioOutputChanged);
            const QVariantMap invalidValues{{"equalizerBandCount", 15.5}, {"equalizerCustomGains", QVariantList{0, 0}},
                {"outputMode", "surround"}, {"outputDeviceId", "invalid key!"}, {"equalizerEnabled", "true"}, {"crossfadeSeconds", "5"}};
            for (auto it = invalidValues.begin(); it != invalidValues.end(); ++it) {
                auto invalid = saved; invalid[it.key()] = it.value();
                QVERIFY(!engine.importOptions(invalid)); QCOMPARE(engine.exportOptions(), saved);
            }
            auto invalid = saved; auto curve = saved.value("equalizerCustomGains").toList();
            curve[2] = std::numeric_limits<double>::infinity(); invalid["equalizerCustomGains"] = curve;
            QVERIFY(!engine.importOptions(invalid)); QCOMPARE(engine.exportOptions(), saved);
            QCOMPARE(eqChanges.count(), 0); QCOMPARE(outputChanges.count(), 0);
            engine.setEqualizerPreset("bass"); engine.setEqualizerPreset("custom"); QCOMPARE(engine.exportOptions(), saved);
        }
        PlaybackEngine reopened; QCOMPARE(reopened.exportOptions(), saved);
        reopened.setOutputMode("stereo"); reopened.setOutputDeviceId({}); reopened.resetEqualizer();
        QVERIFY(reopened.importOptions(saved)); QCOMPARE(reopened.exportOptions(), saved);
        QVariantMap legacy{{"gapless", true}, {"crossfade", false}, {"crossfadeSeconds", 5},
            {"equalizerEnabled", true}, {"equalizerPreset", "vocal"}};
        QVERIFY(reopened.importOptions(legacy)); QCOMPARE(reopened.equalizerBandCount(), 10);
        QCOMPARE(reopened.outputMode(), QString("stereo")); QVERIFY(reopened.outputDeviceId().isEmpty());
    }
    void changingOutputRetainsTheAudibleTrackDuringAPendingTransition()
    {
        PlaybackWorker worker;
        const auto originalUrl = QUrl::fromLocalFile("/original.wav"), nextUrl = QUrl::fromLocalFile("/next.wav");
        auto original = std::make_shared<PcmTrack>(originalUrl, worker.m_format);
        auto next = std::make_shared<PcmTrack>(nextUrl, worker.m_format);
        const QByteArray silence(worker.m_format.bytesPerFrame() * worker.m_format.sampleRate(), 0);
        QVERIFY(original->append(silence.constData(), silence.size())); original->complete = true;
        QVERIFY(next->append(silence.constData(), silence.size())); next->complete = true;
        const std::weak_ptr<PcmTrack> originalWeak = original;
        worker.m_buffers.insert(originalUrl.toString(), original); worker.m_buffers.insert(nextUrl.toString(), next);
        worker.m_source = originalUrl; worker.m_entryId = "original"; worker.m_position = 5; worker.m_duration = 1000;
        worker.m_state = QMediaPlayer::PausedState;
        worker.m_mixer.setCurrent(next, "next"); // The mixer has generated an incoming voice ahead of hardware.
        MixerEvent pending; pending.kind = MixerEvent::Started; pending.entryId = "original";
        pending.url = originalUrl; pending.track = original;
        worker.m_events.enqueue({0, pending}); pending.track.reset(); original.reset();
        QCOMPARE(originalWeak.use_count(), 1); // Only the pending hardware event still owns this track.
        worker.rebuildOutputForDevice(QAudioDevice());
        QVERIFY(!originalWeak.expired()); QCOMPARE(worker.m_mixer.current().track, originalWeak.lock());
        QCOMPARE(worker.m_mixer.current().entryId, QString("original"));
        QCOMPARE(worker.m_mixer.current().frame, 5LL * worker.m_format.sampleRate() / 1000);
        QCOMPARE(worker.m_mixer.next().track, next); QCOMPARE(worker.m_mixer.next().entryId, QString("next"));
        QCOMPARE(worker.playbackState(), QMediaPlayer::PausedState);
    }
};

QTEST_GUILESS_MAIN(AudioFeatureTests)
#include "tst_audiofeatures.moc"
