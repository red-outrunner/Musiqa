import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer 1.0

Dialog {
    id: root
    property var controller: null
    objectName: "displaySettingsDialog"
    modal: true
    title: "Settings"
    width: Math.min(Theme.dp(640), parent.width - Theme.dp(24))
    height: Math.min(implicitHeight, parent.height - Theme.dp(24))
    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    font: Theme.controlFont
    padding: Theme.gap(20)
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { color: Theme.surface; radius: Theme.dp(12); border.color: Theme.border }
    readonly property var sectionNames: ["Appearance", "Playback", "Laptop and performance", "Audio output", "Equalizer", "Artist credits", "Display and layout", "Artwork cache", "Keyboard shortcuts", "Library tools", "About"]
    function goToSection(name) { scrollToSection(sectionNames.indexOf(name)); }
    function scrollToSection(index) {
        if (index < 0 || index >= sectionNames.length) return;
        settingsSections.currentIndex = index;
        Qt.callLater(function() {
            const sections = [appearanceSection, playbackSection, desktopSection, audioOutputSection, equalizerSection,
                artistSection, displaySection, cacheSection, keyboardSection, libraryToolsSection, aboutSection];
            const flickable = settingsScroll.contentItem;
            if (flickable && sections[index])
                flickable.contentY = Math.max(0, Math.min(sections[index].y, flickable.contentHeight - flickable.height));
        });
    }
    header: ColumnLayout {
        spacing: Theme.gap(10)
        Label {
            text: root.title; color: Theme.text
            font: Qt.font({family: Theme.preferences.fontFamily, pointSize: Theme.pointSize, bold: true})
            Layout.topMargin: root.padding; Layout.leftMargin: root.padding; Layout.rightMargin: root.padding
        }
        ComboBox {
            id: settingsSections
            objectName: "settingsSectionSelector"
            Layout.fillWidth: true
            Layout.leftMargin: root.padding; Layout.rightMargin: root.padding
            model: root.sectionNames
            font: Theme.controlFont
            Accessible.name: "Settings section"
            onActivated: function(index) { root.scrollToSection(index); }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
    }
    contentItem: ScrollView {
        id: settingsScroll
        objectName: "displaySettingsScroll"
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        clip: true
        implicitHeight: settingsColumn.implicitHeight
        ColumnLayout {
            id: settingsColumn
            width: settingsScroll.availableWidth
            spacing: Theme.gap(14)
            Label { id: appearanceSection; text: "Appearance"; color: Theme.text; font: Qt.font({family: Theme.preferences.fontFamily, pointSize: Theme.pointSize, bold: true}) }
            ComboBox {
                objectName: "appearanceThemeBox"
                Layout.fillWidth: true
                model: ["Teal dark", "Teal light"]
                currentIndex: Theme.preferences.theme === "light" ? 1 : 0
                onActivated: function(index) { Theme.preferences.theme = index === 1 ? "light" : "dark"; }
                Accessible.name: "Color theme"
            }
            CheckBox {
                objectName: "artworkColorsCheckBox"
                Layout.fillWidth: true
                contentItem: Text { text: parent.text; font: parent.font; color: Theme.text; wrapMode: Text.WordWrap; leftPadding: parent.indicator.width + parent.spacing; verticalAlignment: Text.AlignVCenter }
                text: "Adapt accent colors to album artwork"
                checked: Theme.preferences.artworkColors
                onToggled: Theme.preferences.artworkColors = checked
                font: Theme.controlFont
            }
            Text { text: "Turn this off to return to teal. Songs without artwork use the normal teal theme."; color: Theme.secondary; wrapMode: Text.WordWrap; font: Theme.controlFont; Layout.fillWidth: true }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
            Label { id: playbackSection; text: "Playback · keyboard seeking"; color: Theme.text; font: Qt.font({family: Theme.preferences.fontFamily, pointSize: Theme.pointSize, bold: true}) }
            RowLayout {
                Layout.fillWidth: true
                Text { text: "Left / Right seek step"; color: Theme.secondary; font: Theme.controlFont; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                SpinBox {
                    objectName: "seekSecondsBox"
                    from: 5; to: 30; editable: true
                    value: Theme.preferences.seekSeconds
                    onValueModified: Theme.preferences.seekSeconds = value
                    Accessible.name: "Seek step in seconds, 5 to 30"
                }
                Text { text: "sec"; color: Theme.secondary; font: Theme.controlFont }
            }
            Text { text: "The seek step applies to your forward/backward shortcuts. Change the keys in Keyboard shortcuts below. Text fields and other sliders keep their arrow-key behavior."; color: Theme.secondary; wrapMode: Text.WordWrap; font: Theme.controlFont; Layout.fillWidth: true }
            CheckBox {
                objectName: "avoidRecentShuffleCheckBox"
                text: "Prefer songs not played recently when shuffling"
                checked: Theme.preferences.avoidRecentShuffle
                onToggled: Theme.preferences.avoidRecentShuffle = checked
                font: Theme.controlFont; Layout.fillWidth: true
                contentItem: Text { text: parent.text; font: parent.font; color: Theme.text; wrapMode: Text.WordWrap; leftPadding: parent.indicator.width + parent.spacing; verticalAlignment: Text.AlignVCenter }
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: "Recent listening window"; color: Theme.secondary; font: Theme.controlFont; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                SpinBox { objectName: "shuffleRecentDaysBox"; from: 1; to: 90; value: Theme.preferences.shuffleRecentDays; onValueModified: Theme.preferences.shuffleRecentDays = value; enabled: Theme.preferences.avoidRecentShuffle; font: Theme.controlFont }
                Label { text: "days"; color: Theme.secondary; font: Theme.controlFont }
            }
            Text { text: "Recently heard songs move toward the end; every queued song remains available. Uses saved listening history, with no audio analysis."; color: Theme.muted; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            DesktopSettings {
                id: desktopSection; controller: root.controller
                monitoring: root.visible && y + height > settingsScroll.contentItem.contentY
                    && y < settingsScroll.contentItem.contentY + settingsScroll.contentItem.height
            }
            Loader { id: audioOutputSection; Layout.fillWidth: true; active: !!root.controller; sourceComponent: AudioOutputSettings { player: root.controller.playbackBackend } }
            Loader { id: equalizerSection; Layout.fillWidth: true; active: !!root.controller; sourceComponent: EqualizerSettings { player: root.controller.playbackBackend } }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
            Label { id: artistSection; text: "Library · artist credits"; color: Theme.text; font: Qt.font({family: Theme.preferences.fontFamily, pointSize: Theme.pointSize, bold: true}) }
            CheckBox {
                objectName: "splitArtistsCheckBox"
                Layout.fillWidth: true
                contentItem: Text { text: parent.text; font: parent.font; color: Theme.text; wrapMode: Text.WordWrap; leftPadding: parent.indicator.width + parent.spacing; verticalAlignment: Text.AlignVCenter }
                text: "Show individual artists in shared credits"
                checked: Theme.preferences.splitArtists
                onToggled: Theme.preferences.splitArtists = checked
                font: Theme.controlFont
            }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.gap(10)
                Repeater {
                    model: [{name: "Comma ,", separator: ",", key: "comma"}, {name: "Semicolon ;", separator: ";", key: "semicolon"}, {name: "Vertical bar |", separator: "|", key: "pipe"}]
                    delegate: CheckBox {
                        required property var modelData
                        objectName: "artistSeparator_" + modelData.key
                        text: modelData.name
                        enabled: Theme.preferences.splitArtists
                        checked: Theme.preferences.artistSeparators.indexOf(modelData.separator) >= 0
                        font: Theme.controlFont
                        onToggled: {
                            const separators = Theme.preferences.artistSeparators;
                            Theme.preferences.artistSeparators = checked ? separators + modelData.separator : separators.replace(modelData.separator, "");
                        }
                    }
                }
            }
            Text { text: "Each artist gets a page and a separate clickable name. Disable any separator used within an artist’s own name. This changes the view immediately; your audio tags and albums stay as saved."; color: Theme.secondary; wrapMode: Text.WordWrap; font: Theme.controlFont; Layout.fillWidth: true }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
            Label { id: displaySection; text: "Display and layout"; color: Theme.text; font: Qt.font({family: Theme.preferences.fontFamily, pointSize: Theme.pointSize, bold: true}) }
            Text {
                Layout.fillWidth: true
                text: "Use your computer’s font and display scaling. Changes apply immediately."
                color: Theme.secondary; wrapMode: Text.WordWrap
                font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(12)
            }
            Text {
                objectName: "systemFontDescription"
                Layout.fillWidth: true
                text: "System font: " + Theme.preferences.fontFamily + " · " + Math.round(Theme.preferences.fontPointSize * 10) / 10 + " pt"
                color: Theme.muted; wrapMode: Text.WordWrap
                font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(11)
            }
            Label { text: "Layout density"; color: Theme.text; font: Theme.controlFont }
            ComboBox {
                objectName: "displayDensityBox"
                Layout.fillWidth: true
                model: ["Automatic", "Compact", "Comfortable"]
                currentIndex: ["automatic", "compact", "comfortable"].indexOf(Theme.preferences.density)
                onActivated: Theme.preferences.density = ["automatic", "compact", "comfortable"][currentIndex]
                Accessible.name: "Layout density"
                font: Theme.controlFont
            }
            Text {
                Layout.fillWidth: true
                text: Theme.preferences.density === "automatic" ? "Fits the available window space automatically."
                    : Theme.preferences.density === "compact" ? "Less spacing and shorter rows, with the same readable text."
                    : "More room around controls and library rows."
                color: Theme.muted; wrapMode: Text.WordWrap
                font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(11)
            }
            Label { text: "Text size · " + Math.round(Theme.preferences.textScale * 100) + "%"; color: Theme.text; font: Theme.controlFont }
            Slider {
                objectName: "displayTextScaleSlider"
                Layout.fillWidth: true
                from: 100; to: 200; stepSize: 5
                value: Math.round(Theme.preferences.textScale * 100)
                onMoved: Theme.preferences.textScale = value / 100
                Accessible.name: "Text size as a percentage of the system font"
            }
            Text {
                objectName: "displayFontPreview"
                text: "Text preview"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap; color: Theme.accent
                font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(16)
            }
            ActionButton {
                objectName: "resetDisplaySettingsButton"
                text: "Use system defaults"; primary: false
                Layout.fillWidth: true
                onClicked: Theme.preferences.reset()
            }
            ActionButton {
                objectName: "songColumnsSettingsButton"; text: "Song columns…"; primary: false; Layout.fillWidth: true
                visible: !!root.controller
                onClicked: { root.close(); root.controller.openSongColumnSettings(); }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
            CheckBox {
                objectName: "reducedMotionCheckBox"
                Layout.fillWidth: true
                contentItem: Text { text: parent.text; font: parent.font; color: Theme.text; wrapMode: Text.WordWrap; leftPadding: parent.indicator.width + parent.spacing; verticalAlignment: Text.AlignVCenter }
                text: "Reduce decorative motion"
                checked: Theme.preferences.reducedMotion
                onToggled: Theme.preferences.reducedMotion = checked
                font: Theme.controlFont
            }
            CheckBox {
                objectName: "highContrastCheckBox"; text: "High contrast text and outlines"; checked: Theme.preferences.highContrast
                onToggled: Theme.preferences.highContrast = checked; font: Theme.controlFont; palette.text: Theme.text; Layout.fillWidth: true
                contentItem: Text { text: parent.text; font: parent.font; color: Theme.text; wrapMode: Text.WordWrap; leftPadding: parent.indicator.width + parent.spacing; verticalAlignment: Text.AlignVCenter }
            }
            CheckBox {
                objectName: "largeControlsCheckBox"; text: "Larger icon controls"; checked: Theme.preferences.largeControls
                onToggled: Theme.preferences.largeControls = checked; font: Theme.controlFont; palette.text: Theme.text; Layout.fillWidth: true
                contentItem: Text { text: parent.text; font: parent.font; color: Theme.text; wrapMode: Text.WordWrap; leftPadding: parent.indicator.width + parent.spacing; verticalAlignment: Text.AlignVCenter }
            }
            Text { text: "Simplifies fades and hover transitions while keeping scrolling smooth."; color: Theme.muted; font: Theme.controlFont; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            CacheSettings { id: cacheSection }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
            KeyboardSettings { id: keyboardSection }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
            Label { id: libraryToolsSection; visible: !!root.controller; text: "Library tools"; color: Theme.text; font: Qt.font({family: Theme.preferences.fontFamily, pointSize: Theme.pointSize, bold: true}) }
            ActionButton { objectName: "playlistMigrationSettingsButton"; visible: !!root.controller; text: "Import from another player…"; primary: false; Layout.fillWidth: true; onClicked: { root.close(); root.controller.openPlaylistMigration(); } }
            ActionButton { objectName: "repairMovedFilesSettingsButton"; visible: !!root.controller; text: "Repair moved files…"; primary: false; Layout.fillWidth: true; onClicked: { root.close(); root.controller.openLibraryTools("repair"); } }
            ActionButton { objectName: "backupRestoreSettingsButton"; visible: !!root.controller; text: "Backup and restore…"; primary: false; Layout.fillWidth: true; onClicked: { root.close(); root.controller.openLibraryTools("backup"); } }
            ActionButton { objectName: "duplicateFinderSettingsButton"; visible: !!root.controller; text: "Find duplicate files…"; primary: false; Layout.fillWidth: true; onClicked: { root.close(); root.controller.openLibraryTools("duplicates"); } }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
            ActionButton {
                id: aboutSection
                objectName: "aboutMusicaButton"
                text: "About Musica"; primary: false
                Layout.fillWidth: true
                onClicked: aboutDialog.open()
            }
        }
    }
    AboutDialog { id: aboutDialog; parent: root.parent }
    footer: Item {
        implicitHeight: closeSettingsButton.implicitHeight + Theme.gap(20)
        Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.border }
        ActionButton {
            id: closeSettingsButton
            objectName: "closeSettingsButton"
            text: "Close"; primary: false
            anchors.right: parent.right; anchors.rightMargin: root.padding
            anchors.verticalCenter: parent.verticalCenter
            onClicked: root.close()
        }
    }
}
