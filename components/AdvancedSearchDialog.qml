import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer 1.0

Dialog {
    id: root
    objectName: "advancedSearchDialog"
    property var details: ({})
    property string errorMessage: ""
    signal filtersApplied(var filters)
    title: "Advanced search"
    modal: true
    width: Math.min(Theme.dp(500), parent.width - Theme.dp(24))
    height: Math.min(implicitHeight, parent.height - Theme.dp(24))
    anchors.centerIn: parent
    padding: Theme.gap(20); font: Theme.controlFont
    background: Rectangle { color: Theme.surface; radius: Theme.dp(12); border.color: Theme.border }
    onOpened: {
        const filters = details || {};
        titleField.text = filters.title || ""; artistField.text = filters.artist || "";
        albumField.text = filters.album || ""; genreField.text = filters.genre || "";
        yearMin.text = filters.yearMin === undefined ? "" : String(filters.yearMin);
        yearMax.text = filters.yearMax === undefined ? "" : String(filters.yearMax);
        durationMin.text = filters.durationMin === undefined ? "" : String(filters.durationMin / 60000);
        durationMax.text = filters.durationMax === undefined ? "" : String(filters.durationMax / 60000);
        favoriteBox.currentIndex = filters.favorite === "yes" ? 1 : filters.favorite === "no" ? 2 : 0;
        errorMessage = "";
    }
    function apply() {
        const filters = {};
        for (const field of [{key: "title", item: titleField}, {key: "artist", item: artistField}, {key: "album", item: albumField}, {key: "genre", item: genreField}])
            if (field.item.text.trim()) filters[field.key] = field.item.text.trim();
        for (const field of [{key: "yearMin", item: yearMin, factor: 1}, {key: "yearMax", item: yearMax, factor: 1}, {key: "durationMin", item: durationMin, factor: 60000}, {key: "durationMax", item: durationMax, factor: 60000}]) {
            if (!field.item.text.trim()) continue;
            const value = Number(field.item.text.trim());
            if (!isFinite(value) || value < 0 || value > (field.factor === 1 ? 9999 : 1000000) || (field.factor === 1 && Math.floor(value) !== value)) {
                errorMessage = "Enter years from 0 to 9999 and non-negative durations in minutes."; return;
            }
            filters[field.key] = Math.round(value * field.factor);
        }
        for (const prefix of ["year", "duration"]) if (filters[prefix + "Min"] !== undefined && filters[prefix + "Max"] !== undefined && filters[prefix + "Min"] > filters[prefix + "Max"]) {
            errorMessage = "A minimum cannot exceed its maximum."; return;
        }
        if (favoriteBox.currentIndex) filters.favorite = favoriteBox.currentIndex === 1 ? "yes" : "no";
        filtersApplied(filters); close();
    }
    contentItem: ScrollView {
        id: scroll
        implicitHeight: fields.implicitHeight
        contentWidth: availableWidth; clip: true
        ColumnLayout {
            id: fields
            width: scroll.availableWidth; spacing: Theme.gap(12)
            Label { text: "Combine any fields. Blank fields include all songs."; color: Theme.muted; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Label { text: "Title contains"; color: Theme.text; font: Theme.controlFont }
            TextField { id: titleField; objectName: "filterTitle"; Layout.fillWidth: true; font: Theme.controlFont; selectByMouse: true }
            Label { text: "Artist contains"; color: Theme.text; font: Theme.controlFont }
            TextField { id: artistField; objectName: "filterArtist"; Layout.fillWidth: true; font: Theme.controlFont; selectByMouse: true }
            Label { text: "Album contains"; color: Theme.text; font: Theme.controlFont }
            TextField { id: albumField; objectName: "filterAlbum"; Layout.fillWidth: true; font: Theme.controlFont; selectByMouse: true }
            Label { text: "Genre contains"; color: Theme.text; font: Theme.controlFont }
            TextField { id: genreField; objectName: "filterGenre"; Layout.fillWidth: true; font: Theme.controlFont; selectByMouse: true }
            Label { text: "Release year · inclusive range"; color: Theme.text; font: Theme.controlFont }
            RowLayout {
                Layout.fillWidth: true
                TextField { id: yearMin; objectName: "filterYearMin"; placeholderText: "From"; Layout.fillWidth: true; Layout.minimumWidth: 0; font: Theme.controlFont; selectByMouse: true }
                TextField { id: yearMax; objectName: "filterYearMax"; placeholderText: "Through"; Layout.fillWidth: true; Layout.minimumWidth: 0; font: Theme.controlFont; selectByMouse: true }
            }
            Label { text: "Duration · minutes"; color: Theme.text; font: Theme.controlFont }
            RowLayout {
                Layout.fillWidth: true
                TextField { id: durationMin; objectName: "filterDurationMin"; placeholderText: "Minimum"; Layout.fillWidth: true; Layout.minimumWidth: 0; font: Theme.controlFont; selectByMouse: true }
                TextField { id: durationMax; objectName: "filterDurationMax"; placeholderText: "Maximum"; Layout.fillWidth: true; Layout.minimumWidth: 0; font: Theme.controlFont; selectByMouse: true }
            }
            Label { text: "Favorites"; color: Theme.text; font: Theme.controlFont }
            ComboBox { id: favoriteBox; objectName: "filterFavorite"; model: ["All songs", "Favorites only", "Not favorites"]; Layout.fillWidth: true; font: Theme.controlFont }
        }
    }
    footer: ColumnLayout {
        spacing: Theme.gap(8)
        Label {
            objectName: "advancedFilterError"
            visible: root.errorMessage.length > 0; text: root.errorMessage
            color: Theme.danger; wrapMode: Text.WordWrap; font: Theme.controlFont
            Layout.fillWidth: true; Layout.leftMargin: root.padding; Layout.rightMargin: root.padding
        }
        RowLayout {
            Layout.fillWidth: true; spacing: Theme.gap(8)
            ActionButton { objectName: "clearAdvancedFilters"; text: "Reset"; primary: false; onClicked: { root.filtersApplied({}); root.close(); } }
            Item { Layout.fillWidth: true }
            ActionButton { text: "Cancel"; primary: false; onClicked: root.close() }
            ActionButton { objectName: "applyAdvancedFilters"; text: "Apply"; onClicked: root.apply() }
        }
    }
}
