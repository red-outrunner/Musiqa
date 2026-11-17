import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer 1.0

Dialog {
    id: root
    objectName: "songColumnsDialog"
    title: "Song columns"; modal: true; font: Theme.controlFont
    width: Math.min(Theme.dp(540), parent.width - Theme.dp(24))
    height: Math.min(implicitHeight, parent.height - Theme.dp(24))
    anchors.centerIn: parent; padding: Theme.gap(20)
    background: Rectangle { color: Theme.surface; radius: Theme.dp(12); border.color: Theme.border }
    ListModel { id: columns }
    function load(values) {
        columns.clear();
        for (const item of values) columns.append({key: item.key, columnWidth: item.width, shown: item.visible});
    }
    onOpened: load(Theme.preferences.songColumns)
    function apply() {
        const values = [];
        for (let i = 0; i < columns.count; ++i) { const item = columns.get(i); values.push({key: item.key, width: item.columnWidth, visible: item.shown}); }
        Theme.preferences.songColumns = values; close();
    }
    contentItem: ScrollView {
        id: scroll
        objectName: "songColumnsScroll"
        implicitHeight: content.implicitHeight; contentWidth: availableWidth; clip: true
        ColumnLayout {
            id: content
            width: scroll.availableWidth; spacing: Theme.gap(14)
            Text { text: "Choose columns, change their order, and set widths in logical pixels. You can also drag a column header's right edge. Wide tables scroll sideways; hold Shift while using the mouse wheel."; color: Theme.muted; wrapMode: Text.WordWrap; font: Theme.controlFont; Layout.fillWidth: true }
            Repeater {
                model: columns
                delegate: ColumnLayout {
                    id: row
                    required property string key
                    required property int columnWidth
                    required property bool shown
                    required property int index
                    Layout.fillWidth: true; spacing: Theme.gap(6)
                    RowLayout {
                        Layout.fillWidth: true
                        CheckBox {
                            objectName: "columnVisible_" + row.key
                            text: row.key.charAt(0).toUpperCase() + row.key.slice(1)
                            checked: row.shown; onToggled: columns.setProperty(row.index, "shown", checked)
                            font: Theme.controlFont; Layout.fillWidth: true
                        }
                        IconButton { objectName: "columnUp_" + row.key; symbol: "up"; label: "Move column up"; enabled: row.index > 0; onClicked: columns.move(row.index, row.index - 1, 1) }
                        IconButton { objectName: "columnDown_" + row.key; symbol: "down"; label: "Move column down"; enabled: row.index + 1 < columns.count; onClicked: columns.move(row.index, row.index + 1, 1) }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Width"; color: Theme.secondary; font: Theme.controlFont; Layout.fillWidth: true }
                        SpinBox { objectName: "columnWidth_" + row.key; from: 50; to: 500; editable: true; value: row.columnWidth; onValueModified: columns.setProperty(row.index, "columnWidth", value); font: Theme.controlFont }
                        Label { text: "px"; color: Theme.muted; font: Theme.controlFont }
                    }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
                }
            }
        }
    }
    footer: RowLayout {
        ActionButton { text: "Reset"; primary: false; onClicked: root.load(Theme.preferences.defaultSongColumns()) }
        Item { Layout.fillWidth: true }
        ActionButton { text: "Cancel"; primary: false; onClicked: root.close() }
        ActionButton { objectName: "applySongColumns"; text: "Apply"; onClicked: root.apply() }
    }
}
