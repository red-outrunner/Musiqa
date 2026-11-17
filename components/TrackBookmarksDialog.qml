import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer 1.0
import "TimeFormat.js" as TimeFormat

Dialog {
    id: root
    required property var library
    property var controller: null
    property var track: null
    property real position: 0
    property var bookmarks: []
    property string editingId: ""
    property string errorMessage: ""
    readonly property bool busy: false
    signal seekRequested(var track, real position)

    function reload() { bookmarks = track ? library.trackBookmarks(track.fileUrl) : []; }
    function parseTime(text) {
        if (!/^\d+:\d{1,2}(:\d{1,2})?$/.test(text.trim())) return -1;
        const parts = text.trim().split(":").map(Number);
        if (parts[parts.length - 1] > 59 || (parts.length === 3 && parts[1] > 59)) return -1;
        const result = parts.length === 3 ? (parts[0] * 3600 + parts[1] * 60 + parts[2]) * 1000 : (parts[0] * 60 + parts[1]) * 1000;
        return Number.isSafeInteger(result) ? result : -1;
    }
    function resetEditor() { editingId = ""; nameField.clear(); timestamp.text = TimeFormat.duration(position); errorMessage = ""; }
    function save() {
        const time = parseTime(timestamp.text);
        if (time < 0) { errorMessage = "Enter a timestamp as m:ss or h:mm:ss."; return; }
        if (library.saveTrackBookmark(track.fileUrl, time, nameField.text, editingId)) resetEditor();
    }
    onOpened: { reload(); resetEditor(); }
    Connections {
        target: root.library
        function onBookmarksChanged(url) { if (root.track && root.track.fileUrl === url && root.visible) root.reload(); }
        function onImportError(message) { if (root.visible) root.errorMessage = message; }
    }
    title: "Track bookmarks"
    font: Theme.controlFont
    modal: true
    anchors.centerIn: parent
    width: Math.min(Theme.dp(650), parent.width - Theme.gap(24))
    height: Math.min(Theme.dp(630), parent.height - Theme.gap(24))
    padding: Theme.gap(20)
    standardButtons: Dialog.Close
    palette.windowText: Theme.text
    palette.text: Theme.text
    palette.base: Theme.input
    palette.button: Theme.raised
    palette.buttonText: Theme.text
    background: Rectangle { color: Theme.surface; radius: Theme.dp(12); border.color: Theme.border }
    contentItem: ColumnLayout {
        spacing: Theme.gap(12)
        Text { text: root.track ? root.track.title : "No song selected"; color: Theme.text; font: Theme.controlFont; elide: Text.ElideRight; Layout.fillWidth: true }
        Text { text: root.bookmarks.length ? "Select a bookmark to play from its timestamp." : "Save a named timestamp in this song."; color: Theme.secondary; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        ListView {
            objectName: "trackBookmarkList"
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true; spacing: Theme.gap(4)
            model: root.bookmarks
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: row
                required property var modelData
                required property int index
                width: ListView.view.width
                height: Theme.controlHeight(52)
                onClicked: { root.seekRequested(root.track, row.modelData.position); root.close(); }
                background: Rectangle { color: row.hovered ? Theme.hover : "transparent"; radius: Theme.dp(8) }
                contentItem: RowLayout {
                    spacing: Theme.gap(10)
                    Text { text: TimeFormat.duration(row.modelData.position); color: Theme.accent; font: Theme.controlFont }
                    Text { text: row.modelData.name; color: Theme.text; font: Theme.controlFont; elide: Text.ElideRight; Layout.fillWidth: true }
                    IconButton {
                        objectName: "editTrackBookmark" + row.index
                        symbol: "edit"; label: "Edit bookmark"
                        onClicked: { root.editingId = row.modelData.id; nameField.text = row.modelData.name; timestamp.text = TimeFormat.duration(row.modelData.position); root.errorMessage = ""; }
                    }
                    IconButton { objectName: "removeTrackBookmark" + row.index; symbol: "close"; label: "Remove bookmark"; onClicked: root.library.removeTrackBookmark(root.track.fileUrl, row.modelData.id) }
                }
            }
        }
        Text { text: root.editingId ? "Edit bookmark" : "Add bookmark"; color: Theme.text; font: Theme.controlFont }
        RowLayout {
            Layout.fillWidth: true
            TextField { id: timestamp; objectName: "bookmarkTimestampField"; Layout.preferredWidth: Theme.dp(105); maximumLength: 14; placeholderText: "h:mm:ss"; selectByMouse: true }
            TextField { id: nameField; objectName: "bookmarkNameField"; Layout.fillWidth: true; maximumLength: 80; placeholderText: "Bookmark name"; selectByMouse: true; onAccepted: root.save() }
        }
        Text { visible: !!root.errorMessage; text: root.errorMessage; color: Theme.danger; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        RowLayout {
            Layout.fillWidth: true
            ActionButton { text: "Use current position"; primary: false; onClicked: timestamp.text = TimeFormat.duration(root.position) }
            Item { Layout.fillWidth: true }
            ActionButton { visible: !!root.editingId; text: "Cancel edit"; primary: false; onClicked: root.resetEditor() }
            ActionButton { objectName: "saveTrackBookmarkButton"; text: root.editingId ? "Save" : "Add"; enabled: !!root.track; onClicked: root.save() }
        }
    }
}
