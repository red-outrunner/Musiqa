import QtQuick
import MusicPlayer 1.0
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: root
    modal: true
    dim: false
    focus: true
    font: Theme.controlFont
    required property var player
    width: Math.min(Theme.dp(320), parent.width - Theme.gap(24))
    padding: Theme.gap(20)
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { color: Theme.surface; radius: Theme.dp(12); border.color: Theme.border }
    height: Math.min(implicitHeight, parent.height - Theme.gap(24))
    contentItem: ScrollView {
        id: popupScroll
        contentWidth: availableWidth
        implicitHeight: popupColumn.implicitHeight
        clip: true
        ColumnLayout {
            id: popupColumn
            width: popupScroll.availableWidth
        spacing: Theme.gap(15)
        Text { text: "Sleep timer"; color: Theme.text; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(17); font.bold: true }
        Text { objectName: "sleepTimerStatus"; text: root.player.sleepAfterTrack ? "Stops at the end of this song" : root.player.sleepRemainingMs > 0 ? "Stops in " + Math.floor(root.player.sleepRemainingMs / 60000) + ":" + String(Math.ceil(root.player.sleepRemainingMs / 1000) % 60).padStart(2, "0") : "Keep playing until you choose a stop time."; color: Theme.secondary; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(11); wrapMode: Text.WordWrap; Layout.fillWidth: true }
        RowLayout {
            Text { text: "Minutes"; color: Theme.secondary; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(12); Layout.fillWidth: true }
            SpinBox { id: minutes; objectName: "sleepMinutesBox"; from: 1; to: 720; value: 30; editable: true; Accessible.name: "Sleep duration in minutes" }
        }
        ActionButton { objectName: "startSleepTimerButton"; text: "Start timer"; symbol: "clock"; Layout.fillWidth: true; onClicked: { root.player.startSleepSeconds(minutes.value * 60); root.close(); } }
        ActionButton { objectName: "sleepAfterTrackButton"; text: "At the end of this song"; symbol: "music"; primary: false; Layout.fillWidth: true; enabled: !!root.player.source.toString(); onClicked: { root.player.sleepAtEndOfTrack(); root.close(); } }
        ActionButton { objectName: "cancelSleepTimerButton"; text: "Cancel sleep timer"; primary: false; Layout.fillWidth: true; enabled: root.player.sleepRemainingMs > 0 || root.player.sleepAfterTrack; onClicked: root.player.cancelSleep() }
        Text { text: "A timed stop keeps your position for resuming later. End-of-song mode finishes the current song before advancing or crossfading."; color: Theme.muted; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(10); wrapMode: Text.WordWrap; Layout.fillWidth: true }
    }
    }
}
