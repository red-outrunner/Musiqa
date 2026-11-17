import QtQuick

Loader {
    id: root
    active: false
    property var track: null
    property var tracks: []
    property var details: ({})
    function prepare() { active = true; }
    function open() {
        prepare();
        if (item.track !== undefined) item.track = track;
        if (item.tracks !== undefined) item.tracks = track ? [] : tracks;
        if (item.details !== undefined) item.details = details;
        item.open();
    }
    function begin(argument) { prepare(); item.begin(argument); }
    function start(argument) { prepare(); item.start(argument); }
    function close() { if (item) item.close(); }
    onLoaded: item.closed.connect(function() {
        Qt.callLater(function() {
            if (root.item && !root.item.visible && !root.item.busy) {
                root.active = false; root.track = null; root.tracks = []; root.details = ({});
            }
        });
    })
}
