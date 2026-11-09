import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs as NativeDialogs
import QtMultimedia
import MusicPlayer 1.0
import "components"

ApplicationWindow {
    id: win
    objectName: "musicaWindow"
    visible: true
    width: 1280
    height: 850
    minimumWidth: 900
    minimumHeight: 640
    title: (currentTrack ? currentTrack.title + " · Musica " : "Musica ") + Qt.application.version
    color: "#131918"
    font.family: "Noto Sans"
    font.pixelSize: 13
    palette.window: "#202e24"
    palette.base: "#16231b"
    palette.text: "#edf2ef"
    palette.windowText: "#edf2ef"
    palette.button: "#344b3a"
    palette.buttonText: "#edf2ef"
    palette.highlight: "#53765f"
    palette.highlightedText: "#ffffff"

    readonly property color accent: "#a7e8cb"
    readonly property color textColor: "#edf2ef"
    readonly property color secondary: "#8f9c96"
    readonly property var libraryBackend: library
    readonly property var playbackBackend: player
    property string activePage: "Overview"
    property bool miniPlayerActive: false
    property bool shuttingDown: false
    property string collectionName: ""
    property string collectionKind: ""
    property string collectionArtist: ""
    property string collectionId: ""
    property string searchQuery: ""
    property string currentUrl: ""
    property var selectedTrack: null
    property var queueSource: []
    property var queueItems: []
    readonly property var queueUrls: queueItems.map(item => item.fileUrl)
    property string currentQueueId: ""
    property int queueSerial: 0
    readonly property int queueCount: queueItems.length
    readonly property int currentQueueIndex: queueItems.findIndex(item => item.id === currentQueueId)
    property bool nowPlayingOpen: false
    property bool shuffleEnabled: false
    property int repeatMode: 0
    property bool queueOpen: false
    property bool muted: false
    property string sortBy: "Title"
    property string toastMessage: ""
    property bool sessionRestored: false
    property bool restoringSession: false
    property double restorePosition: -1
    property bool desktopControlsAvailable: false
    property string pendingOpenUrl: ""
    signal playbackSeeked(double position)
    readonly property bool isPlaying: player.playbackState === MediaPlayer.PlayingState
    readonly property bool editingText: !!activeFocusItem && activeFocusItem.cursorPosition !== undefined
    readonly property var currentTrack: {
        if (!currentUrl || !library.trackCount)
            return selectedTrack;
        const track = library.trackForUrl(currentUrl);
        return track.fileUrl ? track : selectedTrack;
    }
    readonly property var albums: library.albums
    readonly property var artists: library.artists
    readonly property int favoriteCount: library.favoriteCount
    readonly property var visibleTracks: activePage === "Most Played" ? library.mostPlayed : library.songs
    readonly property var visibleGroups: activePage === "Artists" ? artists : activePage === "Playlists" ? library.playlists : albums
    readonly property string pageTitle: collectionName || (activePage === "Overview" ? "Made for your everyday." : activePage === "Songs" ? "All songs" : activePage)

    function navigate(page) {
        nowPlayingOpen = false;
        queueOpen = false;
        activePage = page;
        sortBy = page === "Most Played" ? "Plays" : "Title";
        collectionName = "";
        collectionKind = "";
        collectionArtist = "";
        collectionId = "";
        searchField.clear();
        contentScroll.contentY = 0;
        groupGrid.positionViewAtBeginning();
        songList.positionViewAtBeginning();
    }
    function showGroup(group) {
        collectionKind = activePage === "Artists" ? "artist" : activePage === "Playlists" ? "playlist" : "album";
        sortBy = collectionKind === "album" ? "Track" : collectionKind === "playlist" ? "Order" : "Title";
        collectionName = group.name;
        collectionId = group.id;
        collectionArtist = collectionKind === "album" ? group.subtitle : "";
        searchField.clear();
        contentScroll.contentY = 0;
        songList.positionViewAtBeginning();
    }
    function playGroup(group, kind) {
        const tracks = library.tracksForGroup(kind, group.id);
        if (tracks.length)
            playTrack(tracks[0], tracks);
    }
    function formatTime(ms) {
        const seconds = Math.max(0, Math.floor(ms / 1000));
        return Math.floor(seconds / 60) + ":" + (seconds % 60 < 10 ? "0" : "") + seconds % 60;
    }
    function showToast(message) {
        toastMessage = message;
        toastTimer.restart();
    }
    function trackForUrl(url) {
        if (!library.trackCount)
            return null;
        const track = library.trackForUrl(url);
        return track.fileUrl ? track : null;
    }
    function shuffled(urls) {
        const result = urls.slice();
        for (let i = result.length - 1; i > 0; --i) {
            const j = Math.floor(Math.random() * (i + 1));
            const value = result[i];
            result[i] = result[j];
            result[j] = value;
        }
        return result;
    }
    function playUrl(url) {
        const entry = queueItems.find(item => item.fileUrl === url);
        if (entry)
            playQueueItem(entry.id);
        else {
            const track = trackForUrl(url);
            if (track)
                playTrack(track, [track]);
        }
    }
    function newQueueItem(url) {
        return {
            id: String(++queueSerial),
            fileUrl: url
        };
    }
    function playQueueItem(id) {
        const entry = queueItems.find(item => item.id === id);
        if (!entry)
            return;
        const track = trackForUrl(entry.fileUrl);
        if (!track) {
            showToast("This song is no longer in your library.");
            return;
        }
        currentQueueId = id;
        restorePosition = -1;
        selectedTrack = track;
        currentUrl = entry.fileUrl;
        player.playEntry(id, entry.fileUrl, track);
        prepareTimer.restart();
    }
    function playTrack(track, tracks) {
        if (!track)
            return;
        queueSource = tracks.map(t => newQueueItem(t.fileUrl));
        const selected = queueSource.find(item => item.fileUrl === track.fileUrl);
        if (!selected)
            return;
        let items = shuffleEnabled ? shuffled(queueSource) : queueSource.slice();
        if (shuffleEnabled) {
            items = items.filter(item => item.id !== selected.id);
            items.unshift(selected);
        }
        queueItems = items;
        playQueueItem(selected.id);
    }
    function enqueueTrack(track, playNext) {
        if (!track)
            return;
        const entry = newQueueItem(track.fileUrl);
        let original = queueSource.slice();
        let active = queueItems.slice();
        const originalIndex = original.findIndex(item => item.id === currentQueueId);
        if (playNext && currentQueueIndex >= 0) {
            active.splice(currentQueueIndex + 1, 0, entry);
            original.splice(originalIndex + 1, 0, entry);
        } else {
            active.push(entry);
            original.push(entry);
        }
        queueSource = original;
        queueItems = active;
        showToast(playNext ? "Playing next: " + track.title : "Added to queue: " + track.title);
    }
    function stopAndClearTrack() {
        library.beginPlayback("");
        player.stop();
        player.source = "";
        currentUrl = "";
        selectedTrack = null;
        currentQueueId = "";
        nowPlayingOpen = false;
    }
    function removeQueueItem(id) {
        const index = queueItems.findIndex(item => item.id === id);
        if (index < 0)
            return;
        const wasCurrent = id === currentQueueId;
        queueSource = queueSource.filter(item => item.id !== id);
        queueItems = queueItems.filter(item => item.id !== id);
        if (wasCurrent) {
            if (queueItems.length)
                playQueueItem(queueItems[Math.min(index, queueItems.length - 1)].id);
            else
                stopAndClearTrack();
        }
    }
    function removeDeletedSong(url) {
        const index = currentQueueIndex;
        const wasCurrent = currentUrl === url;
        queueSource = queueSource.filter(item => item.fileUrl !== url);
        queueItems = queueItems.filter(item => item.fileUrl !== url);
        if (wasCurrent) {
            if (queueItems.length)
                playQueueItem(queueItems[Math.min(Math.max(index, 0), queueItems.length - 1)].id);
            else
                stopAndClearTrack();
        }
    }
    function openArtist(track) {
        if (!track)
            return;
        navigate("Artists");
        showGroup({
            id: track.artistId,
            name: track.artist,
            subtitle: ""
        });
    }
    function openAlbum(track) {
        if (!track)
            return;
        navigate("Albums");
        showGroup({
            id: track.albumId,
            name: track.album,
            subtitle: track.albumArtist
        });
    }
    function openTrackMenu(track, anchor, queueId) {
        if (!track)
            return;
        trackMenu.track = track;
        trackMenu.queueId = queueId || "";
        trackMenu.inPlaylist = collectionKind === "playlist" && !!collectionName && track.playlistIds && track.playlistIds.indexOf(collectionId) >= 0;
        trackMenu.parent = anchor;
        trackMenu.x = Math.max(0, anchor.width - trackMenu.width);
        trackMenu.y = anchor.height;
        trackMenu.open();
    }
    function handleTrackAction(action) {
        const track = trackMenu.track;
        if (!track)
            return;
        if (action === "queue" || action === "next")
            enqueueTrack(track, action === "next");
        else if (action === "play") {
            if (trackMenu.queueId)
                playQueueItem(trackMenu.queueId);
            else {
                const tracks = visibleTracks.entries();
                playTrack(track, tracks.some(t => t.fileUrl === track.fileUrl) ? tracks : [track]);
            }
        } else if (action === "favorite")
            library.toggleFavorite(track.fileUrl);
        else if (action === "artist")
            openArtist(track);
        else if (action === "album")
            openAlbum(track);
        else if (action === "playlist") {
            playlistDialog.track = track;
            playlistDialog.open();
        } else if (action === "removePlaylist")
            library.removeFromPlaylist(collectionId, track.fileUrl);
        else if (action === "info") {
            infoDialog.details = library.trackInfo(track.fileUrl);
            infoDialog.open();
        } else if (action === "reveal")
            library.revealTrack(track.fileUrl);
        else if (action === "editMetadata") metadataEditor.begin(track.fileUrl);
        else if (action === "trash") {
            trashDialog.track = track;
            trashDialog.open();
        }
    }
    function playAll(shuffle) {
        if (visibleTracks.count === 0)
            return;
        shuffleEnabled = shuffle;
        const first = visibleTracks.get(shuffle ? Math.floor(Math.random() * visibleTracks.count) : 0);
        playTrack(first, visibleTracks.entries());
    }
    function togglePlayback() {
        if (isPlaying)
            player.pause();
        else if (currentUrl) {
            if (player.mediaStatus === MediaPlayer.EndOfMedia)
                library.beginPlayback(currentUrl);
            player.play();
        } else if (queueItems.length)
            playQueueItem(queueItems[0].id);
        else
            playAll(false);
    }
    function nextTrack(automatic) {
        if (!queueUrls.length)
            return;
        if (automatic && repeatMode === 2) {
            player.playEntry(currentQueueId, currentUrl, currentTrack || {});
            playbackSeeked(0);
            prepareTimer.restart();
            return;
        }
        let index = currentQueueIndex + 1;
        if (index >= queueUrls.length) {
            if (automatic && repeatMode === 0)
                return;
            index = 0;
        }
        playQueueItem(queueItems[index].id);
    }
    function previousTrack() {
        if (player.position > 3000) {
            player.playEntry(currentQueueId, currentUrl, currentTrack || {});
            playbackSeeked(0);
            prepareTimer.restart();
            return;
        }
        if (!queueUrls.length)
            return;
        const index = currentQueueIndex;
        playQueueItem(queueItems[(index - 1 + queueItems.length) % queueItems.length].id);
    }
    function recordListeningSample() {
        player.checkpoint();
    }
    function seekPlayback(position) {
        recordListeningSample();
        player.position = position;
        prepareTimer.restart();
        playbackSeeked(position);
    }
    function persistSession() {
        if (!sessionRestored || restoringSession || !library.libraryReady) return;
        recordListeningSample();
        library.savePlaybackSession({queue: queueItems, source: queueSource, currentId: currentQueueId,
            position: restorePosition >= 0 ? restorePosition : player.position,
            shuffle: shuffleEnabled, repeat: repeatMode, serial: queueSerial, muted: muted});
    }
    function restoreSession() {
        if (sessionRestored || !library.libraryReady) return;
        restoringSession = true;
        const saved = library.playbackSession();
        queueSource = saved.source || [];
        shuffleEnabled = !!saved.shuffle;
        repeatMode = saved.repeat || 0;
        muted = !!saved.muted;
        queueSerial = saved.serial || 0;
        queueItems = saved.queue || [];
        currentQueueId = saved.currentId || "";
        const entry = queueItems.find(item => item.id === currentQueueId);
        if (entry) {
            selectedTrack = trackForUrl(entry.fileUrl);
            currentUrl = entry.fileUrl;
            library.beginPlayback(currentUrl);
            restorePosition = saved.position || 0;
            player.source = currentUrl;
            applyRestoredPosition();
        }
        restoringSession = false;
        sessionRestored = true;
    }
    function applyRestoredPosition() {
        if (restorePosition < 0 || player.mediaStatus !== MediaPlayer.LoadedMedia || !player.seekable) return;
        const position = Math.min(restorePosition, Math.max(0, player.duration - 1));
        restorePosition = -1;
        library.seekPlayback(currentUrl, position);
        player.position = position;
        playbackSeeked(position);
    }
    function moveQueueItem(id, beforeId) {
        if (id === beforeId) return;
        const item = queueItems.find(entry => entry.id === id);
        if (!item) return;
        const move = function(items) {
            const result = items.filter(entry => entry.id !== id);
            const index = beforeId ? result.findIndex(entry => entry.id === beforeId) : result.length;
            result.splice(index < 0 ? result.length : index, 0, item);
            return result;
        };
        queueSource = move(queueSource);
        queueItems = move(queueItems);
    }
    function movePlaylistSong(url, beforeUrl) {
        if (collectionKind === "playlist" && sortBy === "Order" && !searchQuery)
            library.movePlaylistTrack(collectionId, url, beforeUrl);
    }
    function openSmart(id, name) {
        navigate("Smart playlists");
        collectionKind = "smart";
        collectionId = id;
        collectionName = name;
        sortBy = id === "recent" ? "Added" : id === "rare" ? "Rare" : "Title";
    }
    function openPlaylistMenu(group, anchor) {
        playlistMenu.playlist = group;
        playlistMenu.parent = anchor;
        playlistMenu.popup();
    }
    function quitPlayer() {
        if (shuttingDown) return;
        shuttingDown = true;
        miniPlayerActive = false;
        miniPlayer.allowClose = true;
        sessionSave.stop(); prepareTimer.stop();
        player.pause();
        persistSession();
        miniPlayer.hide(); win.hide();
        Qt.quit();
    }
    function openMiniPlayer() {
        miniPlayerActive = true;
        win.showMinimized();
        miniPlayer.openOnScreen(win);
    }
    function restoreMainPlayer() {
        miniPlayerActive = false;
        win.showNormal(); win.raise(); win.requestActivate();
        miniPlayer.hide();
    }
    function desktopCommand(command, value) {
        if (command === "toggle") togglePlayback();
        else if (command === "play") { if (!isPlaying) togglePlayback(); }
        else if (command === "pause") player.pause();
        else if (command === "stop") { recordListeningSample(); player.stop(); library.beginPlayback(currentUrl); playbackSeeked(0); }
        else if (command === "next") { if (currentQueueIndex + 1 < queueItems.length || repeatMode !== 0) nextTrack(false); }
        else if (command === "previous") previousTrack();
        else if (command === "seek") seekPlayback(value);
        else if (command === "shuffle") shuffleEnabled = value;
        else if (command === "repeat") repeatMode = value;
        else if (command === "volume") { muted = false; library.volume = value; }
        else if (command === "raise") restoreMainPlayer();
        else if (command === "quit") quitPlayer();
        else if (command === "open") {
            if (trackForUrl(value)) playUrl(value);
            else { pendingOpenUrl = value; library.addFiles([value]); }
        }
    }
    function prepareNextTrack() {
        if (!currentUrl || !queueItems.length) { player.prepareNext("", "", {}); return; }
        const current = library.trackForUrl(currentUrl);
        player.updateTrackInfo(currentUrl, current);
        let index = repeatMode === 2 ? currentQueueIndex : currentQueueIndex + 1;
        if (index >= queueItems.length) index = repeatMode === 1 ? 0 : -1;
        const next = index >= 0 ? queueItems[index] : null;
        player.prepareNext(next ? next.id : "", next ? next.fileUrl : "", next ? library.trackForUrl(next.fileUrl) : {});
    }
    onClosing: function(event) { event.accepted = true; quitPlayer(); }
    onVisibilityChanged: function(visibility) {
        if (miniPlayerActive && (visibility === Window.Windowed || visibility === Window.Maximized || visibility === Window.FullScreen))
            restoreMainPlayer();
    }
    onQueueItemsChanged: { sessionSave.restart(); prepareTimer.restart(); }
    onCurrentQueueIdChanged: { sessionSave.restart(); prepareTimer.restart(); }
    onRepeatModeChanged: { sessionSave.restart(); prepareTimer.restart(); }
    onMutedChanged: sessionSave.restart()
    onShuffleEnabledChanged: {
        sessionSave.restart();
        if (restoringSession || !queueSource.length)
            return;
        let items = shuffleEnabled ? shuffled(queueSource) : queueSource.slice();
        const current = items.find(item => item.id === currentQueueId);
        if (shuffleEnabled && current) {
            items = items.filter(item => item.id !== currentQueueId);
            items.unshift(current);
        }
        queueItems = items;
    }
    Timer { id: sessionSave; interval: 400; onTriggered: win.persistSession() }
    Timer { id: prepareTimer; interval: 40; onTriggered: win.prepareNextTrack() }
    Timer { interval: 5000; repeat: true; running: win.sessionRestored; onTriggered: win.persistSession() }

    TrackListModel {
        id: library
        objectName: "library"
        onLibraryReadyChanged: win.restoreSession()
        onTracksChanged: {
            prepareTimer.restart();
            if (win.pendingOpenUrl && win.trackForUrl(win.pendingOpenUrl)) {
                const url = win.pendingOpenUrl;
                win.pendingOpenUrl = "";
                win.playUrl(url);
            }
        }
        onImportError: function (message) {
            win.showToast(message);
        }
        onAlbumMerged: function (previousId, albumId) {
            if (win.collectionKind === "album" && win.collectionId === previousId)
                win.collectionId = albumId;
        }
        onTrackRemoved: function (url) {
            win.removeDeletedSong(url);
        }
    }
    Binding {
        target: library.songs
        property: "search"
        value: win.searchQuery
    }
    Binding {
        target: library.songs
        property: "favoritesOnly"
        value: win.activePage === "Favorites"
    }
    Binding {
        target: library.songs
        property: "collectionKind"
        value: win.collectionKind
    }
    Binding {
        target: library.songs
        property: "collectionId"
        value: win.collectionName ? win.collectionId : ""
    }
    Binding {
        target: library.songs
        property: "sortKey"
        value: win.sortBy
    }
    Binding {
        target: library.albums
        property: "search"
        value: win.searchQuery
    }
    Binding {
        target: library.artists
        property: "search"
        value: win.searchQuery
    }
    Binding {
        target: library.mostPlayed
        property: "search"
        value: win.searchQuery
    }
    Binding {
        target: library.playlists
        property: "search"
        value: win.searchQuery
    }
    TrackMenu {
        id: trackMenu
        objectName: "trackMenu"
        onActionRequested: function (action) {
            win.handleTrackAction(action);
        }
    }
    PlaylistDialog {
        id: playlistDialog
        objectName: "playlistDialog"
        parent: Overlay.overlay
        library: win.libraryBackend
        onSaved: function (name) {
            win.showToast("Saved to playlist: " + name);
        }
    }
    AlbumEditorDialog {
        id: albumEditor
        objectName: "albumEditor"
        parent: Overlay.overlay
        library: win.libraryBackend
        onGroupingSaved: { win.navigate("Albums"); win.showToast("Album grouping updated." + (library.canUndoAlbumEdit ? " Undo is available in Manage albums." : "")); }
    }
    Menu {
        id: playlistMenu
        objectName: "playlistMenu"
        property var playlist: ({})
        palette.window: "#26342b"
        palette.text: "#e5eee6"
        palette.windowText: "#e5eee6"
        palette.highlight: "#3d5846"
        MenuItem {
            objectName: "renamePlaylistAction"
            text: "Rename playlist"
            onTriggered: { playlistRenameField.text = playlistMenu.playlist.name; renamePlaylistDialog.open(); }
        }
        MenuItem {
            objectName: "exportPlaylistAction"
            text: "Export M3U playlist…"
            onTriggered: {
                exportPlaylistDialog.selectedFile = library.musicFolder.toString() + "/" + playlistMenu.playlist.name.replace(/[/\\:*?\"<>|]/g, "_") + ".m3u8";
                exportPlaylistDialog.open();
            }
        }
        MenuSeparator {}
        MenuItem { objectName: "deletePlaylistAction"; text: "Delete playlist…"; onTriggered: deletePlaylistDialog.open() }
    }
    Dialog {
        id: renamePlaylistDialog
        objectName: "renamePlaylistDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 400
        modal: true
        title: "Rename playlist"
        standardButtons: Dialog.Save | Dialog.Cancel
        palette.windowText: "#e5eee6"
        palette.text: "#e5eee6"
        palette.button: "#344b3a"
        palette.buttonText: "#e5eee6"
        background: Rectangle { color: "#202e24"; radius: 12; border.color: "#405b47" }
        contentItem: TextField {
            id: playlistRenameField
            objectName: "playlistRenameField"
            color: "#edf2ef"
            selectByMouse: true
            maximumLength: 80
            Accessible.name: "Playlist name"
            background: Rectangle { color: "#16231b"; radius: 6; border.color: "#405b47" }
        }
        onOpened: { playlistRenameField.forceActiveFocus(); playlistRenameField.selectAll(); }
        onAccepted: {
            if (library.renamePlaylist(playlistMenu.playlist.id, playlistRenameField.text)) {
                if (win.collectionId === playlistMenu.playlist.id) win.collectionName = playlistRenameField.text.trim();
                win.showToast("Playlist renamed.");
            }
        }
    }
    Dialog {
        id: deletePlaylistDialog
        objectName: "deletePlaylistDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 410
        modal: true
        title: "Delete playlist?"
        standardButtons: Dialog.Ok | Dialog.Cancel
        palette.windowText: "#e5eee6"
        palette.text: "#e5eee6"
        palette.button: "#344b3a"
        palette.buttonText: "#e5eee6"
        background: Rectangle { color: "#202e24"; radius: 12; border.color: "#405b47" }
        contentItem: Text { text: "Delete “" + (playlistMenu.playlist.name || "this playlist") + "”? Your music files stay in your library."; wrapMode: Text.WordWrap; color: "#c4d5c8" }
        onOpened: { standardButton(Dialog.Ok).text = "Delete playlist"; standardButton(Dialog.Ok).objectName = "confirmDeletePlaylist"; }
        onAccepted: {
            if (library.deletePlaylist(playlistMenu.playlist.id)) {
                win.navigate("Playlists"); win.showToast("Playlist deleted.");
            }
        }
    }
    SongInfoDialog {
        id: infoDialog
        objectName: "songInfoDialog"
        parent: Overlay.overlay
    }
    MetadataEditor {
        id: metadataEditor
        objectName: "metadataEditor"
        parent: Overlay.overlay
        library: win.libraryBackend
        onSaved: function(url) {
            const track = library.trackForUrl(url);
            if (win.currentUrl === url) win.selectedTrack = track;
            if (win.collectionKind === "album" && win.collectionName && library.tracksForGroup("album", win.collectionId).length === 0) {
                win.navigate("Albums"); win.showGroup({id: track.albumId, name: track.album, subtitle: track.albumArtist});
            }
            win.showToast("Metadata and artwork saved to the audio file.");
        }
    }
    MiniPlayer {
        id: miniPlayer
        objectName: "miniPlayer"
        track: win.currentTrack
        player: win.playbackBackend
        playing: win.isPlaying
        onRestoreRequested: win.restoreMainPlayer()
        onQuitRequested: win.quitPlayer()
        onVisibleChanged: if (win.miniPlayerActive && !visible) win.restoreMainPlayer()
        onToggleRequested: win.togglePlayback()
        onNextRequested: win.nextTrack(false)
        onPreviousRequested: win.previousTrack()
        onFavoriteRequested: if (win.currentUrl) library.toggleFavorite(win.currentUrl)
        onSeekRequested: function(position) { win.seekPlayback(position); }
    }
    PlaybackOptions {
        id: playbackOptions
        objectName: "playbackOptions"
        parent: Overlay.overlay
        player: win.playbackBackend
        x: win.width - width - 20
        y: win.height - playerBar.height - height - 10
    }
    SleepTimerPopup {
        id: sleepPopup
        objectName: "sleepTimerPopup"
        parent: Overlay.overlay
        player: win.playbackBackend
        x: win.width - width - 20
        y: win.height - playerBar.height - height - 10
    }
    Dialog {
        id: trashDialog
        objectName: "trashSongDialog"
        property var track: null
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 430
        modal: true
        title: "Delete song?"
        padding: 22
        standardButtons: Dialog.Ok | Dialog.Cancel
        palette.windowText: "#e5eee6"
        palette.text: "#e5eee6"
        palette.button: "#344b3a"
        palette.buttonText: "#e5eee6"
        background: Rectangle {
            color: "#202e24"
            radius: 12
            border.color: "#405b47"
        }
        contentItem: Text {
            text: "Move “" + (trashDialog.track ? trashDialog.track.title : "this song") + "” to Trash?\n\nIt will also be removed from your queue and playlists. You can restore the file from your system Trash."
            color: "#c4d5c8"
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }
        onOpened: {
            standardButton(Dialog.Ok).text = "Move to Trash";
            standardButton(Dialog.Ok).objectName = "confirmTrashButton";
        }
        onAccepted: {
            if (!track)
                return;
            const url = track.fileUrl;
            const wasCurrent = win.currentUrl === url;
            const wasPlaying = win.isPlaying;
            const oldPosition = player.position;
            if (wasCurrent) {
                player.stop();
                player.source = "";
            }
            if (library.trashTrack(url))
                win.showToast("Moved to Trash: " + track.title);
            else if (wasCurrent) {
                player.source = url;
                player.position = oldPosition;
                if (wasPlaying)
                    player.play();
            }
        }
    }
    PlaybackEngine {
        id: player
        objectName: "player"
        entryId: win.currentQueueId
        audioOutput.volume: library.volume
        audioOutput.muted: win.muted
        onDurationChanged: win.applyRestoredPosition()
        onSeekableChanged: win.applyRestoredPosition()
        onMediaStatusChanged: win.applyRestoredPosition()
        onTrackAdvanced: function(id, url) {
            win.currentQueueId = id;
            const track = library.trackForUrl(url.toString());
            win.currentUrl = track.fileUrl || url.toString();
            win.selectedTrack = track;
            prepareTimer.restart();
        }
        onAdvanceRequested: win.nextTrack(true)
        onListeningProgress: function(token, url, position, duration, playing, finished, timestamp) {
            library.observeAudiblePlayback(token, url.toString(), position, duration, playing, finished, timestamp);
        }
        onListeningSeeked: function(token, position, timestamp) { library.seekAudiblePlayback(token, position, timestamp); }
        onSleepChanged: if (!sleepAfterTrack) prepareTimer.restart()
        onSleepTriggered: { win.persistSession(); win.showToast("Sleep timer · Playback stopped."); }
        onErrorOccurred: function (error, errorString) {
            win.showToast("Couldn't play this track. " + errorString);
        }
    }
    NativeDialogs.FileDialog {
        id: fileDialog
        title: "Add music to your library"
        currentFolder: library.musicFolder
        fileMode: NativeDialogs.FileDialog.OpenFiles
        nameFilters: ["Audio files (*.mp3 *.flac *.wav *.ogg *.oga *.opus *.m4a *.aac *.aif *.aiff *.wma)", "All files (*)"]
        onAccepted: library.addFiles(selectedFiles)
    }
    NativeDialogs.FolderDialog {
        id: folderDialog
        title: "Choose a music folder"
        currentFolder: library.musicFolder
        onAccepted: library.addFolder(selectedFolder)
    }
    NativeDialogs.FileDialog {
        id: importPlaylistDialog
        objectName: "importPlaylistDialog"
        title: "Import M3U playlist"
        currentFolder: library.musicFolder
        fileMode: NativeDialogs.FileDialog.OpenFile
        nameFilters: ["M3U playlists (*.m3u *.m3u8)"]
        onAccepted: { if (library.importPlaylist(selectedFile)) { win.navigate("Playlists"); win.showToast("Playlist imported."); } }
    }
    NativeDialogs.FileDialog {
        id: exportPlaylistDialog
        objectName: "exportPlaylistDialog"
        title: "Export M3U playlist"
        fileMode: NativeDialogs.FileDialog.SaveFile
        defaultSuffix: "m3u8"
        nameFilters: ["M3U8 playlist (*.m3u8)", "M3U playlist (*.m3u)"]
        onAccepted: { if (library.exportPlaylist(playlistMenu.playlist.id, selectedFile)) win.showToast("Playlist exported."); }
    }
    Shortcut {
        sequence: "Ctrl+Q"
        context: Qt.ApplicationShortcut
        onActivated: win.quitPlayer()
    }
    Shortcut {
        sequence: "Ctrl+M"
        context: Qt.ApplicationShortcut
        onActivated: if (win.miniPlayerActive) win.restoreMainPlayer(); else win.openMiniPlayer();
    }
    Shortcut {
        sequence: "Space"
        enabled: !win.editingText
        onActivated: win.togglePlayback()
    }
    Shortcut {
        sequence: "Ctrl+Right"
        enabled: !win.editingText
        onActivated: win.nextTrack(false)
    }
    Shortcut {
        sequence: "Ctrl+Left"
        enabled: !win.editingText
        onActivated: win.previousTrack()
    }
    Shortcut {
        sequence: "Ctrl+F"
        onActivated: searchField.forceActiveFocus()
    }
    Shortcut {
        sequence: "Ctrl+O"
        onActivated: fileDialog.open()
    }
    Shortcut {
        sequence: "Escape"
        onActivated: {
            win.queueOpen = false;
            win.nowPlayingOpen = false;
            searchField.focus = false;
        }
    }

    Rectangle {
        id: sidebar
        width: win.width < 1050 ? 190 : 222
        anchors {
            left: parent.left
            top: parent.top
            bottom: playerBar.top
        }
        color: "#101614"
        Rectangle {
            anchors.right: parent.right
            width: 1
            height: parent.height
            color: "#232b27"
        }

        Row {
            x: 26
            y: 35
            spacing: 10
            Image {
                source: "assets/musica.svg"
                width: 30
                height: 30
            }
            Text {
                text: "musica."
                color: win.textColor
                font.pixelSize: 26
                font.weight: Font.Bold
                font.letterSpacing: -1
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        Text {
            x: 27
            y: 109
            text: "YOUR SPACE"
            color: "#617369"
            font.pixelSize: 10
            font.weight: Font.DemiBold
            font.letterSpacing: 1.7
        }
        Flickable {
            id: sidebarScroll
            x: 14
            y: 139
            width: parent.width - 28
            height: parent.height - y - 58
            contentWidth: width
            contentHeight: sidebarNavigation.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            LibraryScrollHandler { view: sidebarScroll }
        Column {
            id: sidebarNavigation
            width: parent.width
            spacing: win.height < 740 ? 4 : 7
            Repeater {
                model: [
                    {
                        page: "Overview",
                        icon: "home"
                    },
                    {
                        page: "Songs",
                        icon: "music"
                    },
                    {
                        page: "Albums",
                        icon: "album"
                    },
                    {
                        page: "Artists",
                        icon: "artist"
                    },
                    {
                        page: "Smart playlists",
                        icon: "smart"
                    },
                    {
                        page: "Listening dashboard",
                        icon: "played"
                    }
                ]
                delegate: Button {
                    id: navButton
                    required property var modelData
                    objectName: "nav" + modelData.page
                    width: parent.width
                    height: win.height < 740 ? 34 : 44
                    hoverEnabled: true
                    Accessible.name: modelData.page
                    onClicked: win.navigate(modelData.page)
                    background: Rectangle {
                        radius: 7
                        color: win.activePage === navButton.modelData.page ? "#24372d" : navButton.hovered ? "#1c2721" : "transparent"
                        border.width: navButton.visualFocus ? 1 : 0
                        border.color: win.accent
                    }
                    contentItem: RowLayout {
                        spacing: 13
                        Icon {
                            name: navButton.modelData.icon
                            color: win.activePage === navButton.modelData.page ? win.accent : "#8f9e95"
                            Layout.preferredWidth: 19
                            Layout.preferredHeight: 19
                            Layout.leftMargin: 12
                        }
                        Text {
                            text: navButton.modelData.page
                            color: win.activePage === navButton.modelData.page ? win.accent : "#a4b0a9"
                            font.pixelSize: navButton.modelData.page === "Listening dashboard" ? 10 : 12
                            font.weight: win.activePage === navButton.modelData.page ? Font.DemiBold : Font.Normal
                            Layout.fillWidth: true
                        }
                        Rectangle {
                            Layout.preferredWidth: 5
                            Layout.preferredHeight: 5
                            radius: 3
                            color: win.accent
                            visible: win.activePage === navButton.modelData.page
                            Layout.rightMargin: 10
                        }
                    }
                }
            }
            Rectangle {
                width: parent.width - 22
                height: 1
                color: "#273029"
                anchors.horizontalCenter: parent.horizontalCenter
                Layout.topMargin: 12
            }
            Text {
                text: "YOUR COLLECTION"
                x: 13
                height: win.height < 740 ? 28 : 44
                verticalAlignment: Text.AlignVCenter
                color: "#617369"
                font.pixelSize: 10
                font.weight: Font.DemiBold
                font.letterSpacing: 1.5
            }
            Button {
                id: favoritesNav
                objectName: "navFavorites"
                width: parent.width
                height: win.height < 740 ? 34 : 44
                hoverEnabled: true
                Accessible.name: "Favorites"
                onClicked: win.navigate("Favorites")
                background: Rectangle {
                    radius: 7
                    color: win.activePage === "Favorites" ? "#24372d" : favoritesNav.hovered ? "#1c2721" : "transparent"
                    border.width: favoritesNav.visualFocus ? 1 : 0
                    border.color: win.accent
                }
                contentItem: RowLayout {
                    spacing: 13
                    Icon {
                        name: "heart"
                        color: win.favoriteCount > 0 ? "#ef4444" : win.activePage === "Favorites" ? win.accent : "#8f9e95"
                        filled: win.favoriteCount > 0
                        Layout.preferredWidth: 19
                        Layout.preferredHeight: 19
                        Layout.leftMargin: 12
                    }
                    Text {
                        text: "Favorites"
                        color: win.activePage === "Favorites" ? win.accent : "#a4b0a9"
                        font.pixelSize: 12
                        Layout.fillWidth: true
                    }
                    Text {
                        text: win.favoriteCount
                        color: "#788a7f"
                        font.pixelSize: 11
                        Layout.rightMargin: 10
                    }
                }
            }
            Button {
                id: mostPlayedNav
                objectName: "navMostPlayed"
                width: parent.width
                height: win.height < 740 ? 34 : 44
                hoverEnabled: true
                Accessible.name: "Most Played"
                onClicked: win.navigate("Most Played")
                background: Rectangle {
                    radius: 7
                    color: win.activePage === "Most Played" ? "#24372d" : mostPlayedNav.hovered ? "#1c2721" : "transparent"
                    border.width: mostPlayedNav.visualFocus ? 1 : 0
                    border.color: win.accent
                }
                contentItem: RowLayout {
                    spacing: 13
                    Icon {
                        name: "played"
                        color: win.activePage === "Most Played" ? win.accent : "#8f9e95"
                        Layout.preferredWidth: 19
                        Layout.preferredHeight: 19
                        Layout.leftMargin: 12
                    }
                    Text {
                        text: "Most Played"
                        color: win.activePage === "Most Played" ? win.accent : "#a4b0a9"
                        font.pixelSize: 12
                        Layout.fillWidth: true
                    }
                    Text {
                        text: library.mostPlayed.count
                        color: "#788a7f"
                        font.pixelSize: 11
                        Layout.rightMargin: 10
                    }
                }
            }
            Button {
                id: playlistsNav
                objectName: "navPlaylists"
                width: parent.width
                height: win.height < 740 ? 34 : 44
                hoverEnabled: true
                Accessible.name: "Playlists"
                onClicked: win.navigate("Playlists")
                background: Rectangle {
                    radius: 7
                    color: win.activePage === "Playlists" ? "#24372d" : playlistsNav.hovered ? "#1c2721" : "transparent"
                    border.width: playlistsNav.visualFocus ? 1 : 0
                    border.color: win.accent
                }
                contentItem: RowLayout {
                    spacing: 13
                    Icon {
                        name: "queue"
                        color: win.activePage === "Playlists" ? win.accent : "#8f9e95"
                        Layout.preferredWidth: 19
                        Layout.preferredHeight: 19
                        Layout.leftMargin: 12
                    }
                    Text {
                        text: "Playlists"
                        color: win.activePage === "Playlists" ? win.accent : "#a4b0a9"
                        font.pixelSize: 12
                        Layout.fillWidth: true
                    }
                    Text {
                        text: library.playlists.count
                        color: "#788a7f"
                        font.pixelSize: 11
                        Layout.rightMargin: 10
                    }
                }
            }
            Button {
                id: addFolderNav
                width: parent.width
                height: win.height < 740 ? 34 : 44
                hoverEnabled: true
                Accessible.name: "Add music folder"
                onClicked: folderDialog.open()
                background: Rectangle {
                    radius: 7
                    color: addFolderNav.hovered ? "#1c2721" : "transparent"
                    border.width: addFolderNav.visualFocus ? 1 : 0
                    border.color: win.accent
                }
                contentItem: RowLayout {
                    spacing: 13
                    Icon {
                        name: "folder"
                        color: "#8f9e95"
                        Layout.preferredWidth: 19
                        Layout.preferredHeight: 19
                        Layout.leftMargin: 12
                    }
                    Text {
                        text: "Add a folder"
                        color: "#a4b0a9"
                        font.pixelSize: 12
                        Layout.fillWidth: true
                    }
                    Icon {
                        name: "plus"
                        color: "#788a7f"
                        Layout.preferredWidth: 14
                        Layout.preferredHeight: 14
                        Layout.rightMargin: 8
                    }
                }
            }
        }
        }
        Rectangle {
            x: 22
            width: parent.width - 44
            height: 106
            anchors.bottom: offlineStatus.top
            anchors.bottomMargin: 22
            radius: 10
            color: "#1a2620"
            border.color: "#2d3d32"
            visible: false
            Icon {
                x: 14
                y: 14
                name: "headphones"
                color: win.accent
                width: 20
                height: 20
            }
            Text {
                x: 14
                y: 43
                text: "A little more you."
                color: "#d4dfd7"
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
            Text {
                x: 14
                y: 65
                width: parent.width - 26
                text: "All your music. All right here."
                wrapMode: Text.WordWrap
                color: "#809588"
                font.pixelSize: 10
                lineHeight: 1.3
            }
        }
        Row {
            id: offlineStatus
            x: 27
            spacing: 8
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 22
            Rectangle {
                width: 6
                height: 6
                radius: 3
                color: win.accent
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: "Local music, always yours"
                color: "#6e8376"
                font.pixelSize: 10
            }
        }
    }

    Item {
        id: mainArea
        enabled: !win.nowPlayingOpen
        anchors {
            left: sidebar.right
            right: parent.right
            top: parent.top
            bottom: playerBar.top
        }
        RowLayout {
            id: topBar
            height: 91
            anchors {
                left: parent.left
                right: parent.right
                leftMargin: 34
                rightMargin: 34
                top: parent.top
            }
            spacing: 16
            Text {
                text: "Your library"
                color: "#b3bfb6"
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            Item {
                Layout.fillWidth: true
            }
            Rectangle {
                Layout.preferredWidth: Math.min(mainArea.width * 0.4, 345)
                Layout.preferredHeight: 38
                radius: 20
                color: "#1c2520"
                border.color: searchField.activeFocus ? "#698e78" : "#29342b"
                Icon {
                    x: 13
                    anchors.verticalCenter: parent.verticalCenter
                    name: "search"
                    color: "#829587"
                    width: 17
                    height: 17
                }
                TextField {
                    id: searchField
                    objectName: "searchField"
                    anchors {
                        fill: parent
                        leftMargin: 35
                        rightMargin: 28
                    }
                    padding: 0
                    font.pixelSize: 11
                    color: win.textColor
                    placeholderText: "Search songs, artists, albums"
                    placeholderTextColor: "#788b7c"
                    selectByMouse: true
                    background: Item {}
                    onTextChanged: win.searchQuery = text
                    onAccepted: if (win.visibleTracks.count)
                        win.playTrack(win.visibleTracks.get(0), win.visibleTracks.entries())
                    Accessible.name: "Search library"
                }
                IconButton {
                    width: 27
                    height: 27
                    padding: 8
                    anchors {
                        right: parent.right
                        rightMargin: 4
                        verticalCenter: parent.verticalCenter
                    }
                    symbol: "close"
                    label: "Clear search"
                    visible: searchField.text.length > 0
                    onClicked: searchField.clear()
                }
            }
            IconButton {
                symbol: "refresh"
                label: library.scanning ? library.scanStatus : "Refresh library · " + library.scanStatus
                enabled: !library.scanning
                onClicked: library.rescan()
            }
            Rectangle {
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                radius: 16
                color: "#304336"
                Text {
                    anchors.centerIn: parent
                    text: "m"
                    color: win.accent
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                }
            }
        }

        Flickable {
            id: contentScroll
            objectName: "contentScroll"
            anchors {
                left: parent.left
                right: parent.right
                top: topBar.bottom
                bottom: parent.bottom
                leftMargin: 34
                rightMargin: 34
                bottomMargin: 16
            }
            clip: true
            contentWidth: width
            contentHeight: win.activePage === "Overview" ? contentColumn.implicitHeight : height
            interactive: win.activePage === "Overview"
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds
            maximumFlickVelocity: 12000
            flickDeceleration: 1000
            pixelAligned: false
            LibraryScrollHandler {
                view: contentScroll
            }
            ScrollBar.vertical: ScrollBar {
                policy: contentScroll.interactive ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
            }
            ColumnLayout {
                id: contentColumn
                width: contentScroll.width
                spacing: 24
                RowLayout {
                    id: pageHeading
                    Layout.fillWidth: true
                    spacing: 12
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 7
                        Text {
                            text: library.scanning || library.metadataFailures > 0 ? library.scanStatus.toUpperCase() : win.activePage === "Overview" ? "PRESS PLAY. FEEL AT HOME." : "YOUR MUSIC, YOUR WAY"
                            color: "#7f9485"
                            font.pixelSize: 9
                            font.weight: Font.DemiBold
                            font.letterSpacing: 1.7
                        }
                        RowLayout {
                            spacing: 8
                            IconButton {
                                visible: win.collectionName.length > 0
                                symbol: "back"
                                label: "Back to collection"
                                onClicked: win.collectionName = ""
                            }
                            Text {
                                text: win.pageTitle
                                color: win.textColor
                                font.pixelSize: win.width < 1050 ? 27 : 31
                                font.weight: Font.DemiBold
                                font.letterSpacing: -0.9
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                    }
                    ActionButton {
                        objectName: "manageAlbumsButton"
                        visible: win.activePage === "Albums"
                        symbol: "grid"
                        text: "Manage albums"
                        primary: false
                        onClicked: albumEditor.start(win.collectionKind === "album" && win.collectionName ? win.collectionId : "")
                    }
                    ActionButton {
                        objectName: "importPlaylistButton"
                        visible: win.activePage === "Playlists"
                        symbol: "folder"
                        text: "Import M3U"
                        primary: false
                        onClicked: importPlaylistDialog.open()
                    }
                    IconButton {
                        id: collectionPlaylistMore
                        objectName: "collectionPlaylistOptions"
                        visible: win.collectionKind === "playlist" && !!win.collectionName
                        symbol: "more"
                        label: "Rename, export, or delete playlist"
                        onClicked: win.openPlaylistMenu({id: win.collectionId, name: win.collectionName}, collectionPlaylistMore)
                    }
                    ActionButton {
                        text: win.activePage === "Playlists" ? "New playlist" : "Add music"
                        symbol: "plus"
                        primary: false
                        onClicked: {
                            if (win.activePage === "Playlists") {
                                playlistDialog.track = null;
                                playlistDialog.open();
                            } else
                                fileDialog.open();
                        }
                    }
                }

                Rectangle {
                    id: hero
                    visible: win.activePage === "Overview" && !win.searchQuery
                    Layout.fillWidth: true
                    Layout.preferredHeight: win.width < 1050 ? 228 : 252
                    radius: 13
                    clip: true
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop {
                            position: 0
                            color: "#29483a"
                        }
                        GradientStop {
                            position: 1
                            color: "#1d302b"
                        }
                    }
                    Image {
                        source: "assets/hero.svg"
                        width: parent.width * 0.59
                        height: parent.height + 32
                        fillMode: Image.PreserveAspectFit
                        anchors {
                            right: parent.right
                            rightMargin: -12
                            verticalCenter: parent.verticalCenter
                        }
                        opacity: 0.95
                    }
                    Column {
                        x: 30
                        y: 27
                        spacing: 10
                        Text {
                            text: "A SOUNDTRACK THAT'S ALL YOURS"
                            color: "#b3d6b9"
                            font.pixelSize: 9
                            font.weight: Font.DemiBold
                            font.letterSpacing: 1.3
                        }
                        Text {
                            text: "Your music.\nYour space."
                            color: "#f1f1dc"
                            font.pixelSize: win.width < 1050 ? 33 : 40
                            font.weight: Font.DemiBold
                            font.letterSpacing: -1.6
                            lineHeight: 1.02
                        }
                        Text {
                            text: library.trackCount ? "Rediscover the songs that feel like you." : "Bring your favorite sounds a little closer."
                            color: "#a6bdaa"
                            font.pixelSize: 11
                        }
                        ActionButton {
                            text: library.trackCount ? "Play your library" : "Build your library"
                            symbol: library.trackCount ? "play" : "plus"
                            height: 37
                            implicitHeight: 37
                            onClicked: library.trackCount ? win.playAll(false) : folderDialog.open()
                        }
                    }
                }

                // Album cards are populated exclusively from the user's music.
                ColumnLayout {
                    visible: win.activePage === "Overview" && win.albums.count > 0 && !win.searchQuery
                    Layout.fillWidth: true
                    spacing: 16
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "On your shelves"
                            color: win.textColor
                            font.pixelSize: 18
                            font.weight: Font.DemiBold
                            Layout.fillWidth: true
                        }
                        Button {
                            text: "View albums  →"
                            flat: true
                            onClicked: win.navigate("Albums")
                            contentItem: Text {
                                text: parent.text
                                color: "#98ae9e"
                                font.pixelSize: 11
                            }
                            background: Item {}
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 18
                        Repeater {
                            model: library.shelfAlbums.slice(0, win.width < 1050 ? 3 : 4)
                            delegate: Item {
                                id: shelfCard
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredHeight: 86
                                Rectangle {
                                    anchors.fill: parent
                                    radius: 8
                                    color: shelfMouse.containsMouse ? "#28392d" : "#1d2b22"
                                }
                                CoverArt {
                                    id: shelfCover
                                    x: 11
                                    y: 11
                                    width: 64
                                    height: 64
                                    artwork: shelfCard.modelData.artwork
                                    artStyle: shelfCard.modelData.artStyle
                                }
                                Text {
                                    x: 87
                                    y: 22
                                    width: parent.width - 99
                                    text: shelfCard.modelData.name
                                    color: "#e1e8e2"
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Text {
                                    x: 87
                                    y: 46
                                    width: parent.width - 99
                                    text: shelfCard.modelData.subtitle
                                    color: "#819688"
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                }
                                MouseArea {
                                    id: shelfMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        win.navigate("Albums");
                                        win.showGroup(shelfCard.modelData);
                                    }
                                }
                                IconButton {
                                    anchors {
                                        right: shelfCover.right
                                        bottom: shelfCover.bottom
                                        margins: 10
                                    }
                                    width: 38
                                    height: 38
                                    padding: 11
                                    visible: shelfMouse.containsMouse || hovered
                                    symbol: "play"
                                    label: "Play " + shelfCard.modelData.name
                                    surfaceColor: win.accent
                                    glyphColor: "#153023"
                                    onClicked: win.playGroup(shelfCard.modelData, "album")
                                }
                            }
                        }
                    }
                }

                ColumnLayout {
                    visible: library.trackCount === 0 && win.activePage === "Overview" && !win.searchQuery
                    Layout.fillWidth: true
                    spacing: 15
                    Text {
                        text: "Make yourself at home"
                        color: win.textColor
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 14
                        Repeater {
                            model: [
                                {
                                    icon: "folder",
                                    title: "Bring your collection",
                                    body: "Add a folder. We'll find the music.",
                                    action: "Choose a folder",
                                    kind: "folder"
                                },
                                {
                                    icon: "music",
                                    title: "A song for every moment",
                                    body: "Start with a few of your favorites.",
                                    action: "Add audio files",
                                    kind: "files"
                                },
                                {
                                    icon: "heart",
                                    title: "Keep the good ones close",
                                    body: "Your most-loved tracks in one place.",
                                    action: "Your favorites",
                                    kind: "favorites"
                                }
                            ]
                            delegate: Rectangle {
                                id: welcomeCard
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredHeight: 160
                                color: "#1b2420"
                                radius: 10
                                border.color: "#2a372e"
                                Icon {
                                    x: 17
                                    y: 17
                                    name: welcomeCard.modelData.icon
                                    color: win.accent
                                    width: 22
                                    height: 22
                                }
                                Text {
                                    x: 17
                                    y: 55
                                    width: parent.width - 28
                                    text: welcomeCard.modelData.title
                                    color: "#dfe8df"
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Text {
                                    x: 17
                                    y: 77
                                    width: parent.width - 28
                                    text: welcomeCard.modelData.body
                                    color: "#829888"
                                    font.pixelSize: 10
                                    wrapMode: Text.WordWrap
                                }
                                Button {
                                    x: 12
                                    y: 117
                                    height: 29
                                    text: welcomeCard.modelData.action
                                    onClicked: welcomeCard.modelData.kind === "folder" ? folderDialog.open() : welcomeCard.modelData.kind === "files" ? fileDialog.open() : win.navigate("Favorites")
                                    contentItem: Text {
                                        text: parent.text + "  →"
                                        color: win.accent
                                        font.pixelSize: 10
                                        font.weight: Font.DemiBold
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    background: Item {}
                                }
                            }
                        }
                    }
                }

                ListeningDashboard {
                    objectName: "listeningDashboard"
                    visible: win.activePage === "Listening dashboard"
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(0, contentScroll.height - pageHeading.implicitHeight - contentColumn.spacing)
                    statistics: library.dashboard
                    onPlayTrack: function(track) { win.playTrack(track, [track]); }
                    onOpenArtist: function(artist) { win.navigate("Artists"); win.showGroup({id: artist.id, name: artist.name, subtitle: ""}); }
                }
                ColumnLayout {
                    visible: win.activePage === "Smart playlists" && !win.collectionName
                    Layout.fillWidth: true
                    spacing: 18
                    Text { text: "A fresh way into your library. These playlists update as you add music and listen."; color: "#8ba392"; wrapMode: Text.WordWrap; font.pixelSize: 12; Layout.fillWidth: true }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 14
                        Repeater {
                            model: library.smartPlaylists
                            delegate: Button {
                                id: smartCard
                                required property var modelData
                                required property int index
                                objectName: "smartPlaylist" + index
                                Layout.fillWidth: true
                                Layout.preferredHeight: 220
                                padding: 20
                                onClicked: win.openSmart(modelData.id, modelData.name)
                                background: Rectangle { radius: 12; color: smartCard.hovered ? "#2b4032" : "#202e27"; border.color: "#3a5142" }
                                contentItem: ColumnLayout {
                                    spacing: 14
                                    Icon { name: smartCard.modelData.icon; color: "#a7e8cb"; Layout.preferredWidth: 32; Layout.preferredHeight: 32 }
                                    Text { text: smartCard.modelData.name; color: "#edf2ef"; font.pixelSize: win.width < 1050 ? 14 : 18; font.weight: Font.DemiBold; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                                    Text { text: smartCard.modelData.description; color: "#8ba392"; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                                    Item { Layout.fillHeight: true }
                                    Text { text: smartCard.modelData.count + " songs  →"; color: "#a7e8cb"; font.pixelSize: 12 }
                                }
                            }
                        }
                    }
                }

                // Full album and artist collections.
                GridView {
                    id: groupGrid
                    objectName: "groupGrid"
                    visible: (win.activePage === "Albums" || win.activePage === "Artists" || win.activePage === "Playlists") && !win.collectionName && win.visibleGroups.count > 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(0, contentScroll.height - pageHeading.implicitHeight - contentColumn.spacing)
                    cellWidth: width / (win.width < 1050 ? 3 : 4)
                    cellHeight: cellWidth + 48
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    flickableDirection: Flickable.VerticalFlick
                    maximumFlickVelocity: 12000
                    flickDeceleration: 1000
                    pixelAligned: false
                    LibraryScrollHandler {
                        view: groupGrid
                    }
                    cacheBuffer: cellHeight
                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                    }
                    model: win.visibleGroups
                    delegate: Item {
                        id: groupCard
                        required property var modelData
                        required property int index
                        objectName: "groupCard" + index
                        width: groupGrid.cellWidth
                        height: groupGrid.cellHeight
                        CoverArt {
                            id: groupCover
                            width: parent.width - 20
                            height: width
                            artwork: groupCard.modelData.artwork
                            artStyle: groupCard.modelData.artStyle
                            radius: win.activePage === "Artists" ? width / 2 : 8
                        }
                        Text {
                            y: groupCover.height + 13
                            width: groupCover.width
                            text: groupCard.modelData.name
                            color: win.textColor
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Text {
                            y: groupCover.height + 35
                            width: groupCover.width
                            text: (groupCard.modelData.subtitle ? groupCard.modelData.subtitle + " · " : "") + groupCard.modelData.trackCount + " songs"
                            color: win.secondary
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                        MouseArea {
                            id: groupMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: win.showGroup(groupCard.modelData)
                        }
                        IconButton {
                            id: groupPlaylistMore
                            objectName: "playlistOptions" + groupCard.index
                            visible: win.activePage === "Playlists"
                            anchors.right: groupCover.right
                            anchors.top: groupCover.top
                            anchors.margins: 8
                            width: 32
                            height: 32
                            padding: 8
                            symbol: "more"
                            label: "Playlist options"
                            surfaceColor: "#202e24"
                            onClicked: win.openPlaylistMenu(groupCard.modelData, groupPlaylistMore)
                        }
                        IconButton {
                            anchors {
                                right: groupCover.right
                                bottom: groupCover.bottom
                                margins: 10
                            }
                            width: 40
                            height: 40
                            padding: 11
                            visible: groupMouse.containsMouse || hovered
                            symbol: "play"
                            label: "Play " + groupCard.modelData.name
                            surfaceColor: win.accent
                            glyphColor: "#153023"
                            onClicked: win.playGroup(groupCard.modelData, win.activePage === "Artists" ? "artist" : win.activePage === "Playlists" ? "playlist" : "album")
                        }
                    }
                }

                ColumnLayout {
                    id: songsSection
                    Layout.fillWidth: true
                    spacing: 15
                    visible: win.activePage === "Overview" || win.activePage === "Songs" || win.activePage === "Favorites" || win.activePage === "Most Played" || win.collectionName.length > 0 || ((win.activePage === "Albums" || win.activePage === "Artists" || win.activePage === "Playlists") && win.visibleGroups.count === 0)
                    RowLayout {
                        id: songActions
                        Layout.fillWidth: true
                        Text {
                            text: win.activePage === "Overview" ? "All your sounds" : ""
                            visible: text.length > 0
                            color: win.textColor
                            font.pixelSize: 18
                            font.weight: Font.DemiBold
                        }
                        Text {
                            text: library.scanning ? library.scanStatus : win.visibleTracks.count + (win.visibleTracks.count === 1 ? " song" : " songs") + (win.collectionKind === "playlist" && win.sortBy === "Order" && !win.searchQuery ? " · Drag the handles to reorder" : "")
                            color: "#778c7d"
                            font.pixelSize: 11
                            Layout.fillWidth: true
                        }
                        ActionButton {
                            objectName: "playAllButton"
                            text: "Play all"
                            symbol: "play"
                            implicitHeight: 33
                            enabled: win.visibleTracks.count > 0
                            onClicked: win.playAll(false)
                        }
                        IconButton {
                            symbol: "shuffle"
                            label: "Shuffle all"
                            enabled: win.visibleTracks.count > 0
                            onClicked: win.playAll(true)
                        }
                        Button {
                            id: sortButton
                            enabled: win.activePage !== "Most Played"
                            implicitHeight: 32
                            implicitWidth: 84
                            hoverEnabled: true
                            onClicked: sortMenu.popup()
                            background: Rectangle {
                                radius: 6
                                color: sortButton.hovered ? "#29362d" : "transparent"
                            }
                            contentItem: RowLayout {
                                spacing: 7
                                Icon {
                                    name: "sort"
                                    color: "#8b9e90"
                                    Layout.preferredWidth: 16
                                    Layout.preferredHeight: 16
                                }
                                Text {
                                    text: win.sortBy
                                    color: "#a9b8ad"
                                    font.pixelSize: 11
                                }
                                Icon {
                                    name: "chevron"
                                    color: "#8b9e90"
                                    Layout.preferredWidth: 12
                                    Layout.preferredHeight: 12
                                }
                            }
                            Menu {
                                id: sortMenu
                                palette.window: "#26342b"
                                palette.windowText: "#e5eee6"
                                palette.highlight: "#3d5846"
                                palette.highlightedText: "#ffffff"
                                palette.text: "#e5eee6"
                                Repeater {
                                    model: win.collectionKind === "playlist" ? ["Order", "Title", "Artist", "Album"] : ["Track", "Title", "Artist", "Album"]
                                    delegate: MenuItem {
                                        required property string modelData
                                        text: modelData
                                        checkable: true
                                        checked: win.sortBy === modelData
                                        onTriggered: win.sortBy = modelData
                                    }
                                }
                            }
                        }
                    }
                    RowLayout {
                        id: songColumns
                        Layout.fillWidth: true
                        Layout.leftMargin: 15
                        Layout.rightMargin: 18
                        spacing: 12
                        Text {
                            text: "#"
                            color: "#6b8272"
                            font.pixelSize: 10
                            Layout.preferredWidth: 24
                        }
                        Text {
                            text: "TITLE"
                            color: "#6b8272"
                            font.pixelSize: 9
                            font.letterSpacing: 1.3
                            Layout.fillWidth: true
                        }
                        Text {
                            text: "ALBUM"
                            color: "#6b8272"
                            font.pixelSize: 9
                            font.letterSpacing: 1.3
                            Layout.preferredWidth: contentScroll.width * 0.23
                            visible: win.width > 980
                        }
                        Text {
                            text: "PLAYS"
                            visible: win.activePage === "Most Played"
                            color: "#6b8272"
                            font.pixelSize: 9
                            font.letterSpacing: 1.3
                            Layout.preferredWidth: 84
                        }
                        Item {
                            Layout.preferredWidth: 30
                        }
                        Item {
                            Layout.preferredWidth: 30
                        }
                        Icon {
                            name: "clock"
                            color: "#6b8272"
                            Layout.preferredWidth: 14
                            Layout.preferredHeight: 14
                            Layout.rightMargin: 19
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: "#29372d"
                    }
                    ListView {
                        id: songList
                        objectName: "songList"
                        Layout.fillWidth: true
                        Layout.preferredHeight: win.visibleTracks.count === 0 ? 0 : win.activePage === "Overview" ? Math.min(contentHeight, 390) : Math.max(0, contentScroll.height - pageHeading.implicitHeight - contentColumn.spacing - songActions.implicitHeight - songColumns.implicitHeight - 1 - songsSection.spacing * 3)
                        interactive: win.activePage !== "Overview"
                        flickableDirection: Flickable.VerticalFlick
                        maximumFlickVelocity: 12000
                        flickDeceleration: 1000
                        pixelAligned: false
                        LibraryScrollHandler {
                            view: songList
                        }
                        reuseItems: true
                        clip: true
                        model: win.visibleTracks
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {
                            policy: songList.interactive ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                        }
                        delegate: Rectangle {
                            id: songRow
                            required property var modelData
                            required property int index
                            objectName: "songRow" + index
                            width: songList.width
                            height: 65
                            radius: 6
                            readonly property bool current: win.currentUrl === modelData.fileUrl
                            readonly property bool reorderable: win.collectionKind === "playlist" && win.sortBy === "Order" && !win.searchQuery
                            color: playlistDrop.containsDrag ? "#344e3b" : current ? "#24372d" : rowMouse.containsMouse ? "#1f2d24" : "transparent"
                            DropArea {
                                id: playlistDrop
                                objectName: "playlistDrop" + songRow.index
                                anchors.fill: parent
                                enabled: songRow.reorderable
                                keys: ["playlist:" + win.collectionId]
                                onDropped: function(drop) {
                                    const after = drop.y >= height / 2;
                                    const next = after && songRow.index + 1 < win.visibleTracks.count ? win.visibleTracks.get(songRow.index + 1).fileUrl : "";
                                    win.movePlaylistSong(drop.source.entryId, after ? next : songRow.modelData.fileUrl);
                                    drop.acceptProposedAction();
                                }
                            }
                            Rectangle { visible: playlistDrop.containsDrag; width: parent.width; height: 2; y: playlistDrop.drag.y < parent.height / 2 ? 0 : parent.height - 2; color: win.accent }
                            MouseArea {
                                id: rowMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                onClicked: function (mouse) {
                                    if (mouse.button === Qt.RightButton)
                                        win.openTrackMenu(songRow.modelData, rowMoreButton);
                                    else
                                        win.playTrack(songRow.modelData, win.visibleTracks.entries());
                                }
                            }
                            RowLayout {
                                anchors {
                                    fill: parent
                                    leftMargin: 15
                                    rightMargin: 18
                                }
                                spacing: 12
                                ReorderHandle {
                                    objectName: "playlistDrag" + songRow.index
                                    visible: songRow.reorderable
                                    Layout.preferredWidth: 24
                                    Layout.preferredHeight: 30
                                    entryId: songRow.modelData.fileUrl
                                    dragGroup: "playlist:" + win.collectionId
                                    title: songRow.modelData.title
                                    view: songList
                                    onStep: function(direction) {
                                        const target = direction < 0 ? songRow.index - 1 : songRow.index + 2;
                                        if (target < 0) return;
                                        win.movePlaylistSong(entryId, target >= win.visibleTracks.count ? "" : win.visibleTracks.get(target).fileUrl);
                                    }
                                }
                                Item {
                                    visible: !songRow.reorderable
                                    Layout.preferredWidth: 24
                                    Layout.preferredHeight: 22
                                    Text {
                                        anchors.centerIn: parent
                                        text: win.collectionName && win.collectionKind === "album" && songRow.modelData.trackNumber >= 0 ? (songRow.modelData.discNumber > 1 ? songRow.modelData.discNumber + "." : "") + songRow.modelData.trackNumber : songRow.index + 1
                                        color: "#7c9383"
                                        font.pixelSize: 11
                                        visible: !songRow.current && !rowMouse.containsMouse
                                    }
                                    Icon {
                                        anchors.centerIn: parent
                                        name: songRow.current && win.isPlaying ? "pause" : "play"
                                        color: songRow.current ? win.accent : "#dbe9dd"
                                        width: 15
                                        height: 15
                                        visible: songRow.current || rowMouse.containsMouse
                                    }
                                }
                                CoverArt {
                                    Layout.preferredWidth: 40
                                    Layout.preferredHeight: 40
                                    radius: 5
                                    artwork: songRow.modelData.artwork
                                    artStyle: songRow.modelData.artStyle
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 5
                                    Text {
                                        text: songRow.modelData.title
                                        color: songRow.current ? win.accent : "#dce7dd"
                                        font.pixelSize: 12
                                        font.weight: Font.Medium
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    Text {
                                        text: songRow.modelData.artist
                                        color: "#7e9585"
                                        font.pixelSize: 10
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                }
                                Text {
                                    text: songRow.modelData.album
                                    color: "#8fa393"
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                    Layout.preferredWidth: contentScroll.width * 0.23
                                    visible: win.width > 980
                                }
                                Text {
                                    text: songRow.modelData.playCount
                                    visible: win.activePage === "Most Played"
                                    color: "#6e8977"
                                    font.pixelSize: 9
                                    Layout.preferredWidth: 84
                                }
                                IconButton {
                                    objectName: "favorite" + songRow.index
                                    symbol: "heart"
                                    label: songRow.modelData.favorite ? "Remove from favorites" : "Add to favorites"
                                    glyphColor: songRow.modelData.favorite ? "#ef4444" : "#607b69"
                                    filled: songRow.modelData.favorite
                                    Layout.preferredWidth: 30
                                    Layout.preferredHeight: 30
                                    padding: 8
                                    onClicked: library.toggleFavorite(songRow.modelData.fileUrl)
                                }
                                Text {
                                    text: songRow.modelData.duration > 0 ? win.formatTime(songRow.modelData.duration) : "—"
                                    color: "#8aa18f"
                                    font.pixelSize: 10
                                    horizontalAlignment: Text.AlignRight
                                    Layout.preferredWidth: 45
                                }
                                IconButton {
                                    id: rowMoreButton
                                    objectName: "songOptions" + songRow.index
                                    symbol: "more"
                                    label: "Song options"
                                    Layout.preferredWidth: 30
                                    Layout.preferredHeight: 30
                                    padding: 7
                                    onClicked: win.openTrackMenu(songRow.modelData, rowMoreButton)
                                }
                            }
                        }
                    }
                    ColumnLayout {
                        visible: win.visibleTracks.count === 0
                        Layout.fillWidth: true
                        Layout.topMargin: 12
                        Layout.bottomMargin: 23
                        spacing: 9
                        Icon {
                            name: win.searchQuery ? "search" : win.activePage === "Favorites" ? "heart" : "music"
                            color: "#60856e"
                            Layout.preferredWidth: 29
                            Layout.preferredHeight: 29
                            Layout.alignment: Qt.AlignHCenter
                        }
                        Text {
                            text: library.scanning ? library.scanStatus : win.searchQuery ? "No matching music" : win.activePage === "Most Played" ? "Your most-played songs will live here" : win.activePage === "Playlists" ? (win.collectionName ? "This playlist is waiting for a song" : "Your next playlist starts here") : win.activePage === "Favorites" ? "A home for your favorites" : "Your next favorite is already out there."
                            color: "#c1d0c4"
                            font.pixelSize: 13
                            font.weight: Font.Medium
                            Layout.alignment: Qt.AlignHCenter
                        }
                        Text {
                            text: win.searchQuery ? "Try another song, artist, or album." : win.activePage === "Most Played" ? "Play at least 50% of a song to add a listen." : win.activePage === "Playlists" ? "Use a song's three-dot menu to add it to a playlist." : win.activePage === "Favorites" ? "Tap the heart beside a song to keep it here." : "Add your music to get started. MP3, FLAC, WAV, and more."
                            color: "#789282"
                            font.pixelSize: 11
                            Layout.alignment: Qt.AlignHCenter
                        }
                    }
                }
                Text {
                    text: "A little less noise. A little more music."
                    visible: win.activePage === "Overview"
                    color: "#4e6b59"
                    font.pixelSize: 10
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 5
                    Layout.bottomMargin: 6
                }
            }
        }
    }

    NowPlayingPane {
        id: nowPlayingPane
        objectName: "nowPlayingPane"
        z: 2
        anchors {
            left: sidebar.right
            right: parent.right
            top: parent.top
            bottom: playerBar.top
        }
        visible: win.nowPlayingOpen
        track: win.currentTrack
        playing: win.isPlaying
        shuffled: win.shuffleEnabled
        repeatMode: win.repeatMode
        position: player.position
        duration: player.duration
        volume: library.volume
        muted: win.muted
        seekable: player.seekable
        onCloseRequested: win.nowPlayingOpen = false
        onArtistRequested: win.openArtist(win.currentTrack)
        onAlbumRequested: win.openAlbum(win.currentTrack)
        onPlayPauseRequested: win.togglePlayback()
        onPreviousRequested: win.previousTrack()
        onNextRequested: win.nextTrack(false)
        onShuffleRequested: win.shuffleEnabled = !win.shuffleEnabled
        onRepeatRequested: win.repeatMode = (win.repeatMode + 1) % 3
        onFavoriteRequested: if (win.currentTrack)
            library.toggleFavorite(win.currentUrl)
        onSeekRequested: function (position) {
            win.seekPlayback(position);
        }
        onVolumeRequested: function (volume) {
            library.volume = volume;
            win.muted = false;
        }
        onMuteRequested: win.muted = !win.muted
        onQueueRequested: win.queueOpen = !win.queueOpen
        onMenuRequested: function (anchor) {
            win.openTrackMenu(win.currentTrack, anchor);
        }
    }

    Rectangle {
        id: playerBar
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        height: 104
        color: "#18221b"
        Rectangle {
            width: parent.width
            height: 1
            color: "#314133"
        }
        RowLayout {
            anchors {
                fill: parent
                leftMargin: 24
                rightMargin: 25
            }
            spacing: win.width < 1050 ? 8 : 13
            Button {
                id: bottomCoverButton
                objectName: "nowPlayingCoverButton"
                Layout.preferredWidth: 56
                Layout.preferredHeight: 56
                padding: 0
                enabled: !!win.currentTrack
                Accessible.name: "Open now playing"
                ToolTip.visible: hovered && enabled
                ToolTip.text: "Open now playing"
                background: Item {}
                contentItem: CoverArt {
                    radius: 7
                    artwork: win.currentTrack ? win.currentTrack.artwork : ""
                    artStyle: win.currentTrack ? win.currentTrack.artStyle : 0
                    opacity: win.currentTrack ? 1 : 0.65
                }
                onClicked: win.nowPlayingOpen = !win.nowPlayingOpen
            }
            ColumnLayout {
                Layout.preferredWidth: win.width < 1050 ? 125 : 195
                spacing: 4
                Text {
                    text: win.currentTrack ? win.currentTrack.title : "Find your next favorite"
                    color: "#e1ebe0"
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Button {
                    id: footerArtist
                    objectName: "footerArtistLink"
                    Layout.fillWidth: true
                    padding: 0
                    enabled: !!win.currentTrack
                    hoverEnabled: true
                    Accessible.name: "Go to artist"
                    contentItem: Text {
                        text: win.currentTrack ? win.currentTrack.artist : "Your music starts here"
                        color: footerArtist.hovered ? win.accent : "#819c87"
                        font.pixelSize: 10
                        elide: Text.ElideRight
                        font.underline: footerArtist.hovered
                    }
                    background: Item {}
                    onClicked: win.openArtist(win.currentTrack)
                }
            }
            IconButton {
                objectName: "footerFavoriteButton"
                symbol: "heart"
                label: win.currentTrack && win.currentTrack.favorite ? "Remove from favorites" : "Add to favorites"
                enabled: !!win.currentTrack
                glyphColor: win.currentTrack && win.currentTrack.favorite ? "#ef4444" : "#819c87"
                filled: !!win.currentTrack && win.currentTrack.favorite
                onClicked: library.toggleFavorite(win.currentUrl)
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.maximumWidth: 590
                Layout.leftMargin: win.width > 1100 ? 22 : 0
                Layout.rightMargin: win.width > 1100 ? 22 : 0
                spacing: 6
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 15
                    IconButton {
                        objectName: "shuffleButton"
                        symbol: "shuffle"
                        label: win.shuffleEnabled ? "Turn shuffle off" : "Turn shuffle on"
                        checkable: true
                        checked: win.shuffleEnabled
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                        padding: 6
                        onClicked: win.shuffleEnabled = !win.shuffleEnabled
                    }
                    IconButton {
                        objectName: "previousButton"
                        symbol: "previous"
                        label: "Previous track"
                        enabled: win.queueUrls.length > 0
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: 30
                        padding: 7
                        onClicked: win.previousTrack()
                    }
                    IconButton {
                        objectName: "playPauseButton"
                        symbol: win.isPlaying ? "pause" : "play"
                        label: win.isPlaying ? "Pause" : "Play"
                        enabled: !!win.currentTrack || win.visibleTracks.count > 0 || win.queueItems.length > 0
                        Layout.preferredWidth: 38
                        Layout.preferredHeight: 38
                        padding: 11
                        surfaceColor: win.accent
                        glyphColor: "#162e1f"
                        onClicked: win.togglePlayback()
                    }
                    IconButton {
                        objectName: "nextButton"
                        symbol: "next"
                        label: "Next track"
                        enabled: win.queueUrls.length > 0
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: 30
                        padding: 7
                        onClicked: win.nextTrack(false)
                    }
                    Item {
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                        IconButton {
                            objectName: "repeatButton"
                            anchors.fill: parent
                            symbol: "repeat"
                            label: win.repeatMode === 0 ? "Repeat off" : win.repeatMode === 1 ? "Repeat all" : "Repeat one"
                            checkable: true
                            checked: win.repeatMode > 0
                            padding: 6
                            onClicked: win.repeatMode = (win.repeatMode + 1) % 3
                        }
                        Text {
                            anchors {
                                right: parent.right
                                top: parent.top
                            }
                            text: "1"
                            visible: win.repeatMode === 2
                            color: win.accent
                            font.pixelSize: 8
                            font.weight: Font.Bold
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Text {
                        text: win.formatTime(player.position)
                        color: "#7a9581"
                        font.pixelSize: 9
                        Layout.preferredWidth: 30
                        horizontalAlignment: Text.AlignRight
                    }
                    Slider {
                        id: progressSlider
                        objectName: "progressSlider"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 16
                        from: 0
                        to: Math.max(1, player.duration)
                        value: player.position
                        enabled: player.seekable
                        onMoved: win.seekPlayback(value)
                        Accessible.name: "Playback position"
                        background: Rectangle {
                            x: progressSlider.leftPadding
                            y: progressSlider.topPadding + progressSlider.availableHeight / 2 - 2
                            width: progressSlider.availableWidth
                            height: 3
                            radius: 2
                            color: "#3a5140"
                            Rectangle {
                                width: progressSlider.visualPosition * parent.width
                                height: parent.height
                                radius: 2
                                color: "#a7e8cb"
                            }
                        }
                        handle: Rectangle {
                            x: progressSlider.leftPadding + progressSlider.visualPosition * (progressSlider.availableWidth - width)
                            y: progressSlider.topPadding + (progressSlider.availableHeight - height) / 2
                            width: 9
                            height: 9
                            radius: 5
                            color: "#d1f5dd"
                            visible: progressSlider.hovered || progressSlider.pressed || progressSlider.visualFocus
                        }
                    }
                    Text {
                        text: win.formatTime(player.duration)
                        color: "#7a9581"
                        font.pixelSize: 9
                        Layout.preferredWidth: 30
                    }
                }
            }
            Item {
                Layout.fillWidth: true
                Layout.maximumWidth: 15
            }
            IconButton {
                objectName: "queueButton"
                symbol: "queue"
                label: "Play queue"
                checkable: true
                checked: win.queueOpen
                onClicked: win.queueOpen = !win.queueOpen
            }
            IconButton {
                objectName: "crossfadeSettingsButton"
                symbol: "crossfade"
                label: "Gapless / crossfade · " + (player.crossfadeEnabled ? player.crossfadeSeconds + " seconds" : "Crossfade off")
                checked: player.crossfadeEnabled && player.crossfadeSeconds > 0
                Layout.preferredWidth: 28; Layout.preferredHeight: 28; padding: 6
                onClicked: playbackOptions.open()
            }
            IconButton {
                objectName: "sleepTimerButton"
                symbol: "moon"
                label: player.sleepAfterTrack ? "Sleep after this song" : player.sleepRemainingMs > 0 ? "Sleep timer · " + Math.ceil(player.sleepRemainingMs / 60000) + " minutes" : "Sleep timer"
                checked: player.sleepAfterTrack || player.sleepRemainingMs > 0
                Layout.preferredWidth: 28; Layout.preferredHeight: 28; padding: 6
                onClicked: sleepPopup.open()
            }
            IconButton {
                objectName: "miniPlayerButton"
                symbol: "mini"
                label: "Open always-on-top mini player (Ctrl+M)"
                Layout.preferredWidth: 28; Layout.preferredHeight: 28; padding: 6
                onClicked: win.openMiniPlayer()
            }
            Rectangle {
                Layout.preferredWidth: 1
                Layout.preferredHeight: 21
                color: "#344b3a"
            }
            IconButton {
                symbol: win.muted || library.volume === 0 ? "mute" : "volume"
                label: win.muted ? "Unmute" : "Mute"
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
                padding: 6
                onClicked: win.muted = !win.muted
            }
            Slider {
                id: volumeSlider
                objectName: "volumeSlider"
                Layout.preferredWidth: win.width < 1050 ? 65 : 90
                from: 0
                to: 1
                value: library.volume
                onMoved: {
                    library.volume = value;
                    win.muted = false;
                }
                Accessible.name: "Volume"
                background: Rectangle {
                    x: volumeSlider.leftPadding
                    y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - 2
                    width: volumeSlider.availableWidth
                    height: 3
                    radius: 2
                    color: "#3a5140"
                    Rectangle {
                        width: volumeSlider.visualPosition * parent.width
                        height: parent.height
                        radius: 2
                        color: "#a7e8cb"
                    }
                }
                handle: Rectangle {
                    x: volumeSlider.leftPadding + volumeSlider.visualPosition * (volumeSlider.availableWidth - width)
                    y: volumeSlider.topPadding + (volumeSlider.availableHeight - height) / 2
                    width: 9
                    height: 9
                    radius: 5
                    color: "#d1f5dd"
                    visible: volumeSlider.hovered || volumeSlider.pressed || volumeSlider.visualFocus
                }
            }
        }
    }

    Popup {
        id: queuePopup
        objectName: "queuePopup"
        x: win.width - width - 22
        y: win.height - playerBar.height - height - 12
        width: 390
        height: Math.min(420, win.height - 150)
        visible: win.queueOpen
        onClosed: win.queueOpen = false
        padding: 18
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            color: "#202e24"
            radius: 12
            border.color: "#435b48"
        }
        contentItem: ColumnLayout {
            spacing: 14
            RowLayout {
                Text {
                    text: "Up next"
                    font.pixelSize: 17
                    font.weight: Font.DemiBold
                    color: win.textColor
                    Layout.fillWidth: true
                }
                IconButton {
                    symbol: "close"
                    label: "Close queue"
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    onClicked: win.queueOpen = false
                }
            }
            Text {
                visible: win.queueUrls.length === 0
                text: "Play a song to start your queue."
                color: win.secondary
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            ListView {
                id: queueList
                objectName: "queueList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                LibraryScrollHandler { view: queueList }
                model: win.queueItems
                delegate: ItemDelegate {
                    id: queuedTrack
                    required property var modelData
                    required property int index
                    readonly property var track: win.trackForUrl(modelData.fileUrl)
                    width: ListView.view.width
                    height: 54
                    onClicked: win.playQueueItem(modelData.id)
                    DropArea {
                        id: queueDrop
                        anchors.fill: parent
                        keys: ["queue"]
                        onDropped: function(drop) {
                            const after = drop.y >= height / 2;
                            const next = after && queuedTrack.index + 1 < win.queueItems.length ? win.queueItems[queuedTrack.index + 1].id : "";
                            win.moveQueueItem(drop.source.entryId, after ? next : queuedTrack.modelData.id);
                            drop.acceptProposedAction();
                        }
                    }
                    Rectangle { visible: queueDrop.containsDrag; width: parent.width; height: 2; y: queueDrop.drag.y < parent.height / 2 ? 0 : parent.height - 2; color: win.accent; z: 2 }
                    background: Rectangle {
                        radius: 5
                        color: queueDrop.containsDrag || queuedTrack.modelData.id === win.currentQueueId ? "#344e3b" : queuedTrack.hovered ? "#2b4032" : "transparent"
                    }
                    contentItem: RowLayout {
                        ReorderHandle {
                            objectName: "queueDrag" + queuedTrack.index
                            Layout.preferredWidth: 20
                            Layout.preferredHeight: 30
                            entryId: queuedTrack.modelData.id
                            dragGroup: "queue"
                            title: queuedTrack.track ? queuedTrack.track.title : "Song"
                            view: queueList
                            onStep: function(direction) {
                                const target = direction < 0 ? queuedTrack.index - 1 : queuedTrack.index + 2;
                                if (target < 0) return;
                                win.moveQueueItem(entryId, target >= win.queueItems.length ? "" : win.queueItems[target].id);
                            }
                        }
                        CoverArt {
                            Layout.preferredWidth: 34
                            Layout.preferredHeight: 34
                            artwork: queuedTrack.track ? queuedTrack.track.artwork : ""
                            artStyle: queuedTrack.track ? queuedTrack.track.artStyle : 0
                            radius: 4
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Text {
                                text: queuedTrack.track ? queuedTrack.track.title : "Unavailable track"
                                color: queuedTrack.modelData.id === win.currentQueueId ? win.accent : win.textColor
                                font.pixelSize: 11
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Text {
                                text: queuedTrack.track ? queuedTrack.track.artist : ""
                                color: win.secondary
                                font.pixelSize: 10
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                        IconButton {
                            id: queuedMore
                            symbol: "more"
                            label: "Song options"
                            Layout.preferredWidth: 27
                            Layout.preferredHeight: 27
                            padding: 6
                            onClicked: win.openTrackMenu(queuedTrack.track, queuedMore, queuedTrack.modelData.id)
                        }
                        IconButton {
                            objectName: "removeQueue" + queuedTrack.index
                            symbol: "close"
                            label: "Remove from queue"
                            Layout.preferredWidth: 27
                            Layout.preferredHeight: 27
                            padding: 7
                            onClicked: win.removeQueueItem(queuedTrack.modelData.id)
                        }
                    }
                }
                ScrollBar.vertical: ScrollBar {}
            }
        }
    }
    Rectangle {
        z: 5
        visible: win.toastMessage.length > 0
        width: Math.min(600, win.width - sidebar.width - 70)
        height: toastText.implicitHeight + 28
        anchors {
            horizontalCenter: mainArea.horizontalCenter
            bottom: playerBar.top
            bottomMargin: 18
        }
        color: "#36513c"
        border.color: "#638469"
        radius: 9
        Text {
            id: toastText
            anchors {
                left: parent.left
                right: closeToast.left
                verticalCenter: parent.verticalCenter
                leftMargin: 16
                rightMargin: 10
            }
            text: win.toastMessage
            color: "#e3f0e4"
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }
        IconButton {
            id: closeToast
            anchors {
                right: parent.right
                verticalCenter: parent.verticalCenter
                rightMargin: 5
            }
            symbol: "close"
            label: "Dismiss message"
            onClicked: win.toastMessage = ""
        }
    }
    Timer {
        id: toastTimer
        interval: 6500
        onTriggered: win.toastMessage = ""
    }
    DropArea {
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: function (drop) {
            if (drop.hasUrls) {
                library.addFiles(drop.urls);
                drop.acceptProposedAction();
            }
        }
        Rectangle {
            anchors {
                fill: parent
                margins: 10
            }
            visible: parent.containsDrag
            color: "#e61a3022"
            radius: 12
            border {
                color: win.accent
                width: 2
            }
            Column {
                anchors.centerIn: parent
                spacing: 14
                Icon {
                    name: "music"
                    width: 44
                    height: 44
                    color: win.accent
                    anchors.horizontalCenter: parent.horizontalCenter
                }
                Text {
                    text: "Drop your music here"
                    color: win.textColor
                    font.pixelSize: 24
                    font.weight: Font.DemiBold
                }
                Text {
                    text: "Audio files or whole folders. Make yourself at home."
                    color: "#a0c0a8"
                    font.pixelSize: 12
                    anchors.horizontalCenter: parent.horizontalCenter
                }
            }
        }
    }
}
