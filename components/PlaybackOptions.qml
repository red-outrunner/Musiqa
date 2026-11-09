import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: root
    required property var player
    width: 355
    padding: 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { color: "#202e24"; radius: 12; border.color: "#435b48" }
    contentItem: ColumnLayout {
        spacing: 12
        Text { text: "Playback transitions"; color: "#edf2ef"; font.pixelSize: 17; font.bold: true }
        CheckBox { objectName: "gaplessCheckBox"; text: "Gapless playback"; checked: root.player.gaplessEnabled; onToggled: root.player.gaplessEnabled = checked; palette.text: "#e5eee6" }
        CheckBox { objectName: "crossfadeCheckBox"; text: "Enable crossfade"; checked: root.player.crossfadeEnabled; onToggled: root.player.crossfadeEnabled = checked; palette.text: "#e5eee6" }
        RowLayout {
            Layout.fillWidth: true
            Text { text: "Fade duration"; color: "#9eb3a5"; Layout.fillWidth: true; font.pixelSize: 12 }
            SpinBox { objectName: "crossfadeSecondsBox"; from: 0; to: 70; value: Math.round(root.player.crossfadeSeconds); editable: true; enabled: root.player.crossfadeEnabled; onValueModified: root.player.crossfadeSeconds = value; Accessible.name: "Crossfade duration in seconds" }
            Text { text: "sec"; color: "#9eb3a5"; font.pixelSize: 12 }
        }
        Slider { objectName: "crossfadeSecondsSlider"; Layout.fillWidth: true; from: 0; to: 70; stepSize: 1; value: root.player.crossfadeSeconds; enabled: root.player.crossfadeEnabled; onMoved: root.player.crossfadeSeconds = value; Accessible.name: "Crossfade seconds, 0 to 70" }
        Text { text: "Songs overlap for the chosen duration. Short tracks reduce the overlap. 0 seconds plays without a fade."; color: "#7e9585"; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        Text { text: "Settings are saved automatically."; color: "#a7e8cb"; font.pixelSize: 10 }
    }
}
