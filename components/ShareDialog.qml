import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs as NativeDialogs
import MusicPlayer 1.0

Dialog {
    id: root
    objectName: "shareDialog"
    required property var sharing
    property var details: ({})
    title: details.image ? "Share Now Playing" : "Share song"
    modal: true
    anchors.centerIn: parent
    width: Math.min(Theme.dp(520), parent.width - Theme.gap(24))
    height: Math.min(implicitHeight, parent.height - Theme.gap(24))
    font: Theme.controlFont
    padding: Theme.gap(20)
    standardButtons: Dialog.Close
    background: Rectangle { color: Theme.surface; radius: Theme.dp(12); border.color: Theme.border }
    Connections {
        target: root.sharing
        function onReady(details) { root.details = details; root.open(); }
    }
    NativeDialogs.FileDialog {
        id: destination
        objectName: "shareSaveDialog"
        title: root.details.image ? "Save Now Playing screenshot" : "Save a copy of this song"
        fileMode: NativeDialogs.FileDialog.SaveFile
        nameFilters: root.details.image ? ["PNG image (*.png)"] : ["Audio files (*)"]
        onVisibleChanged: if (visible) selectedFile = root.details.suggestedFile || ""
        onAccepted: root.sharing.exportTo(selectedFile)
    }
    contentItem: ScrollView {
        id: body
        contentWidth: availableWidth
        implicitHeight: column.implicitHeight
        clip: true
        ColumnLayout {
            id: column
            width: body.availableWidth
            spacing: Theme.gap(14)
            Image {
                visible: !!root.details.image
                source: root.details.image ? root.details.fileUrl : ""
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.dp(200)
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                sourceSize: Qt.size(800, 800)
            }
            Text { text: root.details.title || ""; color: Theme.text; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Text { text: root.details.name || ""; color: Theme.secondary; font: Theme.controlFont; elide: Text.ElideMiddle; Layout.fillWidth: true }
            ActionButton {
                objectName: "copySharedFileButton"
                text: root.details.image ? "Copy image" : "Copy audio file"
                symbol: "copy"
                Layout.fillWidth: true
                onClicked: root.sharing.copyToClipboard()
            }
            ActionButton {
                objectName: "saveSharedFileButton"; text: "Save a copy…"; symbol: "download"; primary: false
                Layout.fillWidth: true; enabled: !root.sharing.busy
                onClicked: destination.open()
            }
            ActionButton {
                objectName: "emailSharedFileButton"; text: "Email as attachment…"; symbol: "share"; primary: false
                visible: root.sharing.emailAttachmentsAvailable
                Layout.fillWidth: true; enabled: !root.sharing.busy
                onClicked: root.sharing.composeEmail()
            }
            Text {
                text: root.details.image ? "Paste the copied image into a chat or another app, or save the PNG to share later." :
                    "Paste the copied file into a chat or file manager. Saved copies include the song’s tags and embedded artwork."
                color: Theme.secondary; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true
            }
            BusyIndicator { visible: root.sharing.busy; running: root.sharing.busy; Layout.alignment: Qt.AlignHCenter }
        }
    }
}
