import QtQuick
import MusicPlayer 1.0
import QtQuick.Controls
import QtQuick.Layouts
import "TimeFormat.js" as TimeFormat

Rectangle {
    id: root
    property var track: null
    property bool playing: false
    property bool shuffled: false
    property int repeatMode: 0
    property real position: 0
    property real duration: 0
    property real volume: 0.65
    property bool muted: false
    property bool seekable: false
    property bool fullscreen: false
    property bool queueOpen: false
    property alias queueButtonItem: nowPlayingQueueControl
    signal fullscreenRequested
    signal shareSongRequested
    signal shareScreenshotRequested
    signal closeRequested
    signal artistRequested(var credit)
    signal albumRequested
    signal playPauseRequested
    signal previousRequested
    signal nextRequested
    signal shuffleRequested
    signal repeatRequested
    signal favoriteRequested
    signal seekRequested(real position)
    signal volumeRequested(real volume)
    signal muteRequested
    signal queueRequested
    signal menuRequested(Item anchor)
    signal lyricsRequested()
    signal bookmarksRequested()
    function time(ms) {
        return TimeFormat.duration(ms);
    }
    gradient: Gradient {
        GradientStop {
            position: 0
            color: Theme.nowPlayingTop
        }
        GradientStop {
            position: 0.75
            color: Theme.background
        }
        GradientStop {
            position: 1
            color: Theme.background
        }
    }
    // A painted Rectangle doesn't consume input. Keep this behind the visible
    // controls so artwork and empty space cannot activate covered library rows.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        preventStealing: true
        onPressed: function(mouse) { mouse.accepted = true; }
        onWheel: function(event) { event.accepted = true; }
    }
    RowLayout {
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            margins: Theme.gap(28)
        }
        spacing: Theme.gap(12)
        IconButton {
            objectName: "closeNowPlayingButton"
            symbol: "back"
            label: "Back (Alt+Left)"
            onClicked: root.closeRequested()
        }
        ColumnLayout {
            spacing: Theme.gap(5)
            Text {
                text: "NOW PLAYING"
                font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(10)
                font.weight: Font.DemiBold
                font.letterSpacing: 2
                color: Theme.accent
            }
            Text {
                text: root.track ? root.track.album : ""
                font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(12)
                color: Theme.secondary
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Layout.fillWidth: true
        }
        IconButton {
            objectName: "nowPlayingFullscreenButton"
            symbol: root.fullscreen ? "collapse" : "expand"
            label: (root.fullscreen ? "Exit fullscreen" : "Fullscreen Now Playing") + (Theme.preferences.shortcuts.fullscreen ? " (" + Theme.preferences.shortcuts.fullscreen + ")" : "")
            checked: root.fullscreen
            onClicked: root.fullscreenRequested()
        }
        IconButton {
            objectName: "nowPlayingShareButton"
            symbol: "share"; label: "Share song or screenshot"
            enabled: !!root.track
            onClicked: shareOptions.popup()
            Menu {
                id: shareOptions
                font: Theme.controlFont
                palette.window: Theme.surface; palette.text: Theme.text; palette.windowText: Theme.text
                palette.highlight: Theme.accent; palette.highlightedText: Theme.onAccent
                MenuItem { objectName: "nowPlayingShareSong"; text: "Share audio file…"; onTriggered: root.shareSongRequested() }
                MenuItem { objectName: "nowPlayingShareScreenshot"; text: "Share screenshot…"; onTriggered: root.shareScreenshotRequested() }
            }
        }
        IconButton {
            id: nowPlayingQueueControl
            objectName: "nowPlayingQueueButton"
            symbol: "queue"
            label: root.queueOpen ? "Close play queue" : "Open play queue"
            checked: root.queueOpen
            onClicked: root.queueRequested()
        }
    }
    Flickable {
        id: detailsScroll
        objectName: "nowPlayingScroll"
        anchors {
            fill: parent
            leftMargin: Theme.gap(24); rightMargin: Theme.gap(24)
            topMargin: Theme.controlHeight(80); bottomMargin: Theme.gap(20)
        }
        clip: true
        contentWidth: width
        contentHeight: nowPlayingContent.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        LibraryScrollHandler { view: detailsScroll }
        ScrollBar.vertical: ScrollBar {}
    RowLayout {
        id: nowPlayingContent
        width: detailsScroll.width

        spacing: Theme.gap(root.width < Theme.dp(850) ? 20 : 32)
        CoverArt {
            Layout.preferredWidth: Math.min(Theme.dp(430), root.width * 0.36, Math.max(Theme.dp(110), root.height - Theme.dp(160)))
            Layout.preferredHeight: Layout.preferredWidth
            Layout.alignment: Qt.AlignVCenter
            artwork: root.track ? root.track.artwork : ""
            artStyle: root.track ? root.track.artStyle : 0
            radius: Theme.dp(14)
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.maximumWidth: Theme.dp(520)
            Layout.alignment: Qt.AlignVCenter
            spacing: Theme.gap(root.height < Theme.dp(600) ? 8 : 17)
            Text {
                text: root.track ? root.track.format + "  ·  LOCAL AUDIO" : "LOCAL AUDIO"
                color: Theme.muted
                font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(9)
                font.letterSpacing: 1.6
            }
            RowLayout {
                Layout.fillWidth: true
                Text {
                    id: songTitle
                    objectName: "nowPlayingTitle"
                    text: root.track ? root.track.title : "Nothing playing yet"
                    Layout.fillWidth: true
                    // Keep a two-line slot so titles never shift the controls below.
                    Layout.preferredHeight: Math.ceil(titleFontMetrics.height) * maximumLineCount
                    Layout.minimumHeight: Layout.preferredHeight
                    Layout.maximumHeight: Layout.preferredHeight
                    color: Theme.text
                    font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(root.height < Theme.dp(600) ? 22 : 36)
                    font.weight: Font.DemiBold
                    font.letterSpacing: -0.8
                    verticalAlignment: Text.AlignVCenter
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    FontMetrics {
                        id: titleFontMetrics
                        font: songTitle.font
                    }
                }
                IconButton {
                    objectName: "nowPlayingFavoriteButton"
                    symbol: "heart"
                    label: root.track && root.track.favorite ? "Remove from favorites" : "Add to favorites"
                    glyphColor: root.track && root.track.favorite ? "#ef4444" : Theme.muted
                    filled: !!root.track && root.track.favorite
                    enabled: !!root.track
                    onClicked: root.favoriteRequested()
                }
                IconButton {
                    id: moreButton
                    objectName: "nowPlayingMoreButton"
                    symbol: "more"
                    label: "Song options"
                    enabled: !!root.track
                    onClicked: root.menuRequested(moreButton)
                }
            }
            ArtistLinks {
                objectName: "nowPlayingArtistLink"
                Layout.fillWidth: true
                track: root.track
                placeholder: "Choose a song from your library"
                color: Theme.accent
                font.pointSize: Theme.textSize(root.height < Theme.dp(600) ? 14 : 21)
                onArtistChosen: function(credit) { root.artistRequested(credit); }
            }
            Button {
                id: albumLink
                objectName: "nowPlayingAlbumLink"
                Layout.fillWidth: true
                padding: Theme.gap(0)
                hoverEnabled: true
                enabled: !!root.track
                Accessible.name: "Go to album " + (root.track ? root.track.album : "")
                contentItem: Text {
                    text: root.track ? "From  " + root.track.album : ""
                    color: albumLink.hovered ? Theme.secondary : Theme.muted
                    font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(12)
                    font.underline: albumLink.hovered
                    elide: Text.ElideRight
                }
                background: Item {}
                onClicked: root.albumRequested()
            }
            RowLayout {
                Layout.fillWidth: true
                ActionButton { objectName: "nowPlayingLyricsButton"; text: "Lyrics"; symbol: "lyrics"; primary: false; enabled: !!root.track; onClicked: root.lyricsRequested() }
                ActionButton { objectName: "nowPlayingBookmarksButton"; text: "Bookmarks"; symbol: "bookmark"; primary: false; enabled: !!root.track; onClicked: root.bookmarksRequested() }
                Item { Layout.fillWidth: true }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: root.height < 600 ? 1 : 12
                spacing: Theme.gap(1)
                Slider {
                    id: seekSlider
                    objectName: "nowPlayingSeekSlider"
                    Layout.fillWidth: true
                    from: 0
                    to: Math.max(1, root.duration)
                    value: root.position
                    enabled: root.seekable
                    Accessible.name: "Playback position"
                    onMoved: root.seekRequested(value)
                    background: Rectangle {
                        x: seekSlider.leftPadding
                        y: seekSlider.topPadding + seekSlider.availableHeight / 2 - 2
                        width: seekSlider.availableWidth
                        height: Theme.dp(4)
                        radius: Theme.dp(2)
                        color: Theme.border
                        Rectangle {
                            width: seekSlider.visualPosition * parent.width
                            height: parent.height
                            radius: Theme.dp(2)
                            color: Theme.accent
                        }
                    }
                    handle: Rectangle {
                        x: seekSlider.leftPadding + seekSlider.visualPosition * (seekSlider.availableWidth - width)
                        y: seekSlider.topPadding + (seekSlider.availableHeight - height) / 2
                        width: Theme.dp(10)
                        height: Theme.dp(10)
                        radius: Theme.dp(5)
                        color: Theme.accent
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: root.time(root.position)
                        color: Theme.muted
                        font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(10)
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    Text {
                        text: root.time(root.duration)
                        color: Theme.muted
                        font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(10)
                    }
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: root.width < 850 ? 10 : 18
                IconButton {
                    symbol: "shuffle"
                    label: "Toggle shuffle"
                    checkable: true
                    checked: root.shuffled
                    onClicked: root.shuffleRequested()
                }
                IconButton {
                    symbol: "previous"
                    label: "Previous track"
                    onClicked: root.previousRequested()
                    enabled: !!root.track
                }
                IconButton {
                    objectName: "nowPlayingPlayPauseButton"
                    symbol: root.playing ? "pause" : "play"
                    label: root.playing ? "Pause" : "Play"
                    implicitWidth: Theme.dp(58)
                    implicitHeight: Theme.dp(58)
                    padding: Theme.gap(17)
                    surfaceColor: Theme.accent
                    glyphColor: Theme.onAccent
                    enabled: !!root.track
                    onClicked: root.playPauseRequested()
                }
                IconButton {
                    symbol: "next"
                    label: "Next track"
                    enabled: !!root.track
                    onClicked: root.nextRequested()
                }
                Item {
                    Layout.preferredWidth: Theme.dp(36)
                    Layout.preferredHeight: Theme.dp(36)
                    IconButton {
                        anchors.fill: parent
                        symbol: "repeat"
                        label: root.repeatMode === 0 ? "Repeat off" : root.repeatMode === 1 ? "Repeat all" : "Repeat one"
                        checkable: true
                        checked: root.repeatMode > 0
                        onClicked: root.repeatRequested()
                    }
                    Text {
                        anchors {
                            top: parent.top
                            right: parent.right
                        }
                        text: "1"
                        color: Theme.accent
                        font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(9)
                        visible: root.repeatMode === 2
                    }
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: Theme.gap(12)
                IconButton {
                    symbol: root.muted || root.volume === 0 ? "mute" : "volume"
                    label: root.muted ? "Unmute" : "Mute"
                    onClicked: root.muteRequested()
                }
                Slider {
                    id: volumeSlider
                    Layout.preferredWidth: Math.min(155, root.width * 0.17)
                    from: 0
                    to: 1
                    value: root.volume
                    Accessible.name: "Volume"
                    onMoved: root.volumeRequested(value)
                    background: Rectangle {
                        x: volumeSlider.leftPadding
                        y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - 2
                        width: volumeSlider.availableWidth
                        height: Theme.dp(3)
                        radius: Theme.dp(2)
                        color: Theme.border
                        Rectangle {
                            width: volumeSlider.visualPosition * parent.width
                            height: parent.height
                            radius: Theme.dp(2)
                            color: Theme.accent
                        }
                    }
                    handle: Rectangle {
                        x: volumeSlider.leftPadding + volumeSlider.visualPosition * (volumeSlider.availableWidth - width)
                        y: volumeSlider.topPadding + (volumeSlider.availableHeight - height) / 2
                        width: Theme.dp(8)
                        height: Theme.dp(8)
                        radius: Theme.dp(4)
                        color: Theme.accent
                        visible: volumeSlider.hovered || volumeSlider.pressed
                    }
                }
            }
        }
    }
    }
}
