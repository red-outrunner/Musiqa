import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer 1.0

ItemDelegate {
    id: root
    required property var controller
    required property var track
    property var playbackTracks: null
    property var playbackModel: null
    property string query: ""
    property bool showArtwork: true
    property bool selectable: false
    readonly property bool current: controller.currentUrl === track.fileUrl
    readonly property bool selected: controller.selectedUrls.indexOf(track.fileUrl) >= 0
    implicitHeight: Theme.rowHeight(65)
    height: implicitHeight
    Accessible.name: (track.title || "Unavailable track") + ", " + (track.artist || "Unknown artist")
    Accessible.description: (track.album || "") + ", " + controller.formatTime(track.duration || 0)
    focusPolicy: Qt.StrongFocus
    onClicked: controller.playTrack(track, playbackModel ? playbackModel.entries() : (playbackTracks || controller.visibleTracks.entries()))
    TapHandler { acceptedButtons: Qt.RightButton; onTapped: root.controller.openTrackMenu(root.track, root) }
    background: Rectangle {
        color: root.selected || root.visualFocus ? Theme.selected : root.current ? Theme.playing : root.hovered ? Theme.hover : "transparent"
        border.width: root.visualFocus ? 2 : 0
        border.color: Theme.accent
        radius: Theme.dp(6)
        Rectangle { objectName: root.objectName + "PlayingIndicator"; visible: root.current; width: Theme.dp(3); height: parent.height; radius: Theme.dp(2); color: Theme.accent }
    }
    contentItem: RowLayout {
        spacing: Theme.gap(12)
        CheckBox {
            objectName: root.objectName + "Select"
            visible: root.selectable
            checked: root.selected
            Accessible.name: "Select " + root.track.title
            onClicked: root.controller.toggleSelection(root.track.fileUrl)
        }
        CoverArt {
            visible: root.showArtwork
            Layout.preferredWidth: Theme.dp(42); Layout.preferredHeight: Theme.dp(42)
            artwork: root.track.artwork || ""; artStyle: root.track.artStyle || 0; radius: Theme.dp(5)
        }
        ColumnLayout {
            Layout.fillWidth: true; Layout.minimumWidth: 0
            spacing: Theme.gap(3)
            HighlightedText { value: root.track.title || "Unavailable track"; query: root.query; color: root.current ? Theme.accent : Theme.text; font.weight: Font.DemiBold; Layout.fillWidth: true }
            ArtistLinks { track: root.track; searchQuery: root.query; Layout.fillWidth: true; onArtistChosen: function(credit) { root.controller.openArtistCredit(credit); } }
        }
        HighlightedText { visible: root.showArtwork && !root.controller.narrowLayout; value: root.track.album || ""; query: root.query; color: Theme.muted; Layout.preferredWidth: root.width * 0.22 }
        Text { text: root.controller.formatTime(root.track.duration || 0); color: Theme.muted; font: Theme.controlFont }
        IconButton {
            symbol: "heart"; label: root.track.favorite ? "Remove from favorites" : "Add to favorites"
            filled: !!root.track.favorite; glyphColor: filled ? "#ef4444" : Theme.muted
            onClicked: root.controller.libraryBackend.toggleFavorite(root.track.fileUrl)
        }
        IconButton { id: more; symbol: "more"; label: "Song options"; onClicked: root.controller.openTrackMenu(root.track, more) }
    }
}
