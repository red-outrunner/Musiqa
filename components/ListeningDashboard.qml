import QtQuick
import MusicPlayer 1.0
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
        spacing: Theme.gap(20)
        GridLayout {
            Layout.fillWidth: true
            columns: Math.max(1, Math.min(3, Math.floor(width / Math.max(Theme.dp(250), Theme.textPixels * 18))))
            rowSpacing: Theme.gap(14)
            columnSpacing: Theme.gap(14)
            Repeater {
                model: [
                    {label: "LISTENING · LAST 7 DAYS", value: root.duration(root.statistics.weeklyMs || 0)},
                    {label: "QUALIFIED PLAYS · 7 DAYS", value: String(root.statistics.weeklyPlays || 0)},
                    {label: "TOTAL LISTENING TIME", value: root.duration(root.statistics.totalMs || 0)}
                ]
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(Theme.dp(105), metricContent.implicitHeight + Theme.gap(32))
                    radius: Theme.dp(10)
                    color: Theme.surface
                    border.color: Theme.raised
                    Column {
                        id: metricContent
                        anchors.fill: parent
                        anchors.margins: Theme.gap(16)
                        spacing: Theme.gap(14)
                        Text { width: parent.width; text: modelData.label; wrapMode: Text.WordWrap; color: Theme.muted; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(10); font.letterSpacing: 0.6 }
                        Text { text: modelData.value; color: Theme.accent; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(28); font.weight: Font.DemiBold }
                    }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.dp(178)
            radius: Theme.dp(10)
            color: Theme.hover
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.gap(18)
                Text { text: "Your week in music"; color: Theme.text; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(16); font.weight: Font.DemiBold }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: Theme.gap(18)
                    Repeater {
                        model: root.statistics.days || []
                        delegate: ColumnLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: Theme.gap(6)
                            Item {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    width: Math.min(parent.width, 48)
                                    height: Math.max(3, parent.height * modelData.ms / Math.max(60000, ...root.statistics.days.map(day => day.ms)))
                                    radius: Theme.dp(3)
                                    color: modelData.ms > 0 ? Theme.accent : Theme.hover
                                    ToolTip.visible: barHover.hovered
                                    ToolTip.text: root.duration(modelData.ms)
                                    HoverHandler { id: barHover }
                                }
                            }
                            Text { Layout.alignment: Qt.AlignHCenter; text: modelData.label; color: Theme.muted; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(10)}
                        }
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(14)
            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: Theme.gap(8)
                Text { text: "Weekly favorites"; color: Theme.text; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(17); font.weight: Font.DemiBold }
                Text { text: "Ranked by plays reaching 50%"; color: Theme.muted; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(10)}
                Text { visible: !(root.statistics.weeklyTracks || []).length; text: "Your favorites will appear as you listen."; color: Theme.muted; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(11); wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Repeater {
                    model: root.statistics.weeklyTracks || []
                    delegate: ItemDelegate {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.rowHeight(57)
                        onClicked: root.playTrack(modelData)
                        background: Rectangle { radius: Theme.dp(6); color: parent.hovered ? Theme.hover : Theme.surface }
                        contentItem: RowLayout {
                            CoverArt { artwork: modelData.artwork || ""; artStyle: modelData.artStyle || 0; Layout.preferredWidth: Theme.dp(36); Layout.preferredHeight: Theme.dp(36); radius: Theme.dp(5) }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Theme.gap(4)
                                Text { text: modelData.title; color: Theme.text; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(11); elide: Text.ElideRight; Layout.fillWidth: true }
                                Text { text: modelData.artist; color: Theme.muted; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(10); elide: Text.ElideRight; Layout.fillWidth: true }
                            }
                            Text { text: modelData.weeklyPlays + "×"; color: Theme.accent; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(12)}
                        }
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: Theme.gap(8)
                Text { text: "Top artists"; color: Theme.text; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(17); font.weight: Font.DemiBold }
                Text { text: "All time · ranked by listening time"; color: Theme.muted; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(10)}
                Text { visible: !(root.statistics.topArtists || []).length; text: "Spend time with your favorite artists."; color: Theme.muted; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(11); wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Repeater {
                    model: root.statistics.topArtists || []
                    delegate: ItemDelegate {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.rowHeight(57)
                        onClicked: root.openArtist(modelData)
                        background: Rectangle { radius: Theme.dp(6); color: parent.hovered ? Theme.hover : Theme.surface }
                        contentItem: RowLayout {
                            Icon { name: "artist"; color: Theme.accent; Layout.preferredWidth: Theme.dp(23); Layout.preferredHeight: Theme.dp(23) }
                            Text { text: modelData.name; color: Theme.text; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(11); elide: Text.ElideRight; Layout.fillWidth: true }
                            Text { text: root.duration(modelData.ms); color: Theme.muted; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(10)}
                        }
                    }
                }
            }
        }
        Text { text: "Recent listening"; color: Theme.text; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(17); font.weight: Font.DemiBold }
        Text { visible: !(root.statistics.recent || []).length; text: "Press play to start your listening history."; color: Theme.muted; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(12)}
        Repeater {
            model: root.statistics.recent || []
            delegate: ItemDelegate {
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.rowHeight(55)
                onClicked: root.playTrack(modelData)
                background: Rectangle { radius: Theme.dp(6); color: parent.hovered ? Theme.hover : Theme.hover }
                contentItem: RowLayout {
                    CoverArt { artwork: modelData.artwork || ""; artStyle: modelData.artStyle || 0; Layout.preferredWidth: Theme.dp(36); Layout.preferredHeight: Theme.dp(36); radius: Theme.dp(5) }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.gap(4)
                        Text { text: modelData.title; color: Theme.text; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(11); elide: Text.ElideRight; Layout.fillWidth: true }
                        Text { text: modelData.artist + " · " + modelData.album; color: Theme.muted; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(10); elide: Text.ElideRight; Layout.fillWidth: true }
                    }
                    Text { text: Qt.formatDateTime(new Date(modelData.listenedAt), "ddd dd MMM · hh:mm"); color: Theme.muted; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(10)}
                }
            }
        }
        Text { text: "History starts when you listen in this version. Pauses and seek jumps are excluded."; color: Theme.muted; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(10); wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.bottomMargin: Theme.gap(12) }
    }
}
