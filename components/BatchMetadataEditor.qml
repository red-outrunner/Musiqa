import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs as NativeDialogs
import MusicPlayer 1.0

Dialog {
    id: root
    required property var library
    property var urls: []
    property var initial: ({})
    property var draft: ({})
    property var applied: ({})
    property bool ready: false
    property bool finished: false
    property string errorMessage: ""
    property string coverAction: "keep"
    property url coverFile: ""
    property int completed: 0
    readonly property bool busy: library.metadataBusy
    readonly property bool hasChanges: Object.keys(applied).some(key => applied[key]) || coverAction !== "keep"
    signal saved(int count)

    function begin(tracks) {
        urls = tracks.map(track => track.fileUrl);
        initial = ({}); draft = ({}); applied = ({}); ready = false; finished = false;
        errorMessage = ""; coverAction = "keep"; coverFile = ""; completed = 0;
        open(); library.loadBatchMetadata(urls);
    }
    function applyField(key, checked) {
        const next = Object.assign({}, applied); next[key] = checked; applied = next;
    }
    function edit(key, value) {
        const next = Object.assign({}, draft); next[key] = value; draft = next;
    }
    function saveChanges() {
        const changes = ({});
        for (const key of Object.keys(applied)) if (applied[key]) changes[key] = draft[key] || "";
        completed = 0; errorMessage = "";
        library.saveBatchMetadata(urls, changes, coverAction, coverFile, initial.revisions || {});
    }
    function errorsText(errors) {
        return errors.map(item => decodeURIComponent(item.fileUrl.split("/").pop()) + ": " + item.error).join("\n");
    }
    font: Theme.controlFont
    title: "Edit metadata · " + urls.length + (urls.length === 1 ? " song" : " songs")
    modal: true
    anchors.centerIn: parent
    width: Math.min(Theme.dp(760), parent.width - Theme.gap(24))
    height: Math.min(Theme.dp(760), parent.height - Theme.gap(24))
    padding: Theme.gap(20)
    closePolicy: busy ? Popup.NoAutoClose : Popup.CloseOnEscape
    background: Rectangle { color: Theme.surface; radius: Theme.dp(12); border.color: Theme.border }

    Connections {
        target: root.library
        function onBatchMetadataLoaded(details) {
            if (!root.visible) return;
            root.initial = details; root.draft = Object.assign({}, details.fields || {});
            root.errorMessage = root.errorsText(details.errors || []);
            root.ready = true;
        }
        function onMetadataError(message) { if (root.visible) root.errorMessage = message; }
        function onBatchMetadataProgress(completed, total) { if (root.visible) root.completed = completed; }
        function onBatchMetadataSaved(result) {
            if (!root.visible) return;
            const count = Object.keys(result.saved || {}).length;
            if (count) root.saved(count);
            const errors = result.errors || [];
            if (!errors.length) { root.close(); return; }
            root.urls = errors.map(item => item.fileUrl);
            root.ready = false; root.finished = true;
            root.errorMessage = count + " saved. " + errors.length + " could not be saved.\n" + root.errorsText(errors);
        }
    }
    NativeDialogs.FileDialog {
        id: coverDialog
        objectName: "chooseBatchCoverDialog"
        title: "Choose a cover for all selected songs"
        fileMode: NativeDialogs.FileDialog.OpenFile
        nameFilters: ["Cover images (*.jpg *.jpeg *.png)"]
        onAccepted: { root.coverFile = selectedFile; root.coverAction = "replace"; }
    }
    contentItem: ColumnLayout {
        spacing: Theme.gap(12)
        Text {
            text: "Check the fields to apply to every selected song. Unchecked fields keep their original values; a checked empty field clears that tag. Changes are saved inside the audio files, offline."
            color: Theme.secondary; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true
        }
        RowLayout {
            visible: root.busy
            BusyIndicator { running: root.busy; Layout.preferredWidth: Theme.dp(24); Layout.preferredHeight: Theme.dp(24) }
            Text { text: (root.ready ? "Saving and verifying " : "Reading tags ") + root.completed + " / " + root.urls.length + "…"; color: Theme.accent; font: Theme.controlFont; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        }
        ScrollView {
            id: form
            objectName: "batchMetadataForm"
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: form.availableWidth - Theme.gap(10)
                spacing: Theme.gap(12)
                Text {
                    objectName: "batchMetadataError"
                    visible: !!root.errorMessage; text: root.errorMessage
                    color: Theme.danger; font: Theme.controlFont; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    enabled: root.ready && !root.busy && !!root.initial.editable
                    Repeater {
                        model: [
                            {key: "album", label: "Album"}, {key: "albumArtist", label: "Album artist / collective"},
                            {key: "artist", label: "Artist"}, {key: "genre", label: "Genre"},
                            {key: "date", label: "Release date / year"}, {key: "composer", label: "Composer"},
                            {key: "discNumber", label: "Disc number"}, {key: "discTotal", label: "Total discs"},
                            {key: "trackTotal", label: "Total tracks"}, {key: "title", label: "Title"},
                            {key: "trackNumber", label: "Track number"}, {key: "comment", label: "Comment"}
                        ]
                        delegate: ColumnLayout {
                            id: field
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: Theme.gap(2)
                            CheckBox {
                                objectName: "batchApply_" + field.modelData.key
                                text: field.modelData.label
                                font: Theme.controlFont
                                checked: !!root.applied[field.modelData.key]
                                onToggled: root.applyField(field.modelData.key, checked)
                            }
                            TextField {
                                objectName: "batchMetadata_" + field.modelData.key
                                Layout.fillWidth: true
                                Layout.leftMargin: Theme.dp(30)
                                enabled: !!root.applied[field.modelData.key]
                                opacity: enabled ? 1 : 0.85
                                color: Theme.text
                                placeholderTextColor: Theme.muted
                                text: (root.initial.fields || {})[field.modelData.key] || ""
                                placeholderText: (root.initial.mixed || {})[field.modelData.key] ? "Multiple values" : "Not set"
                                selectByMouse: true
                                maximumLength: 4096
                                Accessible.name: field.modelData.label
                                onTextEdited: root.edit(field.modelData.key, text)
                                background: Rectangle {
                                    radius: Theme.dp(6)
                                    color: Theme.input
                                    border.color: parent.activeFocus ? Theme.accent : Theme.border
                                }
                            }
                            Text {
                                visible: field.modelData.key === "trackNumber" && !!root.applied.trackNumber
                                text: "This sets the SAME track number on every song. Leave unchecked to preserve album order."
                                color: Theme.warning; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true
                            }
                        }
                    }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: Theme.dp(1); color: Theme.border }
                    Text { text: "Cover artwork"; color: Theme.secondary; font: Theme.controlFont }
                    ComboBox {
                        objectName: "batchCoverAction"
                        Layout.fillWidth: true
                        model: ["Keep each song's artwork", "Replace on all selected songs", "Remove from all selected songs"]
                        currentIndex: root.coverAction === "replace" ? 1 : root.coverAction === "remove" ? 2 : 0
                        onActivated: function(index) {
                            if (index === 1) { if (!root.coverFile.toString()) coverDialog.open(); else root.coverAction = "replace"; }
                            else root.coverAction = index === 2 ? "remove" : "keep";
                        }
                    }
                    ActionButton { objectName: "chooseBatchCoverButton"; text: "Choose cover image…"; symbol: "folder"; primary: false; onClicked: coverDialog.open() }
                    CoverArt {
                        visible: root.coverAction === "replace"
                        artwork: root.coverFile.toString(); artStyle: 0
                        Layout.preferredWidth: Theme.dp(110); Layout.preferredHeight: Theme.dp(110)
                    }
                    Text { text: "A checked field left blank clears that tag. Each file is saved and verified individually; any failures are listed here."; color: Theme.muted; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            ActionButton {
                objectName: "reloadBatchMetadataButton"; visible: root.finished; text: "Reload remaining songs"
                primary: false; enabled: !root.busy
                onClicked: {
                    root.ready = false; root.finished = false; root.applied = ({}); root.completed = 0;
                    root.library.loadBatchMetadata(root.urls);
                }
            }
            Item { Layout.fillWidth: true }
            ActionButton { objectName: "cancelBatchMetadataButton"; text: root.finished ? "Close" : "Cancel"; primary: false; enabled: !root.busy; onClicked: root.close() }
            ActionButton {
                objectName: "saveBatchMetadataButton"
                text: "Save to " + root.urls.length + (root.urls.length === 1 ? " file" : " files")
                symbol: "check"
                enabled: root.ready && !!root.initial.editable && !root.busy && root.hasChanges && (root.coverAction !== "replace" || !!root.coverFile.toString())
                onClicked: root.saveChanges()
            }
        }
    }
}
