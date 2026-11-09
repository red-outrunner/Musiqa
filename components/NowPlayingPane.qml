import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property var track: null
    property bool playing: false
    property bool shuffled: false
    property int repeatMode: 0
    property real position: 0
    property real duration: 0
    property real volume: 0.65
    property bool muted: false
    property bool seekable: false
    signal closeRequested
    signal artistRequested
    signal albumRequested
    signal playPauseRequested
    signal previousRequested
    signal nextRequested
    signal shuffleRequested
    signal repeatRequested
    signal favoriteRequested
    signal seekRequested(real position)
    signal volumeRequested(real volume)
    signal muteRequested
    signal queueRequested
    signal menuRequested(Item anchor)
    function time(ms) {
        const seconds = Math.max(0, Math.floor(ms / 1000));
        return Math.floor(seconds / 60) + ":" + String(seconds % 60).padStart(2, "0");
    }
    gradient: Gradient {
        GradientStop {
            position: 0
            color: "#203b2f"
        }
        GradientStop {
            position: 0.75
            color: "#14201a"
        }
        GradientStop {
            position: 1
            color: "#131918"
        }
    }
    // A painted Rectangle doesn't consume input. Keep this behind the visible
    // controls so artwork and empty space cannot activate covered library rows.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        preventStealing: true
        onPressed: function(mouse) { mouse.accepted = true; }
        onWheel: function(event) { event.accepted = true; }
    }
    RowLayout {
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            margins: 28
        }
        spacing: 12
        IconButton {
            objectName: "closeNowPlayingButton"
            symbol: "chevron"
            label: "Back to library"
            onClicked: root.closeRequested()
        }
        ColumnLayout {
            spacing: 5
            Text {
                text: "NOW PLAYING"
                font.pixelSize: 10
                font.weight: Font.DemiBold
                font.letterSpacing: 2
                color: "#a7e8cb"
            }
            Text {
                text: root.track ? root.track.album : "Your music, your space"
                font.pixelSize: 12
                color: "#94ad9d"
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Layout.fillWidth: true
        }
        IconButton {
            objectName: "nowPlayingQueueButton"
            symbol: "queue"
            label: "Open play queue"
            onClicked: root.queueRequested()
        }
    }
    RowLayout {
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            bottom: parent.bottom
            leftMargin: 38
            rightMargin: 38
            topMargin: 96
            bottomMargin: 34
        }
        spacing: root.width < 850 ? 28 : 42
        CoverArt {
            Layout.preferredWidth: Math.min(430, root.width * 0.43, root.height - 155)
            Layout.preferredHeight: Layout.preferredWidth
            Layout.alignment: Qt.AlignVCenter
            artwork: root.track ? root.track.artwork : ""
            artStyle: root.track ? root.track.artStyle : 0
            radius: 14
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.maximumWidth: 440
            Layout.alignment: Qt.AlignVCenter
            spacing: root.height < 600 ? 10 : 17
            Text {
                text: root.track ? root.track.format + "  ·  LOCAL AUDIO" : "LOCAL AUDIO"
                color: "#7ea58c"
                font.pixelSize: 9
                font.letterSpacing: 1.6
            }
            RowLayout {
                Layout.fillWidth: true
                Text {
                    objectName: "nowPlayingTitle"
                    text: root.track ? root.track.title : "Nothing playing yet"
                    Layout.fillWidth: true
                    color: "#eef4e9"
                    font.pixelSize: root.height < 600 ? 27 : 36
                    font.weight: Font.DemiBold
                    font.letterSpacing: -0.8
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
                IconButton {
                    objectName: "nowPlayingFavoriteButton"
                    symbol: "heart"
                    label: root.track && root.track.favorite ? "Remove from favorites" : "Add to favorites"
                    glyphColor: root.track && root.track.favorite ? "#ef4444" : "#91a99a"
                    filled: !!root.track && root.track.favorite
                    enabled: !!root.track
                    onClicked: root.favoriteRequested()
                }
                IconButton {
                    id: moreButton
                    objectName: "nowPlayingMoreButton"
                    symbol: "more"
                    label: "Song options"
                    enabled: !!root.track
                    onClicked: root.menuRequested(moreButton)
                }
            }
            Button {
                id: artistLink
                objectName: "nowPlayingArtistLink"
                Layout.fillWidth: true
                padding: 0
                hoverEnabled: true
                enabled: !!root.track
                Accessible.name: "Go to artist " + (root.track ? root.track.artist : "")
                contentItem: Text {
                    text: root.track ? root.track.artist : "Choose a song from your library"
                    color: artistLink.hovered ? "#c9f4db" : "#a7e8cb"
                    font.pixelSize: root.height < 600 ? 17 : 21
                    font.underline: artistLink.hovered
                    elide: Text.ElideRight
                }
                background: Item {}
                onClicked: root.artistRequested()
            }
            Button {
                id: albumLink
                objectName: "nowPlayingAlbumLink"
                Layout.fillWidth: true
                padding: 0
                hoverEnabled: true
                enabled: !!root.track
                Accessible.name: "Go to album " + (root.track ? root.track.album : "")
                contentItem: Text {
                    text: root.track ? "From  " + root.track.album : ""
                    color: albumLink.hovered ? "#c9dfcf" : "#8fa998"
                    font.pixelSize: 12
                    font.underline: albumLink.hovered
                    elide: Text.ElideRight
                }
                background: Item {}
                onClicked: root.albumRequested()
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: root.height < 600 ? 1 : 12
                spacing: 1
                Slider {
                    id: seekSlider
                    objectName: "nowPlayingSeekSlider"
                    Layout.fillWidth: true
                    from: 0
                    to: Math.max(1, root.duration)
                    value: root.position
                    enabled: root.seekable
                    Accessible.name: "Playback position"
                    onMoved: root.seekRequested(value)
                    background: Rectangle {
                        x: seekSlider.leftPadding
                        y: seekSlider.topPadding + seekSlider.availableHeight / 2 - 2
                        width: seekSlider.availableWidth
                        height: 4
                        radius: 2
                        color: "#385441"
                        Rectangle {
                            width: seekSlider.visualPosition * parent.width
                            height: parent.height
                            radius: 2
                            color: "#a7e8cb"
                        }
                    }
                    handle: Rectangle {
                        x: seekSlider.leftPadding + seekSlider.visualPosition * (seekSlider.availableWidth - width)
                        y: seekSlider.topPadding + (seekSlider.availableHeight - height) / 2
                        width: 10
                        height: 10
                        radius: 5
                        color: "#d1f5dd"
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: root.time(root.position)
                        color: "#89a791"
                        font.pixelSize: 10
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    Text {
                        text: root.time(root.duration)
                        color: "#89a791"
                        font.pixelSize: 10
                    }
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: root.width < 850 ? 10 : 18
                IconButton {
                    symbol: "shuffle"
                    label: "Toggle shuffle"
                    checkable: true
                    checked: root.shuffled
                    onClicked: root.shuffleRequested()
                }
                IconButton {
                    symbol: "previous"
                    label: "Previous track"
                    onClicked: root.previousRequested()
                    enabled: !!root.track
                }
                IconButton {
                    objectName: "nowPlayingPlayPauseButton"
                    symbol: root.playing ? "pause" : "play"
                    label: root.playing ? "Pause" : "Play"
                    implicitWidth: 58
                    implicitHeight: 58
                    padding: 17
                    surfaceColor: "#a7e8cb"
                    glyphColor: "#153023"
                    enabled: !!root.track
                    onClicked: root.playPauseRequested()
                }
                IconButton {
                    symbol: "next"
                    label: "Next track"
                    enabled: !!root.track
                    onClicked: root.nextRequested()
                }
                Item {
                    Layout.preferredWidth: 36
                    Layout.preferredHeight: 36
                    IconButton {
                        anchors.fill: parent
                        symbol: "repeat"
                        label: root.repeatMode === 0 ? "Repeat off" : root.repeatMode === 1 ? "Repeat all" : "Repeat one"
                        checkable: true
                        checked: root.repeatMode > 0
                        onClicked: root.repeatRequested()
                    }
                    Text {
                        anchors {
                            top: parent.top
                            right: parent.right
                        }
                        text: "1"
                        color: "#a7e8cb"
                        font.pixelSize: 9
                        visible: root.repeatMode === 2
                    }
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 12
                IconButton {
                    symbol: root.muted || root.volume === 0 ? "mute" : "volume"
                    label: root.muted ? "Unmute" : "Mute"
                    onClicked: root.muteRequested()
                }
                Slider {
                    id: volumeSlider
                    Layout.preferredWidth: Math.min(155, root.width * 0.17)
                    from: 0
                    to: 1
                    value: root.volume
                    Accessible.name: "Volume"
                    onMoved: root.volumeRequested(value)
                    background: Rectangle {
                        x: volumeSlider.leftPadding
                        y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - 2
                        width: volumeSlider.availableWidth
                        height: 3
                        radius: 2
                        color: "#385441"
                        Rectangle {
                            width: volumeSlider.visualPosition * parent.width
                            height: parent.height
                            radius: 2
                            color: "#a7e8cb"
                        }
                    }
                    handle: Rectangle {
                        x: volumeSlider.leftPadding + volumeSlider.visualPosition * (volumeSlider.availableWidth - width)
                        y: volumeSlider.topPadding + (volumeSlider.availableHeight - height) / 2
                        width: 8
                        height: 8
                        radius: 4
                        color: "#d1f5dd"
                        visible: volumeSlider.hovered || volumeSlider.pressed
                    }
                }
            }
        }
    }
}
