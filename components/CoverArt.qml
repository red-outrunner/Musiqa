import QtQuick
import QtQuick.Window
import MusicPlayer 1.0
Rectangle {
    id: root
    property string artwork: ""
    property int artStyle: 0
    property bool pooled: false
    readonly property int pixels: Math.min(2048, Math.max(64, Math.ceil(Math.max(width, height) * Screen.devicePixelRatio / 32) * 32))
    radius: Theme.dp(8)
    color: Theme.raised
    clip: true
    Image {
        anchors.fill: parent
        source: root.visible && !root.pooled && picture.status !== Image.Ready ? "qrc:/assets/cover-" + (root.artStyle % 4) + ".svg" : ""
        sourceSize: Qt.size(root.pixels, root.pixels)
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
    }
    Image {
        id: picture
        anchors.fill: parent
        source: {
            const revision = Theme.cache.revision;
            return root.visible && !root.pooled ? Theme.cache.thumbnail(root.artwork) : "";
        }
        asynchronous: true
        cache: false
        fillMode: Image.PreserveAspectCrop
        sourceSize: Qt.size(root.pixels, root.pixels)
        visible: status === Image.Ready
    }
}
