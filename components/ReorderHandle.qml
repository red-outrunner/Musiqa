import QtQuick
import QtQml.Models
import MusicPlayer 1.0
import QtQuick.Controls

Item {
    id: root
    property string entryId: ""
    property string dragGroup: ""
    property string title: "Move song"
    required property Flickable view
    property Item delegateItem: null
    readonly property bool dragging: gripMouse.drag.active
    onDraggingChanged: {
        if (!delegateItem) return;
        if (dragging) delegateItem.DelegateModel.inPersistedItems = true;
        else {
            // Keep just the dragged delegate alive beyond the cache. Release
            // it after the drop handler has used its entry id and position.
            const item = delegateItem;
            Qt.callLater(function() { if (item) item.DelegateModel.inPersistedItems = false; });
        }
    }
    signal step(int direction)
    width: Theme.dp(24)
    height: Theme.dp(30)
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: "Reorder " + title + ". Drag, or use the up and down arrows."
    Keys.onUpPressed: step(-1)
    Keys.onDownPressed: step(1)
    Icon { anchors.centerIn: parent; width: Theme.dp(16); height: Theme.dp(16); name: "grip"; color: root.activeFocus || gripMouse.containsMouse ? Theme.accent : Theme.muted }
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
        width: Theme.dp(240)
        height: Theme.dp(36)
        z: 1000
        visible: gripMouse.drag.active
        radius: Theme.dp(6)
        color: Theme.selected
        border.color: Theme.accent
        opacity: 0.95
        Drag.active: gripMouse.drag.active
        Drag.source: root
        Drag.keys: [root.dragGroup]
        Drag.hotSpot.x: Theme.dp(10)
        Drag.hotSpot.y: Theme.dp(15)
        Text { anchors.fill: parent; anchors.margins: Theme.gap(9); text: root.title; color: Theme.text; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(11); elide: Text.ElideRight }
    }
    Timer {
        interval: 25
        repeat: true
        running: gripMouse.drag.active
        onTriggered: {
            const point = ghost.mapToItem(root.view, 10, 15);
            const movement = point.y < 36 ? -12 : point.y > root.view.height - 36 ? 12 : 0;
            const minimum = root.view.originY;
            const maximum = Math.max(minimum, minimum + root.view.contentHeight - root.view.height);
            root.view.contentY = Math.max(minimum, Math.min(maximum, root.view.contentY + movement));
        }
    }
    ToolTip.visible: gripMouse.containsMouse && !gripMouse.drag.active
    ToolTip.delay: 700
    ToolTip.text: "Drag to reorder · ↑ / ↓ when focused"
}
