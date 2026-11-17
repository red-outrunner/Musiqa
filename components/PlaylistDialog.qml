import QtQuick
import MusicPlayer 1.0
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    font: Theme.controlFont
    required property var library
    property var track: null
    property var tracks: []
    readonly property var urls: tracks.length ? tracks.map(item => item.fileUrl) : track ? [track.fileUrl] : []
    onClosed: tracks = []
    signal saved(string name)
    title: urls.length ? "Add to playlist" : "Create a playlist"
    width: Math.min(Theme.dp(420), parent.width - Theme.gap(24))
    modal: true
    anchors.centerIn: parent
    padding: Theme.gap(22)
    standardButtons: Dialog.Cancel
    palette.windowText: Theme.text
    palette.text: Theme.text
    palette.button: Theme.raised
    palette.buttonText: Theme.text
    background: Rectangle {
        color: Theme.surface
        radius: Theme.dp(12)
        border.color: Theme.border
    }
    onOpened: playlistName.clear()
    contentItem: ColumnLayout {
        spacing: Theme.gap(14)
        Text {
            text: root.urls.length > 1 ? root.urls.length + " selected songs" : root.track ? root.track.title : "Playlist name"
            color: Theme.muted
            font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(12)
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
        ListView {
            id: playlistList
            visible: root.urls.length > 0 && root.library.playlists.count > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(180, contentHeight)
            clip: true
            model: root.library.playlists
            delegate: ItemDelegate {
                id: entry
                required property var modelData
                required property int index
                objectName: "choosePlaylist" + index
                width: ListView.view.width
                height: Theme.dp(45)
                contentItem: Text {
                    text: entry.modelData.name + "  ·  " + entry.modelData.trackCount + " songs"
                    color: Theme.text
                    font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(12)
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                background: Rectangle {
                    radius: Theme.dp(6)
                    color: entry.hovered ? Theme.raised : "transparent"
                }
                onClicked: {
                    if (root.urls.length) {
                        root.library.addTracksToPlaylist(modelData.id, root.urls);
                        root.saved(modelData.name);
                        root.close();
                    }
                }
            }
            ScrollBar.vertical: ScrollBar {}
        }
        Text {
            visible: root.urls.length > 0 && root.library.playlists.count === 0
            text: "Create your first playlist below."
            color: Theme.muted
            font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(12)
        }
        TextField {
            id: playlistName
            objectName: "playlistNameField"
            Layout.fillWidth: true
            maximumLength: 80
            placeholderText: "New playlist name"
            color: Theme.text
            placeholderTextColor: Theme.muted
            selectByMouse: true
            background: Rectangle {
                color: Theme.input
                radius: Theme.dp(6)
                border.color: playlistName.activeFocus ? Theme.accent : Theme.border
            }
            onAccepted: createButton.clicked()
        }
        ActionButton {
            id: createButton
            objectName: "createPlaylistButton"
            text: root.urls.length ? "Create & add" : "Create playlist"
            symbol: "plus"
            Layout.alignment: Qt.AlignRight
            enabled: playlistName.text.trim().length > 0
            onClicked: {
                if (!playlistName.text.trim())
                    return;
                const id = root.library.createPlaylist(playlistName.text);
                if (id) {
                    if (root.urls.length) root.library.addTracksToPlaylist(id, root.urls);
                    root.saved(playlistName.text.trim());
                    root.close();
                }
            }
        }
    }
}
