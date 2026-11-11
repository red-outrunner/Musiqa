import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer 1.0

Rectangle {
    id: root
    required property var controller
    readonly property var player: controller.playbackBackend
    readonly property var library: controller.libraryBackend
    readonly property var track: controller.currentTrack
    readonly property bool stacked: width < Theme.dp(1100)
    signal transitionsRequested()
    signal sleepRequested()
    implicitHeight: Math.max(Theme.dp(104), footerGrid.implicitHeight + Theme.gap(24))
    height: implicitHeight
    color: "#18221b"
    Rectangle { width: parent.width; height: 1; color: "#314133" }
    GridLayout {
        id: footerGrid
        anchors.fill: parent
        anchors.margins: Theme.gap(12)
        columns: root.stacked ? 2 : 3
        columnSpacing: Theme.gap(14)
        rowSpacing: Theme.gap(6)
        RowLayout {
            Layout.row: 0; Layout.column: 0
            Layout.rowSpan: root.stacked ? 1 : 2
            Layout.fillWidth: true; Layout.minimumWidth: 0
            Layout.preferredWidth: Math.min(Theme.dp(300), root.width * 0.4)
            spacing: Theme.gap(10)
            Button {
                objectName: "nowPlayingCoverButton"
                Layout.preferredWidth: Theme.dp(44); Layout.preferredHeight: Theme.dp(44)
                padding: 0; enabled: !!root.track
                Accessible.name: "Open now playing"
                ToolTip.visible: hovered && enabled; ToolTip.text: "Open now playing"
                background: Item {}
                contentItem: CoverArt { radius: Theme.dp(7); artwork: root.track ? root.track.artwork : ""; artStyle: root.track ? root.track.artStyle : 0; opacity: root.track ? 1 : 0.65 }
                onClicked: root.controller.nowPlayingOpen = !root.controller.nowPlayingOpen
            }
            ColumnLayout {
                Layout.fillWidth: true; Layout.minimumWidth: 0
                spacing: Theme.gap(4)
                Text {
                    objectName: "footerSongTitle"
                    text: root.track ? root.track.title : "Find your next favorite"
                    color: "#e1ebe0"; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(12)
                    font.weight: Font.DemiBold; elide: Text.ElideRight
                    Layout.fillWidth: true; Layout.minimumWidth: 0
                }
                Button {
                    id: artist
                    objectName: "footerArtistLink"
                    Layout.fillWidth: true; Layout.minimumWidth: 0
                    padding: 0; enabled: !!root.track; hoverEnabled: true
                    Accessible.name: "Go to artist"
                    contentItem: Text {
                        text: root.track ? root.track.artist : "Your music starts here"
                        color: artist.hovered ? "#a7e8cb" : "#819c87"
                        font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(10)
                        font.underline: artist.hovered; elide: Text.ElideRight
                    }
                    background: Item {}
                    onClicked: root.controller.openArtist(root.track)
                }
            }
            IconButton {
                objectName: "footerFavoriteButton"; symbol: "heart"
                Layout.preferredWidth: Theme.dp(28); Layout.preferredHeight: Theme.dp(28)
                label: root.track && root.track.favorite ? "Remove from favorites" : "Add to favorites"
                enabled: !!root.track; filled: !!root.track && root.track.favorite
                glyphColor: filled ? "#ef4444" : "#819c87"
                onClicked: root.library.toggleFavorite(root.controller.currentUrl)
            }
        }
        RowLayout {
            Layout.row: 0; Layout.column: 1
            Layout.fillWidth: true; Layout.alignment: Qt.AlignHCenter
            spacing: Theme.gap(10)
            Item { Layout.fillWidth: true }
            IconButton {
                objectName: "shuffleButton"; symbol: "shuffle"
                Layout.preferredWidth: Theme.dp(28); Layout.preferredHeight: Theme.dp(28)
                label: root.controller.shuffleEnabled ? "Turn shuffle off" : "Turn shuffle on"
                checked: root.controller.shuffleEnabled
                onClicked: root.controller.shuffleEnabled = !root.controller.shuffleEnabled
            }
            IconButton {
                objectName: "previousButton"; symbol: "previous"; label: "Previous song"
                Layout.preferredWidth: Theme.dp(28); Layout.preferredHeight: Theme.dp(28)
                enabled: root.controller.queueUrls.length > 0; onClicked: root.controller.previousTrack()
            }
            IconButton {
                objectName: "playPauseButton"; symbol: root.controller.isPlaying ? "pause" : "play"
                label: root.controller.isPlaying ? "Pause" : "Play"
                Layout.preferredWidth: Theme.dp(42); Layout.preferredHeight: Theme.dp(42)
                surfaceColor: "#a7e8cb"; glyphColor: "#163021"
                onClicked: root.controller.togglePlayback()
            }
            IconButton {
                objectName: "nextButton"; symbol: "next"; label: "Next song"
                Layout.preferredWidth: Theme.dp(28); Layout.preferredHeight: Theme.dp(28)
                enabled: root.controller.queueUrls.length > 0; onClicked: root.controller.nextTrack(false)
            }
            IconButton {
                objectName: "repeatButton"; symbol: "repeat"
                Layout.preferredWidth: Theme.dp(28); Layout.preferredHeight: Theme.dp(28)
                label: root.controller.repeatMode === 0 ? "Repeat off" : root.controller.repeatMode === 1 ? "Repeat queue" : "Repeat song"
                checked: root.controller.repeatMode !== 0
                onClicked: root.controller.repeatMode = (root.controller.repeatMode + 1) % 3
                Text { visible: root.controller.repeatMode === 2; anchors.right: parent.right; anchors.top: parent.top; text: "1"; color: "#a7e8cb"; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(9) }
            }
            Item { Layout.fillWidth: true }
        }
        RowLayout {
            Layout.row: 1; Layout.column: root.stacked ? 0 : 1
            Layout.columnSpan: root.stacked ? 2 : 1
            Layout.fillWidth: true
            spacing: Theme.gap(8)
            Text {
                text: root.controller.formatTime(root.player.position); color: "#7a9581"
                font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(9)
                Layout.preferredWidth: Math.max(implicitWidth, Theme.dp(32))
            }
            Slider {
                id: progress
                objectName: "progressSlider"
                Layout.fillWidth: true; Layout.minimumWidth: Theme.dp(60)
                from: 0; to: Math.max(1, root.player.duration); value: root.player.position
                enabled: !!root.track && root.player.seekable
                onMoved: root.controller.seekPlayback(value)
                Accessible.name: "Song position"
                background: Rectangle {
                    x: progress.leftPadding; y: progress.topPadding + progress.availableHeight / 2 - 2
                    width: progress.availableWidth; height: 3; radius: 2; color: "#3a5140"
                    Rectangle { width: progress.visualPosition * parent.width; height: parent.height; radius: 2; color: "#a7e8cb" }
                }
                handle: Rectangle {
                    x: progress.leftPadding + progress.visualPosition * (progress.availableWidth - width)
                    y: progress.topPadding + (progress.availableHeight - height) / 2
                    width: Theme.dp(9); height: Theme.dp(9); radius: Theme.dp(5); color: "#d1f5dd"
                    visible: progress.hovered || progress.pressed || progress.visualFocus
                }
            }
            Text {
                text: root.controller.formatTime(root.player.duration); color: "#7a9581"
                font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(9)
                Layout.preferredWidth: Math.max(implicitWidth, Theme.dp(32))
            }
        }
        RowLayout {
            Layout.row: root.stacked ? 2 : 0; Layout.column: root.stacked ? 0 : 2
            Layout.rowSpan: root.stacked ? 1 : 2; Layout.columnSpan: root.stacked ? 2 : 1
            Layout.fillWidth: true; Layout.alignment: Qt.AlignVCenter
            spacing: Theme.gap(6)
            Item { Layout.fillWidth: root.stacked }
            IconButton {
                objectName: "queueButton"; symbol: "queue"; label: "Play queue"
                Layout.preferredWidth: Theme.dp(28); Layout.preferredHeight: Theme.dp(28)
                checked: root.controller.queueOpen; onClicked: root.controller.queueOpen = !root.controller.queueOpen
            }
            IconButton {
                objectName: "crossfadeSettingsButton"; symbol: "crossfade"
                Layout.preferredWidth: Theme.dp(28); Layout.preferredHeight: Theme.dp(28)
                label: "Gapless / crossfade · " + (root.player.crossfadeEnabled ? root.player.crossfadeSeconds + " seconds" : "Crossfade off")
                checked: root.player.crossfadeEnabled && root.player.crossfadeSeconds > 0
                onClicked: root.transitionsRequested()
            }
            IconButton {
                objectName: "sleepTimerButton"; symbol: "moon"
                Layout.preferredWidth: Theme.dp(28); Layout.preferredHeight: Theme.dp(28)
                label: root.player.sleepAfterTrack ? "Sleep after this song" : root.player.sleepRemainingMs > 0 ? "Sleep timer" : "Set sleep timer"
                checked: root.player.sleepAfterTrack || root.player.sleepRemainingMs > 0
                onClicked: root.sleepRequested()
            }
            IconButton {
                objectName: "miniPlayerButton"; symbol: "mini"; label: "Open mini player (Ctrl+M)"
                Layout.preferredWidth: Theme.dp(28); Layout.preferredHeight: Theme.dp(28)
                onClicked: root.controller.openMiniPlayer()
            }
            IconButton {
                objectName: "muteButton"; symbol: root.controller.muted || root.library.volume === 0 ? "mute" : "volume"
                Layout.preferredWidth: Theme.dp(28); Layout.preferredHeight: Theme.dp(28)
                label: root.controller.muted ? "Unmute" : "Mute"
                onClicked: root.controller.muted = !root.controller.muted
            }
            Slider {
                objectName: "volumeSlider"
                Layout.preferredWidth: Theme.dp(85)
                from: 0; to: 1; value: root.library.volume
                onMoved: { root.library.volume = value; root.controller.muted = false; }
                Accessible.name: "Volume"
            }
            Item { Layout.fillWidth: root.stacked }
        }
    }
}
