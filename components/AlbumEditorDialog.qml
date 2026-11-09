import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    required property var library
    property var albumChoices: []
    property var selectedIds: []
    property var selectedUrls: []
    property var preview: null
    readonly property bool splitting: editMode.currentIndex === 1
    readonly property bool showingPreview: !!preview && !preview.error
    readonly property string splitBy: ["selected", "artist", "folder", "track"][splitMode.currentIndex]
    readonly property var trackChoices: selectedIds.length === 1 ? library.tracksForGroup("album", selectedIds[0]) : []
    readonly property var previewRows: {
        if (!preview || !preview.groups) return [];
        let rows = [];
        for (const group of preview.groups) {
            rows.push({heading: true, title: group.name + " · " + group.artist + " · " + group.trackCount + " songs"});
            for (const track of group.tracks)
                rows.push({heading: false, title: (track.discNumber > 1 ? track.discNumber + "." : "") + (track.trackNumber > 0 ? track.trackNumber + "  " : "") + track.title + " · " + track.artist});
        }
        return rows;
    }
    signal groupingSaved()
    function start(albumId) {
        selectedIds = albumId ? [albumId] : [];
        editMode.currentIndex = albumId ? 1 : 0;
        selectedUrls = [];
        preview = null;
        albumName.text = "";
        albumArtist.text = "";
        open();
    }
    function toggleAlbum(album) {
        selectedIds = splitting ? [album.id] : selectedIds.indexOf(album.id) >= 0 ? selectedIds.filter(id => id !== album.id) : selectedIds.concat([album.id]);
        selectedUrls = [];
        preview = null;
        if (!splitting && !albumName.text) albumName.text = album.name;
    }
    anchors.centerIn: parent
    width: Math.min(760, parent.width - 40)
    height: Math.min(720, parent.height - 40)
    modal: true
    title: "Manage album grouping"
    padding: 20
    palette.windowText: "#e5eee6"
    palette.text: "#e5eee6"
    palette.button: "#344b3a"
    palette.buttonText: "#e5eee6"
    palette.highlight: "#53765f"
    palette.highlightedText: "#ffffff"
    background: Rectangle { color: "#202e24"; radius: 12; border.color: "#405b47" }
    onOpened: {
        albumChoices = library.albumOptions();
    }
    contentItem: ColumnLayout {
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            ComboBox {
                id: editMode
                objectName: "albumEditMode"
                model: ["Merge albums", "Split an album"]
                Layout.preferredWidth: 180
                onActivated: { root.selectedIds = []; root.selectedUrls = []; root.preview = null; }
                Accessible.name: "Album editing operation"
            }
            Text { text: root.splitting ? "Choose one album below" : root.selectedIds.length + " albums selected"; color: "#8ba392"; font.pixelSize: 11; Layout.fillWidth: true }
            ActionButton {
                objectName: "undoAlbumEditButton"
                text: "Undo last edit"
                symbol: "back"
                primary: false
                enabled: root.library.canUndoAlbumEdit
                onClicked: { if (root.library.undoAlbumEdit()) { root.groupingSaved(); root.close(); } }
            }
        }
        ListView {
            id: albumList
            visible: !root.showingPreview
            objectName: "albumEditList"
            Layout.fillWidth: true
            Layout.preferredHeight: root.splitting && root.splitBy === "selected" ? 95 : 135
            clip: true
            model: root.albumChoices
            ScrollBar.vertical: ScrollBar {}
            delegate: CheckDelegate {
                required property var modelData
                required property int index
                objectName: "albumEditChoice" + index
                width: albumList.width
                height: 40
                text: modelData.name + " · " + modelData.subtitle + " · " + modelData.trackCount + " songs"
                checked: root.selectedIds.indexOf(modelData.id) >= 0
                onClicked: root.toggleAlbum(modelData)
                contentItem: Text { text: parent.text; color: "#d9e6dc"; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter; font.pixelSize: 11; rightPadding: 40 }
                background: Rectangle { color: parent.checked ? "#344e3b" : parent.hovered ? "#2b4032" : "#19281f"; radius: 4 }
            }
        }
        RowLayout {
            visible: root.splitting && !root.showingPreview
            Text { text: "Split by"; color: "#c4d5c8"; font.pixelSize: 11 }
            ComboBox {
                id: splitMode
                objectName: "albumSplitMode"
                Layout.fillWidth: true
                model: ["Select songs for a new album", "Track artist", "Folder", "Each song separately"]
                onActivated: root.preview = null
                Accessible.name: "Split grouping"
            }
        }
        ListView {
            id: splitTracks
            visible: root.splitting && root.splitBy === "selected" && !root.showingPreview
            Layout.fillWidth: true
            Layout.preferredHeight: 105
            clip: true
            model: root.trackChoices
            ScrollBar.vertical: ScrollBar {}
            delegate: CheckDelegate {
                required property var modelData
                required property int index
                objectName: "albumSplitTrack" + index
                width: splitTracks.width
                height: 34
                text: modelData.title + " · " + modelData.artist
                checked: root.selectedUrls.indexOf(modelData.fileUrl) >= 0
                onClicked: {
                    const url = modelData.fileUrl;
                    root.selectedUrls = root.selectedUrls.indexOf(url) >= 0 ? root.selectedUrls.filter(item => item !== url) : root.selectedUrls.concat([url]);
                    root.preview = null;
                }
                contentItem: Text { text: parent.text; color: "#c4d5c8"; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter; font.pixelSize: 11; rightPadding: 40 }
                background: Rectangle { color: parent.checked ? "#344e3b" : "#19281f"; radius: 4 }
            }
        }
        RowLayout {
            visible: (!root.splitting || root.splitBy === "selected") && !root.showingPreview
            Layout.fillWidth: true
            TextField {
                id: albumName
                objectName: "albumEditName"
                Layout.fillWidth: true
                placeholderText: root.splitting ? "New album name" : "Merged album name"
                color: "#e5eee6"
                placeholderTextColor: "#7e9585"
                maximumLength: 80
                selectByMouse: true
                onTextChanged: root.preview = null
                background: Rectangle { color: "#16231b"; radius: 6; border.color: "#405b47" }
            }
            TextField {
                id: albumArtist
                objectName: "albumEditArtist"
                Layout.fillWidth: true
                placeholderText: "Album artist · Various artists"
                color: "#e5eee6"
                placeholderTextColor: "#7e9585"
                maximumLength: 80
                selectByMouse: true
                onTextChanged: root.preview = null
                background: Rectangle { color: "#16231b"; radius: 6; border.color: "#405b47" }
            }
        }
        ActionButton {
            objectName: "previewAlbumEditButton"
            text: root.showingPreview ? "Edit selection" : "Preview changes"
            symbol: "album"
            primary: false
            enabled: root.selectedIds.length > 0
            onClicked: {
                if (root.showingPreview) root.preview = null;
                else root.preview = root.library.previewAlbumEdit(root.splitting ? "split" : "merge", root.selectedIds, root.selectedUrls, albumName.text, albumArtist.text, root.splitBy);
            }
        }
        Text {
            Layout.fillWidth: true
            visible: !!root.preview
            text: root.preview ? root.preview.error || (root.preview.groups.length + " albums · " + root.preview.trackCount + " songs after applying") : ""
            color: root.preview && root.preview.error ? "#f2a79c" : "#a7e8cb"
            wrapMode: Text.WordWrap
            font.pixelSize: 11
        }
        ListView {
            id: previewList
            objectName: "albumEditPreview"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.previewRows
            ScrollBar.vertical: ScrollBar {}
            delegate: Text {
                required property var modelData
                width: previewList.width
                height: 29
                text: modelData.title
                color: modelData.heading ? "#a7e8cb" : "#a1b5a7"
                font.pixelSize: 11
                font.bold: modelData.heading
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
        }
        Text { text: "Changes affect your library grouping. Music files and tags stay untouched. The last 20 edits can be undone, including after restarting."; color: "#7e9585"; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            ActionButton { text: "Cancel"; primary: false; onClicked: root.close() }
            ActionButton {
                objectName: "applyAlbumEditButton"
                text: "Apply changes"
                symbol: "check"
                enabled: !!root.preview && !root.preview.error
                onClicked: { if (root.library.applyAlbumEdit(root.preview)) { root.groupingSaved(); root.close(); } }
            }
        }
    }
}
