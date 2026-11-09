import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    required property var library
    property var track: null
    signal saved(string name)
    title: track ? "Add to playlist" : "Create a playlist"
    width: 420
    modal: true
    anchors.centerIn: parent
    padding: 22
    standardButtons: Dialog.Cancel
    palette.windowText: "#e5eee6"
    palette.text: "#e5eee6"
    palette.button: "#344b3a"
    palette.buttonText: "#e5eee6"
    background: Rectangle {
        color: "#202e24"
        radius: 12
        border.color: "#405b47"
    }
    onOpened: playlistName.clear()
    contentItem: ColumnLayout {
        spacing: 14
        Text {
            text: root.track ? root.track.title : "Give your collection a name."
            color: "#91aa99"
            font.pixelSize: 12
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
        ListView {
            id: playlistList
            visible: !!root.track && root.library.playlists.count > 0
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
                height: 45
                contentItem: Text {
                    text: entry.modelData.name + "  ·  " + entry.modelData.trackCount + " songs"
                    color: "#e1ebe1"
                    font.pixelSize: 12
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                background: Rectangle {
                    radius: 6
                    color: entry.hovered ? "#344c3a" : "transparent"
                }
                onClicked: {
                    if (root.track && root.library.addToPlaylist(modelData.id, root.track.fileUrl)) {
                        root.saved(modelData.name);
                        root.close();
                    }
                }
            }
            ScrollBar.vertical: ScrollBar {}
        }
        Text {
            visible: !!root.track && root.library.playlists.count === 0
            text: "Create your first playlist below."
            color: "#91aa99"
            font.pixelSize: 12
        }
        TextField {
            id: playlistName
            objectName: "playlistNameField"
            Layout.fillWidth: true
            maximumLength: 80
            placeholderText: "New playlist name"
            color: "#e5eee6"
            placeholderTextColor: "#829588"
            selectByMouse: true
            background: Rectangle {
                color: "#152219"
                radius: 6
                border.color: playlistName.activeFocus ? "#a7e8cb" : "#425e49"
            }
            onAccepted: createButton.clicked()
        }
        ActionButton {
            id: createButton
            objectName: "createPlaylistButton"
            text: root.track ? "Create & add song" : "Create playlist"
            symbol: "plus"
            Layout.alignment: Qt.AlignRight
            enabled: playlistName.text.trim().length > 0
            onClicked: {
                if (!playlistName.text.trim())
                    return;
                const id = root.library.createPlaylist(playlistName.text, root.track ? root.track.fileUrl : "");
                if (id) {
                    root.saved(playlistName.text.trim());
                    root.close();
                }
            }
        }
    }
}
