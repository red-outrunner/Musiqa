import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id: root
    property string symbol: ""
    property bool primary: true
    implicitHeight: 42
    leftPadding: 18
    rightPadding: 18
    hoverEnabled: true
    opacity: enabled ? 1 : 0.4
    background: Rectangle {
        radius: 21
        color: root.primary ? (root.down ? "#80c6a6" : root.hovered ? "#c1f4dd" : "#a7e8cb") : (root.hovered ? "#303c38" : "transparent")
        border.width: root.primary && !root.visualFocus ? 0 : 1
        border.color: root.visualFocus ? "#d8ffed" : "#4b6158"
        Behavior on color {
            ColorAnimation {
                duration: 120
            }
        }
    }
    contentItem: RowLayout {
        spacing: 8
        Icon {
            visible: root.symbol.length > 0
            name: root.symbol
            color: root.primary ? "#12251b" : "#e4eeea"
            Layout.preferredWidth: 17
            Layout.preferredHeight: 17
        }
        Text {
            text: root.text
            color: root.primary ? "#12251b" : "#e4eeea"
            font.pixelSize: 12
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
        }
    }
}
