import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import MusicPlayer 1.0

Dialog {
    id: root
    objectName: "lyricsDialog"
    required property var player
    property var track: null
    property bool followCurrentSong: true
    readonly property url displaySource: followCurrentSong || !track ? player.source : track.fileUrl
    signal seekRequested(var track, real position)
    readonly property var document: player.lyrics
    readonly property var lyricLines: document.lines
    property bool followPlayback: true
    readonly property int currentLine: {
        if (!document.synchronized || !lyricLines.length || displaySource.toString() !== player.source.toString()) return -1;
        let low = 0, high = lyricLines.length - 1, found = -1;
        const position = player.position;
        while (low <= high) {
            const middle = Math.floor((low + high) / 2);
            if (lyricLines[middle].time <= position) { found = middle; low = middle + 1; }
            else high = middle - 1;
        }
        return found;
    }
    title: "Lyrics"
    modal: true
    width: Math.min(Theme.dp(640), parent.width - Theme.dp(24))
    height: Math.min(Theme.dp(650), parent.height - Theme.dp(24))
    x: (parent.width - width) / 2; y: (parent.height - height) / 2
    font: Theme.controlFont
    padding: Theme.gap(18)
    standardButtons: Dialog.Close
    background: Rectangle { color: Theme.surface; radius: Theme.dp(12); border.color: Theme.border }
    onOpened: { followPlayback = true; document.load(displaySource); }
    onCurrentLineChanged: {
        if (visible && followPlayback && currentLine >= 0) lyricsList.positionViewAtIndex(currentLine, ListView.Center);
    }
    Connections {
        target: root.player
        function onSourceChanged() { if (root.visible && root.followCurrentSong) { root.followPlayback = true; root.document.load(root.displaySource); } }
    }
    FileDialog {
        id: lyricsPicker
        title: "Choose local lyrics"
        nameFilters: ["Lyrics (*.lrc *.LRC *.txt *.TXT)", "All files (*)"]
        onAccepted: root.document.attachFile(selectedFile)
    }
    contentItem: ColumnLayout {
        spacing: Theme.gap(12)
        Flow {
            Layout.fillWidth: true
            spacing: Theme.gap(8)
            ActionButton { objectName: "attachLyricsButton"; text: "Open lyrics file"; primary: false; enabled: !!root.displaySource.toString(); onClicked: lyricsPicker.open() }
            ActionButton { text: "Reload"; primary: false; enabled: !!root.displaySource.toString() && !root.document.loading; onClicked: root.document.reload() }
            ActionButton { text: "Use song lyrics"; primary: false; visible: root.document.attached; onClicked: root.document.clearAttachment() }
            ActionButton { objectName: "followLyricsButton"; text: root.followPlayback ? "Following playback" : "Follow playback"; primary: root.followPlayback; visible: root.document.synchronized; onClicked: { root.followPlayback = true; if (root.currentLine >= 0) lyricsList.positionViewAtIndex(root.currentLine, ListView.Center); } }
        }
        Text {
            Layout.fillWidth: true
            text: root.document.loading ? "Loading local lyrics…" : root.document.error || root.document.sourceLabel
            visible: text.length > 0
            wrapMode: Text.WordWrap; color: root.document.error ? Theme.danger : Theme.muted; font: Theme.controlFont
        }
        Text {
            Layout.fillWidth: true
            visible: !root.document.loading && !root.document.error && !root.lyricLines.length
            text: "No local lyrics. Place a matching .lrc or .txt file beside the song, or open a lyrics file. Embedded lyrics are also supported."
            wrapMode: Text.WordWrap; color: Theme.secondary; font: Theme.controlFont
        }
        ListView {
            id: lyricsList
            objectName: "lyricsList"
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: root.lyricLines
            spacing: Theme.gap(8)
            maximumFlickVelocity: Theme.dp(6000)
            flickDeceleration: Theme.dp(2400)
            ScrollBar.vertical: ScrollBar {}
            onMovementStarted: root.followPlayback = false
            delegate: ItemDelegate {
                required property var modelData
                required property int index
                width: lyricsList.width
                padding: Theme.gap(10)
                implicitHeight: lyricText.implicitHeight + Theme.gap(20)
                hoverEnabled: root.document.synchronized
                background: Rectangle { radius: Theme.dp(8); color: index === root.currentLine ? Theme.selected : "transparent" }
                contentItem: Text {
                    id: lyricText
                    text: modelData.text || "♪"
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                    color: index === root.currentLine ? Theme.accent : Theme.text
                    font: Qt.font({family: Theme.preferences.fontFamily, pointSize: Theme.pointSize, weight: index === root.currentLine ? Font.DemiBold : Font.Normal})
                }
                onClicked: { if (root.document.synchronized) { root.seekRequested(root.followCurrentSong ? null : root.track, modelData.time); root.followPlayback = true; } }
                Accessible.name: modelData.text
            }
        }
        Text {
            visible: root.document.synchronized
            text: "Select a line to seek. Scrolling pauses automatic following."
            Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.muted; font: Theme.controlFont
        }
    }
}
