import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs as NativeDialogs
import MusicPlayer 1.0

Dialog {
    id: root
    objectName: "playlistMigrationDialog"
    required property var controller
    readonly property var migration: controller.libraryBackend.playlistMigration
    property url playlistFile: ""
    property url searchFolder: ""
    property int resolvingIndex: -1
    readonly property bool hasPreview: !!migration.summary.count
    title: "Import from another player"
    modal: true; font: Theme.controlFont
    width: Math.min(Theme.dp(790), parent.width - Theme.gap(24))
    height: Math.min(Theme.dp(650), parent.height - Theme.gap(24))
    x: (parent.width - width) / 2; y: (parent.height - height) / 2
    padding: Theme.gap(18)
    background: Rectangle { color: Theme.surface; radius: Theme.dp(12); border.color: Theme.border }
    onClosed: migration.cancel()
    function statusLabel(status) { return status === "ready" ? "Found" : status === "matched" ? "Relinked" : status === "ambiguous" ? "Multiple matches · choose file" : status === "unsupported" ? "Online entry · skipped" : "Missing · choose file"; }
    contentItem: ColumnLayout {
        spacing: Theme.gap(10)
        ScrollView {
            id: sourceScroll
            objectName: "migrationSourceScroll"
            visible: !root.hasPreview
            Layout.fillWidth: true; Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: sourceScroll.availableWidth
                spacing: Theme.gap(10)
        Label { text: "Export a playlist from your previous player, then preview and import its local songs here."; color: Theme.secondary; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        RowLayout {
            Layout.fillWidth: true
            ActionButton { objectName: "migrationChoosePlaylist"; text: "Choose playlist…"; primary: false; enabled: !root.migration.busy; onClicked: playlistPicker.open() }
            Label { text: root.playlistFile ? root.playlistFile.toString().split('/').pop() : "M3U · M3U8 · PLS · XSPF · WPL"; color: Theme.secondary; font: Theme.controlFont; elide: Text.ElideLeft; Layout.fillWidth: true; Layout.minimumWidth: 0 }
        }
        RowLayout {
            Layout.fillWidth: true
            ActionButton { objectName: "migrationChooseFolder"; text: "Find moved files in…"; primary: false; enabled: !root.migration.busy; onClicked: folderPicker.open() }
            Label { text: root.searchFolder ? root.searchFolder.toString() : "Optional music folder"; color: Theme.secondary; font: Theme.controlFont; elide: Text.ElideLeft; Layout.fillWidth: true; Layout.minimumWidth: 0 }
        }
        Label { text: "Only your chosen folder is searched, when you press Preview. Multiple matches stay unselected until you choose the correct file."; color: Theme.muted; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            }
        }
        TextField { id: playlistName; objectName: "migrationPlaylistName"; placeholderText: "New playlist name"; font: Theme.controlFont; Layout.fillWidth: true; maximumLength: 80; Accessible.name: "Imported playlist name" }
        RowLayout {
            Layout.fillWidth: true
            ActionButton { objectName: "migrationPreviewButton"; text: root.migration.busy ? "Finding files…" : "Preview"; enabled: !!root.playlistFile && !root.migration.busy; onClicked: { errorText.text = ""; root.migration.preview(root.playlistFile, root.searchFolder); } }
            ActionButton { text: "Change source"; primary: false; visible: root.hasPreview; onClicked: root.migration.clear() }
            BusyIndicator { running: root.migration.busy; visible: running; Layout.preferredWidth: Theme.dp(28); Layout.preferredHeight: Theme.dp(28); Accessible.name: "Finding playlist files" }
            ActionButton { text: "Cancel search"; primary: false; visible: root.migration.busy; onClicked: root.migration.cancel() }
        }
        Label { objectName: "migrationSummary"; text: root.migration.summary.count ? root.migration.summary.count + " entries · " + root.migration.selectedCount + " selected · " + ((root.migration.summary.missing || 0) + (root.migration.summary.ambiguous || 0)) + " need attention" : ""; color: Theme.secondary; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        ListView {
            id: previewList
            objectName: "migrationPreviewList"
            visible: root.hasPreview
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: Theme.dp(70)
            clip: true; reuseItems: true; model: root.migration.entries
            boundsBehavior: Flickable.StopAtBounds
            LibraryScrollHandler { view: previewList }
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: row
                required property int index
                required property var entry
                width: previewList.width; height: Theme.rowHeight(75)
                Accessible.name: entry.title + ", " + root.statusLabel(entry.status)
                Accessible.description: entry.fileUrl || entry.original
                onClicked: root.migration.select(index, !entry.selected)
                background: Rectangle { color: row.visualFocus ? Theme.selected : row.hovered ? Theme.hover : "transparent"; radius: Theme.dp(5); border.width: row.visualFocus ? 2 : 0; border.color: Theme.accent }
                contentItem: RowLayout {
                    spacing: Theme.gap(10)
                    CheckBox { checked: row.entry.selected; enabled: !!row.entry.fileUrl; onToggled: root.migration.select(row.index, checked); Accessible.name: "Import " + row.entry.title }
                    ColumnLayout {
                        Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: Theme.gap(2)
                        Label { text: row.entry.title; color: Theme.text; font: Theme.controlFont; elide: Text.ElideRight; Layout.fillWidth: true }
                        Label { text: root.statusLabel(row.entry.status); color: row.entry.fileUrl ? Theme.accent : Theme.warning; font: Theme.controlFont; elide: Text.ElideRight; Layout.fillWidth: true }
                        Label { text: row.entry.fileUrl || row.entry.original; color: Theme.muted; font.pointSize: Theme.textSize(10); elide: Text.ElideLeft; Layout.fillWidth: true }
                    }
                    IconButton { symbol: "folder"; label: "Choose file for " + row.entry.title; visible: row.entry.status !== "unsupported"; onClicked: { root.resolvingIndex = row.index; filePicker.open(); } }
                }
            }
        }
        Label { id: errorText; text: ""; visible: !!text; color: Theme.danger; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true; Accessible.role: Accessible.AlertMessage }
    }
    footer: DialogButtonBox {
        font: Theme.controlFont
        Button { text: "Close"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole; onClicked: root.close() }
        Button { objectName: "migrationImportButton"; text: "Import selected (" + root.migration.selectedCount + ")"; enabled: root.migration.selectedCount > 0 && !root.migration.busy && !!playlistName.text.trim(); DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole; onClicked: root.migration.importSelected(playlistName.text) }
    }
    Connections {
        target: root.migration
        function onChanged() { if (!playlistName.text && root.migration.summary.name) playlistName.text = root.migration.summary.name; }
        function onError(message) { errorText.text = message; }
        function onImported(id, name) { root.close(); root.controller.openCollection("Playlists", {id: id, name: name, kind: "playlist"}); root.controller.showToast("Playlist imported: " + name); }
    }
    Connections { target: root.controller.libraryBackend; function onImportError(message) { if (root.visible) errorText.text = message; } }
    NativeDialogs.FileDialog { id: playlistPicker; title: "Choose an exported playlist"; nameFilters: ["Playlists (*.m3u *.m3u8 *.pls *.xspf *.wpl)"]; onAccepted: { root.playlistFile = selectedFile; errorText.text = ""; playlistName.text = ""; } }
    NativeDialogs.FolderDialog { id: folderPicker; title: "Choose your music folder"; onAccepted: root.searchFolder = selectedFolder }
    NativeDialogs.FileDialog { id: filePicker; title: "Locate this song"; nameFilters: ["Audio files (*.mp3 *.flac *.ogg *.oga *.opus *.wav *.m4a *.aac *.aif *.aiff *.wma)"]; onAccepted: { if (!root.migration.resolve(root.resolvingIndex, selectedFile)) errorText.text = "Choose a readable local audio file."; } }
}
