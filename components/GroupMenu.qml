import QtQuick
import QtQuick.Controls
import MusicPlayer 1.0

Menu {
    id: root
    property var group: null
    property string kind: "album"
    property var tracks: []
    property bool pinned: false
    signal actionRequested(string action)
    font: Theme.controlFont
    width: Math.max(Theme.dp(250), Theme.textPixels * 17)
    palette.window: Theme.raised
    palette.windowText: Theme.text
    palette.text: Theme.text
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.onAccent
    MenuItem { text: root.group ? root.group.name : ""; enabled: false }
    MenuItem { objectName: "groupViewSongs"; text: "View songs"; onTriggered: root.actionRequested("view") }
    MenuItem { objectName: "groupPinCollection"; text: root.pinned ? "Unpin from sidebar" : "Pin to sidebar"; onTriggered: root.actionRequested("pin") }
    MenuSeparator {}
    MenuItem { objectName: "groupPlay"; text: "Play all"; enabled: root.tracks.length > 0; onTriggered: root.actionRequested("play") }
    MenuItem { text: "Shuffle"; enabled: root.tracks.length > 0; onTriggered: root.actionRequested("shuffle") }
    MenuItem { objectName: "groupPlayNext"; text: "Play next"; enabled: root.tracks.length > 0; onTriggered: root.actionRequested("next") }
    MenuItem { objectName: "groupAddToQueue"; text: "Add to queue"; enabled: root.tracks.length > 0; onTriggered: root.actionRequested("queue") }
    MenuItem { objectName: "groupAddToPlaylist"; text: "Add to playlist…"; enabled: root.tracks.length > 0; onTriggered: root.actionRequested("playlist") }
    MenuItem { text: "Add all to favorites"; enabled: root.tracks.length > 0; onTriggered: root.actionRequested("favorite") }
    MenuSeparator {}
    MenuItem { objectName: "groupEditMetadata"; text: "Edit all metadata & cover…"; enabled: root.tracks.length > 0; onTriggered: root.actionRequested("metadata") }
    MenuItem { text: "Merge or split album…"; visible: root.kind === "album"; height: visible ? implicitHeight : 0; onTriggered: root.actionRequested("grouping") }
    MenuItem { text: "Show in folder"; enabled: root.tracks.length > 0; onTriggered: root.actionRequested("reveal") }
    MenuSeparator {}
    MenuItem { objectName: "groupDelete"; text: root.kind === "artist" ? "Delete artist's songs…" : root.kind === "genre" ? "Delete genre's songs…" : "Delete album's songs…"; enabled: root.tracks.length > 0; onTriggered: root.actionRequested("trash") }
}
