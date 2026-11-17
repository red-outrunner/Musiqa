import QtQuick
import MusicPlayer 1.0
import QtQuick.Controls
import "TimeFormat.js" as TimeFormat

Dialog {
    id: root
    font: Theme.controlFont
    property var details: ({})
    signal artistChosen(var credit)
    title: "Song info"
    width: Math.min(Theme.dp(465), parent.width - Theme.gap(24))
    height: Math.min(implicitHeight, parent.height - Theme.dp(24))
    modal: true
    anchors.centerIn: parent
    padding: Theme.gap(22)
    standardButtons: Dialog.Close
    palette.windowText: Theme.text
    palette.text: Theme.text
    palette.button: Theme.raised
    palette.buttonText: Theme.text
    background: Rectangle {
        color: Theme.surface
        radius: Theme.dp(12)
        border.color: Theme.border
    }
    contentItem: ScrollView {
        id: scroll
        implicitHeight: Theme.dp(340)
        contentWidth: availableWidth
        clip: true
        Column {
            width: scroll.availableWidth
            spacing: Theme.gap(15)
            Repeater {
                model: [
                    {
                        label: "TITLE",
                        value: root.details.title
                    },
                    {
                        label: "ARTIST",
                        value: root.details.artist
                    },
                    {
                        label: "ALBUM",
                        value: root.details.album
                    },
                    {
                        label: "ALBUM ARTIST",
                        value: root.details.albumArtist
                    },
                    {
                        label: "FORMAT / SIZE",
                        value: (root.details.format || "") + " · " + (root.details.sizeText || "")
                    },
                    {
                        label: "DURATION",
                        value: TimeFormat.duration(root.details.duration || 0)
                    },
                    {
                        label: "DISC / TRACK",
                        value: (root.details.discNumber || 1) + " / " + (root.details.trackNumber >= 0 ? root.details.trackNumber : "Unknown")
                    },
                    {
                        label: "GENRE",
                        value: root.details.genre
                    },
                    {
                        label: "QUALIFIED PLAYS",
                        value: String(root.details.playCount || 0)
                    },
                    {
                        label: "FILE",
                        value: root.details.path
                    }
                ]
                delegate: Column {
                    required property var modelData
                    width: scroll.availableWidth
                    spacing: Theme.gap(5)
                    Text {
                        text: parent.modelData.label
                        color: Theme.muted
                        font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(9)
                        font.letterSpacing: 1.2
                    }
                    TextEdit {
                        visible: parent.modelData.label !== "ARTIST"
                        width: parent.width
                        text: parent.modelData.value || "Unknown"
                        color: Theme.text
                        font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(12)
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.Wrap
                    }
                    ArtistLinks {
                        visible: parent.modelData.label === "ARTIST"
                        objectName: visible ? "songInfoArtistLink" : ""
                        width: parent.width
                        track: root.details
                        placeholder: "Unknown"
                        color: Theme.accent
                        font.pointSize: Theme.textSize(12)
                        wrapMode: Text.Wrap
                        elide: Text.ElideNone
                        onArtistChosen: function(credit) { root.artistChosen(credit); }
                    }
                }
            }
        }
    }
}
