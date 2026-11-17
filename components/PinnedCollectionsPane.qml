import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer 1.0

Column {
    id: root
    required property var controller
    readonly property var pins: controller.libraryBackend.pinnedCollections
    visible: pins.length > 0
    spacing: Theme.gap(4)
    height: visible ? implicitHeight : 0
    Text { text: "PINNED"; visible: !root.controller.narrowLayout; color: Theme.muted; font: Theme.controlFont; padding: Theme.gap(10) }
    Repeater {
        model: root.pins
        delegate: RowLayout {
            required property var modelData
            required property int index
            width: root.width
            spacing: 0
            Button {
                id: pinButton
                objectName: "pinnedCollection" + parent.index
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                implicitHeight: Theme.controlHeight(34)
                hoverEnabled: true
                Accessible.name: parent.modelData.name
                ToolTip.visible: hovered; ToolTip.text: parent.modelData.name
                onClicked: root.controller.openPinned(parent.modelData)
                background: Rectangle { color: pinButton.hovered ? Theme.hover : "transparent"; radius: Theme.dp(7) }
                contentItem: RowLayout {
                    spacing: Theme.gap(8)
                    Icon { name: pinButton.parent.modelData.icon || "pin"; color: Theme.accent; Layout.preferredWidth: Theme.dp(18); Layout.preferredHeight: Theme.dp(18) }
                    Text { visible: !root.controller.narrowLayout; text: pinButton.parent.modelData.name; color: Theme.secondary; font: Theme.controlFont; elide: Text.ElideRight; Layout.fillWidth: true; Layout.minimumWidth: 0 }
                }
            }
            IconButton {
                visible: !root.controller.narrowLayout
                symbol: "close"; label: "Unpin " + parent.modelData.name
                Layout.preferredWidth: Theme.dp(24); Layout.preferredHeight: Theme.dp(28)
                onClicked: root.controller.libraryBackend.setCollectionPinned(parent.modelData.kind, parent.modelData.id, parent.modelData.name, false)
            }
        }
    }
}
