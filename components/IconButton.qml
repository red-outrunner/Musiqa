import QtQuick
import MusicPlayer 1.0
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id: root
    font: Theme.controlFont
    property string symbol: "music"
    property string label: ""
    property color glyphColor: checked ? Theme.accent : Theme.secondary
    property color surfaceColor: "transparent"
    property bool filled: false
    implicitWidth: Theme.dp(Theme.preferences.largeControls ? 44 : 36)
    implicitHeight: Theme.dp(Theme.preferences.largeControls ? 44 : 36)
    Layout.minimumWidth: Theme.preferences.largeControls ? Theme.dp(44) : 0
    Layout.minimumHeight: Theme.preferences.largeControls ? Theme.dp(44) : 0
    focusPolicy: Qt.StrongFocus
    padding: Theme.gap(8)
    hoverEnabled: true
    opacity: enabled ? 1 : 0.32
    Accessible.name: label
    ToolTip.visible: (hovered || visualFocus) && label.length > 0
    ToolTip.text: label
    ToolTip.delay: 600
    background: Rectangle {
        radius: width / 2
        color: root.down ? Theme.hover : root.hovered ? Theme.hover : root.surfaceColor
        border.width: root.visualFocus ? 2 : 0
        border.color: Theme.accent
        Behavior on color {
            ColorAnimation {
                duration: Theme.preferences.reduceEffects ? 0 : 120
            }
        }
    }
    contentItem: Icon {
        name: root.symbol
        color: root.glyphColor
        filled: root.filled
    }
}
