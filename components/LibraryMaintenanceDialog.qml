import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs as NativeDialogs
import MusicPlayer 1.0

Dialog {
    id: root
    required property var controller
    readonly property var library: controller.libraryBackend
    readonly property var maintenance: library.maintenance
    readonly property bool busy: maintenance.busy
    property var repairPreview: ({})
    property var backupPreview: ({})
    property string message: ""
    property bool messageIsError: false
    property string keptUrl: ""
    property var duplicateUrls: []
    objectName: "libraryMaintenanceDialog"
    title: "Library tools"
    modal: true
    anchors.centerIn: parent
    width: Math.min(Theme.dp(790), parent.width - Theme.gap(24))
    height: Math.min(Theme.dp(700), parent.height - Theme.gap(24))
    padding: Theme.gap(18)
    font: Theme.controlFont
    standardButtons: Dialog.Close
    background: Rectangle { color: Theme.surface; radius: Theme.dp(12); border.color: Theme.border }
    Connections {
        target: root.maintenance
        function onError(text) { root.message = text; root.messageIsError = true; }
        function onCompleted(text) { root.message = text; root.messageIsError = false; }
    }
    function begin(page) { tabs.currentIndex = page === "backup" ? 1 : page === "duplicates" ? 2 : 0; open(); }
    contentItem: ColumnLayout {
        spacing: Theme.gap(12)
        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton { objectName: "repairFilesTab"; text: "Moved files"; font: Theme.controlFont }
            TabButton { objectName: "backupLibraryTab"; text: "Backup"; font: Theme.controlFont }
            TabButton { objectName: "duplicateFilesTab"; text: "Duplicates"; font: Theme.controlFont }
        }
        StackLayout {
            currentIndex: tabs.currentIndex
            Layout.fillWidth: true; Layout.fillHeight: true
            ScrollView {
                id: repairScroll
                contentWidth: availableWidth; clip: true
                ColumnLayout {
                    width: repairScroll.availableWidth
                    spacing: Theme.gap(12)
                    Text { text: "Relink a moved folder using its relative file paths and saved file sizes. Favorites, playlists, queues, bookmarks, and listening history follow the repaired songs. Audio files are not moved or edited."; color: Theme.secondary; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Label { text: "Old folder"; color: Theme.text; font: Theme.controlFont }
                    ComboBox {
                        id: oldFolder
                        objectName: "repairOldFolder"
                        model: root.library.folders
                        editable: true; font: Theme.controlFont; Layout.fillWidth: true
                        onEditTextChanged: root.repairPreview = ({})
                    }
                    Text { text: "Type the old path if that folder no longer exists."; color: Theme.secondary; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Label { text: "New folder"; color: Theme.text; font: Theme.controlFont }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField { id: newFolder; objectName: "repairNewFolder"; placeholderText: "/path/to/moved/music"; selectByMouse: true; font: Theme.controlFont; Layout.fillWidth: true; onTextEdited: root.repairPreview = ({}) }
                        ActionButton { text: "Browse…"; primary: false; onClicked: chooseFolder.open() }
                    }
                    ActionButton {
                        objectName: "previewMovedFilesButton"
                        text: "Preview repair"; enabled: !root.busy && !root.library.scanning && !root.library.metadataBusy
                        onClicked: {
                            root.controller.persistSession();
                            root.repairPreview = root.maintenance.previewRelink(oldFolder.editText, newFolder.text);
                            if (root.repairPreview.error) { root.message = root.repairPreview.error; root.messageIsError = true; }
                        }
                    }
                    Text {
                        visible: root.repairPreview.matches !== undefined
                        text: root.repairPreview.matches ? root.repairPreview.matches.length + " songs match · " + root.repairPreview.skipped.length + " unmatched" : ""
                        color: Theme.text; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                    Repeater {
                        model: root.repairPreview.matches ? root.repairPreview.matches.slice(0, 30) : []
                        delegate: Text { required property var modelData; text: modelData.relative; color: Theme.secondary; font: Theme.controlFont; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
                    }
                    Text { visible: root.repairPreview.matches !== undefined && root.repairPreview.matches.length > 30; text: "The preview lists the first 30 matches. All matching songs will be repaired."; color: Theme.secondary; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    ActionButton {
                        objectName: "applyMovedFilesButton"
                        text: "Repair matching songs"
                        enabled: !root.busy && root.repairPreview.matches !== undefined && root.repairPreview.matches.length > 0
                        onClicked: { root.controller.persistSession(); if (root.maintenance.applyRelink(root.repairPreview)) root.repairPreview = ({}); }
                    }
                }
            }
            ScrollView {
                id: backupScroll
                contentWidth: availableWidth; clip: true
                ColumnLayout {
                    width: backupScroll.availableWidth
                    spacing: Theme.gap(16)
                    Text { text: "Back up library metadata, settings, favorites, playlists, queues, bookmarks, and listening history. Audio files are not included. Keep a separate copy of your music."; color: Theme.secondary; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    ActionButton { objectName: "createLibraryBackupButton"; text: "Create backup…"; enabled: !root.busy && !root.library.scanning && !root.library.metadataBusy; onClicked: saveBackup.open() }
                    ActionButton { objectName: "chooseLibraryRestoreButton"; text: "Restore backup…"; primary: false; enabled: !root.busy && !root.library.scanning && !root.library.metadataBusy; onClicked: loadBackup.open() }
                    Text { text: "Restoring replaces the current library and app preferences. Music files stay in place. Missing paths can be repaired in Moved files after restoring."; color: Theme.secondary; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                }
            }
            ColumnLayout {
                spacing: Theme.gap(12)
                Text { text: "Find byte-identical files on demand. Files with matching sizes are compared using SHA-256. Choose a file to keep before moving the other copies to Trash."; color: Theme.secondary; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                ActionButton { objectName: "findDuplicateFilesButton"; text: "Find duplicates"; enabled: !root.busy && !root.library.scanning && !root.library.metadataBusy; onClicked: root.maintenance.findDuplicates() }
                Text { text: root.maintenance.status; visible: text.length > 0; color: Theme.secondary; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                ListView {
                    id: duplicateList
                    objectName: "duplicateGroupsList"
                    Layout.fillWidth: true; Layout.fillHeight: true
                    clip: true; spacing: Theme.gap(12); reuseItems: true
                    model: root.maintenance.duplicateGroups
                    ScrollBar.vertical: ScrollBar {}
                    LibraryScrollHandler { view: duplicateList }
                    delegate: Rectangle {
                        id: duplicateGroup
                        required property var modelData
                        property string keepUrl: modelData.tracks.length ? modelData.tracks[0].fileUrl : ""
                        width: duplicateList.width
                        implicitHeight: duplicateColumn.implicitHeight + Theme.gap(20)
                        color: Theme.raised; radius: Theme.dp(8); border.color: Theme.border
                        ColumnLayout {
                            id: duplicateColumn
                            anchors { left: parent.left; right: parent.right; top: parent.top; margins: Theme.gap(10) }
                            spacing: Theme.gap(8)
                            Text { text: duplicateGroup.modelData.tracks.length + " identical files · " + Math.round(duplicateGroup.modelData.size / 1024) + " KB each"; color: Theme.text; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            Repeater {
                                model: duplicateGroup.modelData.tracks
                                delegate: RadioButton {
                                    required property var modelData
                                    text: modelData.title + "\n" + decodeURIComponent(modelData.fileUrl.replace("file://", ""))
                                    checked: duplicateGroup.keepUrl === modelData.fileUrl
                                    autoExclusive: false
                                    onClicked: duplicateGroup.keepUrl = modelData.fileUrl
                                    Layout.fillWidth: true; font: Theme.controlFont
                                    contentItem: Text { text: parent.text; color: Theme.text; font: Theme.controlFont; leftPadding: parent.indicator.width + parent.spacing; wrapMode: Text.WrapAnywhere; verticalAlignment: Text.AlignVCenter }
                                }
                            }
                            ActionButton {
                                text: "Keep selected, trash other copies…"; primary: false; enabled: !root.busy
                                onClicked: {
                                    root.keptUrl = duplicateGroup.keepUrl;
                                    root.duplicateUrls = duplicateGroup.modelData.tracks.filter(function(track) { return track.fileUrl !== root.keptUrl; }).map(function(track) { return track.fileUrl; });
                                    confirmTrash.open();
                                }
                            }
                        }
                    }
                }
            }
        }
        BusyIndicator { running: root.busy; visible: running; Layout.alignment: Qt.AlignHCenter }
        Text { text: root.message; visible: text.length > 0; color: root.messageIsError ? "#ef4444" : Theme.secondary; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true }
    }
    NativeDialogs.FolderDialog { id: chooseFolder; title: "Moved music folder"; onAccepted: { newFolder.text = selectedFolder.toString(); root.repairPreview = ({}); } }
    NativeDialogs.FileDialog {
        id: saveBackup
        title: "Save Musica backup"; fileMode: NativeDialogs.FileDialog.SaveFile
        nameFilters: ["Musica backup (*.musica-backup)"]; defaultSuffix: "musica-backup"
        onAccepted: root.maintenance.createBackup(selectedFile, root.controller.prepareLibraryBackup())
    }
    NativeDialogs.FileDialog {
        id: loadBackup
        title: "Restore Musica backup"; fileMode: NativeDialogs.FileDialog.OpenFile
        nameFilters: ["Musica backup (*.musica-backup)", "All files (*)"]
        onAccepted: {
            root.backupPreview = root.maintenance.inspectBackup(selectedFile);
            if (root.backupPreview.error) { root.message = root.backupPreview.error; root.messageIsError = true; }
            else confirmRestore.open();
        }
    }
    Dialog {
        id: confirmRestore
        objectName: "confirmLibraryRestoreDialog"
        parent: Overlay.overlay; anchors.centerIn: parent
        width: Math.min(Theme.dp(470), parent.width - Theme.gap(24)); height: Math.min(Theme.dp(360), parent.height - Theme.gap(24))
        title: "Replace the current library?"; modal: true; font: Theme.controlFont
        standardButtons: Dialog.Ok | Dialog.Cancel
        background: Rectangle { color: Theme.surface; radius: Theme.dp(10); border.color: Theme.border }
        contentItem: ScrollView {
            id: restorePreviewScroll
            contentWidth: availableWidth; clip: true
            Text { width: restorePreviewScroll.availableWidth; text: (root.backupPreview.songs || 0) + " songs · " + (root.backupPreview.playlists || 0) + " playlists · " + (root.backupPreview.queues || 0) + " queues\n\nThe backup replaces your current app settings and library. Your audio files are not changed."; color: Theme.text; font: Theme.controlFont; wrapMode: Text.WordWrap }
        }
        onAccepted: { root.controller.prepareLibraryRestore(); root.maintenance.restoreBackup(root.backupPreview.file); }
    }
    Dialog {
        id: confirmTrash
        objectName: "confirmDuplicateTrashDialog"
        parent: Overlay.overlay; anchors.centerIn: parent
        width: Math.min(Theme.dp(470), parent.width - Theme.gap(24)); height: Math.min(Theme.dp(330), parent.height - Theme.gap(24))
        title: "Move duplicate copies to Trash?"; modal: true; font: Theme.controlFont
        standardButtons: Dialog.Ok | Dialog.Cancel
        background: Rectangle { color: Theme.surface; radius: Theme.dp(10); border.color: Theme.border }
        contentItem: ScrollView {
            id: duplicatePreviewScroll
            contentWidth: availableWidth; clip: true
            Text { width: duplicatePreviewScroll.availableWidth; text: root.duplicateUrls.length + " files will be moved to Trash and removed from their playlists. The selected file stays in your library."; color: Theme.text; font: Theme.controlFont; wrapMode: Text.WordWrap }
        }
        onAccepted: root.maintenance.trashDuplicates(root.keptUrl, root.duplicateUrls)
    }
}
