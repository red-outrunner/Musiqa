import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer 1.0

ColumnLayout {
    id: root
    required property var player
    Layout.fillWidth: true
    spacing: Theme.gap(10)
    Label { text: "Audio output"; color: Theme.text; font: Theme.controlFont }
    ComboBox {
        objectName: "outputDeviceBox"
        Layout.fillWidth: true
        model: root.player.outputDevices
        textRole: "name"
        valueRole: "id"
        currentIndex: {
            const devices = root.player.outputDevices;
            for (let i = 0; i < devices.length; ++i)
                if (devices[i].id === root.player.outputDeviceId) return i;
            return 0;
        }
        onActivated: function(index) { root.player.outputDeviceId = root.player.outputDevices[index].id; }
        font: Theme.controlFont
        Accessible.name: "Audio output device"
    }
    RowLayout {
        Layout.fillWidth: true
        Label { text: "Channels"; color: Theme.text; font: Theme.controlFont }
        ComboBox {
            objectName: "outputModeBox"
            Layout.fillWidth: true
            model: ["Stereo", "Mono"]
            currentIndex: root.player.outputMode === "mono" ? 1 : 0
            onActivated: function(index) { root.player.outputMode = index === 1 ? "mono" : "stereo"; }
            font: Theme.controlFont
            Accessible.name: "Mono or stereo output"
        }
    }
    Text {
        Layout.fillWidth: true
        text: "Uses shared system audio. EasyEffects can process Musica on Linux. On Windows, select an output configured for Equalizer APO or a virtual audio device. An unavailable selection falls back to the system default."
        wrapMode: Text.WordWrap; color: Theme.muted; font: Theme.controlFont
    }
    Label { text: "ReplayGain · loudness tags"; color: Theme.text; font: Theme.controlFont }
    ComboBox {
        objectName: "replayGainModeBox"
        Layout.fillWidth: true; font: Theme.controlFont
        model: ["Off", "Track", "Album"]
        currentIndex: ["off", "track", "album"].indexOf(root.player.replayGainMode)
        onActivated: function(index) { root.player.replayGainMode = ["off", "track", "album"][index]; }
        Accessible.name: "ReplayGain playback mode"
    }
    Text {
        Layout.fillWidth: true
        text: "Uses existing ReplayGain tags only. Album mode falls back to track gain when album gain is missing. Peak tags help prevent clipping. Untagged songs stay unchanged; no library analysis or scan is started."
        wrapMode: Text.WordWrap; color: Theme.muted; font: Theme.controlFont
    }
}
