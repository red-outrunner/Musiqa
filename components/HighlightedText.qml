import QtQuick
import MusicPlayer 1.0
import "SearchHighlight.js" as SearchHighlight

Text {
    property string value: ""
    property string query: ""
    text: SearchHighlight.marked(value, query, Theme.accent)
    textFormat: Text.StyledText
    color: Theme.text
    font: Theme.controlFont
    elide: Text.ElideRight
}
