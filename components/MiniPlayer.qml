import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Window {
    id: root
    required property var track
    required property var player
    property bool playing: false
    property bool allowClose: false
    property bool positioned: false
    signal restoreRequested()
    signal toggleRequested()
    signal nextRequested()
    signal previousRequested()
    signal favoriteRequested()
    signal seekRequested(real position)
    title: "Musica · Mini player"
    width: 430
    height: 190
    minimumWidth: 370
    minimumHeight: 180
    maximumHeight: 210
    // An independent window stays available when the full player is minimized.
    // Tool windows can be hidden by the window manager along with their owner.
    transientParent: null
    flags: Qt.Window | Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowMinimizeButtonHint | Qt.WindowCloseButtonHint | Qt.WindowStaysOnTopHint
    color: "#15231b"
    visible: false
    function openOnScreen(mainWindow) {
        screen = mainWindow.screen;
        const margin = 24;
        const left = screen.virtualX + margin;
        const top = screen.virtualY + margin;
        const right = Math.max(left, screen.virtualX + screen.width - width - margin);
        const bottom = Math.max(top, screen.virtualY + screen.height - height - margin * 2);
        if (!positioned) {
            x = mainWindow.x + mainWindow.width - width - margin;
            y = mainWindow.y + mainWindow.height - height - margin * 2;
            positioned = true;
        }
        // Recover a window left on a disconnected monitor or outside the screen.
        x = Math.round(Math.max(left, Math.min(x, right)));
        y = Math.round(Math.max(top, Math.min(y, bottom)));
        showNormal();
        raise();
        requestActivate();
    }
    onClosing: function(event) {
        if (!allowClose) { event.accepted = false; restoreRequested(); }
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 15
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            CoverArt { Layout.preferredWidth: 60; Layout.preferredHeight: 60; radius: 7; artwork: root.track ? root.track.artwork || "" : ""; artStyle: root.track ? root.track.artStyle || 0 : 0 }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5
                Text { objectName: "miniPlayerTitle"; text: root.track ? root.track.title : "Your music, close by"; color: "#edf2ef"; font.pixelSize: 13; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                Text { text: root.track ? root.track.artist : "Play a song to get started"; color: "#91a697"; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                Text { text: root.track ? root.track.album : ""; color: "#6f8c78"; font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
            }
            IconButton { objectName: "miniFavoriteButton"; symbol: "heart"; filled: !!root.track && !!root.track.favorite; glyphColor: filled ? "#ef4444" : "#8ba392"; label: "Toggle favorite"; width: 29; height: 29; enabled: !!root.track; onClicked: root.favoriteRequested() }
            IconButton { objectName: "restoreMainPlayerButton"; symbol: "expand"; label: "Open full player (Ctrl+M)"; width: 29; height: 29; onClicked: root.restoreRequested() }
        }
        Slider {
            id: seek
            objectName: "miniSeekSlider"
            Layout.fillWidth: true
            Layout.preferredHeight: 20
            from: 0
            to: Math.max(1, root.player.duration)
            value: root.player.position
            enabled: !!root.track && root.player.seekable
            onMoved: root.seekRequested(value)
            Accessible.name: "Mini player song position"
            background: Rectangle {
                x: seek.leftPadding; y: seek.topPadding + seek.availableHeight / 2 - 2; width: seek.availableWidth; height: 3; radius: 2; color: "#344c3c"
                Rectangle { width: seek.visualPosition * parent.width; height: 3; radius: 2; color: "#a7e8cb" }
            }
            handle: Rectangle { x: seek.leftPadding + seek.visualPosition * (seek.availableWidth - width); y: seek.topPadding + (seek.availableHeight - height) / 2; width: 9; height: 9; radius: 5; color: "#d1f5dd" }
        }
        RowLayout {
            Layout.fillWidth: true
            Text { text: root.player.crossfading ? "Crossfading" : root.player.sleepAfterTrack ? "Sleep after this song" : root.player.sleepRemainingMs > 0 ? "Sleep in " + Math.ceil(root.player.sleepRemainingMs / 60000) + " min" : "ALWAYS CLOSE BY"; color: "#7e9a87"; font.pixelSize: 9; Layout.fillWidth: true }
            IconButton { objectName: "miniPreviousButton"; symbol: "previous"; label: "Previous song"; enabled: !!root.track; onClicked: root.previousRequested() }
            IconButton { objectName: "miniPlayPauseButton"; symbol: root.playing ? "pause" : "play"; label: root.playing ? "Pause" : "Play"; surfaceColor: "#a7e8cb"; glyphColor: "#163021"; enabled: !!root.track; onClicked: root.toggleRequested() }
            IconButton { objectName: "miniNextButton"; symbol: "next"; label: "Next song"; enabled: !!root.track; onClicked: root.nextRequested() }
        }
    }
}
