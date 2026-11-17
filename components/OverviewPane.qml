import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer 1.0

ColumnLayout {
    id: root
    required property var controller
    readonly property var library: controller.libraryBackend
    readonly property var currentTrack: controller.currentTrack
    spacing: Theme.gap(24)

    GridLayout {
        Layout.fillWidth: true
        readonly property real cardWidth: Math.max(Theme.dp(150), Theme.textPixels * 9)
        columns: width >= cardWidth * 4 + columnSpacing * 3 ? 4 : width >= cardWidth * 2 + columnSpacing ? 2 : 1
        columnSpacing: Theme.gap(12)
        rowSpacing: Theme.gap(12)
        Repeater {
            model: [
                {name: "Songs", count: root.library.trackCount, icon: "music"},
                {name: "Albums", count: root.library.albums.count, icon: "album"},
                {name: "Artists", count: root.library.artists.count, icon: "artist"},
                {name: "Genres", count: root.library.genres.count, icon: "genre"}
            ]
            delegate: Button {
                id: summaryCard
                required property var modelData
                objectName: "overview" + modelData.name
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                padding: Theme.gap(16)
                Accessible.name: modelData.count + " " + modelData.name + ", open collection"
                onClicked: root.controller.navigate(modelData.name)
                background: Rectangle { color: summaryCard.hovered ? Theme.hover : Theme.surface; radius: Theme.dp(10); border.color: Theme.border }
                contentItem: ColumnLayout {
                    spacing: Theme.gap(8)
                    RowLayout {
                        Layout.fillWidth: true
                        Icon { name: summaryCard.modelData.icon; color: Theme.accent; Layout.preferredWidth: Theme.dp(18); Layout.preferredHeight: Theme.dp(18) }
                        Text { text: summaryCard.modelData.name; color: Theme.secondary; font: Theme.controlFont; elide: Text.ElideRight; Layout.fillWidth: true }
                    }
                    Text {
                        text: summaryCard.modelData.count.toLocaleString(Qt.locale(), "f", 0)
                        color: Theme.text; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(23); font.weight: Font.DemiBold
                        elide: Text.ElideRight; Layout.fillWidth: true
                    }
                }
            }
        }
    }

    ColumnLayout {
        visible: !!root.currentTrack && !!root.currentTrack.fileUrl
        Layout.fillWidth: true
        spacing: Theme.gap(12)
        Label { text: "Continue listening"; color: Theme.text; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(17); font.bold: true }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(12)
            Button {
                objectName: "overviewNowPlaying"
                Layout.preferredWidth: Theme.dp(56); Layout.preferredHeight: Theme.dp(56)
                padding: 0
                Accessible.name: "Open Now Playing"
                background: Item {}
                contentItem: CoverArt { artwork: root.currentTrack ? root.currentTrack.artwork : ""; artStyle: root.currentTrack ? root.currentTrack.artStyle : 0 }
                onClicked: root.controller.toggleNowPlaying()
            }
            ColumnLayout {
                Layout.fillWidth: true; Layout.minimumWidth: 0
                spacing: Theme.gap(4)
                Text { text: root.currentTrack ? root.currentTrack.title : ""; color: Theme.text; font: Theme.controlFont; elide: Text.ElideRight; Layout.fillWidth: true }
                ArtistLinks { track: root.currentTrack; Layout.fillWidth: true; Layout.minimumWidth: 0; onArtistChosen: function(credit) { root.controller.openArtistCredit(credit); } }
            }
            ActionButton {
                objectName: "overviewResumeButton"
                text: root.controller.isPlaying ? "Pause" : "Resume"
                symbol: root.controller.isPlaying ? "pause" : "play"
                onClicked: root.controller.togglePlayback()
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.gap(12)
        Label { text: "Listen"; color: Theme.text; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(17); font.bold: true }
        GridLayout {
            Layout.fillWidth: true
            columns: Math.max(1, Math.min(3, Math.floor(width / Math.max(Theme.dp(180), Theme.textPixels * 11))))
            columnSpacing: Theme.gap(12); rowSpacing: Theme.gap(12)
            Repeater {
                model: [
                    {name: "Favorites", icon: "heart"},
                    {name: "Most Played", icon: "played"},
                    {name: "Recently added", icon: "clock"}
                ]
                delegate: ActionButton {
                    required property var modelData
                    objectName: "overviewListen" + modelData.name.replace(/ /g, "")
                    Layout.fillWidth: true
                    text: modelData.name
                    symbol: modelData.icon
                    primary: false
                    onClicked: modelData.name === "Recently added" ? root.controller.openSmart("recent", modelData.name) : root.controller.navigate(modelData.name)
                }
            }
        }
    }

    ColumnLayout {
        visible: root.library.trackCount > 0
        Layout.fillWidth: true
        spacing: Theme.gap(12)
        RowLayout {
            Layout.fillWidth: true
            Label { text: "Albums"; color: Theme.text; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(17); font.bold: true; Layout.fillWidth: true }
            ActionButton { objectName: "overviewViewAlbums"; text: "View all"; primary: false; onClicked: root.controller.navigate("Albums") }
        }
        GridLayout {
            Layout.fillWidth: true
            columns: Math.max(1, Math.min(2, Math.floor(width / Math.max(Theme.dp(260), Theme.textPixels * 16))))
            columnSpacing: Theme.gap(12); rowSpacing: Theme.gap(12)
            Repeater {
                model: root.visible ? root.library.shelfAlbums : []
                delegate: Item {
                    id: albumCard
                    required property var modelData
                    required property int index
                    objectName: "overviewAlbum" + index
                    Layout.fillWidth: true
                    implicitHeight: Math.max(Theme.dp(88), albumDetails.implicitHeight + Theme.gap(24))
                    Rectangle { anchors.fill: parent; color: albumMouse.containsMouse ? Theme.hover : Theme.surface; radius: Theme.dp(10); border.color: Theme.border }
                    MouseArea {
                        id: albumMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: function(mouse) {
                            if (mouse.button === Qt.RightButton) root.controller.openGroupMenu(albumCard.modelData, "album", albumCard, mouse);
                            else root.controller.openCollection("Albums", albumCard.modelData);
                        }
                    }
                    RowLayout {
                        anchors.fill: parent; anchors.margins: Theme.gap(12)
                        spacing: Theme.gap(12)
                        CoverArt { artwork: albumCard.modelData.artwork; artStyle: albumCard.modelData.artStyle; Layout.preferredWidth: Theme.dp(58); Layout.preferredHeight: Theme.dp(58) }
                        ColumnLayout {
                            id: albumDetails
                            Layout.fillWidth: true; Layout.minimumWidth: 0
                            spacing: Theme.gap(6)
                            Text { text: albumCard.modelData.name; color: Theme.text; font: Theme.controlFont; elide: Text.ElideRight; Layout.fillWidth: true }
                            Text { text: albumCard.modelData.subtitle; color: Theme.muted; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(10); elide: Text.ElideRight; Layout.fillWidth: true }
                        }
                        IconButton { symbol: "play"; label: "Play " + albumCard.modelData.name; onClicked: root.controller.playGroup(albumCard.modelData, "album") }
                    }
                }
            }
        }
    }

    Text {
        visible: root.library.trackCount === 0
        Layout.fillWidth: true
        text: "Add music files or a folder to start your library."
        color: Theme.muted; font: Theme.controlFont; wrapMode: Text.WordWrap
    }
}
