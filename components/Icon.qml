import QtQuick
import MusicPlayer 1.0

Item {
    id: root
    property string name: "music"
    property color color: Theme.text
    property bool filled: false
    property real strokeWidth: 1.7
    readonly property var paths: ({
            sidebar: '<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M9 4v16"/>',
            up: '<path d="m5 15 7-7 7 7"/>',
            down: '<path d="m5 9 7 7 7-7"/>',
            columns: '<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M9 4v16M15 4v16"/>',
            list: '<path d="M8 5h13M8 12h13M8 19h13M3 5h1M3 12h1M3 19h1"/>',
            undo: '<path d="M9 4 3 10l6 6M4 10h11a6 6 0 0 1 0 12"/>',
            filter: '<path d="M3 4h18l-7 8v7l-4 2v-9Z"/>',
            copy: '<rect x="8" y="8" width="12" height="13" rx="2"/><path d="M16 8V3H3v13h5"/>',
            save: '<path d="M3 3h14l4 4v14H3Z"/><path d="M7 3v6h10V3M7 21v-8h10v8"/>',
            share: '<circle cx="18" cy="5" r="3"/><circle cx="6" cy="12" r="3"/><circle cx="18" cy="19" r="3"/><path d="m9 10 6-4M9 14l6 4"/>',
            camera: '<path d="M3 7h4l2-3h6l2 3h4v14H3Z"/><circle cx="12" cy="14" r="4"/>',
            download: '<path d="M12 3v12m-5-5 5 5 5-5M4 16v5h16v-5"/>',
            edit: '<path d="m16 3 5 5-12 12-6 1 1-6ZM14 5l5 5"/>',
            settings: '<path d="M4 7h16M4 17h16"/><circle cx="8" cy="7" r="3"/><circle cx="16" cy="17" r="3"/>',
            crossfade: '<path d="M3 6h4c5 0 5 12 10 12h4M3 18h4c5 0 5-12 10-12h4"/>',
            collapse: '<path d="M3 8h5V3m13 5h-5V3M3 16h5v5m13-5h-5v5M3 3l5 5m13-5-5 5M3 21l5-5m13 5-5-5"/>',
            expand: '<path d="M8 3H3v5m13-5h5v5M3 16v5h5m13-5v5h-5M8 8 3 3m13 5 5-5M8 16l-5 5m13-5 5 5"/>',
            moon: '<path d="M20.5 14A9 9 0 0 1 10 3.5 9 9 0 1 0 20.5 14Z"/>',
            grip: '<path d="M8 5h1M15 5h1M8 12h1M15 12h1M8 19h1M15 19h1" stroke-width="3"/>',
            smart: '<path d="m12 3 2.5 6.5L21 12l-6.5 2.5L12 21l-2.5-6.5L3 12l6.5-2.5Z"/>',
            music: '<path d="M9 18V5l12-2v13M9 8l12-2"/><ellipse cx="6" cy="18" rx="3" ry="3"/><ellipse cx="18" cy="16" rx="3" ry="3"/>',
            library: '<path d="M4 5v15M9 3v17M14 5v15M18 4l4 15"/>',
            home: '<path d="m3 10 9-7 9 7v10H3zM9 20v-7h6v7"/>',
            album: '<rect x="3" y="3" width="18" height="18" rx="4"/><circle cx="12" cy="12" r="5"/><circle cx="12" cy="12" r="1"/>',
            artist: '<circle cx="12" cy="8" r="4"/><path d="M4 21v-2a8 8 0 0 1 16 0v2"/>',
            genre: '<path d="M3 3h8l10 10-8 8L3 11Z"/><circle cx="7.5" cy="7.5" r="1.5"/>',
            pin: '<path d="m8 3 8 0-1 6 4 4H5l4-4ZM12 13v9"/>',
            bookmark: '<path d="M5 3h14v19l-7-5-7 5Z"/>',
            lyrics: '<path d="M4 5h16M4 10h12M4 15h16M4 20h9"/>',
            heart: '<path d="M20.8 4.6a5.5 5.5 0 0 0-7.8 0L12 5.7l-1.1-1.1a5.5 5.5 0 0 0-7.8 7.8L12 21l8.8-8.6a5.5 5.5 0 0 0 0-7.8Z"/>',
            search: '<circle cx="10.5" cy="10.5" r="6.5"/><path d="m16 16 5 5"/>',
            plus: '<path d="M12 5v14M5 12h14"/>',
            folder: '<path d="M3 7V4h6l3 3h9v13H3Z"/>',
            play: '<path d="m8 4 12 8-12 8Z" fill="currentColor" stroke="none"/>',
            pause: '<path d="M7 5h3v14H7zM14 5h3v14h-3z" fill="currentColor" stroke="none"/>',
            next: '<path d="m5 5 10 7L5 19Z" fill="currentColor" stroke="none"/><path d="M19 5v14" stroke-width="2.5"/>',
            previous: '<path d="m19 5-10 7 10 7Z" fill="currentColor" stroke="none"/><path d="M5 5v14" stroke-width="2.5"/>',
            shuffle: '<path d="M3 6h3c5 0 7 12 12 12h3m-4-4 4 4-4 4M3 18h3c2 0 3-2 4-4m4-4c1-2 2-4 4-4h3m-4-4 4 4-4 4"/>',
            repeat: '<path d="m17 2 4 4-4 4M3 11V9a3 3 0 0 1 3-3h15M7 22l-4-4 4-4m14-1v2a3 3 0 0 1-3 3H3"/>',
            volume: '<path d="m11 4-6 5H2v6h3l6 5Z"/><path d="M15 8a6 6 0 0 1 0 8m3-11a10 10 0 0 1 0 14"/>',
            mute: '<path d="m11 4-6 5H2v6h3l6 5ZM17 9l5 6m0-6-5 6"/>',
            queue: '<path d="M4 6h16M4 12h16M4 18h10m4-3 4 3-4 3Z"/>',
            arrow: '<path d="M4 12h16m-6-6 6 6-6 6"/>',
            back: '<path d="m14 5-7 7 7 7"/>',
            chevron: '<path d="m8 10 4 4 4-4"/>',
            clock: '<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>',
            refresh: '<path d="M20 7a9 9 0 1 0 1 9M20 2v6h-6"/>',
            close: '<path d="m6 6 12 12M6 18 18 6"/>',
            check: '<path d="m5 12 4 4L19 6"/>',
            headphones: '<path d="M3 14v-2a9 9 0 0 1 18 0v2M3 12h4v9H3zm14 0h4v9h-4z"/>',
            sort: '<path d="M4 6h16M4 12h11M4 18h6"/>',
            sortAscending: '<path d="M5 19V5m-3 3 3-3 3 3M12 6h4M12 12h7M12 18h10"/>',
            sortDescending: '<path d="M5 5v14m-3-3 3 3 3-3M12 6h10M12 12h7M12 18h4"/>',
            grid: '<rect x="3" y="3" width="7" height="7" rx="1"/><rect x="14" y="3" width="7" height="7" rx="1"/><rect x="3" y="14" width="7" height="7" rx="1"/><rect x="14" y="14" width="7" height="7" rx="1"/>',
            more: '<circle cx="5" cy="12" r="1"/><circle cx="12" cy="12" r="1"/><circle cx="19" cy="12" r="1"/>',
            played: '<path d="M4 20v-7M9 20V6M14 20v-9M19 20V3"/>'
        })
    implicitWidth: Theme.dp(22)
    implicitHeight: Theme.dp(22)
    Image {
        anchors.fill: parent
        sourceSize.width: width * 2
        sourceSize.height: height * 2
        source: "data:image/svg+xml;utf8," + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="' + (root.filled ? root.color : 'none') + '" color="' + root.color + '" stroke="' + root.color + '" stroke-width="' + root.strokeWidth + '" stroke-linecap="round" stroke-linejoin="round">' + (root.paths[root.name] || root.paths.music) + '</svg>')
    }
}
