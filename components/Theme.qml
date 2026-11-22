pragma Singleton
import QtQuick
import MusicPlayer 1.0

QtObject {
    id: root
    readonly property DisplaySettings preferences: DisplaySettings { objectName: "displayPreferences" }
    readonly property var cache: ArtworkCache
    readonly property bool light: preferences.theme === "light"
    readonly property color accent: preferences.accentColor
    readonly property color onAccent: light ? "#ffffff" : "#062c2d"
    readonly property color background: light ? "#f1f8f8" : "#0f191d"
    readonly property color sidebar: light ? "#e4f0f1" : "#0b1418"
    readonly property color surface: light ? "#ffffff" : "#17262b"
    readonly property color raised: light ? "#e5f1f2" : "#20373e"
    readonly property color input: light ? "#f2f8f9" : "#102127"
    readonly property color hover: Qt.tint(light ? "#e3f1f2" : "#20353b", Qt.rgba(accent.r, accent.g, accent.b, 0.08))
    readonly property color selected: Qt.tint(light ? "#c9e8e9" : "#24464c", Qt.rgba(accent.r, accent.g, accent.b, 0.16))
    readonly property color playing: Qt.tint(background, Qt.rgba(accent.r, accent.g, accent.b, 0.08))
    readonly property color text: preferences.highContrast ? (light ? "#000000" : "#ffffff") : (light ? "#142f35" : "#e5f2f4")
    readonly property color secondary: preferences.highContrast ? (light ? "#222222" : "#eeeeee") : (light ? "#3e626b" : "#b0cbd1")
    readonly property color muted: preferences.highContrast ? secondary : (light ? "#526f78" : "#8ba8b1")
    readonly property color border: preferences.highContrast ? secondary : (light ? "#a9c6cd" : "#3c5964")
    readonly property color nowPlayingTop: Qt.tint(light ? "#e0f3f3" : "#142f36", Qt.rgba(accent.r, accent.g, accent.b, 0.14))
    readonly property color danger: light ? "#a52735" : "#ffb0b8"
    readonly property color warning: light ? "#885118" : "#eec58f"
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
