import QtQuick
import MusicPlayer 1.0
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id: root
    font: Theme.controlFont
    property string symbol: ""
    property bool primary: true
    implicitHeight: Math.max(Theme.controlHeight(42), contentItem.implicitHeight + Theme.gap(16))
    leftPadding: Theme.gap(18)
    rightPadding: Theme.gap(18)
    hoverEnabled: true
    opacity: enabled ? 1 : 0.4
    background: Rectangle {
        radius: Theme.dp(21)
        color: root.primary ? (root.down ? Qt.darker(Theme.accent, 1.12) : root.hovered ? Qt.lighter(Theme.accent, 1.08) : Theme.accent) : (root.hovered ? Theme.hover : "transparent")
        border.width: root.primary && !root.visualFocus ? 0 : 1
        border.color: root.visualFocus ? Theme.accent : Theme.border
        Behavior on color {
            ColorAnimation {
                duration: Theme.preferences.reducedMotion ? 0 : 120
            }
        }
    }
    contentItem: RowLayout {
        spacing: Theme.gap(8)
        Icon {
            visible: root.symbol.length > 0
            name: root.symbol
            color: root.primary ? Theme.onAccent : Theme.text
            Layout.preferredWidth: Theme.dp(17)
            Layout.preferredHeight: Theme.dp(17)
        }
        Text {
            text: root.text
            color: root.primary ? Theme.onAccent : Theme.text
            font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(12)
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
        }
    }
}
