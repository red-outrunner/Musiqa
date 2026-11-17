#include "mprisservice.h"

#if MUSICA_HAS_DBUS
#include "tracklibrary.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QFileInfo>

namespace {
const QString path = QStringLiteral("/org/mpris/MediaPlayer2");
const QString playerInterface = QStringLiteral("org.mpris.MediaPlayer2.Player");
const QString rootInterface = QStringLiteral("org.mpris.MediaPlayer2");
const QString propertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");
}

MprisService::MprisService(QObject *window, TrackListModel *library, PlaybackEngine *player,
    const QDBusConnection &connection, const QString &service)
    : QDBusVirtualObject(window), m_window(window), m_library(library), m_player(player),
      m_connection(connection), m_service(service)
{
    if (!m_connection.isConnected()) return;
    if (!m_connection.registerService(m_service)) {
        m_service += QStringLiteral(".instance%1").arg(QCoreApplication::applicationPid());
        if (!m_connection.registerService(m_service)) return;
    }
    if (!m_connection.registerVirtualObject(path, this)) {
        m_connection.unregisterService(m_service);
        return;
    }
    m_registered = true;
    connect(m_player, &PlaybackEngine::playbackStateChanged, this, &MprisService::refresh);
    connect(m_player, &PlaybackEngine::sourceChanged, this, &MprisService::refresh);
    connect(m_player, &PlaybackEngine::durationChanged, this, &MprisService::refresh);
    connect(m_player, &PlaybackEngine::seekableChanged, this, &MprisService::refresh);
    connect(m_library, &TrackListModel::tracksChanged, this, &MprisService::refresh);
    connect(m_library, &TrackListModel::volumeChanged, this, &MprisService::refresh);
    for (const char *signal : {SIGNAL(shuffleEnabledChanged()), SIGNAL(repeatModeChanged()),
            SIGNAL(queueItemsChanged()), SIGNAL(currentQueueIdChanged()), SIGNAL(currentUrlChanged()), SIGNAL(mutedChanged())})
        QObject::connect(window, signal, this, SLOT(refresh()));
    QObject::connect(window, SIGNAL(playbackSeeked(double)), this, SLOT(notifySeeked(double)));
    m_lastProperties = playerProperties();
}

MprisService::~MprisService()
{
    if (m_registered) {
        m_connection.unregisterObject(path);
        m_connection.unregisterService(m_service);
    }
}

QVariantMap MprisService::rootProperties() const
{
    return {{"CanQuit", true}, {"CanRaise", true}, {"HasTrackList", false}, {"Identity", QStringLiteral("Musica")},
        {"DesktopEntry", QStringLiteral("musica")}, {"SupportedUriSchemes", QStringList{"file"}},
        {"SupportedMimeTypes", QStringList{"audio/mpeg", "audio/flac", "audio/ogg", "audio/opus", "audio/x-wav", "audio/mp4", "audio/aac", "audio/x-aiff", "audio/x-ms-wma"}}};
}

QVariantMap MprisService::playerProperties() const
{
    if (!m_window) return {};
    const QString url = m_window->property("currentUrl").toString();
    const auto track = m_library->trackForUrl(url);
    const bool hasTrack = !track.isEmpty();
    QVariantMap metadata;
    if (hasTrack) {
        const auto key = url.toUtf8() + m_window->property("currentQueueId").toString().toUtf8();
        const QString id = QStringLiteral("/com/musica/track/") + QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha256).toHex());
        metadata = {{"mpris:trackid", QVariant::fromValue(QDBusObjectPath(id))},
            {"mpris:length", QVariant::fromValue<qlonglong>((m_player->duration() > 0 ? m_player->duration() : track.value("duration").toLongLong()) * 1000)},
            {"xesam:url", url}, {"xesam:title", track.value("title")}, {"xesam:album", track.value("album")},
            {"xesam:artist", QStringList{track.value("artist").toString()}},
            {"xesam:albumArtist", QStringList{track.value("albumArtist").toString()}}};
        if (track.value("trackNumber").toInt() > 0) metadata["xesam:trackNumber"] = track.value("trackNumber").toInt();
        const auto art = track.value("artwork").toString();
        if (QUrl(art).isLocalFile()) metadata["mpris:artUrl"] = art;
    }
    const auto status = m_player->playbackState();
    const int repeat = m_window->property("repeatMode").toInt();
    const int index = m_window->property("currentQueueIndex").toInt();
    const int count = m_window->property("queueCount").toInt();
    return {{"PlaybackStatus", status == QMediaPlayer::PlayingState ? QStringLiteral("Playing") : status == QMediaPlayer::PausedState ? QStringLiteral("Paused") : QStringLiteral("Stopped")},
        {"LoopStatus", repeat == 2 ? QStringLiteral("Track") : repeat == 1 ? QStringLiteral("Playlist") : QStringLiteral("None")},
        {"Rate", 1.0}, {"Shuffle", m_window->property("shuffleEnabled").toBool()}, {"Metadata", metadata},
        {"Volume", m_window->property("muted").toBool() ? 0.0 : double(m_library->volume())},
        {"Position", QVariant::fromValue<qlonglong>(m_player->position() * 1000)}, {"MinimumRate", 1.0}, {"MaximumRate", 1.0},
        {"CanGoNext", count > 1 && (index + 1 < count || repeat != 0)}, {"CanGoPrevious", count > 1 || hasTrack},
        {"CanPlay", hasTrack || count > 0 || m_library->trackCount() > 0}, {"CanPause", hasTrack},
        {"CanSeek", hasTrack && m_player->isSeekable()}, {"CanControl", true}};
}

void MprisService::command(const QString &name, const QVariant &value)
{
    if (m_window) QMetaObject::invokeMethod(m_window, "desktopCommand", Q_ARG(QVariant, name), Q_ARG(QVariant, value));
    refresh();
}

void MprisService::refresh()
{
    if (!m_registered) return;
    const auto current = playerProperties();
    QVariantMap changes;
    for (auto it = current.cbegin(); it != current.cend(); ++it) {
        if (it.key() == QStringLiteral("Position") || it.key() == QStringLiteral("CanControl")) continue;
        // QDBusObjectPath doesn't provide QVariant equality on all supported Qt versions.
        if (it.value() != m_lastProperties.value(it.key())) changes.insert(it.key(), it.value());
    }
    m_lastProperties = current;
    if (changes.isEmpty()) return;
    auto signal = QDBusMessage::createSignal(path, propertiesInterface, QStringLiteral("PropertiesChanged"));
    signal << playerInterface << changes << QStringList();
    m_connection.send(signal);
}

void MprisService::notifySeeked(double position)
{
    if (!m_registered) return;
    auto signal = QDBusMessage::createSignal(path, playerInterface, QStringLiteral("Seeked"));
    signal << QVariant::fromValue<qlonglong>(qMax<qlonglong>(0, qRound64(position)) * 1000);
    m_connection.send(signal);
}

bool MprisService::handleMessage(const QDBusMessage &message, const QDBusConnection &connection)
{
    const auto args = message.arguments();
    const auto member = message.member();
    auto reply = [&connection, &message](const QVariantList &values = {}) { connection.send(message.createReply(values)); return true; };
    auto error = [&connection, &message](QDBusError::ErrorType type, const QString &text) { connection.send(message.createErrorReply(type, text)); return true; };
    if (message.interface() == QStringLiteral("org.freedesktop.DBus.Introspectable") && member == QStringLiteral("Introspect") && args.isEmpty())
        return reply({QStringLiteral("<node>") + introspect(path) + QStringLiteral("<interface name=\"org.freedesktop.DBus.Introspectable\"><method name=\"Introspect\"><arg type=\"s\" direction=\"out\"/></method></interface></node>")});
    if (message.interface() == propertiesInterface) {
        if (args.isEmpty()) return error(QDBusError::InvalidArgs, QStringLiteral("Missing interface"));
        const auto interface = args[0].toString();
        if (interface != rootInterface && interface != playerInterface) return error(QDBusError::UnknownInterface, QStringLiteral("Unknown interface"));
        const auto properties = interface == rootInterface ? rootProperties() : playerProperties();
        if (member == QStringLiteral("GetAll") && args.size() == 1) return reply({properties});
        if (args.size() < 2 || !properties.contains(args[1].toString())) return error(QDBusError::UnknownProperty, QStringLiteral("Unknown property"));
        const auto property = args[1].toString();
        if (member == QStringLiteral("Get") && args.size() == 2) return reply({QVariant::fromValue(QDBusVariant(properties.value(property)))});
        if (member == QStringLiteral("Set") && args.size() == 3) {
            const QVariant value = qvariant_cast<QDBusVariant>(args[2]).variant();
            if (interface != playerInterface) return error(QDBusError::PropertyReadOnly, QStringLiteral("Read-only property"));
            if (property == QStringLiteral("LoopStatus") && value.metaType() == QMetaType::fromType<QString>() && QStringList{"None", "Track", "Playlist"}.contains(value.toString()))
                command(QStringLiteral("repeat"), value.toString() == QStringLiteral("Track") ? 2 : value.toString() == QStringLiteral("Playlist") ? 1 : 0);
            else if (property == QStringLiteral("Shuffle") && value.metaType() == QMetaType::fromType<bool>()) command(QStringLiteral("shuffle"), value);
            else if (property == QStringLiteral("Volume") && value.metaType() == QMetaType::fromType<double>()) command(QStringLiteral("volume"), qBound(0.0, value.toDouble(), 1.0));
            else if (property == QStringLiteral("Rate") && value.metaType() == QMetaType::fromType<double>()) { if (value.toDouble() == 0) command(QStringLiteral("pause")); }
            else if (QStringList{"LoopStatus", "Shuffle", "Volume"}.contains(property)) return error(QDBusError::InvalidArgs, QStringLiteral("Invalid property value"));
            else return error(QDBusError::PropertyReadOnly, QStringLiteral("Read-only property"));
            return reply();
        }
        return error(QDBusError::InvalidArgs, QStringLiteral("Invalid property call"));
    }
    if (message.interface() == rootInterface && args.isEmpty()) {
        if (member == QStringLiteral("Raise")) { command(QStringLiteral("raise")); return reply(); }
        if (member == QStringLiteral("Quit")) { command(QStringLiteral("quit")); return reply(); }
    }
    if (message.interface() == playerInterface) {
        const QHash<QString, QString> methods{{"Next", "next"}, {"Previous", "previous"}, {"Pause", "pause"}, {"PlayPause", "toggle"}, {"Stop", "stop"}, {"Play", "play"}};
        if (methods.contains(member) && args.isEmpty()) { command(methods.value(member)); return reply(); }
        if (member == QStringLiteral("Seek") && args.size() == 1 && message.signature() == QStringLiteral("x")) {
            const qint64 offset = args[0].toLongLong() / 1000;
            if (m_player->isSeekable()) {
                if (offset > m_player->duration() - m_player->position()) command(QStringLiteral("next"));
                else command(QStringLiteral("seek"), qMax<qint64>(0, m_player->position() + offset));
            }
            return reply();
        }
        if (member == QStringLiteral("SetPosition") && args.size() == 2 && message.signature() == QStringLiteral("ox")) {
            const auto metadata = playerProperties().value("Metadata").toMap();
            const auto id = qvariant_cast<QDBusObjectPath>(metadata.value("mpris:trackid"));
            const qint64 pos = args[1].toLongLong() / 1000;
            if (qvariant_cast<QDBusObjectPath>(args[0]).path() == id.path() && pos >= 0 && pos <= m_player->duration() && m_player->isSeekable()) command(QStringLiteral("seek"), pos);
            return reply();
        }
        if (member == QStringLiteral("OpenUri") && args.size() == 1 && message.signature() == QStringLiteral("s")) {
            const QUrl url(args[0].toString());
            const QFileInfo info(url.toLocalFile());
            if (!url.isLocalFile() || !info.isFile() || !info.isReadable() || !QStringList{"mp3", "flac", "ogg", "oga", "opus", "wav", "m4a", "aac", "aif", "aiff", "wma"}.contains(info.suffix().toLower()))
                return error(QDBusError::InvalidArgs, QStringLiteral("Choose a readable local audio file"));
            command(QStringLiteral("open"), QUrl::fromLocalFile(info.canonicalFilePath()).toString());
            return reply();
        }
        if (methods.contains(member) || QStringList{"Seek", "SetPosition", "OpenUri"}.contains(member)) return error(QDBusError::InvalidArgs, QStringLiteral("Invalid method arguments"));
    }
    return error(QDBusError::UnknownMethod, QStringLiteral("Unknown method"));
}

QString MprisService::introspect(const QString &) const
{
    return QStringLiteral(R"XML(
<interface name="org.mpris.MediaPlayer2">
 <method name="Raise"/><method name="Quit"/>
 <property name="CanQuit" type="b" access="read"/><property name="CanRaise" type="b" access="read"/>
 <property name="HasTrackList" type="b" access="read"/><property name="Identity" type="s" access="read"/>
 <property name="DesktopEntry" type="s" access="read"/><property name="SupportedUriSchemes" type="as" access="read"/>
 <property name="SupportedMimeTypes" type="as" access="read"/>
</interface>
<interface name="org.mpris.MediaPlayer2.Player">
 <method name="Next"/><method name="Previous"/><method name="Pause"/><method name="PlayPause"/><method name="Stop"/><method name="Play"/>
 <method name="Seek"><arg name="Offset" type="x" direction="in"/></method>
 <method name="SetPosition"><arg name="TrackId" type="o" direction="in"/><arg name="Position" type="x" direction="in"/></method>
 <method name="OpenUri"><arg name="Uri" type="s" direction="in"/></method>
 <signal name="Seeked"><arg name="Position" type="x"/></signal>
 <property name="PlaybackStatus" type="s" access="read"/><property name="LoopStatus" type="s" access="readwrite"/>
 <property name="Rate" type="d" access="readwrite"/><property name="Shuffle" type="b" access="readwrite"/>
 <property name="Metadata" type="a{sv}" access="read"/><property name="Volume" type="d" access="readwrite"/>
 <property name="Position" type="x" access="read"/><property name="MinimumRate" type="d" access="read"/>
 <property name="MaximumRate" type="d" access="read"/><property name="CanGoNext" type="b" access="read"/>
 <property name="CanGoPrevious" type="b" access="read"/><property name="CanPlay" type="b" access="read"/>
 <property name="CanPause" type="b" access="read"/><property name="CanSeek" type="b" access="read"/>
 <property name="CanControl" type="b" access="read"/>
</interface>
<interface name="org.freedesktop.DBus.Properties">
 <method name="Get"><arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="v" direction="out"/></method>
 <method name="GetAll"><arg type="s" direction="in"/><arg type="a{sv}" direction="out"/></method>
 <method name="Set"><arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="v" direction="in"/></method>
 <signal name="PropertiesChanged"><arg type="s"/><arg type="a{sv}"/><arg type="as"/></signal>
</interface>)XML");
}
#endif
