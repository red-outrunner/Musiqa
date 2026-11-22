#include "windowsmediacontrols.h"
#include "playbackengine.h"
#include "tracklibrary.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QWindow>
#ifdef Q_OS_WIN
#include <windows.h>
#include <roapi.h>
#include <SystemMediaTransportControlsInterop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
namespace Media = winrt::Windows::Media;
namespace Storage = winrt::Windows::Storage;
namespace Streams = winrt::Windows::Storage::Streams;
#endif

struct WindowsMediaControls::Native
{
#ifdef Q_OS_WIN
    Media::SystemMediaTransportControls controls{nullptr};
    winrt::event_token buttons{}, seek{};
    quint64 artworkRevision = 0;
    QString artwork;
    bool apartment = false;
    ~Native()
    {
        try {
            if (controls) {
                if (buttons.value) controls.ButtonPressed(buttons);
                if (seek.value) controls.PlaybackPositionChangeRequested(seek);
                controls.IsEnabled(false);
            }
        } catch (const winrt::hresult_error &) {}
        if (apartment) RoUninitialize();
    }
#endif
};

WindowsMediaControls::WindowsMediaControls(QObject *window, TrackListModel *library, PlaybackEngine *player)
    : QObject(window), m_window(window), m_library(library), m_player(player), m_timeline(this)
{
#ifdef Q_OS_WIN
    auto *nativeWindow = qobject_cast<QWindow *>(window);
    if (!nativeWindow || !player) return;
    try {
        m_native = std::make_unique<Native>();
        const auto initialized = RoInitialize(RO_INIT_SINGLETHREADED);
        if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) winrt::check_hresult(initialized);
        m_native->apartment = SUCCEEDED(initialized);
        auto interop = winrt::get_activation_factory<Media::SystemMediaTransportControls, ISystemMediaTransportControlsInterop>();
        winrt::check_hresult(interop->GetForWindow(reinterpret_cast<HWND>(nativeWindow->winId()),
            winrt::guid_of<Media::SystemMediaTransportControls>(), winrt::put_abi(m_native->controls)));
        const QPointer<WindowsMediaControls> guard(this);
        auto *dispatcher = QCoreApplication::instance();
        m_native->buttons = m_native->controls.ButtonPressed([guard, dispatcher](const auto &, const auto &event) {
            QString command;
            switch (event.Button()) {
            case Media::SystemMediaTransportControlsButton::Play: command = "play"; break;
            case Media::SystemMediaTransportControlsButton::Pause: command = "pause"; break;
            case Media::SystemMediaTransportControlsButton::Stop: command = "stop"; break;
            case Media::SystemMediaTransportControlsButton::Next: command = "next"; break;
            case Media::SystemMediaTransportControlsButton::Previous: command = "previous"; break;
            default: return;
            }
            QMetaObject::invokeMethod(dispatcher, [guard, command] { if (guard) guard->command(command); }, Qt::QueuedConnection);
        });
        m_native->seek = m_native->controls.PlaybackPositionChangeRequested([guard, dispatcher](const auto &, const auto &event) {
            const auto position = event.RequestedPlaybackPosition().count() / 10000;
            QMetaObject::invokeMethod(dispatcher, [guard, position] { if (guard) guard->command("seek", qint64(position)); }, Qt::QueuedConnection);
        });
        m_available = true;
    } catch (const winrt::hresult_error &) { m_native.reset(); return; }
    connect(player, &PlaybackEngine::sourceChanged, this, &WindowsMediaControls::refresh);
    connect(player, &PlaybackEngine::playbackStateChanged, this, &WindowsMediaControls::refresh);
    connect(player, &PlaybackEngine::durationChanged, this, &WindowsMediaControls::refresh);
    connect(player, &PlaybackEngine::seekableChanged, this, &WindowsMediaControls::refresh);
    if (library) connect(library, &TrackListModel::tracksChanged, this, &WindowsMediaControls::refresh);
    for (const char *signal : {SIGNAL(queueItemsChanged()), SIGNAL(currentQueueIdChanged()), SIGNAL(currentUrlChanged()), SIGNAL(repeatModeChanged())})
        QObject::connect(window, signal, this, SLOT(refresh()));
    QObject::connect(window, SIGNAL(playbackSeeked(double)), this, SLOT(updateTimeline()));
    m_timeline.setInterval(1000);
    connect(&m_timeline, &QTimer::timeout, this, &WindowsMediaControls::updateTimeline);
    refresh();
#endif
}
WindowsMediaControls::~WindowsMediaControls() = default;

QVariantMap WindowsMediaControls::playerProperties() const
{
    if (!m_window || !m_player) return {};
    const auto url = m_window->property("currentUrl").toString();
    const auto track = m_library ? m_library->trackForUrl(url) : m_window->property("currentTrack").toMap();
    const auto index = m_window->property("currentQueueIndex").toInt(), count = m_window->property("queueCount").toInt();
    return {{"title", track.value("title")}, {"artist", track.value("artist")}, {"album", track.value("album")},
        {"artwork", track.value("artwork")}, {"hasTrack", !track.isEmpty()}, {"canPlay", !track.isEmpty() || count > 0},
        {"canNext", count > 1 && (index + 1 < count || m_window->property("repeatMode").toInt() != 0)},
        {"canPrevious", count > 1 || !track.isEmpty()}, {"duration", m_player->duration() > 0 ? m_player->duration() : track.value("duration").toLongLong()}};
}
void WindowsMediaControls::command(const QString &name, const QVariant &value)
{
    if (!m_window || !m_player || !QStringList{"play", "pause", "stop", "next", "previous", "toggle", "seek"}.contains(name)) return;
    QVariant argument = value;
    if (name == "seek") {
        bool valid = false; const auto position = value.toLongLong(&valid);
        if (!valid || !m_player->isSeekable()) return;
        argument = qBound<qint64>(0, position, m_player->duration());
    }
    emit commandRequested(name, argument);
    QMetaObject::invokeMethod(m_window, "desktopCommand", Q_ARG(QVariant, name), Q_ARG(QVariant, argument));
    refresh();
}
void WindowsMediaControls::refresh()
{
#ifdef Q_OS_WIN
    if (!m_available || !m_native || !m_player) return;
    try {
        const auto data = playerProperties();
        auto controls = m_native->controls;
        const bool track = data.value("hasTrack").toBool();
        controls.IsEnabled(data.value("canPlay").toBool());
        controls.IsPlayEnabled(data.value("canPlay").toBool()); controls.IsPauseEnabled(track);
        controls.IsStopEnabled(track); controls.IsNextEnabled(data.value("canNext").toBool());
        controls.IsPreviousEnabled(data.value("canPrevious").toBool());
        const auto state = m_player->playbackState();
        controls.PlaybackStatus(state == QMediaPlayer::PlayingState ? Media::MediaPlaybackStatus::Playing
            : state == QMediaPlayer::PausedState ? Media::MediaPlaybackStatus::Paused : Media::MediaPlaybackStatus::Stopped);
        auto updater = controls.DisplayUpdater(); updater.Type(Media::MediaPlaybackType::Music);
        auto properties = updater.MusicProperties();
        properties.Title(winrt::hstring(data.value("title").toString().toStdWString()));
        properties.Artist(winrt::hstring(data.value("artist").toString().toStdWString()));
        properties.AlbumTitle(winrt::hstring(data.value("album").toString().toStdWString()));
        const auto artwork = data.value("artwork").toString();
        if (artwork != m_native->artwork) {
            m_native->artwork = artwork; const auto revision = ++m_native->artworkRevision;
            updater.Thumbnail(nullptr);
            const QUrl url(artwork);
            if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile()) {
                auto operation = Storage::StorageFile::GetFileFromPathAsync(winrt::hstring(QDir::toNativeSeparators(QFileInfo(url.toLocalFile()).absoluteFilePath()).toStdWString()));
                const QPointer<WindowsMediaControls> guard(this); auto *dispatcher = QCoreApplication::instance();
                operation.Completed([guard, dispatcher, revision](const auto &completed, winrt::Windows::Foundation::AsyncStatus status) {
                    if (status != winrt::Windows::Foundation::AsyncStatus::Completed) return;
                    try {
                        auto file = completed.GetResults();
                        QMetaObject::invokeMethod(dispatcher, [guard, revision, file] {
                            if (!guard || !guard->m_native || revision != guard->m_native->artworkRevision) return;
                            try {
                                auto updater = guard->m_native->controls.DisplayUpdater();
                                updater.Thumbnail(Streams::RandomAccessStreamReference::CreateFromFile(file)); updater.Update();
                            } catch (const winrt::hresult_error &) {}
                        }, Qt::QueuedConnection);
                    } catch (const winrt::hresult_error &) {}
                });
            }
        }
        updater.Update(); updateTimeline();
        if (state == QMediaPlayer::PlayingState) m_timeline.start(); else m_timeline.stop();
    } catch (const winrt::hresult_error &) { m_timeline.stop(); }
#endif
}
void WindowsMediaControls::updateTimeline()
{
#ifdef Q_OS_WIN
    if (!m_available || !m_native || !m_player) return;
    try {
        const auto duration = qMax<qint64>(0, m_player->duration());
        Media::SystemMediaTransportControlsTimelineProperties timeline;
        timeline.StartTime(winrt::Windows::Foundation::TimeSpan{0}); timeline.MinSeekTime(winrt::Windows::Foundation::TimeSpan{0});
        timeline.EndTime(winrt::Windows::Foundation::TimeSpan{duration * 10000});
        timeline.MaxSeekTime(winrt::Windows::Foundation::TimeSpan{duration * 10000});
        timeline.Position(winrt::Windows::Foundation::TimeSpan{qBound<qint64>(0, m_player->position(), duration) * 10000});
        m_native->controls.UpdateTimelineProperties(timeline);
    } catch (const winrt::hresult_error &) {}
#endif
}
