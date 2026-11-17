import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer 1.0

Dialog {
    id: root
    objectName: "aboutMusicaDialog"
    modal: true
    title: "About Musica"
    width: Math.min(Theme.dp(480), parent.width - Theme.dp(24))
    height: Math.min(Theme.dp(460) + Theme.textPixels * 6, parent.height - Theme.dp(24))
    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    padding: Theme.gap(20)
    font: Theme.controlFont
    background: Rectangle { color: Theme.surface; radius: Theme.dp(12); border.color: Theme.border }
    contentItem: ScrollView {
        id: scroll
        clip: true
        contentWidth: availableWidth
        ColumnLayout {
            width: scroll.availableWidth
            spacing: Theme.gap(14)
            Image { source: "qrc:/assets/musica.png"; sourceSize: Qt.size(128, 128); fillMode: Image.PreserveAspectFit; mipmap: true; Layout.preferredWidth: Theme.dp(72); Layout.preferredHeight: Theme.dp(72) }
            Label { text: "Musica " + Qt.application.version; color: Theme.text; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(21); font.bold: true; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Label { objectName: "musicaCreatorCredit"; text: "Created by weo sikho fuzile"; color: Theme.text; font: Theme.controlFont; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Text {
                objectName: "musicaRepositoryLink"
                text: '<a href="https://github.com/red-outrunner/Musiqa">red-outrunner/Musiqa</a>'
                textFormat: Text.StyledText
                color: Theme.secondary; linkColor: Theme.accent; font: Theme.controlFont
                wrapMode: Text.WordWrap; Layout.fillWidth: true
                onLinkActivated: function(link) { Qt.openUrlExternally(link); }
            }
            Text { text: "Free software · GPL-3.0-or-later\nContributions and donations are welcome. You can use, modify, and redistribute Musica under the GNU GPL version 3 or later."; color: Theme.secondary; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Text {
                text: 'Built with <a href="https://doc.qt.io/qt-6/licensing.html">Qt 6</a> (LGPL/GPL) and <a href="https://taglib.org/">TagLib</a> (LGPL/MPL). Audio decoding may use your system’s FFmpeg. Dependency licenses remain their own.<br><br><a href="https://www.gnu.org/licenses/gpl-3.0.html">Read the GNU GPL v3</a>'
                textFormat: Text.StyledText
                color: Theme.muted; linkColor: Theme.accent; font: Theme.controlFont
                wrapMode: Text.WordWrap; Layout.fillWidth: true
                onLinkActivated: function(link) { Qt.openUrlExternally(link); }
            }
        }
    }
    footer: DialogButtonBox { standardButtons: DialogButtonBox.Close; font: Theme.controlFont; onRejected: root.close() }
}
