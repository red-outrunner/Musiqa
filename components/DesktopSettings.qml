import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer 1.0

ColumnLayout {
    id: root
    required property var controller
    property bool monitoring: false
    readonly property var diagnostics: controller ? controller.appDiagnostics : null
    readonly property var environment: controller ? controller.desktopEnvironment : null
    spacing: Theme.gap(12)
    Layout.fillWidth: true
    Label { text: "Laptop and performance"; color: Theme.text; font: Theme.controlFont }
    Label { text: "Desktop controls: " + (root.controller ? root.controller.desktopControlsName : "Unavailable"); color: Theme.secondary; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true }
    Label { text: "Battery saver"; color: Theme.text; font: Theme.controlFont }
    ComboBox {
        objectName: "batterySaverModeBox"
        Layout.fillWidth: true
        model: ["Off", "On battery power", "Always"]
        currentIndex: Math.max(0, ["off", "battery", "always"].indexOf(Theme.preferences.batterySaverMode))
        onActivated: function(index) { Theme.preferences.batterySaverMode = ["off", "battery", "always"][index]; }
        font: Theme.controlFont; Accessible.name: "Battery saver mode"
    }
    Label {
        objectName: "batterySaverStatus"
        text: (Theme.preferences.batterySaverActive ? "Battery saver is active." : "Battery saver is inactive.")
            + (root.environment && root.environment.powerMonitoringAvailable ? (Theme.preferences.onBattery ? " Running on battery." : " External power connected.") : " Automatic power detection is unavailable in this session.")
        color: Theme.secondary; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true
        Accessible.role: Accessible.StaticText
    }
    Label { text: "Reduces decorative effects and display updates. Audio mixing and scrolling keep their normal timing."; color: Theme.muted; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true }
    CheckBox {
        objectName: "pauseOnDisconnectCheckBox"
        text: "Pause when the audio output disconnects"; checked: Theme.preferences.pauseOnDisconnect
        onToggled: Theme.preferences.pauseOnDisconnect = checked
        font: Theme.controlFont; palette.text: Theme.text; Layout.fillWidth: true
        contentItem: Text { text: parent.text; font: parent.font; color: Theme.text; wrapMode: Text.WordWrap; leftPadding: parent.indicator.width + parent.spacing; verticalAlignment: Text.AlignVCenter }
    }
    CheckBox {
        objectName: "resumeAfterSleepCheckBox"
        text: "Resume playback after waking"; checked: Theme.preferences.resumeAfterSleep
        onToggled: Theme.preferences.resumeAfterSleep = checked
        font: Theme.controlFont; palette.text: Theme.text; Layout.fillWidth: true
        contentItem: Text { text: parent.text; font: parent.font; color: Theme.text; wrapMode: Text.WordWrap; leftPadding: parent.indicator.width + parent.spacing; verticalAlignment: Text.AlignVCenter }
    }
    Label { text: "Resumes an already playing song only when its previous output is available. Headphone detection depends on the system audio backend."; color: Theme.muted; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true }
    Label {
        objectName: "processMeasurements"
        text: root.diagnostics ? "Startup: " + (root.diagnostics.startupMs >= 0 ? root.diagnostics.startupMs + " ms" : "measuring…")
            + "\nMemory: " + root.diagnostics.memoryMiB.toFixed(1) + " MiB"
            + "\nCPU: " + root.diagnostics.cpuPercent.toFixed(1) + "% (one CPU core = 100%)"
            + "\nLibrary: " + root.controller.libraryBackend.trackCount + " songs; " + root.controller.libraryBackend.residentTrackCount + " resident track records"
            : "Process measurements are available when running the application."
        color: Theme.secondary; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true
        Accessible.name: text
    }
    Label { text: "Measurements refresh only while this section is open and the window is visible. No measurements are uploaded."; color: Theme.muted; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true }
    Timer { interval: 2000; repeat: true; running: root.monitoring && root.visible && root.controller && root.controller.uiActive && !!root.diagnostics; triggeredOnStart: true; onTriggered: root.diagnostics.refresh() }
}
