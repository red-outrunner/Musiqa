import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer 1.0

ColumnLayout {
    spacing: Theme.gap(10)
    Layout.fillWidth: true
    Component.onCompleted: Theme.cache.refresh()
    Label { text: "Artwork cache and storage"; color: Theme.text; font: Theme.controlFont }
    Text { objectName: "cacheUsageLabel"; text: "Thumbnails on disk: " + (Theme.cache.diskBytes / 1048576).toFixed(1) + " MiB · Decoded images: " + (Theme.cache.memoryKiB / 1024).toFixed(1) + " MiB"; color: Theme.secondary; font: Theme.controlFont; Layout.fillWidth: true; wrapMode: Text.WordWrap }
    RowLayout {
        Layout.fillWidth: true
        Text { text: "Decoded artwork memory"; color: Theme.secondary; font: Theme.controlFont; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        SpinBox { objectName: "artworkMemoryLimitBox"; from: 8; to: 256; stepSize: 8; editable: true; value: Theme.cache.memoryLimitMiB; onValueModified: Theme.cache.memoryLimitMiB = value }
        Text { text: "MiB"; color: Theme.secondary; font: Theme.controlFont }
    }
    RowLayout {
        Layout.fillWidth: true
        Text { text: "Thumbnail storage limit"; color: Theme.secondary; font: Theme.controlFont; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        SpinBox { objectName: "artworkDiskLimitBox"; from: 32; to: 2048; stepSize: 32; editable: true; value: Theme.cache.diskLimitMiB; onValueModified: Theme.cache.diskLimitMiB = value }
        Text { text: "MiB"; color: Theme.secondary; font: Theme.controlFont }
    }
    ActionButton { objectName: "refreshCacheUsageButton"; text: "Refresh usage"; primary: false; Layout.fillWidth: true; enabled: !Theme.cache.busy; onClicked: Theme.cache.refresh() }
    ActionButton { objectName: "clearArtworkCacheButton"; text: "Clear thumbnails"; primary: false; Layout.fillWidth: true; enabled: !Theme.cache.busy; onClicked: Theme.cache.clearThumbnails() }
    ActionButton { objectName: "clearAbandonedAudioButton"; text: "Clean abandoned audio buffers"; primary: false; Layout.fillWidth: true; enabled: !Theme.cache.busy; onClicked: Theme.cache.cleanAbandonedAudio() }
    Text { text: "Thumbnails rebuild as you browse. Music files, embedded covers, saved library entries, and buffers belonging to running players are kept."; color: Theme.muted; font: Theme.controlFont; Layout.fillWidth: true; wrapMode: Text.WordWrap }
    BusyIndicator { visible: Theme.cache.busy; running: Theme.cache.busy; Layout.alignment: Qt.AlignHCenter }
}
