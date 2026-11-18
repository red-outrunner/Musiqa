import QtQuick
import MusicPlayer 1.0
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs as NativeDialogs

Dialog {
    id: root
    font: Theme.controlFont
    required property var library
    property string fileUrl: ""
    property var initial: ({})
    property var draft: ({})
    property bool ready: false
    property string errorMessage: ""
    property string coverAction: "keep"
    property url coverFile: ""
    readonly property bool busy: library.metadataBusy
    readonly property bool changed: {
        if (!ready) return false;
        if (coverAction !== "keep") return true;
        for (const key of Object.keys(draft)) if (String(draft[key] || "") !== String((initial.fields || {})[key] || "")) return true;
        return false;
    }
    signal saved(string url)
    function begin(url) {
        fileUrl = url; ready = false; initial = ({}); draft = ({});
        errorMessage = ""; coverAction = "keep"; coverFile = "";
        open(); library.loadSongMetadata(url);
    }
    function edit(key, value) {
        const fields = Object.assign({}, draft); fields[key] = value; draft = fields;
    }
    function saveChanges() {
        const changes = ({});
        for (const key of Object.keys(draft)) {
            if (String(draft[key] || "") !== String((initial.fields || {})[key] || "")) changes[key] = draft[key];
        }
        errorMessage = "";
        library.saveSongMetadata(fileUrl, changes, coverAction, coverFile, initial.revision || "");
    }
    anchors.centerIn: parent
    width: Math.min(Theme.dp(800), parent.width - Theme.gap(24))
    height: Math.min(Theme.dp(770), parent.height - Theme.gap(24))
    title: "Edit song metadata"
    modal: true
    padding: Theme.gap(20)
    closePolicy: busy ? Popup.NoAutoClose : Popup.CloseOnEscape
    background: Rectangle { color: Theme.surface; radius: Theme.dp(12); border.color: Theme.border }
    Connections {
        target: root.library
        function onMetadataLoaded(details) {
            if (details.fileUrl !== root.fileUrl) return;
            root.ready = false;
            root.initial = details; root.draft = Object.assign({}, details.fields || {});
            Qt.callLater(function() { root.ready = true; });
        }
        function onMetadataError(message) { if (root.visible) root.errorMessage = message; }
        function onMetadataSaved(url) { if (url === root.fileUrl) { root.saved(url); root.close(); } }
    }
    NativeDialogs.FileDialog {
        id: chooseCover
        objectName: "chooseMetadataCoverDialog"
        title: "Choose a local cover image"
        fileMode: NativeDialogs.FileDialog.OpenFile
        nameFilters: ["Cover images (*.jpg *.jpeg *.png)"]
        onAccepted: { root.coverFile = selectedFile; root.coverAction = "replace"; }
    }
    contentItem: ColumnLayout {
        spacing: Theme.gap(12)
        Text { text: "Save tags and artwork inside the audio file. They travel with the song to other machines and players. All editing is offline."; color: Theme.secondary; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(12); wrapMode: Text.WordWrap; Layout.fillWidth: true }
        RowLayout {
            visible: root.busy
            BusyIndicator { running: root.busy; Layout.preferredWidth: Theme.dp(24); Layout.preferredHeight: Theme.dp(24) }
            Text { text: root.ready ? "Writing and verifying the audio file…" : "Reading the song's tags…"; color: Theme.accent; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(12); Layout.fillWidth: true }
        }
        Text { visible: root.ready && !root.initial.writable; text: "This song or its folder is read-only."; color: Theme.danger; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(12); Layout.fillWidth: true }
        Text { objectName: "metadataEditorError"; visible: !!root.errorMessage; text: root.errorMessage; color: Theme.danger; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(12); wrapMode: Text.WordWrap; Layout.fillWidth: true }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.gap(20)
            enabled: root.ready && !root.busy
            ScrollView {
                id: coverScroll
                Layout.preferredWidth: root.width < Theme.dp(700) ? 170 : 215
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
            ColumnLayout {
                width: coverScroll.availableWidth - 8
                spacing: Theme.gap(12)
                CoverArt {
                    objectName: "metadataCoverPreview"
                    Layout.fillWidth: true
                    Layout.preferredHeight: width
                    radius: Theme.dp(8)
                    artwork: root.coverAction === "replace" ? root.coverFile.toString() : root.coverAction === "remove" ? "" : root.initial.artwork || ""
                    artStyle: 0
                }
                Text { text: root.coverAction === "replace" ? "New embedded front cover" : root.coverAction === "remove" ? "Embedded artwork will be removed" : root.initial.embeddedCover ? "Current embedded cover" : "No embedded cover"; color: Theme.muted; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(10); wrapMode: Text.WordWrap; Layout.fillWidth: true }
                ActionButton { objectName: "chooseMetadataCoverButton"; text: "Choose image…"; symbol: "folder"; primary: false; Layout.fillWidth: true; onClicked: chooseCover.open() }
                ActionButton { objectName: "removeMetadataCoverButton"; text: "Remove artwork"; symbol: "close"; primary: false; Layout.fillWidth: true; enabled: root.initial.embeddedCover || root.coverAction === "replace"; onClicked: { root.coverAction = "remove"; root.coverFile = ""; } }
                Button { text: "Keep original artwork"; flat: true; enabled: root.coverAction !== "keep"; onClicked: { root.coverAction = "keep"; root.coverFile = ""; } }
                Text { text: "JPEG or PNG · up to 20 MB\nArtwork is embedded at its original resolution."; color: Theme.muted; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(10); wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Text { visible: !!root.initial.libraryTrack && !!root.initial.libraryTrack.manualAlbum; text: "Editing album tags will reset this song's library-only album grouping override."; color: Theme.secondary; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(10); wrapMode: Text.WordWrap; Layout.fillWidth: true }
            }
            }
            ScrollView {
                id: formScroll
                objectName: "metadataForm"
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: formScroll.availableWidth - 12
                    spacing: Theme.gap(12)
                    Repeater {
                        model: [
                            {key: "title", label: "Title"}, {key: "artist", label: "Artist"}, {key: "album", label: "Album"},
                            {key: "albumArtist", label: "Album artist / collective"}, {key: "genre", label: "Genre"},
                            {key: "date", label: "Release date / year"}, {key: "composer", label: "Composer"},
                            {key: "trackNumber", label: "Track number"}, {key: "trackTotal", label: "Total tracks"},
                            {key: "discNumber", label: "Disc number"}, {key: "discTotal", label: "Total discs"}
                        ]
                        delegate: ColumnLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: Theme.gap(4)
                            Text { text: parent.modelData.label; color: Theme.muted; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(11)}
                            MetadataTextField {
                                objectName: "metadata_" + modelData.key
                                library: root.library
                                metadataKey: modelData.key
                                Layout.fillWidth: true
                                text: (root.initial.fields || {})[modelData.key] || ""
                                selectByMouse: true
                                maximumLength: 4096
                                placeholderText: "Not set"
                                Accessible.name: modelData.label
                                onTextChanged: if (root.ready) root.edit(modelData.key, text)
                            }
                        }
                    }
                    Text { text: "Comment"; color: Theme.muted; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(11)}
                    TextArea {
                        objectName: "metadata_comment"
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.dp(90)
                        text: (root.initial.fields || {}).comment || ""
                        wrapMode: TextEdit.Wrap
                        selectByMouse: true
                        placeholderText: "Not set"
                        onTextChanged: if (root.ready) root.edit("comment", text)
                        Accessible.name: "Comment"
                    }
                }
            }
        }
        TextEdit { text: root.initial.path || root.fileUrl; readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere; color: Theme.muted; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(10); Layout.fillWidth: true }
        RowLayout {
            Layout.fillWidth: true
            Text { text: "Audio is preserved without re-encoding."; color: Theme.muted; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(10); wrapMode: Text.WordWrap; Layout.fillWidth: true }
            ActionButton { objectName: "cancelMetadataButton"; text: "Cancel"; primary: false; enabled: !root.busy; onClicked: root.close() }
            ActionButton { objectName: "saveMetadataButton"; text: "Save to file"; symbol: "check"; enabled: root.ready && !!root.initial.writable && root.changed && !root.busy; onClicked: root.saveChanges() }
        }
    }
}
