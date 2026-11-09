import QtQuick

// High-resolution gestures apply immediately; rapid mouse notches accelerate
// through large collections without animating through every intermediate row.
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
    onWheel: function (event) {
        const pixels = event.pixelDelta.y;
        const now = Date.now();
        const direction = Math.sign(pixels || event.angleDelta.y);
        wheelBurst = !pixels && direction === lastDirection && now - lastWheelTime < 160 ? Math.min(6, wheelBurst + 1) : 0;
        lastDirection = direction;
        lastWheelTime = now;
        const step = Math.max(144, view.height * 0.30);
        const distance = pixels ? pixels * gestureMultiplier : event.angleDelta.y / 120 * step * (1 + wheelBurst * 0.35);
        if (!distance) {
            event.accepted = false;
            return;
        }
        if (view.flicking) view.cancelFlick();
        const maximum = Math.max(view.originY, view.originY + view.contentHeight - view.height);
        view.contentY = Math.max(view.originY, Math.min(maximum, view.contentY - distance));
        event.accepted = true;
    }
}
