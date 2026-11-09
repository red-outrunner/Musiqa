import QtQuick
import QtQuick.Controls

Dialog {
    id: root
    property var details: ({})
    title: "Song info"
    width: 465
    modal: true
    anchors.centerIn: parent
    padding: 22
    standardButtons: Dialog.Close
    palette.windowText: "#e5eee6"
    palette.text: "#e5eee6"
    palette.button: "#344b3a"
    palette.buttonText: "#e5eee6"
    background: Rectangle {
        color: "#202e24"
        radius: 12
        border.color: "#405b47"
    }
    contentItem: ScrollView {
        id: scroll
        implicitHeight: 340
        contentWidth: availableWidth
        clip: true
        Column {
            width: scroll.availableWidth
            spacing: 15
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
                        value: Math.floor((root.details.duration || 0) / 60000) + ":" + String(Math.floor((root.details.duration || 0) / 1000) % 60).padStart(2, "0")
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
                    spacing: 5
                    Text {
                        text: parent.modelData.label
                        color: "#829d8b"
                        font.pixelSize: 9
                        font.letterSpacing: 1.2
                    }
                    TextEdit {
                        width: parent.width
                        text: parent.modelData.value || "Unknown"
                        color: "#e1ebe1"
                        font.pixelSize: 12
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.Wrap
                    }
                }
            }
        }
    }
}
