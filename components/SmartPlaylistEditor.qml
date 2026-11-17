import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer 1.0

Dialog {
    id: root
    required property var library
    property var details: ({})
    property string errorMessage: ""
    readonly property bool busy: false
    signal saved(string id, string name)
    signal deleted(string id)

    function rules() {
        const result = ({genre: genreField.text.trim(), favorite: favoriteChoice.currentValue || "any"});
        if (yearFrom.text.trim()) result.yearFrom = Number(yearFrom.text);
        if (yearTo.text.trim()) result.yearTo = Number(yearTo.text);
        if (minPlays.text.trim()) result.minPlays = Number(minPlays.text);
        if (maxPlays.text.trim()) result.maxPlays = Number(maxPlays.text);
        return result;
    }
    function save() {
        errorMessage = "";
        if ((yearFrom.text.trim() && !yearFrom.acceptableInput) || (yearTo.text.trim() && !yearTo.acceptableInput)
                || (minPlays.text.trim() && !minPlays.acceptableInput) || (maxPlays.text.trim() && !maxPlays.acceptableInput)) {
            errorMessage = "Enter whole years and play counts, or leave a field empty."; return;
        }
        const id = library.saveSmartPlaylist(details.id || "", nameField.text, rules());
        if (id) { root.saved(id, nameField.text.trim()); root.close(); }
    }
    onOpened: {
        const rule = details.rules || {};
        nameField.text = details.name || ""; genreField.text = rule.genre || "";
        yearFrom.text = rule.yearFrom === undefined ? "" : String(rule.yearFrom);
        yearTo.text = rule.yearTo === undefined ? "" : String(rule.yearTo);
        minPlays.text = rule.minPlays === undefined ? "" : String(rule.minPlays);
        maxPlays.text = rule.maxPlays === undefined ? "" : String(rule.maxPlays);
        favoriteChoice.currentIndex = rule.favorite === "yes" ? 1 : rule.favorite === "no" ? 2 : 0;
        errorMessage = ""; nameField.forceActiveFocus();
    }
    Connections { target: root.library; function onImportError(message) { if (root.visible) root.errorMessage = message; } }
    title: details.id ? "Edit smart playlist" : "New smart playlist"
    font: Theme.controlFont
    modal: true
    anchors.centerIn: parent
    width: Math.min(Theme.dp(610), parent.width - Theme.gap(24))
    height: Math.min(Theme.dp(660), parent.height - Theme.gap(24))
    padding: Theme.gap(20)
    standardButtons: Dialog.Cancel
    palette.windowText: Theme.text
    palette.text: Theme.text
    palette.base: Theme.input
    palette.button: Theme.raised
    palette.buttonText: Theme.text
    palette.highlight: Theme.accent
    background: Rectangle { color: Theme.surface; radius: Theme.dp(12); border.color: Theme.border }
    contentItem: ScrollView {
        id: form
        clip: true
        contentWidth: availableWidth
        ColumnLayout {
            width: form.availableWidth - Theme.gap(8)
            spacing: Theme.gap(12)
            Text { text: "Songs must match all rules. Leave a field empty to ignore it."; color: Theme.secondary; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Text { text: "Name"; color: Theme.text; font: Theme.controlFont }
            TextField { id: nameField; objectName: "smartPlaylistName"; Layout.fillWidth: true; maximumLength: 80; placeholderText: "Smart playlist name"; selectByMouse: true }
            Text { text: "Genre"; color: Theme.text; font: Theme.controlFont }
            TextField { id: genreField; objectName: "smartPlaylistGenre"; Layout.fillWidth: true; maximumLength: 200; placeholderText: "Any genre"; selectByMouse: true }
            Text { text: "Year range"; color: Theme.text; font: Theme.controlFont }
            RowLayout {
                Layout.fillWidth: true
                TextField { id: yearFrom; objectName: "smartPlaylistYearFrom"; Layout.fillWidth: true; placeholderText: "From year"; selectByMouse: true; validator: IntValidator { bottom: 0; top: 9999 } }
                Text { text: "to"; color: Theme.muted; font: Theme.controlFont }
                TextField { id: yearTo; objectName: "smartPlaylistYearTo"; Layout.fillWidth: true; placeholderText: "To year"; selectByMouse: true; validator: IntValidator { bottom: 0; top: 9999 } }
            }
            Text { text: "Favorites"; color: Theme.text; font: Theme.controlFont }
            ComboBox {
                id: favoriteChoice
                objectName: "smartPlaylistFavoriteRule"
                Layout.fillWidth: true
                textRole: "label"; valueRole: "value"
                model: [{label: "Any songs", value: "any"}, {label: "Favorites only", value: "yes"}, {label: "Non-favorites only", value: "no"}]
            }
            Text { text: "Play count range"; color: Theme.text; font: Theme.controlFont }
            RowLayout {
                Layout.fillWidth: true
                TextField { id: minPlays; objectName: "smartPlaylistMinPlays"; Layout.fillWidth: true; placeholderText: "Minimum plays"; selectByMouse: true; validator: IntValidator { bottom: 0; top: 1000000000 } }
                Text { text: "to"; color: Theme.muted; font: Theme.controlFont }
                TextField { id: maxPlays; objectName: "smartPlaylistMaxPlays"; Layout.fillWidth: true; placeholderText: "Maximum plays"; selectByMouse: true; validator: IntValidator { bottom: 0; top: 1000000000 } }
            }
            Text { visible: !!root.errorMessage; objectName: "smartPlaylistError"; text: root.errorMessage; color: Theme.danger; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            RowLayout {
                Layout.fillWidth: true
                ActionButton {
                    visible: !!root.details.id
                    text: "Delete"; primary: false
                    onClicked: { if (root.library.deleteSmartPlaylist(root.details.id)) { root.deleted(root.details.id); root.close(); } }
                }
                Item { Layout.fillWidth: true }
                ActionButton { objectName: "saveSmartPlaylistButton"; text: "Save smart playlist"; symbol: "save"; enabled: !!nameField.text.trim(); onClicked: root.save() }
            }
        }
    }
}
