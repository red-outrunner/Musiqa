import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer 1.0

ColumnLayout {
    id: root
    Layout.fillWidth: true
    property var editing: ({})
    spacing: Theme.gap(8)
    Label { text: "Keyboard shortcuts"; color: Theme.text; font: Theme.controlFont }
    Text { text: "Choose a command, then press its new key combination. Escape cancels. Conflicting shortcuts are rejected."; color: Theme.secondary; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true }
    Repeater {
        model: Theme.preferences.shortcutOptions
        delegate: RowLayout {
            required property var modelData
            Layout.fillWidth: true
            Text { text: modelData.label; color: Theme.secondary; font: Theme.controlFont; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Button {
                objectName: "shortcut_" + modelData.id
                text: modelData.sequence || "Unassigned"
                font: Theme.controlFont
                onClicked: { root.editing = modelData; capture.errorMessage = ""; capture.open(); }
            }
        }
    }
    ActionButton { objectName: "resetShortcutsButton"; text: "Restore default shortcuts"; primary: false; Layout.fillWidth: true; onClicked: Theme.preferences.resetShortcuts() }
    Dialog {
        id: capture
        objectName: "shortcutCaptureDialog"
        property string errorMessage: ""
        parent: Overlay.overlay
        modal: true; focus: true
        width: Math.min(Theme.dp(440), parent.width - Theme.dp(24))
        height: Math.min(Theme.dp(250) + Theme.textPixels * 5, parent.height - Theme.dp(24))
        x: (parent.width - width) / 2; y: (parent.height - height) / 2
        title: "Shortcut · " + (root.editing.label || "")
        font: Theme.controlFont
        closePolicy: Popup.CloseOnEscape
        background: Rectangle { color: Theme.surface; radius: Theme.dp(12); border.color: Theme.border }
        onOpened: { Theme.preferences.capturingShortcut = true; receiver.forceActiveFocus(); }
        onClosed: Theme.preferences.capturingShortcut = false
        contentItem: ColumnLayout {
            id: receiver
            focus: true
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Escape) { capture.close(); event.accepted = true; return; }
                if ([Qt.Key_Control, Qt.Key_Shift, Qt.Key_Alt, Qt.Key_Meta].indexOf(event.key) >= 0) { event.accepted = true; return; }
                capture.errorMessage = Theme.preferences.assignShortcut(root.editing.id, event.key, event.modifiers);
                event.accepted = true;
                if (!capture.errorMessage) capture.close();
            }
            Text { text: "Press a key combination…"; color: Theme.accent; font: Theme.controlFont; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Text { objectName: "shortcutConflictMessage"; text: capture.errorMessage; color: Theme.danger; font: Theme.controlFont; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Button { text: "Unassign this shortcut"; font: Theme.controlFont; onClicked: { Theme.preferences.setShortcut(root.editing.id, ""); capture.close(); } }
        }
        footer: DialogButtonBox { standardButtons: DialogButtonBox.Cancel; onRejected: capture.close() }
    }
}
