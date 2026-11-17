import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer 1.0

ColumnLayout {
    id: root
    required property var controller
    property var details: ({folders: [], path: "", parent: "", count: 0})
    signal folderRequested(var folder)
    spacing: Theme.gap(8)
    RowLayout {
        Layout.fillWidth: true
        ActionButton { objectName: "folderUpButton"; symbol: "back"; text: "Up"; primary: false; enabled: !!root.details.path; onClicked: root.controller.openFolderParent() }
        Text { text: root.details.path || "Music folders"; color: Theme.secondary; font: Theme.controlFont; elide: Text.ElideLeft; Layout.fillWidth: true; Layout.minimumWidth: 0 }
        IconButton {
            symbol: "pin"; label: root.controller.isPinned("folder", root.details.path) ? "Unpin folder" : "Pin folder"
            visible: !!root.details.path
            checked: visible && root.controller.isPinned("folder", root.details.path)
            onClicked: root.controller.togglePin("folder", root.details.path, root.controller.collectionName)
        }
    }
    ListView {
        id: folders
        objectName: "folderBrowserList"
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(contentHeight, Theme.controlHeight(44) * (root.controller.shortLayout ? 2 : 3))
        model: root.details.folders || []
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        LibraryScrollHandler { view: folders }
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        delegate: ItemDelegate {
            id: row
            required property var modelData
            required property int index
            objectName: "folderEntry" + index
            width: folders.width
            height: Theme.controlHeight(44)
            onClicked: root.folderRequested(row.modelData)
            background: Rectangle { radius: Theme.dp(7); color: row.hovered ? Theme.hover : "transparent" }
            contentItem: RowLayout {
                spacing: Theme.gap(12)
                Icon { name: "folder"; color: Theme.accent; Layout.preferredWidth: Theme.dp(22); Layout.preferredHeight: Theme.dp(22) }
                Text { text: row.modelData.name || row.modelData.id; color: Theme.text; font: Theme.controlFont; elide: Text.ElideRight; Layout.fillWidth: true; Layout.minimumWidth: 0 }
                Text { text: row.modelData.trackCount + " songs"; color: Theme.muted; font: Theme.controlFont }
                IconButton { symbol: "play"; label: "Play folder"; enabled: row.modelData.trackCount > 0; onClicked: root.controller.playGroup(row.modelData, "folder") }
                IconButton { symbol: "pin"; label: "Pin folder"; checked: root.controller.isPinned("folder", row.modelData.id); onClicked: root.controller.togglePin("folder", row.modelData.id, row.modelData.name) }
            }
        }
    }
    Text { visible: !(root.details.folders || []).length && !root.details.count; text: "No saved music in this folder. Add music or scan the library to discover files."; color: Theme.muted; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true }
}
