import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Flickable {
    id: root
    required property var statistics
    signal playTrack(var track)
    signal openArtist(var artist)
    contentWidth: width
    contentHeight: dashboardColumn.implicitHeight
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    maximumFlickVelocity: 12000
    flickDeceleration: 1000
    LibraryScrollHandler { view: root }
    ScrollBar.vertical: ScrollBar {}
    function duration(ms) {
        const minutes = Math.floor(ms / 60000);
        return minutes >= 60 ? Math.floor(minutes / 60) + "h " + minutes % 60 + "m" : minutes + " min";
    }
    ColumnLayout {
        id: dashboardColumn
        width: root.width - 8
        spacing: 20
        RowLayout {
            Layout.fillWidth: true
            spacing: 14
            Repeater {
                model: [
                    {label: "LISTENING · LAST 7 DAYS", value: root.duration(root.statistics.weeklyMs || 0)},
                    {label: "QUALIFIED PLAYS · 7 DAYS", value: String(root.statistics.weeklyPlays || 0)},
                    {label: "TOTAL LISTENING TIME", value: root.duration(root.statistics.totalMs || 0)}
                ]
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: 105
                    radius: 10
                    color: "#202e27"
                    border.color: "#304638"
                    Column {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 14
                        Text { text: modelData.label; color: "#8ba392"; font.pixelSize: root.width < 700 ? 8 : 10; font.letterSpacing: 0.6 }
                        Text { text: modelData.value; color: "#a7e8cb"; font.pixelSize: 28; font.weight: Font.DemiBold }
                    }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 178
            radius: 10
            color: "#1b2821"
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                Text { text: "Your week in music"; color: "#edf2ef"; font.pixelSize: 16; font.weight: Font.DemiBold }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 18
                    Repeater {
                        model: root.statistics.days || []
                        delegate: ColumnLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 6
                            Item {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    width: Math.min(parent.width, 48)
                                    height: Math.max(3, parent.height * modelData.ms / Math.max(60000, ...root.statistics.days.map(day => day.ms)))
                                    radius: 3
                                    color: modelData.ms > 0 ? "#79b69b" : "#34483b"
                                    ToolTip.visible: barHover.hovered
                                    ToolTip.text: root.duration(modelData.ms)
                                    HoverHandler { id: barHover }
                                }
                            }
                            Text { Layout.alignment: Qt.AlignHCenter; text: modelData.label; color: "#8ba392"; font.pixelSize: 10 }
                        }
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 14
            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 8
                Text { text: "Weekly favorites"; color: "#edf2ef"; font.pixelSize: 17; font.weight: Font.DemiBold }
                Text { text: "Ranked by plays reaching 50%"; color: "#7e9585"; font.pixelSize: 10 }
                Text { visible: !(root.statistics.weeklyTracks || []).length; text: "Your favorites will appear as you listen."; color: "#8ba392"; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Repeater {
                    model: root.statistics.weeklyTracks || []
                    delegate: ItemDelegate {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 57
                        onClicked: root.playTrack(modelData)
                        background: Rectangle { radius: 6; color: parent.hovered ? "#2b4032" : "#1d2b23" }
                        contentItem: RowLayout {
                            CoverArt { artwork: modelData.artwork || ""; artStyle: modelData.artStyle || 0; Layout.preferredWidth: 36; Layout.preferredHeight: 36; radius: 5 }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Text { text: modelData.title; color: "#e5eee6"; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                                Text { text: modelData.artist; color: "#8ba392"; font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
                            }
                            Text { text: modelData.weeklyPlays + "×"; color: "#a7e8cb"; font.pixelSize: 12 }
                        }
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 8
                Text { text: "Top artists"; color: "#edf2ef"; font.pixelSize: 17; font.weight: Font.DemiBold }
                Text { text: "All time · ranked by listening time"; color: "#7e9585"; font.pixelSize: 10 }
                Text { visible: !(root.statistics.topArtists || []).length; text: "Spend time with your favorite artists."; color: "#8ba392"; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Repeater {
                    model: root.statistics.topArtists || []
                    delegate: ItemDelegate {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 57
                        onClicked: root.openArtist(modelData)
                        background: Rectangle { radius: 6; color: parent.hovered ? "#2b4032" : "#1d2b23" }
                        contentItem: RowLayout {
                            Icon { name: "artist"; color: "#a7e8cb"; Layout.preferredWidth: 23; Layout.preferredHeight: 23 }
                            Text { text: modelData.name; color: "#e5eee6"; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                            Text { text: root.duration(modelData.ms); color: "#8ba392"; font.pixelSize: 10 }
                        }
                    }
                }
            }
        }
        Text { text: "Recent listening"; color: "#edf2ef"; font.pixelSize: 17; font.weight: Font.DemiBold }
        Text { visible: !(root.statistics.recent || []).length; text: "Press play to start your listening history."; color: "#8ba392"; font.pixelSize: 12 }
        Repeater {
            model: root.statistics.recent || []
            delegate: ItemDelegate {
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredHeight: 55
                onClicked: root.playTrack(modelData)
                background: Rectangle { radius: 6; color: parent.hovered ? "#2b4032" : "#1b2821" }
                contentItem: RowLayout {
                    CoverArt { artwork: modelData.artwork || ""; artStyle: modelData.artStyle || 0; Layout.preferredWidth: 36; Layout.preferredHeight: 36; radius: 5 }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text { text: modelData.title; color: "#e5eee6"; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                        Text { text: modelData.artist + " · " + modelData.album; color: "#8ba392"; font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
                    }
                    Text { text: Qt.formatDateTime(new Date(modelData.listenedAt), "ddd dd MMM · hh:mm"); color: "#8ba392"; font.pixelSize: 10 }
                }
            }
        }
        Text { text: "History starts when you listen in this version. Pauses and seek jumps are excluded."; color: "#6e8878"; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.bottomMargin: 12 }
    }
}
