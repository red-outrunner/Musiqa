import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer 1.0

Dialog {
    id: root
    required property var controller
    readonly property var library: controller.libraryBackend
    property string editingId: ""
    property string deletingId: ""
    objectName: "queueManagerDialog"
    title: "Queues · " + library.savedQueues.length + " / 4"
    modal: true
    anchors.centerIn: parent
    width: Math.min(Theme.dp(620), parent.width - Theme.gap(24))
    height: Math.min(implicitHeight, parent.height - Theme.gap(24))
    padding: Theme.gap(20)
    font: Theme.controlFont
    standardButtons: Dialog.Close
    background: Rectangle { color: Theme.surface; radius: Theme.dp(12); border.color: Theme.border }
    function begin() { controller.persistSession(); open(); }
    contentItem: ScrollView {
        id: scroll
        contentWidth: availableWidth
        implicitHeight: column.implicitHeight
        clip: true
        ColumnLayout {
            id: column
            width: scroll.availableWidth
            spacing: Theme.gap(14)
            Text { text: "New playback creates a named queue. At four queues, the oldest (top) queue is replaced."; color: Theme.secondary; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Repeater {
                model: root.library.savedQueues
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    implicitHeight: Math.max(Theme.rowHeight(65), entry.implicitHeight + Theme.gap(12))
                    radius: Theme.dp(8)
                    color: modelData.active ? Theme.selected : Theme.raised
                    RowLayout {
                        id: entry
                        anchors { fill: parent; margins: Theme.gap(8) }
                        RadioButton {
                            objectName: "activateSavedQueue" + row.index
                            checked: row.modelData.active
                            Accessible.name: "Switch to " + row.modelData.name
                            onClicked: root.controller.activateSavedQueue(row.modelData.id)
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Text { text: row.modelData.name; color: Theme.text; font: Theme.controlFont; elide: Text.ElideRight; Layout.fillWidth: true }
                            Text { text: row.modelData.trackCount + " songs" + (row.modelData.active ? " · Active" : ""); color: Theme.secondary; font: Theme.controlFont }
                        }
                        IconButton {
                            objectName: "renameSavedQueue" + row.index
                            symbol: "edit"; label: "Rename queue"
                            onClicked: { root.editingId = row.modelData.id; queueName.text = row.modelData.name; nameDialog.open(); }
                        }
                        IconButton {
                            objectName: "deleteSavedQueue" + row.index
                            symbol: "close"; label: "Delete queue"
                            enabled: root.library.savedQueues.length > 1
                            onClicked: { root.deletingId = row.modelData.id; deleteDialog.open(); }
                        }
                    }
                }
            }
            ActionButton {
                objectName: "savePlayingQueuePlaylistButton"; text: "Save playing queue as playlist"; primary: false
                Layout.fillWidth: true; enabled: root.controller.queueCount > 0
                onClicked: { root.close(); root.controller.saveQueueAsPlaylist(); }
            }
            ActionButton {
                objectName: "newSavedQueueButton"; text: "New queue"; symbol: "plus"
                Layout.fillWidth: true; enabled: root.library.savedQueues.length < 4
                onClicked: { root.editingId = ""; queueName.text = "Queue " + (root.library.savedQueues.length + 1); nameDialog.open(); }
            }
            ActionButton {
                objectName: "removeOtherQueuesButton"; text: "Remove other queues…"; primary: false
                Layout.fillWidth: true; enabled: root.library.savedQueues.length > 1
                onClicked: { root.deletingId = ""; deleteDialog.open(); }
            }
        }
    }
    Dialog {
        id: nameDialog
        objectName: "queueNameDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Theme.dp(400), parent.width - Theme.gap(24))
        title: root.editingId ? "Rename queue" : "New queue"
        modal: true
        font: Theme.controlFont
        standardButtons: Dialog.Ok | Dialog.Cancel
        background: Rectangle { color: Theme.surface; radius: Theme.dp(10); border.color: Theme.border }
        contentItem: TextField { id: queueName; objectName: "queueNameField"; maximumLength: 80; selectByMouse: true; placeholderText: "Queue name" }
        onAccepted: {
            if (root.editingId) root.library.renameQueue(root.editingId, queueName.text);
            else root.controller.createSavedQueue(queueName.text);
        }
    }
    Dialog {
        id: deleteDialog
        objectName: "deleteSavedQueueDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Theme.dp(420), parent.width - Theme.gap(24))
        height: Math.min(Theme.dp(200) + Theme.textPixels * 6, parent.height - Theme.gap(24))
        title: root.deletingId ? "Delete queue?" : "Remove other queues?"
        modal: true
        font: Theme.controlFont
        standardButtons: Dialog.Ok | Dialog.Cancel
        background: Rectangle { color: Theme.surface; radius: Theme.dp(10); border.color: Theme.border }
        contentItem: ScrollView {
            id: deleteScroll
            contentWidth: availableWidth
            clip: true
            Text { width: deleteScroll.availableWidth; text: "The queue entries will be removed. Your audio files and playlists are kept."; color: Theme.text; font: Theme.controlFont; wrapMode: Text.WordWrap }
        }
        onAccepted: {
            if (root.deletingId) root.controller.deleteSavedQueue(root.deletingId);
            else { root.controller.persistSession(); root.library.removeOtherQueues(); }
        }
    }
}
