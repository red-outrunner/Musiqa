import QtQuick
import QtQuick.Controls
import MusicPlayer 1.0

TextField {
    id: root
    property var library: null
    property string metadataKey: ""
    property var suggestions: []
    property int highlightedSuggestion: -1
    readonly property bool supportsSuggestions: ["artist", "albumArtist", "album", "genre"].indexOf(metadataKey) >= 0
    readonly property bool suggestionsOpen: suggestionsPopup.visible
    signal suggestionChosen(string value)

    function dismissSuggestions() {
        suggestionDelay.stop();
        suggestionsPopup.close();
        suggestions = []; highlightedSuggestion = -1;
    }
    function updateSuggestions() {
        if (!library || !supportsSuggestions || !activeFocus || !enabled || !visible || !text.trim()) {
            dismissSuggestions(); return;
        }
        suggestions = library.metadataSuggestions(metadataKey, text);
        highlightedSuggestion = suggestions.length ? 0 : -1;
        if (suggestions.length) suggestionsPopup.open();
        else suggestionsPopup.close();
    }
    function chooseSuggestion(index) {
        if (index < 0 || index >= suggestions.length) return;
        const value = suggestions[index];
        dismissSuggestions();
        text = value; cursorPosition = text.length;
        forceActiveFocus();
        suggestionChosen(value);
    }
    onTextEdited: {
        // Discard results for the previous text while waiting for the next query.
        dismissSuggestions();
        if (supportsSuggestions && text.trim()) suggestionDelay.restart();
    }
    onActiveFocusChanged: if (!activeFocus) dismissSuggestions()
    onEnabledChanged: if (!enabled) dismissSuggestions()
    onVisibleChanged: if (!visible) dismissSuggestions()
    Keys.onPressed: function(event) {
        if (!suggestionsPopup.visible) {
            if (supportsSuggestions && event.key === Qt.Key_Down) {
                updateSuggestions(); event.accepted = suggestionsPopup.visible;
            }
            return;
        }
        if (event.key === Qt.Key_Down || event.key === Qt.Key_Up) {
            const step = event.key === Qt.Key_Down ? 1 : -1;
            highlightedSuggestion = (highlightedSuggestion + step + suggestions.length) % suggestions.length;
            suggestionList.positionViewAtIndex(highlightedSuggestion, ListView.Contain);
            event.accepted = true;
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            chooseSuggestion(highlightedSuggestion); event.accepted = true;
        } else if (event.key === Qt.Key_Escape) {
            dismissSuggestions(); event.accepted = true;
        }
    }
    Timer { id: suggestionDelay; interval: 90; onTriggered: root.updateSuggestions() }
    Popup {
        id: suggestionsPopup
        objectName: root.objectName + "Suggestions"
        x: 0; y: root.height + Theme.gap(3)
        width: root.width
        height: Math.min(suggestionList.contentHeight, Theme.controlHeight(38) * 6) + padding * 2
        margins: Theme.gap(8)
        padding: Theme.gap(4)
        focus: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        background: Rectangle { color: Theme.raised; radius: Theme.dp(6); border.color: Theme.border }
        contentItem: ListView {
            id: suggestionList
            clip: true
            model: root.suggestions
            currentIndex: root.highlightedSuggestion
            boundsBehavior: Flickable.StopAtBounds
            delegate: ItemDelegate {
                required property string modelData
                required property int index
                objectName: root.objectName + "Suggestion" + index
                width: ListView.view.width
                height: Theme.controlHeight(38)
                focusPolicy: Qt.NoFocus
                highlighted: index === root.highlightedSuggestion
                contentItem: Text {
                    text: modelData
                    color: Theme.text
                    font: Theme.controlFont
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle { color: parent.highlighted || parent.hovered ? Theme.selected : "transparent"; radius: Theme.dp(4) }
                onClicked: root.chooseSuggestion(index)
            }
            ScrollBar.vertical: ScrollBar {}
        }
    }
}
