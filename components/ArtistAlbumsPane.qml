import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer 1.0

Item {
    id: root
    required property var controller
    property var albums: controller.libraryBackend.createCollectionView("album", root)
    readonly property var expandedAlbums: controller.expandedArtistAlbums
    property var albumSongs: ({})
    property alias scrollPosition: albumList.contentY
    onScrollPositionChanged: controller.scheduleViewSave()
    Binding { target: root.albums; property: "collectionKind"; value: "artist" }
    Binding { target: root.albums; property: "collectionId"; value: root.controller.collectionId }
    function rebuild() {
        const previous = albumList.contentY, entries = [], loaded = ({});
        for (let index = 0; index < albums.count; ++index) {
            const album = albums.get(index);
            entries.push({rowKind: "album", entry: album});
            if (controller.expandedArtistAlbums.indexOf(album.id) >= 0) {
                const songs = controller.libraryBackend.artistAlbumTracks(controller.collectionId, album.id);
                loaded[album.id] = songs;
                for (const song of songs) entries.push({rowKind: "song", entry: song});
            }
        }
        albumSongs = loaded;
        rows.clear(); rows.append(entries);
        Qt.callLater(function() { albumList.contentY = Math.max(0, Math.min(previous, albumList.contentHeight - albumList.height)); });
    }
    function toggleAlbum(id) {
        controller.expandedArtistAlbums = expandedAlbums.indexOf(id) < 0 ? expandedAlbums.concat([id]) : expandedAlbums.filter(value => value !== id);
        rebuild();
    }
    Component.onCompleted: refresh.restart()
    Connections { target: root.albums; function onEntriesChanged() { refresh.restart(); } }
    Connections { target: root.controller.libraryBackend; function onTracksChanged() { refresh.restart(); } }
    Timer { id: refresh; interval: 30; onTriggered: root.rebuild() }
    ListModel { id: rows; dynamicRoles: true }
    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.gap(14)
        RowLayout {
            Layout.fillWidth: true
            Text { text: root.controller.collectionName; color: Theme.text; font.pointSize: Theme.textSize(24); font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
            ActionButton { objectName: "artistPlayAllButton"; text: "Play all"; symbol: "play"; onClicked: root.controller.playAll(false) }
            IconButton { symbol: "shuffle"; label: "Shuffle artist"; onClicked: root.controller.playAll(true) }
            IconButton { objectName: "showArtistSongsButton"; symbol: "list"; label: "Show all songs"; onClicked: root.controller.artistAlbumView = false }
        }
        Text { text: root.albums.count + (root.albums.count === 1 ? " album" : " albums") + " · Expand an album to browse its songs"; color: Theme.muted; font: Theme.controlFont; Layout.fillWidth: true; elide: Text.ElideRight }
        ListView {
            id: albumList
            objectName: "artistAlbumList"
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true; reuseItems: true; model: rows
            boundsBehavior: Flickable.StopAtBounds
            LibraryScrollHandler { view: albumList }
            ScrollBar.vertical: ScrollBar {}
            delegate: Loader {
                id: row
                required property string rowKind
                required property var entry
                required property int index
                width: albumList.width
                height: item ? item.implicitHeight : Theme.rowHeight(72)
                sourceComponent: rowKind === "album" ? albumHeader : songEntry
                Component {
                    id: albumHeader
                    ItemDelegate {
                        id: header
                        objectName: "artistAlbumHeader" + row.index
                        implicitHeight: Theme.rowHeight(82)
                        onClicked: root.toggleAlbum(row.entry.id)
                        TapHandler { acceptedButtons: Qt.RightButton; onTapped: root.controller.openGroupMenu(row.entry, "album", header) }
                        background: Rectangle { color: header.hovered ? Theme.hover : Theme.surface; radius: Theme.dp(7) }
                        contentItem: RowLayout {
                            spacing: Theme.gap(12)
                            Icon { name: root.expandedAlbums.indexOf(row.entry.id) >= 0 ? "down" : "arrow"; color: Theme.muted; Layout.preferredWidth: Theme.dp(16); Layout.preferredHeight: Theme.dp(16) }
                            CoverArt { artwork: row.entry.artwork || ""; artStyle: row.entry.artStyle || 0; radius: Theme.dp(6); Layout.preferredWidth: Theme.dp(54); Layout.preferredHeight: Theme.dp(54) }
                            ColumnLayout {
                                Layout.fillWidth: true; Layout.minimumWidth: 0
                                Text { text: row.entry.name || "Album"; color: Theme.text; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(12); font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                                Text { text: (row.entry.year ? row.entry.year + " · " : "") + (row.entry.subtitle || ""); color: Theme.muted; font: Theme.controlFont; Layout.fillWidth: true; elide: Text.ElideRight }
                            }
                            IconButton { symbol: "play"; label: "Play album"; onClicked: root.controller.playGroup(row.entry, "album") }
                            IconButton { id: more; symbol: "more"; label: "Album options"; onClicked: root.controller.openGroupMenu(row.entry, "album", more) }
                        }
                    }
                }
                Component {
                    id: songEntry
                    TrackResultRow { objectName: "artistAlbumSong" + row.index; controller: root.controller; track: row.entry; selectable: true; showArtwork: false; playbackTracks: root.albumSongs[row.entry.albumId] || [] }
                }
            }
            Label { anchors.centerIn: parent; visible: !rows.count; text: "No albums for this artist"; color: Theme.muted; font: Theme.controlFont }
        }
    }
}
