import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer 1.0

Item {
    id: root
    required property var controller
    property var songs: controller.libraryBackend.createCollectionView("song", root)
    property var albums: controller.libraryBackend.createCollectionView("album", root)
    property var artists: controller.libraryBackend.createCollectionView("artist", root)
    property var genres: controller.libraryBackend.createCollectionView("genre", root)
    readonly property var resultModel: controller.searchCategory === "album" ? albums : controller.searchCategory === "artist" ? artists : controller.searchCategory === "genre" ? genres : songs
    property alias scrollPosition: results.contentY
    onScrollPositionChanged: controller.scheduleViewSave()
    Binding { target: root.songs; property: "search"; value: root.controller.appliedSearchQuery }
    Binding { target: root.songs; property: "filters"; value: root.controller.songFilters }
    Binding { target: root.songs; property: "sortKey"; value: root.controller.sortBy }
    Binding { target: root.albums; property: "search"; value: root.controller.appliedSearchQuery }
    Binding { target: root.artists; property: "search"; value: root.controller.appliedSearchQuery }
    Binding { target: root.genres; property: "search"; value: root.controller.appliedSearchQuery }
    function resetScroll() { results.positionViewAtBeginning(); }
    function restoreScroll(offset) { results.forceLayout(); results.contentY = Math.max(results.originY, Math.min(results.originY + results.contentHeight - results.height, offset)); }
    function playFirst() { if (controller.searchCategory === "song" && songs.count) controller.playTrack(songs.get(0), songs.entries()); }
    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.gap(14)
        RowLayout {
            Layout.fillWidth: true
            Label { text: "Search results"; color: Theme.text; font.pointSize: Theme.textSize(22); font.bold: true; Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight }
            CheckBox {
                objectName: "selectAllSearchSongs"
                visible: root.controller.searchCategory === "song"
                text: "Select all"; font: Theme.controlFont
                enabled: !!root.controller.appliedSearchQuery && root.songs.count > 0
                tristate: true
                checkState: !root.controller.selectedVisibleCount ? Qt.Unchecked : root.controller.selectedVisibleCount === root.songs.count ? Qt.Checked : Qt.PartiallyChecked
                nextCheckState: function() { return root.controller.selectedVisibleCount === root.songs.count ? Qt.Unchecked : Qt.Checked; }
                onClicked: root.controller.selectVisibleSongs()
                Accessible.name: "Select all matching songs"
            }
        }
        Flickable {
            Layout.fillWidth: true; Layout.preferredHeight: categories.implicitHeight
            contentWidth: categories.implicitWidth; contentHeight: height; clip: true
            flickableDirection: Flickable.HorizontalFlick; boundsBehavior: Flickable.StopAtBounds
            Row {
                id: categories
                spacing: Theme.gap(6)
                Repeater {
                    model: [{key: "song", name: "Songs", count: root.songs.count}, {key: "album", name: "Albums", count: root.albums.count},
                        {key: "artist", name: "Artists", count: root.artists.count}, {key: "genre", name: "Genres", count: root.genres.count}]
                    delegate: Button {
                        required property var modelData
                        objectName: "searchCategory_" + modelData.key
                        text: modelData.name + " · " + (root.controller.appliedSearchQuery ? modelData.count : 0)
                        font: Theme.controlFont
                        checked: root.controller.searchCategory === modelData.key
                        onClicked: { root.controller.searchCategory = modelData.key; root.resetScroll(); }
                        background: Rectangle { color: parent.checked ? Theme.selected : parent.hovered ? Theme.hover : Theme.surface; radius: Theme.dp(7); border.color: parent.checked ? Theme.accent : Theme.border }
                    }
                }
            }
        }
        ListView {
            id: results
            objectName: "searchResultsList"
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true; reuseItems: true
            boundsBehavior: Flickable.StopAtBounds
            model: root.controller.appliedSearchQuery ? root.resultModel : null
            LibraryScrollHandler { view: results }
            ScrollBar.vertical: ScrollBar {}
            delegate: Loader {
                id: resultRow
                required property int index
                readonly property var entry: { const revision = root.resultModel.revision; return root.resultModel.get(index); }
                width: results.width
                height: item ? item.implicitHeight : Theme.rowHeight(65)
                sourceComponent: root.controller.searchCategory === "song" ? songRow : groupRow
                Component {
                    id: songRow
                    TrackResultRow { objectName: "searchSong" + resultRow.index; controller: root.controller; track: resultRow.entry; query: root.controller.searchQuery; playbackModel: root.songs; selectable: true }
                }
                Component {
                    id: groupRow
                    ItemDelegate {
                        id: group
                        objectName: "searchGroup" + resultRow.index
                        implicitHeight: Theme.rowHeight(72)
                        onClicked: root.controller.openCollection(root.controller.searchCategory === "artist" ? "Artists" : root.controller.searchCategory === "genre" ? "Genres" : "Albums", Object.assign({}, resultRow.entry, {kind: root.controller.searchCategory}))
                        TapHandler { acceptedButtons: Qt.RightButton; onTapped: root.controller.openGroupMenu(resultRow.entry, root.controller.searchCategory, group) }
                        background: Rectangle { radius: Theme.dp(6); color: group.hovered ? Theme.hover : "transparent" }
                        contentItem: RowLayout {
                            spacing: Theme.gap(12)
                            CoverArt { artwork: resultRow.entry.artwork || ""; artStyle: resultRow.entry.artStyle || 0; Layout.preferredWidth: Theme.dp(48); Layout.preferredHeight: Theme.dp(48); radius: root.controller.searchCategory === "artist" ? width / 2 : Theme.dp(6) }
                            ColumnLayout {
                                Layout.fillWidth: true; Layout.minimumWidth: 0
                                HighlightedText { value: resultRow.entry.name || ""; query: root.controller.searchQuery; font.weight: Font.DemiBold; Layout.fillWidth: true }
                                HighlightedText { value: (resultRow.entry.subtitle ? resultRow.entry.subtitle + " · " : "") + resultRow.entry.trackCount + " songs"; query: root.controller.searchQuery; color: Theme.muted; Layout.fillWidth: true }
                            }
                            IconButton { symbol: "play"; label: "Play " + (resultRow.entry.name || "collection"); onClicked: root.controller.playGroup(resultRow.entry, root.controller.searchCategory) }
                            IconButton { id: options; symbol: "more"; label: "Collection options"; onClicked: root.controller.openGroupMenu(resultRow.entry, root.controller.searchCategory, options) }
                        }
                    }
                }
            }
            Label { anchors.centerIn: parent; visible: results.count === 0; text: root.controller.appliedSearchQuery ? "No matching " + root.controller.searchCategory + "s" : "Searching…"; color: Theme.muted; font: Theme.controlFont }
        }
    }
}
