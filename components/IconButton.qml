import QtQuick
import QtQuick.Controls

Button {
    id: root
    property string symbol: "music"
    property string label: ""
    property color glyphColor: checked ? "#a7e8cb" : "#b2bdbb"
    property color surfaceColor: "transparent"
    property bool filled: false
    implicitWidth: 36
    implicitHeight: 36
    padding: 8
    hoverEnabled: true
    opacity: enabled ? 1 : 0.32
    Accessible.name: label
    ToolTip.visible: hovered && label.length > 0
    ToolTip.text: label
    ToolTip.delay: 600
    background: Rectangle {
        radius: width / 2
        color: root.down ? "#394741" : root.hovered ? "#293632" : root.surfaceColor
        border.width: root.visualFocus ? 2 : 0
        border.color: "#a7e8cb"
        Behavior on color {
            ColorAnimation {
                duration: 120
            }
        }
    }
    contentItem: Icon {
        name: root.symbol
        color: root.glyphColor
        filled: root.filled
    }
}
