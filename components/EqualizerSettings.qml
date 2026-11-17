import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer 1.0

ColumnLayout {
    id: root
    required property var player
    Layout.fillWidth: true
    spacing: Theme.gap(10)
    Label { text: "Equalizer"; color: Theme.text; font: Theme.controlFont }
    CheckBox {
        objectName: "equalizerEnabledCheckBox"
        text: "Enable equalizer"
        checked: root.player.equalizerEnabled
        onToggled: root.player.equalizerEnabled = checked
        font: Theme.controlFont
        Layout.fillWidth: true
        contentItem: Text { text: parent.text; font: parent.font; color: Theme.text; wrapMode: Text.WordWrap; leftPadding: parent.indicator.width + parent.spacing; verticalAlignment: Text.AlignVCenter }
    }
    ComboBox {
        objectName: "equalizerPresetBox"
        Layout.fillWidth: true
        enabled: root.player.equalizerEnabled
        model: ["Flat", "Bass", "Vocal", "Headphones", "Custom"]
        readonly property var presets: ["flat", "bass", "vocal", "headphones", "custom"]
        currentIndex: presets.indexOf(root.player.equalizerPreset)
        onActivated: function(index) { root.player.equalizerPreset = presets[index]; }
        font: Theme.controlFont
        Accessible.name: "Equalizer preset"
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.gap(10)
        Label { text: "Bands"; color: Theme.text; font: Theme.controlFont }
        ComboBox {
            id: countBox
            objectName: "equalizerBandCountBox"
            enabled: root.player.equalizerEnabled
            model: [5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]
            currentIndex: root.player.equalizerBandCount - 5
            onActivated: function(index) { root.player.equalizerBandCount = index + 5; }
            font: Theme.controlFont
            Accessible.name: "Number of equalizer bands"
            Layout.preferredWidth: Theme.dp(100)
        }
        Item { Layout.fillWidth: true }
        ActionButton {
            objectName: "equalizerResetButton"
            text: "Reset curve"
            enabled: root.player.equalizerEnabled
            onClicked: root.player.resetEqualizer()
        }
    }
    ScrollView {
        id: bandScroll
        Layout.fillWidth: true
        objectName: "equalizerBandsScroll"
        Layout.preferredHeight: Math.max(Theme.dp(235), Theme.controlHeight(30) * 2 + Theme.dp(185))
        clip: true
        enabled: root.player.equalizerEnabled
        opacity: enabled ? 1 : 0.5
        ScrollBar.vertical.policy: ScrollBar.AlwaysOff
        ScrollBar.horizontal.policy: contentWidth > availableWidth ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
        contentWidth: bandRow.implicitWidth
        contentHeight: availableHeight
        Row {
            id: bandRow
            height: parent.height
            spacing: Theme.gap(5)
            Repeater {
                // Keep slider delegates alive while their gains change.
                model: root.player.equalizerBandCount
                delegate: Column {
                    id: band
                    required property int index
                    readonly property var settings: root.player.equalizerBands[index] || ({ frequency: 0, gain: 0 })
                    width: Math.max(Theme.dp(52), Theme.controlHeight(30) * 1.8)
                    spacing: Theme.gap(4)
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: (band.settings.gain > 0 ? "+" : "") + Number(band.settings.gain).toFixed(1)
                        color: Theme.text; font: Theme.controlFont
                    }
                    Slider {
                        objectName: "equalizerGainSlider" + band.index
                        anchors.horizontalCenter: parent.horizontalCenter
                        orientation: Qt.Vertical
                        height: Theme.dp(155)
                        from: -12; to: 12; stepSize: 0.5
                        value: band.settings.gain
                        onMoved: root.player.setEqualizerBandGain(band.index, value)
                        Accessible.name: "Equalizer " + Math.round(band.settings.frequency) + " Hz gain"
                    }
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: band.settings.frequency >= 1000 ? (band.settings.frequency / 1000).toFixed(band.settings.frequency % 1000 < 1 ? 0 : 1) + "k" : Math.round(band.settings.frequency)
                        color: Theme.muted; font: Theme.controlFont
                    }
                }
            }
        }
    }
    Text {
        visible: bandScroll.contentWidth > bandScroll.availableWidth
        Layout.fillWidth: true
        text: "Scroll sideways to adjust all " + root.player.equalizerBandCount + " bands."
        wrapMode: Text.WordWrap; color: Theme.muted; font: Theme.controlFont
    }
    Text {
        Layout.fillWidth: true
        text: "Adjust any band to create a custom curve. Band counts keep its shape. Headroom reduces clipping; disable this equalizer when using external effects."
        wrapMode: Text.WordWrap; color: Theme.muted; font: Theme.controlFont
    }
}
