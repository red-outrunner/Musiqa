pragma Singleton
import QtQuick
import MusicPlayer 1.0

QtObject {
    id: root
    readonly property DisplaySettings preferences: DisplaySettings { objectName: "displayPreferences" }
    readonly property real pointSize: preferences.fontPointSize * preferences.textScale
    readonly property real systemPixels: preferences.fontPointSize * preferences.logicalDpi / 72
    readonly property real unitScale: Math.max(1, systemPixels / 13)
    readonly property real textPixels: systemPixels * preferences.textScale
    readonly property font controlFont: Qt.font({family: preferences.fontFamily, pointSize: root.pointSize})
    readonly property bool compact: preferences.density === "compact" || (preferences.density === "automatic"
        && (preferences.windowWidth < dp(1100) || preferences.windowHeight < dp(760)))
    function textSize(designSize) { return pointSize * Math.max(0.9, designSize / 12); }
    function dp(value) { return Math.round(value * unitScale); }
    function gap(value) { return Math.round(value * unitScale * (compact ? 0.86 : 1.1)); }
    function controlHeight(value) { return Math.ceil(Math.max(dp(value) * (compact ? 0.9 : 1.1), textPixels * 1.6 + gap(8))); }
    function rowHeight(value) { return Math.ceil(Math.max(dp(value) * (compact ? 0.9 : 1.1), textPixels * 2.8 + gap(12))); }
}
