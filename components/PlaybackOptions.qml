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
    width: Math.min(Theme.dp(355), parent.width - Theme.gap(24))
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
        spacing: Theme.gap(12)
        Text { text: "Playback transitions"; color: Theme.text; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(17); font.bold: true }
        CheckBox { objectName: "gaplessCheckBox"; text: "Gapless playback"; checked: root.player.gaplessEnabled; onToggled: root.player.gaplessEnabled = checked; palette.text: Theme.text }
        CheckBox { objectName: "crossfadeCheckBox"; text: "Enable crossfade"; checked: root.player.crossfadeEnabled; onToggled: root.player.crossfadeEnabled = checked; palette.text: Theme.text }
        CheckBox { objectName: "crossfadeOnSkipCheckBox"; text: "Crossfade when skipping"; checked: root.player.crossfadeOnSkip; enabled: root.player.crossfadeEnabled && root.player.crossfadeSeconds > 0; onToggled: root.player.crossfadeOnSkip = checked; palette.text: Theme.text }
        Text { text: "Also fade into the next song when you press Next. Off keeps skipping immediate."; color: Theme.muted; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(10); wrapMode: Text.WordWrap; Layout.fillWidth: true }
        RowLayout {
            Layout.fillWidth: true
            Text { text: "Fade duration"; color: Theme.secondary; Layout.fillWidth: true; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(12)}
            SpinBox { objectName: "crossfadeSecondsBox"; from: 0; to: 70; value: Math.round(root.player.crossfadeSeconds); editable: true; enabled: root.player.crossfadeEnabled; onValueModified: root.player.crossfadeSeconds = value; Accessible.name: "Crossfade duration in seconds" }
            Text { text: "sec"; color: Theme.secondary; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(12)}
        }
        Slider { objectName: "crossfadeSecondsSlider"; Layout.fillWidth: true; from: 0; to: 70; stepSize: 1; value: root.player.crossfadeSeconds; enabled: root.player.crossfadeEnabled; onMoved: root.player.crossfadeSeconds = value; Accessible.name: "Crossfade seconds, 0 to 70" }
        Text { text: "Songs overlap for the chosen duration. Short tracks reduce the overlap. 0 seconds plays without a fade."; color: Theme.muted; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(10); wrapMode: Text.WordWrap; Layout.fillWidth: true }
        Text { text: "Settings are saved automatically."; color: Theme.accent; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(10)}
    }
    }
}
