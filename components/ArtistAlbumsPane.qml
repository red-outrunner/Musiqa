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
    property var rowOffsets: [0]
    property int scrollRevision: 0
    readonly property real scrollPosition: {
        const y = albumList.contentY, origin = albumList.originY, count = rows.count, offsets = rowOffsets;
        const index = albumList.indexAt(1, y + 1), item = index >= 0 ? albumList.itemAtIndex(index) : null;
        return item && index < offsets.length - 1 ? Math.max(0, offsets[index] + y - item.y) : Math.max(0, y - origin);
    }
    property real pendingScrollOffset: -1
    onScrollPositionChanged: controller.scheduleViewSave()
    Binding { target: root.albums; property: "collectionKind"; value: "artist" }
    Binding { target: root.albums; property: "collectionId"; value: root.controller.collectionId }
    function rebuild() {
        const previous = pendingScrollOffset >= 0 ? pendingScrollOffset : scrollPosition, entries = [], loaded = ({});
        const revision = ++scrollRevision;
        for (let index = 0; index < albums.count; ++index) {
            const album = albums.get(index);
            entries.push({rowKey: "album:" + album.id, rowKind: "album", entry: album});
            if (controller.expandedArtistAlbums.indexOf(album.id) >= 0) {
                const songs = controller.libraryBackend.artistAlbumTracks(controller.collectionId, album.id);
                loaded[album.id] = songs;
                for (const song of songs) entries.push({rowKey: "song:" + song.fileUrl, rowKind: "song", entry: song});
            }
        }
        albumSongs = loaded;
        const offsets = [0];
        for (const entry of entries) offsets.push(offsets[offsets.length - 1] + Theme.rowHeight(entry.rowKind === "album" ? 82 : 65));
        rowOffsets = offsets;
        const wanted = new Set(entries.map(item => item.rowKey));
        for (let last = rows.count - 1; last >= 0;) {
            if (wanted.has(rows.get(last).rowKey)) { --last; continue; }
            let first = last;
            while (first > 0 && !wanted.has(rows.get(first - 1).rowKey)) --first;
            rows.remove(first, last - first + 1); last = first - 1;
        }
        const existing = new Set();
        for (let index = 0; index < rows.count; ++index) existing.add(rows.get(index).rowKey);
        for (let index = 0; index < entries.length; ++index) {
            const entry = entries[index];
            if (index === rows.count) { rows.append(entries.slice(index)); break; }
            if (rows.get(index).rowKey !== entry.rowKey) {
                if (!existing.has(entry.rowKey)) rows.insert(index, entry);
                else {
                    let found = index + 1;
                    while (found < rows.count && rows.get(found).rowKey !== entry.rowKey) ++found;
                    rows.move(found, index, 1);
                }
            }
            if (JSON.stringify(rows.get(index).entry) !== JSON.stringify(entry.entry)) rows.setProperty(index, "entry", entry.entry);
        }
        Qt.callLater(function() {
            if (revision !== root.scrollRevision) return;
            root.applyScroll(previous); root.pendingScrollOffset = -1;
        });
    }
    function applyScroll(offset) {
        if (!rows.count) return;
        const wanted = Math.max(0, Math.min(offset, rowOffsets[rowOffsets.length - 1] - albumList.height));
        let first = 0, last = rows.count - 1;
        while (first < last) {
            const middle = Math.floor((first + last + 1) / 2);
            if (rowOffsets[middle] <= wanted) first = middle; else last = middle - 1;
        }
        // Anchor a real delegate: virtualized mixed-height rows have an estimated origin.
        albumList.positionViewAtIndex(first, ListView.Beginning); albumList.forceLayout();
        const item = albumList.itemAtIndex(first);
        if (item) albumList.contentY = item.y + wanted - rowOffsets[first];
    }
    function restoreScroll(offset) { pendingScrollOffset = Math.max(0, offset); refresh.restart(); }
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
                height: rowKind === "album" ? Theme.rowHeight(82) : Theme.rowHeight(65)
                sourceComponent: rowKind === "album" ? albumHeader : songEntry
                Component {
                    id: albumHeader
                    ItemDelegate {
                        id: header
                        objectName: "artistAlbumHeader" + row.index
                        Accessible.name: (row.entry.name || "Album") + ", " + row.entry.trackCount + " songs"
                        Accessible.description: root.expandedAlbums.indexOf(row.entry.id) >= 0 ? "Collapse album" : "Expand album"
                        implicitHeight: Theme.rowHeight(82)
                        onClicked: root.toggleAlbum(row.entry.id)
                        TapHandler { acceptedButtons: Qt.RightButton; onTapped: root.controller.openGroupMenu(row.entry, "album", header) }
                        background: Rectangle { color: header.visualFocus ? Theme.selected : header.hovered ? Theme.hover : Theme.surface; radius: Theme.dp(7); border.width: header.visualFocus ? 2 : 0; border.color: Theme.accent }
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
