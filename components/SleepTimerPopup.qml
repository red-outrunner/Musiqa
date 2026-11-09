import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: root
    required property var player
    width: 320
    padding: 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { color: "#202e24"; radius: 12; border.color: "#435b48" }
    contentItem: ColumnLayout {
        spacing: 15
        Text { text: "Sleep timer"; color: "#edf2ef"; font.pixelSize: 17; font.bold: true }
        Text { objectName: "sleepTimerStatus"; text: root.player.sleepAfterTrack ? "Stops at the end of this song" : root.player.sleepRemainingMs > 0 ? "Stops in " + Math.floor(root.player.sleepRemainingMs / 60000) + ":" + String(Math.ceil(root.player.sleepRemainingMs / 1000) % 60).padStart(2, "0") : "Keep playing until you choose a stop time."; color: "#9eb3a5"; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        RowLayout {
            Text { text: "Minutes"; color: "#c4d5c8"; font.pixelSize: 12; Layout.fillWidth: true }
            SpinBox { id: minutes; objectName: "sleepMinutesBox"; from: 1; to: 720; value: 30; editable: true; Accessible.name: "Sleep duration in minutes" }
        }
        ActionButton { objectName: "startSleepTimerButton"; text: "Start timer"; symbol: "clock"; Layout.fillWidth: true; onClicked: { root.player.startSleepSeconds(minutes.value * 60); root.close(); } }
        ActionButton { objectName: "sleepAfterTrackButton"; text: "At the end of this song"; symbol: "music"; primary: false; Layout.fillWidth: true; enabled: !!root.player.source.toString(); onClicked: { root.player.sleepAtEndOfTrack(); root.close(); } }
        ActionButton { objectName: "cancelSleepTimerButton"; text: "Cancel sleep timer"; primary: false; Layout.fillWidth: true; enabled: root.player.sleepRemainingMs > 0 || root.player.sleepAfterTrack; onClicked: root.player.cancelSleep() }
        Text { text: "A timed stop keeps your position for resuming later. End-of-song mode finishes the current song before advancing or crossfading."; color: "#7e9585"; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
    }
}
