import QtQuick
import MusicPlayer 1.0

// Touchpad pixel motion already contains OS momentum; preserve it directly.
// Mouse notches accumulate into a short animation instead of jumping whole rows.
WheelHandler {
    id: handler
    required property Flickable view
    parent: view
    target: null
    enabled: view.interactive && view.visible
    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
    property double lastWheelTime: 0
    property int wheelBurst: 0
    property int lastDirection: 0
    property real gestureMultiplier: 1.8
    property real wheelTarget: 0
    onEnabledChanged: if (!enabled) wheelAnimation.stop()
    property NumberAnimation smoothAnimation: NumberAnimation {
        id: wheelAnimation
        target: handler.view
        property: "contentY"
        duration: 100
        easing.type: Easing.OutCubic
    }
    onWheel: function(event) {
        if (view.contentWidth > view.width && ((event.modifiers & Qt.ShiftModifier) || Math.abs(event.pixelDelta.x) > Math.abs(event.pixelDelta.y))) {
            const distance = event.pixelDelta.x || event.pixelDelta.y || (event.angleDelta.x || event.angleDelta.y) / 120 * 144;
            view.contentX = Math.max(view.originX, Math.min(view.originX + view.contentWidth - view.width, view.contentX - distance));
            event.accepted = true; return;
        }
        const pixels = event.pixelDelta.y;
        const now = Date.now(), direction = Math.sign(pixels || event.angleDelta.y);
        wheelBurst = !pixels && direction === lastDirection && now - lastWheelTime < 160 ? Math.min(4, wheelBurst + 1) : 0;
        lastDirection = direction; lastWheelTime = now;
        const minimum = view.originY, maximum = Math.max(minimum, minimum + view.contentHeight - view.height);
        const distance = pixels ? pixels * gestureMultiplier :
            event.angleDelta.y / 120 * Math.max(144, view.height * 0.30) * (1 + wheelBurst * 0.2);
        if (!distance) { event.accepted = false; return; }
        if (view.flicking) view.cancelFlick();
        if (pixels || Math.abs(event.angleDelta.y) < 120) {
            wheelAnimation.stop();
            view.contentY = Math.max(minimum, Math.min(maximum, view.contentY - distance));
        } else {
            const start = wheelAnimation.running ? wheelTarget : view.contentY;
            wheelAnimation.stop();
            wheelTarget = Math.max(minimum, Math.min(maximum, start - distance));
            wheelAnimation.from = view.contentY; wheelAnimation.to = wheelTarget;
            wheelAnimation.start();
        }
        event.accepted = true;
    }
    property Connections dragConnections: Connections {
        target: handler.view
        function onDraggingChanged() { if (handler.view.dragging) wheelAnimation.stop(); }
        function onVisibleChanged() { if (!handler.view.visible) wheelAnimation.stop(); }
    }
}
