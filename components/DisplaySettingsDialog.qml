import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer 1.0

Dialog {
    id: root
    objectName: "displaySettingsDialog"
    modal: true
    title: "Display settings"
    width: Math.min(Theme.dp(540), parent.width - Theme.dp(24))
    height: Math.min(implicitHeight, parent.height - Theme.dp(24))
    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    font: Theme.controlFont
    padding: Theme.gap(20)
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { color: "#202e24"; radius: Theme.dp(12); border.color: "#435b48" }
    contentItem: ScrollView {
        id: settingsScroll
        objectName: "displaySettingsScroll"
        contentWidth: availableWidth
        clip: true
        implicitHeight: settingsColumn.implicitHeight
        ColumnLayout {
            id: settingsColumn
            width: settingsScroll.availableWidth
            spacing: Theme.gap(14)
            Text {
                Layout.fillWidth: true
                text: "Use your computer’s font and display scaling. Changes apply immediately."
                color: "#bdd4c6"; wrapMode: Text.WordWrap
                font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(12)
            }
            Text {
                objectName: "systemFontDescription"
                Layout.fillWidth: true
                text: "System font: " + Theme.preferences.fontFamily + " · " + Math.round(Theme.preferences.fontPointSize * 10) / 10 + " pt"
                color: "#8ba392"; wrapMode: Text.WordWrap
                font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(11)
            }
            Label { text: "Layout density"; color: "#edf2ef"; font: Theme.controlFont }
            ComboBox {
                objectName: "displayDensityBox"
                Layout.fillWidth: true
                model: ["Automatic", "Compact", "Comfortable"]
                currentIndex: ["automatic", "compact", "comfortable"].indexOf(Theme.preferences.density)
                onActivated: Theme.preferences.density = ["automatic", "compact", "comfortable"][currentIndex]
                Accessible.name: "Layout density"
                font: Theme.controlFont
            }
            Text {
                Layout.fillWidth: true
                text: Theme.preferences.density === "automatic" ? "Fits the available window space automatically."
                    : Theme.preferences.density === "compact" ? "Less spacing and shorter rows, with the same readable text."
                    : "More room around controls and library rows."
                color: "#8ba392"; wrapMode: Text.WordWrap
                font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(11)
            }
            Label { text: "Text size · " + Math.round(Theme.preferences.textScale * 100) + "%"; color: "#edf2ef"; font: Theme.controlFont }
            Slider {
                objectName: "displayTextScaleSlider"
                Layout.fillWidth: true
                from: 100; to: 200; stepSize: 5
                value: Math.round(Theme.preferences.textScale * 100)
                onMoved: Theme.preferences.textScale = value / 100
                Accessible.name: "Text size as a percentage of the system font"
            }
            Text {
                objectName: "displayFontPreview"
                text: "Your music, easy to read."
                Layout.fillWidth: true
                wrapMode: Text.WordWrap; color: "#a7e8cb"
                font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(16)
            }
            ActionButton {
                objectName: "resetDisplaySettingsButton"
                text: "Use system defaults"; primary: false
                Layout.fillWidth: true
                onClicked: Theme.preferences.reset()
            }
        }
    }
    footer: DialogButtonBox {
        standardButtons: DialogButtonBox.Close
        font: Theme.controlFont
        onRejected: root.close()
    }
}
