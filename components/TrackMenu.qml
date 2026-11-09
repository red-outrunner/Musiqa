import QtQuick
import QtQuick.Controls

Menu {
    id: root
    property var track: null
    property string queueId: ""
    property bool inPlaylist: false
    signal actionRequested(string action)
    width: 225
    palette.window: "#26342b"
    palette.windowText: "#e5eee6"
    palette.text: "#e5eee6"
    palette.highlight: "#3d5846"
    palette.highlightedText: "#ffffff"
    MenuItem {
        objectName: "menuPlayNow"
        text: "Play now"
        onTriggered: root.actionRequested("play")
    }
    MenuItem {
        objectName: "menuPlayNext"
        text: "Play next"
        onTriggered: root.actionRequested("next")
    }
    MenuItem {
        objectName: "menuAddToQueue"
        text: "Add to queue"
        onTriggered: root.actionRequested("queue")
    }
    MenuItem {
        objectName: "menuAddToPlaylist"
        text: "Add to playlist…"
        onTriggered: root.actionRequested("playlist")
    }
    MenuItem {
        text: root.track && root.track.favorite ? "Remove from favorites" : "Add to favorites"
        onTriggered: root.actionRequested("favorite")
    }
    MenuItem {
        text: "Remove from this playlist"
        visible: root.inPlaylist
        height: visible ? implicitHeight : 0
        onTriggered: root.actionRequested("removePlaylist")
    }
    MenuSeparator {}
    MenuItem {
        objectName: "menuGoToArtist"
        text: "Go to artist"
        onTriggered: root.actionRequested("artist")
    }
    MenuItem {
        objectName: "menuGoToAlbum"
        text: "Go to album"
        onTriggered: root.actionRequested("album")
    }
    MenuItem {
        objectName: "menuEditMetadata"
        text: "Edit metadata & cover…"
        onTriggered: root.actionRequested("editMetadata")
    }
    MenuItem {
        objectName: "menuSongInfo"
        text: "Song info"
        onTriggered: root.actionRequested("info")
    }
    MenuItem {
        text: "Show in folder"
        onTriggered: root.actionRequested("reveal")
    }
    MenuSeparator {}
    MenuItem {
        objectName: "menuTrashSong"
        text: "Delete song…"
        onTriggered: root.actionRequested("trash")
    }
}
