import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer 1.0

Dialog {
    id: root
    objectName: "queuePlaylistDialog"
    required property var library
    property var details: ({})
    property string errorMessage: ""
    readonly property int uniqueCount: new Set(details.urls || []).size
    signal saved(string name)
    title: "Save queue as new playlist"
    modal: true
    width: Math.min(Theme.dp(480), parent.width - Theme.dp(24))
    height: Math.min(Theme.dp(280) + Theme.textPixels * 6, parent.height - Theme.dp(24))
    x: (parent.width - width) / 2; y: (parent.height - height) / 2
    padding: Theme.gap(20)
    font: Theme.controlFont
    standardButtons: Dialog.Cancel
    background: Rectangle { color: Theme.surface; radius: Theme.dp(12); border.color: Theme.border }
    onOpened: { playlistName.text = details.name || "Queue"; errorMessage = ""; playlistName.forceActiveFocus(); playlistName.selectAll(); }
    Connections {
        target: root.library
        function onImportError(message) { if (root.visible) root.errorMessage = message; }
    }
    contentItem: ScrollView {
        id: scroll
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
            width: scroll.availableWidth
            spacing: Theme.gap(14)
            Text { text: root.uniqueCount + (root.uniqueCount === 1 ? " song in queue order" : " songs in queue order"); color: Theme.secondary; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Text { visible: root.uniqueCount < (root.details.urls || []).length; text: "Repeated songs are saved once."; color: Theme.muted; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            TextField {
                id: playlistName
                objectName: "queuePlaylistNameField"
                placeholderText: "Playlist name"
                maximumLength: 80
                selectByMouse: true
                font: Theme.controlFont
                Layout.fillWidth: true
                onAccepted: saveButton.clicked()
                Accessible.name: "New playlist name"
            }
            Text { objectName: "queuePlaylistError"; visible: !!root.errorMessage; text: root.errorMessage; color: Theme.danger; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            ActionButton {
                id: saveButton
                objectName: "confirmSaveQueuePlaylist"
                text: "Save playlist"
                Layout.fillWidth: true
                enabled: playlistName.text.trim().length > 0 && root.uniqueCount > 0
                onClicked: {
                    if (!enabled) return;
                    const id = root.library.createPlaylistFromUrls(playlistName.text, root.details.urls || []);
                    if (id) { root.saved(playlistName.text.trim()); root.close(); }
                }
            }
        }
    }
}
