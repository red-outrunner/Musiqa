import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

ApplicationWindow {
    id: win
    visible: true
    width: 640
    height: 480
    title: "Test"
    ListView {
        anchors.fill: parent
        model: ["Track 1", "Track 2", "Track 3"]
        delegate: Rectangle {
            height: 40
            width: parent.width
            Text { text: modelData; anchors.centerIn: parent }
        }
    }
}
