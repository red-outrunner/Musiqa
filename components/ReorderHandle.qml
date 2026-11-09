import QtQuick
import QtQuick.Controls

Item {
    id: root
    property string entryId: ""
    property string dragGroup: ""
    property string title: "Move song"
    required property Flickable view
    readonly property bool dragging: gripMouse.drag.active
    signal step(int direction)
    width: 24
    height: 30
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: "Reorder " + title + ". Drag, or use the up and down arrows."
    Keys.onUpPressed: step(-1)
    Keys.onDownPressed: step(1)
    Icon { anchors.centerIn: parent; width: 16; height: 16; name: "grip"; color: root.activeFocus || gripMouse.containsMouse ? "#a7e8cb" : "#607b69" }
    MouseArea {
        id: gripMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: drag.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        drag.target: ghost
        drag.axis: Drag.YAxis
        drag.threshold: 5
        drag.smoothed: false
        preventStealing: true
        onPressed: {
            root.forceActiveFocus();
            const point = root.mapToItem(ghost.parent, 0, 0);
            ghost.x = point.x;
            ghost.y = point.y;
        }
        onReleased: ghost.Drag.drop()
        onCanceled: ghost.Drag.cancel()
    }
    Rectangle {
        id: ghost
        parent: root.Overlay.overlay
        width: 240
        height: 36
        z: 1000
        visible: gripMouse.drag.active
        radius: 6
        color: "#344e3b"
        border.color: "#a7e8cb"
        opacity: 0.95
        Drag.active: gripMouse.drag.active
        Drag.source: root
        Drag.keys: [root.dragGroup]
        Drag.hotSpot.x: 10
        Drag.hotSpot.y: 15
        Text { anchors.fill: parent; anchors.margins: 9; text: root.title; color: "#e5eee6"; font.pixelSize: 11; elide: Text.ElideRight }
    }
    Timer {
        interval: 25
        repeat: true
        running: gripMouse.drag.active
        onTriggered: {
            const point = ghost.mapToItem(root.view, 10, 15);
            const movement = point.y < 36 ? -12 : point.y > root.view.height - 36 ? 12 : 0;
            root.view.contentY = Math.max(0, Math.min(Math.max(0, root.view.contentHeight - root.view.height), root.view.contentY + movement));
        }
    }
    ToolTip.visible: gripMouse.containsMouse && !gripMouse.drag.active
    ToolTip.delay: 700
    ToolTip.text: "Drag to reorder · ↑ / ↓ when focused"
}
