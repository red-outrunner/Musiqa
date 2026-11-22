import QtQuick
import QtQuick.Controls
import MusicPlayer 1.0
import "SearchHighlight.js" as SearchHighlight

Text {
    id: root
    property var track: null
    property string placeholder: ""
    property string searchQuery: ""
    readonly property var credits: track ? (track.artistCredits || (track.artist ? [{id: track.artistId, name: track.artist}] : [])) : []
    signal artistChosen(var credit)
    function chooseArtist() {
        if (credits.length === 1) artistChosen(credits[0]);
        else if (credits.length > 1) artistChoice.open();
    }
    function escaped(value) {
        return String(value || "").replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
    }
    text: credits.length ? credits.map((credit, index) => '<a href="artist:' + index + '">' + SearchHighlight.marked(credit.name, searchQuery, Theme.accent) + '</a>').join(" · ") : escaped(placeholder)
    textFormat: Text.StyledText
    color: artistMouse.artistLink ? Theme.accent : Theme.muted
    linkColor: color
    font.family: Theme.preferences.fontFamily
    font.pointSize: Theme.textSize(10)
    elide: Text.ElideRight
    activeFocusOnTab: credits.length > 0
    Accessible.role: Accessible.Link
    Accessible.name: credits.map(credit => credit.name).join(", ")
    Accessible.description: credits.length > 1 ? "Press Enter to choose an artist" : "Open artist page"
    Accessible.onPressAction: root.chooseArtist()
    Keys.onReturnPressed: root.chooseArtist()
    Keys.onEnterPressed: root.chooseArtist()
    Rectangle { anchors.fill: parent; visible: root.activeFocus; color: "transparent"; border.width: 2; border.color: Theme.accent; radius: Theme.dp(3) }
    Menu {
        id: artistChoice
        objectName: "artistKeyboardMenu"
        y: root.height; font: Theme.controlFont
        Repeater { model: root.credits; delegate: MenuItem { required property var modelData; text: modelData.name; onTriggered: root.artistChosen(modelData) } }
    }
    onLinkActivated: function(link) {
        if (link.indexOf("artist:") !== 0) return;
        const index = Number(link.substring(7));
        if (index >= 0 && index < credits.length) artistChosen(credits[index]);
    }
    ToolTip.visible: artistMouse.artistLink.length > 0
    ToolTip.text: credits.map(credit => credit.name).join(" · ")
    MouseArea {
        id: artistMouse
        anchors.fill: parent
        enabled: root.credits.length > 0
        acceptedButtons: Qt.NoButton
        hoverEnabled: true
        readonly property string artistLink: containsMouse ? root.linkAt(mouseX, mouseY) : ""
        cursorShape: artistLink ? Qt.PointingHandCursor : Qt.ArrowCursor
    }
}
