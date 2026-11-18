import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs as NativeDialogs
import QtMultimedia
import MusicPlayer 1.0
import "components"
import "components/TimeFormat.js" as TimeFormat

ApplicationWindow {
    id: win
    objectName: "musicaWindow"
    visible: true
    width: 1280
    height: 850
    minimumWidth: Math.min(720, Math.max(320, Theme.preferences.availableGeometry.width - Theme.dp(16)))
    minimumHeight: Math.min(440, Math.max(240, Theme.preferences.availableGeometry.height - Theme.dp(48)))
    readonly property var displayPreferences: Theme.preferences
    readonly property bool uiActive: visibility !== Window.Minimized && visibility !== Window.Hidden
    readonly property bool narrowLayout: width < Theme.dp(1000)
    readonly property bool shortLayout: height < Theme.dp(700)
    Component.onCompleted: {
        Theme.preferences.watchWindow(win);
        library.setArtistSeparation(Theme.preferences.splitArtists, Theme.preferences.artistSeparators);
        queueEntries.synchronize(queueItems);
    }
    title: (currentTrack ? currentTrack.title + " · Musica " : "Musica ") + Qt.application.version
    color: Theme.background
    font: Theme.controlFont
    palette.window: Theme.surface
    palette.base: Theme.input
    palette.text: Theme.text
    palette.windowText: Theme.text
    palette.button: Theme.raised
    palette.buttonText: Theme.text
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.onAccent
    palette.mid: Theme.border
    palette.light: Theme.surface
    palette.dark: Theme.border
    palette.midlight: Theme.raised
    palette.shadow: Theme.border
    palette.alternateBase: Theme.raised
    palette.placeholderText: Theme.muted

    readonly property color accent: Theme.accent
    readonly property color textColor: Theme.text
    readonly property color secondary: Theme.muted
    readonly property var libraryBackend: library
    readonly property var playbackBackend: player
    readonly property var sharingBackend: sharing
    property string activePage: "Overview"
    property bool shuttingDown: false
    property string collectionName: ""
    property string collectionKind: ""
    property string collectionArtist: ""
    property string collectionId: ""
    property string searchQuery: ""
    property var songFilters: ({})
    readonly property int activeFilterCount: Object.keys(songFilters).length
    readonly property var visibleSongColumns: Theme.preferences.songColumns.filter(column => column.visible && (!narrowLayout || column.key !== "album" || Theme.preferences.columnsCustomized))
    property string resizingColumn: ""
    property real pendingColumnWidth: 0
    readonly property real songTitleMinimumWidth: Math.max(Theme.dp(160), Theme.textPixels * 9)
    readonly property real songTableWidth: Math.max(contentScroll.width, Theme.dp(181) + songTitleMinimumWidth
        + visibleSongColumns.reduce((sum, column) => sum + songColumnWidth(column), 0)
        + (activePage === "Most Played" ? Theme.dp(84) : 0)
        + Theme.gap(12) * (6 + visibleSongColumns.length + (activePage === "Most Played" ? 1 : 0)))
    readonly property bool overviewSummary: activePage === "Overview" && !searchQuery.trim() && activeFilterCount === 0
    property string currentUrl: ""
    property var selectedTrack: null
    property var selectedUrls: []
    readonly property int selectedCount: selectedUrls.length
    readonly property int selectedVisibleCount: {
        if (!selectedUrls.length) return 0;
        const revision = visibleTracks.revision;
        return visibleTracks.countMatchingUrls(selectedUrls);
    }
    property var queueSource: []
    property var queueItems: []
    readonly property var queueUrls: queueItems.map(item => item.fileUrl)
    property string currentQueueId: ""
    property int queueSerial: 0
    readonly property int queueCount: queueItems.length
    readonly property int currentQueueIndex: queueItems.findIndex(item => item.id === currentQueueId)
    property bool nowPlayingOpen: false
    property bool fullscreenNowPlaying: false
    property int fullscreenReturnVisibility: Window.Windowed
    property int mainWindowRestoreVisibility: Window.Windowed
    property var navigationHistory: []
    property bool restoringNavigation: false
    property int navigationRevision: 0
    property var savedPages: ({})
    property bool viewPreferencesLoaded: false
    readonly property bool canGoBack: nowPlayingOpen || navigationHistory.length > 0
    readonly property real playerBarExtent: fullscreenNowPlaying ? 0 : playerBar.height
    property bool shuffleEnabled: false
    property int repeatMode: 0
    property bool queueOpen: false
    property string queueSearch: ""
    onQueueSearchChanged: {
        queueSearchTimer.stop();
        if (!queueSearch.trim()) queueEntries.synchronize(queueItems);
        else queueSearchTimer.restart();
    }
    property bool muted: false
    property string sortBy: "Title"
    property string albumSortKey: "title"
    property bool albumSortDescending: false
    property string artistSortKey: "name"
    property bool artistSortDescending: false
    property string genreSortKey: "name"
    property bool genreSortDescending: false
    property string albumViewStyle: "grid"
    property string artistViewStyle: "grid"
    readonly property bool collectionListView: !collectionName && ((activePage === "Albums" && albumViewStyle === "list") || (activePage === "Artists" && artistViewStyle === "list"))
    readonly property string groupPageKind: activePage === "Artists" ? "artist" : activePage === "Genres" ? "genre" : activePage === "Playlists" ? "playlist" : "album"
    readonly property bool isGroupPage: activePage === "Albums" || activePage === "Artists" || activePage === "Genres" || activePage === "Playlists"
    readonly property bool browsingCollections: !collectionName && isGroupPage && activePage !== "Playlists"
    readonly property string groupSortKey: activePage === "Artists" ? artistSortKey : activePage === "Genres" ? genreSortKey : albumSortKey
    readonly property bool groupSortDescending: activePage === "Artists" ? artistSortDescending : activePage === "Genres" ? genreSortDescending : albumSortDescending
    readonly property var groupSortOptions: activePage === "Genres"
        ? [{key: "name", label: "Genre name"}, {key: "count", label: "Number of songs"}, {key: "duration", label: "Hours"}, {key: "added", label: "Recently added"}]
        : activePage === "Artists"
        ? [{key: "name", label: "Artist name"}, {key: "count", label: "Number of songs"}, {key: "duration", label: "Hours"}]
        : [{key: "added", label: "Recently added"}, {key: "artist", label: "Artist name"}, {key: "title", label: "Title"}]
    readonly property string groupSortLabel: {
        const option = groupSortOptions.find(item => item.key === groupSortKey);
        return option ? option.label : "Title";
    }
    property string toastMessage: ""
    property var undoHistory: []
    property bool applyingUndo: false
    readonly property bool canUndoAction: undoHistory.length > 0
    readonly property string undoActionLabel: canUndoAction ? undoHistory[undoHistory.length - 1].label : ""
    property bool sessionRestored: false
    property bool restoringSession: false
    property double restorePosition: -1
    property bool playAfterQueueRestore: false
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
    readonly property var visibleGroups: activePage === "Artists" ? artists : activePage === "Genres" ? library.genres : activePage === "Playlists" ? library.playlists : albums
    readonly property string pageTitle: collectionName || (activePage === "Songs" ? "All songs" : activePage)
    readonly property string folderPath: activePage === "Folders" && collectionKind === "folder" ? collectionId : ""
    readonly property var folderData: {
        if (activePage !== "Folders") return {path: "", parent: "", folders: [], count: 0};
        const revision = library.songs.revision;
        return library.folderBrowser(folderPath);
    }

    function openExternalFiles(urls, append) { library.openFiles(urls, !!append); }
    function isPinned(kind, id) { const pins = library.pinnedCollections; return library.collectionPinned(kind, id); }
    function togglePin(kind, id, name) {
        const pinned = !isPinned(kind, id);
        if (library.setCollectionPinned(kind, id, name, pinned)) showToast(pinned ? "Pinned to sidebar." : "Unpinned.");
    }
    function openPinned(pin) {
        if (pin.kind === "folder") { openFolder(pin); return; }
        const page = pin.kind === "artist" ? "Artists" : pin.kind === "genre" ? "Genres" : pin.kind === "playlist" ? "Playlists"
            : pin.kind === "smart" || pin.kind === "customSmart" ? "Smart playlists" : "Albums";
        openCollection(page, {id: pin.id, name: pin.name, kind: pin.kind});
    }
    function openFolder(folder) {
        if (!folder.id) { visitPage("Folders", null); return; }
        const name = folder.name || folder.id.split("/").filter(part => part).pop() || folder.id;
        visitPage("Folders", {kind: "folder", id: folder.id, name: name});
    }
    function openFolderParent() {
        if (library.folders.indexOf(folderPath) >= 0) visitPage("Folders", null);
        else openFolder({id: folderData.parent || "", name: ""});
    }
    function openSmartEditor(details) { smartEditor.details = details || {}; smartEditor.open(); }
    function openLibraryTools(page) { libraryTools.begin(page || "repair"); }
    function openLyrics(track) {
        if (!track && !currentTrack) return;
        lyricsPanel.track = track || currentTrack;
        lyricsPanel.details = {followCurrentSong: !track || track.fileUrl === currentUrl};
        lyricsPanel.open();
    }
    function openTrackBookmarks(track) {
        if (!track) return;
        bookmarkPanel.track = track;
        bookmarkPanel.position = track.fileUrl === currentUrl ? player.position : 0;
        bookmarkPanel.open();
    }
    function playBookmark(track, position) {
        if (!track) return;
        if (track.fileUrl === currentUrl) { seekPlayback(position); player.play(); }
        else {
            startPlayback(track, [track], track.title, false);
            restorePosition = position; playAfterQueueRestore = true; applyRestoredPosition();
        }
    }
    function prepareLibraryBackup() {
        savePagePreferences();
        persistSession();
        return {display: Theme.preferences.exportPreferences(), playback: player.exportOptions(), lyrics: player.lyrics.exportAttachments()};
    }
    function prepareLibraryRestore() { persistSession(); player.pause(); sessionSave.stop(); viewSaveTimer.stop(); }
    function restoreLibraryBackup(extra) {
        viewSaveTimer.stop(); viewPreferencesLoaded = false; savedPages = ({}); undoHistory = []; songFilters = ({});
        player.stop(); player.source = "";
        if (extra.display) Theme.preferences.importPreferences(extra.display);
        if (extra.playback) player.importOptions(extra.playback);
        if (extra.lyrics) player.lyrics.importAttachments(extra.lyrics);
        selectedUrls = []; navigationHistory = []; collectionName = ""; collectionId = ""; collectionKind = ""; activePage = "Overview";
        searchField.clear();
        applyQueueSession(library.playbackSession(), false);
        restorePagePreferences();
        showToast("Backup restored. Press Play to resume.");
    }
    function repairedLibraryUrls(mapping) {
        const resume = isPlaying;
        player.lyrics.remapSources(mapping);
        selectedUrls = selectedUrls.map(url => mapping[url] || url);
        player.stop(); player.source = "";
        applyQueueSession(library.playbackSession(), resume);
        showToast("Moved files relinked.");
    }

    function pageSnapshot() {
        return {
            page: activePage, kind: collectionKind, id: collectionId, name: collectionName, artist: collectionArtist,
            search: searchQuery, filters: songFilters, sort: sortBy, selection: selectedUrls.slice(), nowPlaying: nowPlayingOpen,
            scroll: {outer: contentScroll.contentY - contentScroll.originY,
                groups: groupGrid.contentY - groupGrid.originY, songs: songList.contentY - songList.originY, songsX: songList.contentX - songList.originX}
        };
    }
    function rememberPage() {
        cacheCurrentPage();
        if (!restoringNavigation) navigationHistory = navigationHistory.slice(-99).concat([pageSnapshot()]);
    }
    function pageStateKey() { return activePage + "|" + collectionKind + "|" + collectionId; }
    function cacheCurrentPage() {
        if (!viewPreferencesLoaded || restoringNavigation) return;
        const page = pageSnapshot(); delete page.selection; delete page.nowPlaying;
        const pages = Object.assign({}, savedPages);
        const key = pageStateKey(); delete pages[key]; pages[key] = page;
        const keys = Object.keys(pages); for (let i = 0; i < keys.length - 100; ++i) delete pages[keys[i]];
        savedPages = pages;
    }
    function savePagePreferences() {
        if (!viewPreferencesLoaded || restoringNavigation || !library.libraryReady) return;
        cacheCurrentPage();
        library.saveViewState({version: 1, lastKey: pageStateKey(), pages: savedPages,
            albumSortKey: albumSortKey, albumSortDescending: albumSortDescending,
            artistSortKey: artistSortKey, artistSortDescending: artistSortDescending,
            genreSortKey: genreSortKey, genreSortDescending: genreSortDescending,
            albumViewStyle: albumViewStyle, artistViewStyle: artistViewStyle});
    }
    function applyPageState(page) {
        activePage = page.page; collectionKind = page.kind || ""; collectionId = page.id || "";
        collectionName = page.name || ""; collectionArtist = page.artist || "";
        sortBy = page.sort || "Title"; searchField.text = page.search || ""; songFilters = page.filters || {};
    }
    function restorePagePreferences() {
        if (viewPreferencesLoaded) return;
        const saved = library.savedViewState();
        savedPages = saved.pages || {};
        restoringNavigation = true;
        albumSortKey = saved.albumSortKey || "title"; albumSortDescending = !!saved.albumSortDescending;
        artistSortKey = saved.artistSortKey || "name"; artistSortDescending = !!saved.artistSortDescending;
        genreSortKey = saved.genreSortKey || "name"; genreSortDescending = !!saved.genreSortDescending;
        albumViewStyle = saved.albumViewStyle || "grid"; artistViewStyle = saved.artistViewStyle || "grid";
        const page = savedPages[saved.lastKey];
        if (page) applyPageState(page);
        restoringNavigation = false; viewPreferencesLoaded = true;
        if (page) restorePageScroll(page.scroll);
    }
    function restorePageScroll(positions) {
        const revision = ++navigationRevision;
        const saved = positions || {};
        Qt.callLater(function() {
            if (revision !== win.navigationRevision) return;
            groupGrid.forceLayout(); songList.forceLayout();
            // Reset old origins after filtering, then restore relative offsets.
            groupGrid.positionViewAtBeginning(); songList.positionViewAtBeginning();
            for (const pair of [[contentScroll, saved.outer || 0], [groupGrid, saved.groups || 0], [songList, saved.songs || 0]]) {
                const view = pair[0], maximum = Math.max(0, view.contentHeight - view.height);
                view.contentY = view.originY + Math.max(0, Math.min(maximum, pair[1]));
            }
            songList.contentX = songList.originX + Math.max(0, Math.min(songList.contentWidth - songList.width, saved.songsX || 0));
        });
    }
    function visitPage(page, group) {
        const kind = group ? (group.kind || (page === "Artists" ? "artist" : page === "Genres" ? "genre" : page === "Playlists" ? "playlist" : "album")) : "";
        if (!group && page === activePage && !collectionName && !nowPlayingOpen) return;
        rememberPage();
        restoringNavigation = true;
        closeNowPlaying();
        queueOpen = false;
        selectedUrls = [];
        songFilters = ({});
        activePage = page;
        collectionKind = kind;
        collectionId = group ? group.id : "";
        collectionName = group ? group.name : "";
        collectionArtist = kind === "album" ? (group.subtitle || "") : "";
        sortBy = kind === "album" ? "Track" : kind === "playlist" ? "Order" :
            kind === "smart" ? (group.id === "recent" ? "Added" : group.id === "rare" ? "Rare" : "Title") :
            page === "Most Played" ? "Plays" : "Title";
        searchField.clear();
        const savedPage = savedPages[pageStateKey()];
        if (savedPage) applyPageState(savedPage);
        restoringNavigation = false;
        restorePageScroll(savedPage ? savedPage.scroll : {});
    }
    function navigate(page) { visitPage(page, null); }
    function showGroup(group) { visitPage(activePage, group); }
    function openCollection(page, group) { visitPage(page, group); }
    function goBack() {
        if (queueOpen) { queueOpen = false; return; }
        if (nowPlayingOpen) { closeNowPlaying(); return; }
        if (!navigationHistory.length) return;
        const previous = navigationHistory[navigationHistory.length - 1];
        navigationHistory = navigationHistory.slice(0, -1);
        restoringNavigation = true;
        activePage = previous.page;
        collectionKind = previous.kind; collectionId = previous.id;
        collectionName = previous.name; collectionArtist = previous.artist;
        sortBy = previous.sort;
        searchField.text = previous.search;
        songFilters = previous.filters || {};
        selectedUrls = previous.selection.filter(url => !!library.trackForUrl(url).fileUrl);
        nowPlayingOpen = previous.nowPlaying;
        restoringNavigation = false;
        restorePageScroll(previous.scroll);
    }
    function toggleNowPlaying() {
        if (nowPlayingOpen) closeNowPlaying();
        else { queueOpen = false; nowPlayingOpen = true; }
    }
    function closeNowPlaying() {
        leaveFullscreenNowPlaying();
        nowPlayingOpen = false;
    }
    function leaveFullscreenNowPlaying() {
        if (!fullscreenNowPlaying) return;
        fullscreenNowPlaying = false;
        win.showNormal();
        if (fullscreenReturnVisibility === Window.Maximized) win.showMaximized();
    }
    function toggleFullscreenNowPlaying() {
        if (fullscreenNowPlaying) { leaveFullscreenNowPlaying(); return; }
        fullscreenReturnVisibility = win.visibility === Window.Maximized ? Window.Maximized : Window.Windowed;
        queueOpen = false; playbackOptions.close(); sleepPopup.close();
        nowPlayingOpen = true; fullscreenNowPlaying = true;
        win.showFullScreen();
    }
    function playGroup(group, kind) {
        const tracks = library.tracksForGroup(kind, group.id);
        if (tracks.length)
            startPlayback(tracks[0], tracks, group.name, false);
    }
    function setGroupSort(key) {
        if (!groupSortOptions.some(option => option.key === key) || groupSortKey === key) return;
        if (activePage === "Artists") { artistSortKey = key; artistSortDescending = key !== "name"; }
        else if (activePage === "Genres") { genreSortKey = key; genreSortDescending = key !== "name"; }
        else { albumSortKey = key; albumSortDescending = key === "added"; }
    }
    function setGroupSortDescending(descending) {
        if (activePage === "Artists") artistSortDescending = descending;
        else if (activePage === "Genres") genreSortDescending = descending;
        else albumSortDescending = descending;
    }
    function formatCollectionDuration(ms) {
        if (ms <= 0) return "0 min";
        const minutes = Math.max(1, Math.floor(ms / 60000));
        const hours = Math.floor(minutes / 60);
        return hours ? hours + " h" + (minutes % 60 ? " " + minutes % 60 + " min" : "") : minutes + " min";
    }
    function formatTime(ms) {
        return TimeFormat.duration(ms);
    }
    function songColumnWidth(column) {
        return Math.max(Theme.dp(resizingColumn === column.key ? pendingColumnWidth : column.width), Theme.textPixels * (column.key === "duration" ? 4.8 : 2.5));
    }
    function songColumnValue(track, key) {
        if (key === "duration") return track.duration > 0 ? formatTime(track.duration) : "—";
        if (key === "year") return String(track.year || track.date || "—").slice(0, 4);
        if (key === "format") {
            const extension = String(track.fileUrl || "").match(/\.([a-z0-9]{2,8})$/i);
            return track.format || (extension ? extension[1].toUpperCase() : "—");
        }
        return track[key] || "—";
    }
    function commitColumnWidth(key, width) {
        const columns = Theme.preferences.songColumns.map(column => ({key: column.key, width: column.key === key ? Math.max(50, Math.min(500, Math.round(width))) : column.width, visible: column.visible}));
        resizingColumn = ""; Theme.preferences.songColumns = columns;
    }
    function showToast(message) {
        toastMessage = message;
        toastTimer.restart();
    }
    function rememberAction(action) {
        if (applyingUndo) return;
        undoHistory = undoHistory.slice(-19).concat([action]);
    }
    function queueEditSnapshot() {
        return {queue: queueItems.slice(), source: queueSource.slice(), currentId: currentQueueId,
            position: restorePosition >= 0 ? restorePosition : player.position, shuffle: shuffleEnabled,
            repeat: repeatMode, serial: queueSerial, muted: muted};
    }
    function rememberQueueEdit(label, before) {
        rememberAction({kind: "queue", bank: library.activeQueueId, label: label, before: before});
    }
    function undoLastAction() {
        if (!canUndoAction) return;
        const action = undoHistory[undoHistory.length - 1];
        undoHistory = undoHistory.slice(0, -1);
        applyingUndo = true;
        let restored = false;
        if (action.kind === "playlist") {
            restored = library.undoPlaylistEdit(action.token);
            if (restored && collectionKind === "playlist") {
                const playlist = library.playlists.entries().find(entry => entry.id === collectionId);
                if (playlist) collectionName = playlist.name;
                else navigate("Playlists");
            }
        } else if (action.kind === "queue") {
            const active = action.bank === library.activeQueueId;
            if (active) persistSession();
            restored = library.restoreQueueEdit(action.bank, action.before);
            if (restored && active) {
                const saved = library.playbackSession();
                const keepPlayingEntry = (saved.queue || []).some(entry => entry.id === currentQueueId);
                if (keepPlayingEntry) {
                    restoringSession = true; shuffleEnabled = !!saved.shuffle;
                    queueSource = saved.source || []; queueItems = saved.queue || [];
                    restoringSession = false;
                    persistSession();
                } else {
                    player.pause(); player.source = ""; library.beginPlayback("");
                    applyQueueSession(saved, false);
                }
            }
        }
        applyingUndo = false;
        showToast(restored ? "Undone: " + action.label : "This action can no longer be undone.");
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
        if (Theme.preferences.avoidRecentShuffle) {
            const recent = new Set(library.recentlyPlayedUrls(Theme.preferences.shuffleRecentDays));
            return result.filter(entry => !recent.has(entry.fileUrl)).concat(result.filter(entry => recent.has(entry.fileUrl)));
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
                startPlayback(track, [track], track.title, false);
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
        startPlayback(track, tracks, playbackContextName(track), shuffleEnabled);
    }
    function playbackContextName(track) {
        const query = searchQuery.trim();
        if (query) return "Search: " + query;
        if (collectionName) return collectionName;
        if (activePage === "Songs") return "All songs";
        if (activePage === "Overview") return "Library";
        if (activePage === "Listening dashboard") return track ? track.title : "Listening history";
        return activePage;
    }
    function startPlayback(track, tracks, name, shuffle) {
        if (!track || !tracks || !tracks.length || !tracks.some(item => item.fileUrl === track.fileUrl)) return false;
        if (!sessionRestored) restoreSession();
        // Save the outgoing queue before changing its shuffle state or order.
        persistSession();
        const source = tracks.map(item => newQueueItem(item.fileUrl));
        const selected = source.find(item => item.fileUrl === track.fileUrl);
        let items = shuffle ? shuffled(source) : source.slice();
        if (shuffle) {
            items = items.filter(item => item.id !== selected.id);
            items.unshift(selected);
        }
        const id = library.startPlaybackQueue(name || track.title || "Queue", {queue: items, source: source,
            currentId: selected.id, position: 0, shuffle: !!shuffle, repeat: repeatMode, serial: queueSerial, muted: muted});
        if (!id) return false;
        undoHistory = undoHistory.filter(action => action.kind !== "queue" || action.bank !== id);
        restoringSession = true;
        queueSource = source;
        shuffleEnabled = !!shuffle;
        queueItems = items;
        restoringSession = false;
        playAfterQueueRestore = false;
        playQueueItem(selected.id);
        sessionSave.restart();
        Qt.callLater(function() { if (queueOpen) queuePopup.scrollToCurrentSong(); });
        return true;
    }
    function enqueueTrack(track, playNext) {
        if (track) enqueueTracks([track], playNext);
    }
    function enqueueTracks(tracks, playNext) {
        if (!tracks.length) return;
        const before = queueEditSnapshot();
        const entries = tracks.map(track => newQueueItem(track.fileUrl));
        const active = queueItems.slice(), original = queueSource.slice();
        const nextIndex = playNext ? Math.max(0, currentQueueIndex + 1) : active.length;
        const sourceIndex = playNext ? Math.max(0, original.findIndex(item => item.id === currentQueueId) + 1) : original.length;
        // Insert the entire batch once so Play next preserves album/selection order.
        queueSource = original.slice(0, sourceIndex).concat(entries, original.slice(sourceIndex));
        queueItems = active.slice(0, nextIndex).concat(entries, active.slice(nextIndex));
        rememberQueueEdit(playNext ? "Insert songs next" : "Add songs to queue", before);
        showToast((playNext ? "Playing next: " : "Added to queue: ") + (tracks.length === 1 ? tracks[0].title : tracks.length + " songs"));
    }
    function toggleSelection(url) {
        selectedUrls = selectedUrls.indexOf(url) >= 0 ? selectedUrls.filter(item => item !== url) : selectedUrls.concat([url]);
    }
    function selectVisibleSongs() {
        const urls = visibleTracks.fileUrls();
        const visible = new Set(urls), selected = new Set(selectedUrls);
        selectedUrls = selectedVisibleCount === urls.length ? selectedUrls.filter(url => !visible.has(url)) :
            selectedUrls.concat(urls.filter(url => !selected.has(url)));
    }
    function selectedSongs() {
        // Follow the displayed order, with songs hidden by search kept afterwards.
        const visible = visibleTracks.entriesForUrls(selectedUrls);
        const shown = visible.map(track => track.fileUrl);
        return visible.concat(selectedUrls.filter(url => shown.indexOf(url) < 0).map(url => library.trackForUrl(url)).filter(track => !!track.fileUrl));
    }
    function deselectSongs(urls) {
        if (!selectedUrls.length || !urls.length) return;
        const completed = new Set(urls);
        selectedUrls = selectedUrls.filter(url => !completed.has(url));
    }
    function handleBatchAction(action, tracks) {
        if (!tracks || !tracks.length) return;
        const urls = tracks.map(track => track.fileUrl);
        if (action === "play" || action === "shuffle") {
            const shuffle = action === "shuffle";
            const name = (collectionName || "Selected songs") + (collectionName ? " · Selection" : "");
            if (startPlayback(shuffle ? shuffled(tracks)[0] : tracks[0], tracks, name, shuffle)) deselectSongs(urls);
        } else if (action === "queue" || action === "next") {
            enqueueTracks(tracks, action === "next");
            deselectSongs(urls);
        } else if (action === "playlist") {
            playlistDialog.track = null; playlistDialog.tracks = tracks; playlistDialog.open();
        } else if (action === "favorite" || action === "unfavorite") {
            library.setTracksFavorite(urls, action === "favorite");
            deselectSongs(urls);
            showToast(action === "favorite" ? "Added to favorites." : "Removed from favorites.");
        } else if (action === "metadata") batchMetadataEditor.begin(tracks);
        else if (action === "trash") { trashDialog.track = null; trashDialog.tracks = tracks; trashDialog.open(); }
        else if (action === "removePlaylist" && library.removeTracksFromPlaylist(collectionId, urls)) deselectSongs(urls);
    }
    function openGroupMenu(group, kind, anchor, mouse) {
        groupMenu.group = group; groupMenu.kind = kind;
        groupMenu.pinned = isPinned(kind, group.id);
        groupMenu.tracks = library.tracksForGroup(kind, group.id);
        groupMenu.parent = anchor;
        if (mouse) groupMenu.popup(mouse.x, mouse.y);
        else groupMenu.popup();
    }
    function refreshCollectionAfterMetadata(urls) {
        Theme.cache.invalidate();
        if (currentUrl) selectedTrack = library.trackForUrl(currentUrl);
        if (!collectionName || (collectionKind !== "album" && collectionKind !== "artist" && collectionKind !== "genre")) return;
        const remaining = library.tracksForGroup(collectionKind, collectionId);
        const first = remaining.length ? remaining[0] : library.trackForUrl(urls[0] || "");
        if (first && first.fileUrl) {
            if (collectionKind === "genre") {
                const genre = library.genreForTag(first.genre || "");
                collectionId = genre.id; collectionName = genre.name;
                return;
            }
            const credits = first.artistCredits || [{id: first.artistId, name: first.artist}];
            const credit = credits.find(artist => artist.id === collectionId) || credits[0];
            collectionId = collectionKind === "album" ? first.albumId : credit.id;
            collectionName = collectionKind === "album" ? first.album : credit.name;
            collectionArtist = collectionKind === "album" ? first.albumArtist : "";
        }
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
        const before = queueEditSnapshot();
        const wasCurrent = id === currentQueueId;
        queueSource = queueSource.filter(item => item.id !== id);
        queueItems = queueItems.filter(item => item.id !== id);
        if (wasCurrent) {
            if (queueItems.length)
                playQueueItem(queueItems[Math.min(index, queueItems.length - 1)].id);
            else
                stopAndClearTrack();
        }
        rememberQueueEdit("Remove song from queue", before);
        showToast("Removed from queue. Undo is available.");
    }
    function removeDeletedSong(url) {
        const index = currentQueueIndex;
        const wasCurrent = currentUrl === url;
        queueSource = queueSource.filter(item => item.fileUrl !== url && !!library.trackForUrl(item.fileUrl).fileUrl);
        queueItems = queueItems.filter(item => item.fileUrl !== url && !!library.trackForUrl(item.fileUrl).fileUrl);
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
        const credits = track.artistCredits || [{id: track.artistId, name: track.artist}];
        if (credits.length === 1) openArtistCredit(credits[0]);
        else { artistPicker.credits = credits; artistPicker.open(); }
    }
    function openArtistCredit(credit) {
        if (credit) openCollection("Artists", {id: credit.id, name: credit.name, subtitle: ""});
    }
    function openAlbum(track) {
        if (!track)
            return;
        openCollection("Albums", {
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
        else if (action === "share") sharing.prepareSong(track);
        else if (action === "screenshot") shareNowPlayingScreenshot();
        else if (action === "editMetadata") metadataEditor.begin(track.fileUrl);
        else if (action === "bookmarks") openTrackBookmarks(track);
        else if (action === "lyrics") openLyrics(track);
        else if (action === "trash") {
            trashDialog.track = track;
            trashDialog.open();
        }
    }
    function playAll(shuffle) {
        if (visibleTracks.count === 0)
            return;
        const tracks = visibleTracks.entries();
        const first = shuffle ? shuffled(tracks)[0] : tracks[0];
        startPlayback(first, tracks, playbackContextName(first), shuffle);
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
    function seekBy(direction) {
        if (!currentUrl || !player.seekable) return;
        const target = player.position + direction * Theme.preferences.seekSeconds * 1000;
        seekPlayback(Math.max(0, Math.min(Math.max(0, player.duration - 1), target)));
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
        applyQueueSession(library.playbackSession(), false);
        restorePagePreferences();
    }
    function applyQueueSession(saved, autoplay) {
        restoringSession = true;
        queueSource = saved.source || [];
        shuffleEnabled = !!saved.shuffle;
        repeatMode = saved.repeat || 0;
        muted = !!saved.muted;
        queueSerial = Math.max(queueSerial, saved.serial || 0);
        queueItems = saved.queue || [];
        currentQueueId = saved.currentId || "";
        const entry = queueItems.find(item => item.id === currentQueueId);
        currentUrl = ""; selectedTrack = null; restorePosition = -1;
        playAfterQueueRestore = autoplay;
        if (entry) {
            selectedTrack = trackForUrl(entry.fileUrl);
            currentUrl = entry.fileUrl;
            library.beginPlayback(currentUrl);
            player.source = currentUrl;
            // Install the cached duration before duration/status signals may
            // restore a seek. The first decoded fragment is not the track length.
            player.updateTrackInfo(currentUrl, selectedTrack || {});
            restorePosition = saved.position || 0;
            applyRestoredPosition();
        } else playAfterQueueRestore = false;
        restoringSession = false;
        sessionRestored = true;
        Qt.callLater(function() { if (queueOpen) queuePopup.scrollToCurrentSong(); });
    }
    function activateSavedQueue(id) {
        if (id === library.activeQueueId) return;
        persistSession();
        const resume = isPlaying;
        if (!library.activateQueue(id)) return;
        player.stop(); player.source = ""; library.beginPlayback("");
        applyQueueSession(library.playbackSession(), resume);
    }
    function createSavedQueue(name) {
        persistSession();
        const id = library.createQueue(name);
        if (!id) return;
        player.stop(); player.source = ""; library.beginPlayback("");
        applyQueueSession(library.playbackSession(), false);
    }
    function deleteSavedQueue(id) {
        const active = id === library.activeQueueId, resume = isPlaying;
        if (!library.deleteQueue(id)) return;
        if (active) {
            player.stop(); player.source = ""; library.beginPlayback("");
            applyQueueSession(library.playbackSession(), resume);
        }
    }
    function saveQueueAsPlaylist() {
        if (!queueItems.length) return;
        persistSession();
        const active = library.savedQueues.find(queue => queue.id === library.activeQueueId);
        queueOpen = false;
        queuePlaylistPanel.details = {name: active ? active.name : "Queue", urls: queueItems.map(item => item.fileUrl)};
        queuePlaylistPanel.open();
    }
    function shareNowPlayingScreenshot() {
        if (!currentTrack) return;
        queueOpen = false;
        nowPlayingOpen = true;
        Qt.callLater(function() { sharing.captureNowPlaying(nowPlayingLoader.item); });
    }
    function applyRestoredPosition() {
        if (restorePosition < 0 || player.mediaStatus !== MediaPlayer.LoadedMedia || !player.seekable) return;
        const position = Math.min(restorePosition, Math.max(0, player.duration - 1));
        restorePosition = -1;
        library.seekPlayback(currentUrl, position);
        player.position = position;
        playbackSeeked(position);
        if (playAfterQueueRestore) { playAfterQueueRestore = false; player.play(); }
    }
    function moveQueueItem(id, beforeId) {
        if (id === beforeId) return;
        const item = queueItems.find(entry => entry.id === id);
        if (!item) return;
        const oldIndex = queueItems.findIndex(entry => entry.id === id);
        if ((!beforeId && oldIndex === queueItems.length - 1) || (beforeId && queueItems.findIndex(entry => entry.id === beforeId) === oldIndex + 1)) return;
        const before = queueEditSnapshot();
        const move = function(items) {
            const result = items.filter(entry => entry.id !== id);
            const index = beforeId ? result.findIndex(entry => entry.id === beforeId) : result.length;
            result.splice(index < 0 ? result.length : index, 0, item);
            return result;
        };
        queueSource = move(queueSource);
        queueItems = move(queueItems);
        rememberQueueEdit("Reorder queue", before);
    }
    function movePlaylistSong(url, beforeUrl) {
        if (collectionKind === "playlist" && sortBy === "Order" && !searchQuery)
            library.movePlaylistTrack(collectionId, url, beforeUrl);
    }
    function openSmart(id, name) {
        openCollection("Smart playlists", {kind: "smart", id: id, name: name});
    }
    function openPlaylistMenu(group, anchor) {
        playlistMenu.playlist = group;
        playlistMenu.parent = anchor;
        playlistMenu.popup();
    }
    function quitPlayer() {
        savePagePreferences();
        if (shuttingDown) return;
        shuttingDown = true;
        sessionSave.stop(); prepareTimer.stop();
        player.pause();
        persistSession();
        win.hide();
        Qt.quit();
    }
    function openDisplaySettings() {
        displayDialog.open();
    }
    function openSongColumnSettings() { columnSettings.open(); }
    function openAudioSettings() {
        displayDialog.open();
        displayDialog.item.goToSection("Audio output");
    }
    function restoreMainPlayer() {
        if (fullscreenNowPlaying) win.showFullScreen();
        else if (win.visibility === Window.Minimized || win.visibility === Window.Hidden) {
            if (mainWindowRestoreVisibility === Window.Maximized) win.showMaximized();
            else win.showNormal();
        }
        win.raise(); win.requestActivate();
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
        else if (command === "open") openExternalFiles([value], false);
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
        if (!fullscreenNowPlaying && (visibility === Window.Windowed || visibility === Window.Maximized))
            mainWindowRestoreVisibility = visibility;
        if (fullscreenNowPlaying && (visibility === Window.Windowed || visibility === Window.Maximized)) {
            // Some window systems report an intermediate restored state while
            // entering fullscreen. Check the completed transition before syncing.
            Qt.callLater(function() {
                if (win.fullscreenNowPlaying && (win.visibility === Window.Windowed || win.visibility === Window.Maximized))
                    win.fullscreenNowPlaying = false;
            });
        }
    }
    onQueueItemsChanged: {
        sessionSave.restart(); prepareTimer.restart();
        queueEntries.synchronize(queueItems);
    }
    onCurrentQueueIdChanged: { sessionSave.restart(); prepareTimer.restart(); }
    onRepeatModeChanged: { sessionSave.restart(); prepareTimer.restart(); }
    onMutedChanged: sessionSave.restart()
    onSearchQueryChanged: if (!restoringNavigation) restorePageScroll({})
    onSongFiltersChanged: if (!restoringNavigation) restorePageScroll({})
    onNowPlayingOpenChanged: if (!nowPlayingOpen) leaveFullscreenNowPlaying()
    onCurrentTrackChanged: Theme.preferences.setArtwork(currentTrack && currentTrack.artwork ? currentTrack.artwork : "")
    onShuffleEnabledChanged: {
        sessionSave.restart();
        if (restoringSession || !queueSource.length)
            return;
        const before = queueEditSnapshot(); before.shuffle = !shuffleEnabled;
        let items = shuffleEnabled ? shuffled(queueSource) : queueSource.slice();
        const current = items.find(item => item.id === currentQueueId);
        if (shuffleEnabled && current) {
            items = items.filter(item => item.id !== currentQueueId);
            items.unshift(current);
        }
        queueItems = items;
        rememberQueueEdit("Change shuffle order", before);
    }
    Timer { id: sessionSave; interval: 400; onTriggered: win.persistSession() }
    Timer { id: viewSaveTimer; interval: 500; onTriggered: win.savePagePreferences() }
    Timer { id: prepareTimer; interval: 40; onTriggered: win.prepareNextTrack() }
    Timer {
        id: queueSearchTimer; interval: 150
        onTriggered: { queueEntries.synchronize(win.queueItems); queueList.positionViewAtBeginning(); }
    }
    Timer { interval: 5000; repeat: true; running: win.sessionRestored; onTriggered: win.persistSession() }

    TrackListModel {
        id: library
        objectName: "library"
        onPlaylistEditRecorded: function(label, token) {
            win.rememberAction({kind: "playlist", label: label, token: token});
            win.showToast(label + ". Undo is available.");
        }
        onFilesOpened: function(tracks, name, append) {
            if (append) {
                win.enqueueTracks(tracks, false);
            } else if (tracks.length) win.startPlayback(tracks[0], tracks, name, false);
        }
        onOpeningFilesChanged: if (openingFiles) win.showToast("Opening selected music…")
        onScanningChanged: if (!scanning) Theme.cache.invalidate()
        onLibraryReadyChanged: win.restoreSession()
        onTracksChanged: {
            const kept = win.selectedUrls.filter(url => !!library.trackForUrl(url).fileUrl);
            if (kept.length !== win.selectedUrls.length) win.selectedUrls = kept;
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
    Connections {
        target: library.maintenance
        function onUrlsRelinked(mapping) { win.repairedLibraryUrls(mapping); }
        function onRestored(extraSettings) { win.restoreLibraryBackup(extraSettings); }
    }
    Connections {
        target: Theme.preferences
        function onArtistSettingsChanged() {
            library.setArtistSeparation(Theme.preferences.splitArtists, Theme.preferences.artistSeparators);
            if (win.collectionKind === "artist" && win.collectionId && !library.tracksForGroup("artist", win.collectionId).length)
                win.navigate("Artists");
        }
    }
    Binding {
        target: library.songs
        property: "search"
        value: win.searchQuery
    }
    Binding { target: library.songs; property: "filters"; value: win.songFilters }
    Binding { target: library.mostPlayed; property: "filters"; value: win.songFilters }
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
    Binding { target: library.albums; property: "sortKey"; value: win.albumSortKey }
    Binding { target: library.albums; property: "sortDirection"; value: win.albumSortDescending ? "descending" : "ascending" }
    Binding {
        target: library.artists
        property: "search"
        value: win.searchQuery
    }
    Binding { target: library.artists; property: "sortKey"; value: win.artistSortKey }
    Binding { target: library.artists; property: "sortDirection"; value: win.artistSortDescending ? "descending" : "ascending" }
    Binding { target: library.genres; property: "search"; value: win.searchQuery }
    Binding { target: library.genres; property: "sortKey"; value: win.genreSortKey }
    Binding { target: library.genres; property: "sortDirection"; value: win.genreSortDescending ? "descending" : "ascending" }
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
    GroupMenu {
        id: groupMenu
        objectName: "groupMenu"
        onActionRequested: function(action) {
            if (action === "pin") win.togglePin(kind, group.id, group.name);
            else if (action === "view") {
                win.openCollection(kind === "artist" ? "Artists" : kind === "genre" ? "Genres" : "Albums", group);
            } else if (action === "grouping") albumEditor.start(group.id);
            else if (action === "reveal" && tracks.length) library.revealTrack(tracks[0].fileUrl);
            else if ((action === "play" || action === "shuffle") && tracks.length) {
                const shuffle = action === "shuffle";
                win.startPlayback(shuffle ? win.shuffled(tracks)[0] : tracks[0], tracks, group.name, shuffle);
            } else win.handleBatchAction(action, tracks);
        }
    }
    Sharing {
        id: sharing
        objectName: "sharing"
        onReady: function(details) { sharePanel.details = details; sharePanel.open(); }
        onError: function(message) { win.showToast(message); }
        onCopied: win.showToast("Copied. Paste into your chat or another app.")
        onExported: win.showToast("Shared copy saved.")
    }
    LazyPanel { id: sharePanel; sourceComponent: ShareDialog { parent: Overlay.overlay; sharing: win.sharingBackend } }
    LazyPanel {
        id: smartEditor
        objectName: "smartPlaylistEditorLoader"
        sourceComponent: SmartPlaylistEditor {
            objectName: "smartPlaylistEditor"; parent: Overlay.overlay; library: win.libraryBackend
            onSaved: function(id, name) { if (win.collectionKind === "customSmart" && win.collectionId === id) win.collectionName = name; win.showToast("Smart playlist saved."); }
            onDeleted: function(id) { if (win.collectionId === id) win.navigate("Smart playlists"); }
        }
    }
    LazyPanel {
        id: bookmarkPanel
        objectName: "trackBookmarksLoader"
        property real position: 0
        sourceComponent: TrackBookmarksDialog {
            objectName: "trackBookmarksDialog"; parent: Overlay.overlay; library: win.libraryBackend; controller: win
            position: track && track.fileUrl === win.currentUrl ? player.position : bookmarkPanel.position
            onSeekRequested: function(track, position) { win.playBookmark(track, position); }
        }
    }
    LazyPanel {
        id: lyricsPanel
        objectName: "lyricsPanelLoader"
        sourceComponent: LyricsDialog {
            parent: Overlay.overlay; player: win.playbackBackend
            followCurrentSong: lyricsPanel.details.followCurrentSong !== false
            onSeekRequested: function(track, position) { win.playBookmark(track || win.currentTrack, position); }
        }
    }
    LazyPanel {
        id: libraryTools
        objectName: "libraryToolsLoader"
        sourceComponent: LibraryMaintenanceDialog { parent: Overlay.overlay; controller: win }
    }
    LazyPanel {
        id: queuePlaylistPanel
        sourceComponent: QueuePlaylistDialog {
            parent: Overlay.overlay
            library: win.libraryBackend
            onSaved: function(name) { win.showToast("Playlist saved: " + name); }
        }
    }
    LazyPanel {
        id: queueManager
        objectName: "queueManagerLoader"
        sourceComponent: QueueManagerDialog { id: queueManagerContent; parent: Overlay.overlay; controller: win }
    }
    TrackMenu {
        id: trackMenu
        objectName: "trackMenu"
        onActionRequested: function (action) {
            win.handleTrackAction(action);
        }
    }
    Dialog {
        id: artistPicker
        objectName: "artistPicker"
        property var credits: []
        parent: Overlay.overlay
        modal: true
        title: "Choose an artist"
        width: Math.min(Theme.dp(420), parent.width - Theme.dp(24))
        height: Math.min(Theme.dp(100) + artistPickerList.contentHeight, parent.height - Theme.dp(24))
        x: (parent.width - width) / 2; y: (parent.height - height) / 2
        font: Theme.controlFont
        background: Rectangle { color: Theme.surface; radius: Theme.dp(12); border.color: Theme.border }
        contentItem: ListView {
            id: artistPickerList
            clip: true
            model: artistPicker.credits
            delegate: ItemDelegate {
                required property var modelData
                required property int index
                objectName: "chooseArtist" + index
                width: artistPickerList.width
                text: modelData.name
                font: Theme.controlFont
                onClicked: { artistPicker.close(); win.openArtistCredit(modelData); }
            }
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        }
        standardButtons: Dialog.Close
    }
    LazyPanel {
        id: playlistDialog
        objectName: "playlistDialogLoader"
        sourceComponent: PlaylistDialog {
        id: playlistDialogContent
        objectName: "playlistDialog"
        parent: Overlay.overlay
        library: win.libraryBackend
        onSaved: function (name) {
            win.deselectSongs(playlistDialogContent.urls);
            win.showToast("Saved to playlist: " + name);
        }
    }
    }
    LazyPanel {
        id: albumEditor
        objectName: "albumEditorLoader"
        sourceComponent: AlbumEditorDialog {
        id: albumEditorContent
        objectName: "albumEditor"
        parent: Overlay.overlay
        library: win.libraryBackend
        onGroupingSaved: { win.navigate("Albums"); win.showToast("Album grouping updated." + (library.canUndoAlbumEdit ? " Undo is available in Manage albums." : "")); }
    }
    }
    Menu {
        id: savedSmartMenu
        property var details: ({})
        font: Theme.controlFont
        MenuItem { text: "Edit rules…"; visible: savedSmartMenu.details.rules !== undefined; height: visible ? implicitHeight : 0; onTriggered: win.openSmartEditor(savedSmartMenu.details) }
        MenuItem { text: win.isPinned(savedSmartMenu.details.rules !== undefined ? "customSmart" : "smart", savedSmartMenu.details.id || "") ? "Unpin from sidebar" : "Pin to sidebar"; onTriggered: win.togglePin(savedSmartMenu.details.rules !== undefined ? "customSmart" : "smart", savedSmartMenu.details.id, savedSmartMenu.details.name) }
    }
    Menu {
        id: playlistMenu
        objectName: "playlistMenu"
        property var playlist: ({})
        MenuItem { text: win.isPinned("playlist", playlistMenu.playlist.id || "") ? "Unpin from sidebar" : "Pin to sidebar"; onTriggered: win.togglePin("playlist", playlistMenu.playlist.id, playlistMenu.playlist.name) }
        palette.window: Theme.raised
        palette.text: Theme.text
        palette.windowText: Theme.text
        palette.highlight: Theme.accent
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
        width: Theme.dp(400)
        modal: true
        title: "Rename playlist"
        standardButtons: Dialog.Save | Dialog.Cancel
        palette.windowText: Theme.text
        palette.text: Theme.text
        palette.button: Theme.raised
        palette.buttonText: Theme.text
        background: Rectangle { color: Theme.surface; radius: Theme.dp(12); border.color: Theme.border }
        contentItem: TextField {
            id: playlistRenameField
            objectName: "playlistRenameField"
            color: Theme.text
            selectByMouse: true
            maximumLength: 80
            Accessible.name: "Playlist name"
            background: Rectangle { color: Theme.input; radius: Theme.dp(6); border.color: Theme.border }
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
        width: Theme.dp(410)
        modal: true
        title: "Delete playlist?"
        standardButtons: Dialog.Ok | Dialog.Cancel
        palette.windowText: Theme.text
        palette.text: Theme.text
        palette.button: Theme.raised
        palette.buttonText: Theme.text
        background: Rectangle { color: Theme.surface; radius: Theme.dp(12); border.color: Theme.border }
        contentItem: Text { text: "Delete “" + (playlistMenu.playlist.name || "this playlist") + "”? Your music files stay in your library."; wrapMode: Text.WordWrap; color: Theme.secondary }
        onOpened: { standardButton(Dialog.Ok).text = "Delete playlist"; standardButton(Dialog.Ok).objectName = "confirmDeletePlaylist"; }
        onAccepted: {
            if (library.deletePlaylist(playlistMenu.playlist.id)) {
                win.navigate("Playlists"); win.showToast("Playlist deleted.");
            }
        }
    }
    LazyPanel {
        id: infoDialog
        objectName: "infoDialogLoader"
        sourceComponent: SongInfoDialog {
        id: infoDialogContent
        objectName: "songInfoDialog"
        parent: Overlay.overlay
        onArtistChosen: function(credit) { infoDialog.close(); win.openArtistCredit(credit); }
    }
    LazyPanel {
        id: advancedSearch
        objectName: "advancedSearchLoader"
        sourceComponent: AdvancedSearchDialog {
            parent: Overlay.overlay
            onFiltersApplied: function(filters) { win.songFilters = filters; }
        }
    }
    LazyPanel { id: columnSettings; objectName: "songColumnsLoader"; sourceComponent: SongColumnsDialog { parent: Overlay.overlay } }
    }
    LazyPanel {
        id: metadataEditor
        objectName: "metadataEditorLoader"
        sourceComponent: MetadataEditor {
        id: metadataEditorContent
        objectName: "metadataEditor"
        parent: Overlay.overlay
        library: win.libraryBackend
        onSaved: function(url) {
            win.deselectSongs([url]);
            win.refreshCollectionAfterMetadata([url]);
            win.showToast("Metadata and artwork saved to the audio file.");
        }
    }
    }
    LazyPanel {
        id: batchMetadataEditor
        objectName: "batchMetadataEditorLoader"
        sourceComponent: BatchMetadataEditor {
        id: batchMetadataEditorContent
        objectName: "batchMetadataEditor"
        parent: Overlay.overlay
        library: win.libraryBackend
        onSaved: function(count, savedUrls) {
            win.deselectSongs(savedUrls);
            win.refreshCollectionAfterMetadata(savedUrls);
            win.showToast("Metadata saved to " + count + (count === 1 ? " audio file." : " audio files."));
        }
    }
    }
    PlaybackOptions {
        id: playbackOptions
        objectName: "playbackOptions"
        parent: Overlay.overlay
        player: win.playbackBackend
        x: win.width - width - 20
        y: win.height - win.playerBarExtent - height - 10
    }
    SleepTimerPopup {
        id: sleepPopup
        objectName: "sleepTimerPopup"
        parent: Overlay.overlay
        player: win.playbackBackend
        x: win.width - width - 20
        y: win.height - win.playerBarExtent - height - 10
    }
    Dialog {
        id: trashDialog
        objectName: "trashSongDialog"
        property var track: null
        property var tracks: []
        readonly property var pendingTracks: tracks.length ? tracks : track ? [track] : []
        onClosed: tracks = []
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Theme.dp(430), win.width - Theme.gap(24))
        height: Math.min(Theme.dp(240) + Theme.textPixels * 8, win.height - Theme.gap(24))
        font: Theme.controlFont
        modal: true
        title: pendingTracks.length > 1 ? "Delete " + pendingTracks.length + " songs?" : "Delete song?"
        padding: Theme.gap(22)
        standardButtons: Dialog.Ok | Dialog.Cancel
        palette.windowText: Theme.text
        palette.text: Theme.text
        palette.button: Theme.raised
        palette.buttonText: Theme.text
        background: Rectangle {
            color: Theme.surface
            radius: Theme.dp(12)
            border.color: Theme.border
        }
        contentItem: ScrollView {
            id: trashScroll
            contentWidth: availableWidth
            clip: true
            Text {
            width: trashScroll.availableWidth
            text: (trashDialog.pendingTracks.length === 1 ? "Move “" + trashDialog.pendingTracks[0].title + "” to Trash?" :
                "Move these " + trashDialog.pendingTracks.length + " songs to Trash?\n\n" +
                trashDialog.pendingTracks.slice(0, 8).map(track => "• " + track.artist + " — " + track.title).join("\n") +
                (trashDialog.pendingTracks.length > 8 ? "\n… and " + (trashDialog.pendingTracks.length - 8) + " more" : "")) +
                "\n\nThe audio files will also be removed from your queue and playlists. You can restore them from your system Trash."
            color: Theme.secondary
            font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(13)
            wrapMode: Text.WordWrap
            }
        }
        onOpened: {
            standardButton(Dialog.Ok).text = "Move to Trash";
            standardButton(Dialog.Ok).objectName = "confirmTrashButton";
        }
        onAccepted: {
            const urls = pendingTracks.map(item => item.fileUrl);
            const oldUrl = win.currentUrl;
            const wasCurrent = urls.indexOf(oldUrl) >= 0;
            const wasPlaying = win.isPlaying;
            const oldPosition = player.position;
            if (wasCurrent) { player.stop(); player.source = ""; }
            const result = library.trashTracks(urls);
            if (wasCurrent && win.currentUrl === oldUrl && library.trackForUrl(oldUrl).fileUrl) {
                player.source = oldUrl; player.position = oldPosition;
                if (wasPlaying) player.play();
            }
            const errors = result.errors || [];
            if (errors.length) win.showToast(result.removed.length + " moved to Trash. " + errors.length + " failed: " + errors[0].error);
            else if (result.removed.length) win.showToast("Moved to Trash: " + result.removed.length + (result.removed.length === 1 ? " song." : " songs."));
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
        id: openMusicDialog
        objectName: "openMusicDialog"
        title: "Open and play music"
        currentFolder: library.musicFolder
        fileMode: NativeDialogs.FileDialog.OpenFiles
        nameFilters: ["Audio files (*.mp3 *.flac *.wav *.ogg *.oga *.opus *.m4a *.aac *.aif *.aiff *.wma)"]
        onAccepted: win.openExternalFiles(selectedFiles, false)
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
    LazyPanel {
        id: displayDialog
        objectName: "displayDialogLoader"
        sourceComponent: DisplaySettingsDialog {
        id: displayDialogContent
        parent: Overlay.overlay
        controller: win
    }
    }
    Shortcut {
        enabled: !Theme.preferences.capturingShortcut
        sequence: Theme.preferences.shortcuts.settings
        context: Qt.ApplicationShortcut
        onActivated: win.openDisplaySettings()
    }
    Shortcut {
        enabled: !Theme.preferences.capturingShortcut
        sequence: Theme.preferences.shortcuts.quit
        context: Qt.ApplicationShortcut
        onActivated: win.quitPlayer()
    }
    Shortcut {
        sequence: Theme.preferences.shortcuts.playPause
        enabled: !Theme.preferences.capturingShortcut && (!win.editingText)
        onActivated: win.togglePlayback()
    }
    Shortcut {
        sequence: Theme.preferences.shortcuts.undo
        enabled: win.canUndoAction && !Theme.preferences.capturingShortcut && !win.editingText
        onActivated: win.undoLastAction()
    }
    Shortcut {
        sequence: Theme.preferences.shortcuts.next
        enabled: !Theme.preferences.capturingShortcut && (!win.editingText)
        onActivated: win.nextTrack(false)
    }
    Shortcut {
        sequence: Theme.preferences.shortcuts.seekForward
        enabled: !Theme.preferences.capturingShortcut && (!!win.currentUrl && !win.editingText && (!win.activeFocusItem || win.activeFocusItem.value === undefined || win.activeFocusItem.objectName === "progressSlider"))
        onActivated: win.seekBy(1)
    }
    Shortcut {
        sequence: Theme.preferences.shortcuts.seekBackward
        enabled: !Theme.preferences.capturingShortcut && (!!win.currentUrl && !win.editingText && (!win.activeFocusItem || win.activeFocusItem.value === undefined || win.activeFocusItem.objectName === "progressSlider"))
        onActivated: win.seekBy(-1)
    }
    Shortcut {
        sequence: Theme.preferences.shortcuts.previous
        enabled: !Theme.preferences.capturingShortcut && (!win.editingText)
        onActivated: win.previousTrack()
    }
    Shortcut {
        enabled: !Theme.preferences.capturingShortcut
        sequence: Theme.preferences.shortcuts.search
        onActivated: searchField.forceActiveFocus()
    }
    Shortcut {
        enabled: !Theme.preferences.capturingShortcut
        sequence: Theme.preferences.shortcuts.addFiles
        onActivated: openMusicDialog.open()
    }
    Shortcut {
        sequence: Theme.preferences.shortcuts.back
        enabled: !Theme.preferences.capturingShortcut && (win.canGoBack)
        onActivated: win.goBack()
    }
    Shortcut {
        sequence: "Backspace"
        enabled: !Theme.preferences.capturingShortcut && win.canGoBack && !win.editingText
        onActivated: win.goBack()
    }
    Shortcut {
        enabled: !Theme.preferences.capturingShortcut
        sequence: Theme.preferences.shortcuts.fullscreen
        onActivated: win.toggleFullscreenNowPlaying()
    }
    Shortcut {
        sequence: "Escape"
        enabled: !Theme.preferences.capturingShortcut
        onActivated: {
            if (win.queueOpen) win.queueOpen = false;
            else if (win.fullscreenNowPlaying) win.leaveFullscreenNowPlaying();
            else if (win.nowPlayingOpen) win.closeNowPlaying();
            else searchField.focus = false;
        }
    }

    Rectangle {
        id: sidebar
        objectName: "sidebar"
        visible: !win.fullscreenNowPlaying
        width: Theme.dp(win.narrowLayout ? 68 : 222)
        anchors {
            left: parent.left
            top: parent.top
            bottom: playerBar.top
        }
        color: Theme.sidebar
        Rectangle {
            anchors.right: parent.right
            width: Theme.dp(1)
            height: parent.height
            color: Theme.border
        }

        Row {
            x: Theme.dp(win.narrowLayout ? 13 : 26)
            y: Theme.dp(35)
            spacing: Theme.gap(10)
            Image {
                objectName: "appLogo"
                source: "assets/musica.png"
                width: Theme.dp(42)
                height: Theme.dp(42)
                fillMode: Image.PreserveAspectFit
                mipmap: true
            }
            Text {
                objectName: "appWordmark"
                text: "musica."
                visible: !win.narrowLayout
                color: win.textColor
                font.family: Theme.preferences.fontFamily; font.pointSize: Theme.preferences.fontPointSize * 26 / 12
                font.weight: Font.Bold
                font.letterSpacing: -1
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        Text {
            x: Theme.dp(27)
            y: Theme.dp(109)
            text: "LIBRARY"
                        visible: !win.narrowLayout
            color: Theme.muted
            font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(10)
            font.weight: Font.DemiBold
            font.letterSpacing: 1.7
        }
        Flickable {
            id: sidebarScroll
            objectName: "sidebarScroll"
            x: Theme.dp(14)
            y: Theme.dp(win.narrowLayout ? 96 : 139)
            width: parent.width - 28
            height: Math.max(0, parent.height - y - Theme.dp(win.narrowLayout ? 16 : 58))
            contentWidth: width
            contentHeight: sidebarNavigation.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            LibraryScrollHandler { view: sidebarScroll }
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        Column {
            id: sidebarNavigation
            width: parent.width
            spacing: Theme.gap(Theme.compact ? 4 : 7)
            PinnedCollectionsPane { width: parent.width; controller: win }
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
                        page: "Genres",
                        icon: "genre"
                    },
                    { page: "Folders", icon: "folder" },
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
                    height: Theme.controlHeight(Theme.compact ? 34 : 44)
                    hoverEnabled: true
                ToolTip.visible: hovered && win.narrowLayout
                ToolTip.text: Accessible.name
                    Accessible.name: modelData.page
                    onClicked: win.navigate(modelData.page)
                    background: Rectangle {
                        radius: Theme.dp(7)
                        color: win.activePage === navButton.modelData.page ? Theme.selected : navButton.hovered ? Theme.hover : "transparent"
                        border.width: navButton.visualFocus ? 1 : 0
                        border.color: win.accent
                    }
                    contentItem: RowLayout {
                        spacing: win.narrowLayout ? 0 : Theme.gap(13)
                        Icon {
                            name: navButton.modelData.icon
                            color: win.activePage === navButton.modelData.page ? win.accent : Theme.muted
                            Layout.preferredWidth: Theme.dp(19)
                            Layout.preferredHeight: Theme.dp(19)
                            Layout.leftMargin: win.narrowLayout ? 0 : Theme.gap(12)
                        }
                        Text {
                            text: navButton.modelData.page
                        visible: !win.narrowLayout
                            color: win.activePage === navButton.modelData.page ? win.accent : Theme.secondary
                            font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(navButton.modelData.page === "Listening dashboard" ? 10 : 12)
                            font.weight: win.activePage === navButton.modelData.page ? Font.DemiBold : Font.Normal
                            Layout.fillWidth: true
                        }
                        Rectangle {
                            Layout.preferredWidth: Theme.dp(5)
                            Layout.preferredHeight: Theme.dp(5)
                            radius: Theme.dp(3)
                            color: win.accent
                            visible: !win.narrowLayout && win.activePage === navButton.modelData.page
                            Layout.rightMargin: Theme.gap(10)
                        }
                    }
                }
            }
            Rectangle {
                width: parent.width - 22
                height: Theme.dp(1)
                color: Theme.hover
                anchors.horizontalCenter: parent.horizontalCenter
                Layout.topMargin: Theme.gap(12)
            }
            Text {
                text: "COLLECTION"
                        visible: !win.narrowLayout
                x: Theme.dp(13)
                height: Theme.controlHeight(Theme.compact ? 28 : 36)
                verticalAlignment: Text.AlignVCenter
                color: Theme.muted
                font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(10)
                font.weight: Font.DemiBold
                font.letterSpacing: 1.5
            }
            Button {
                id: favoritesNav
                objectName: "navFavorites"
                width: parent.width
                height: Theme.controlHeight(Theme.compact ? 34 : 44)
                hoverEnabled: true
                ToolTip.visible: hovered && win.narrowLayout
                ToolTip.text: Accessible.name
                Accessible.name: "Favorites"
                onClicked: win.navigate("Favorites")
                background: Rectangle {
                    radius: Theme.dp(7)
                    color: win.activePage === "Favorites" ? Theme.selected : favoritesNav.hovered ? Theme.hover : "transparent"
                    border.width: favoritesNav.visualFocus ? 1 : 0
                    border.color: win.accent
                }
                contentItem: RowLayout {
                    spacing: win.narrowLayout ? 0 : Theme.gap(13)
                    Icon {
                        name: "heart"
                        color: win.favoriteCount > 0 ? "#ef4444" : win.activePage === "Favorites" ? win.accent : Theme.muted
                        filled: win.favoriteCount > 0
                        Layout.preferredWidth: Theme.dp(19)
                        Layout.preferredHeight: Theme.dp(19)
                        Layout.leftMargin: win.narrowLayout ? 0 : Theme.gap(12)
                    }
                    Text {
                        text: "Favorites"
                        visible: !win.narrowLayout
                        color: win.activePage === "Favorites" ? win.accent : Theme.secondary
                        font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(12)
                        Layout.fillWidth: true
                    }
                    Text {
                        text: win.favoriteCount
                        visible: !win.narrowLayout
                        color: Theme.muted
                        font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(11)
                        Layout.rightMargin: Theme.gap(10)
                    }
                }
            }
            Button {
                id: mostPlayedNav
                objectName: "navMostPlayed"
                width: parent.width
                height: Theme.controlHeight(Theme.compact ? 34 : 44)
                hoverEnabled: true
                ToolTip.visible: hovered && win.narrowLayout
                ToolTip.text: Accessible.name
                Accessible.name: "Most Played"
                onClicked: win.navigate("Most Played")
                background: Rectangle {
                    radius: Theme.dp(7)
                    color: win.activePage === "Most Played" ? Theme.selected : mostPlayedNav.hovered ? Theme.hover : "transparent"
                    border.width: mostPlayedNav.visualFocus ? 1 : 0
                    border.color: win.accent
                }
                contentItem: RowLayout {
                    spacing: win.narrowLayout ? 0 : Theme.gap(13)
                    Icon {
                        name: "played"
                        color: win.activePage === "Most Played" ? win.accent : Theme.muted
                        Layout.preferredWidth: Theme.dp(19)
                        Layout.preferredHeight: Theme.dp(19)
                        Layout.leftMargin: win.narrowLayout ? 0 : Theme.gap(12)
                    }
                    Text {
                        text: "Most Played"
                        visible: !win.narrowLayout
                        color: win.activePage === "Most Played" ? win.accent : Theme.secondary
                        font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(12)
                        Layout.fillWidth: true
                    }
                }
            }
            Button {
                id: playlistsNav
                objectName: "navPlaylists"
                width: parent.width
                height: Theme.controlHeight(Theme.compact ? 34 : 44)
                hoverEnabled: true
                ToolTip.visible: hovered && win.narrowLayout
                ToolTip.text: Accessible.name
                Accessible.name: "Playlists"
                onClicked: win.navigate("Playlists")
                background: Rectangle {
                    radius: Theme.dp(7)
                    color: win.activePage === "Playlists" ? Theme.selected : playlistsNav.hovered ? Theme.hover : "transparent"
                    border.width: playlistsNav.visualFocus ? 1 : 0
                    border.color: win.accent
                }
                contentItem: RowLayout {
                    spacing: win.narrowLayout ? 0 : Theme.gap(13)
                    Icon {
                        name: "queue"
                        color: win.activePage === "Playlists" ? win.accent : Theme.muted
                        Layout.preferredWidth: Theme.dp(19)
                        Layout.preferredHeight: Theme.dp(19)
                        Layout.leftMargin: win.narrowLayout ? 0 : Theme.gap(12)
                    }
                    Text {
                        text: "Playlists"
                        visible: !win.narrowLayout
                        color: win.activePage === "Playlists" ? win.accent : Theme.secondary
                        font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(12)
                        Layout.fillWidth: true
                    }
                    Text {
                        text: library.playlists.count
                        visible: !win.narrowLayout
                        color: Theme.muted
                        font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(11)
                        Layout.rightMargin: Theme.gap(10)
                    }
                }
            }
            Button {
                id: addFolderNav
                width: parent.width
                height: Theme.controlHeight(Theme.compact ? 34 : 44)
                hoverEnabled: true
                ToolTip.visible: hovered && win.narrowLayout
                ToolTip.text: Accessible.name
                Accessible.name: "Add music folder"
                onClicked: folderDialog.open()
                background: Rectangle {
                    radius: Theme.dp(7)
                    color: addFolderNav.hovered ? Theme.hover : "transparent"
                    border.width: addFolderNav.visualFocus ? 1 : 0
                    border.color: win.accent
                }
                contentItem: RowLayout {
                    spacing: win.narrowLayout ? 0 : Theme.gap(13)
                    Icon {
                        name: "folder"
                        color: Theme.muted
                        Layout.preferredWidth: Theme.dp(19)
                        Layout.preferredHeight: Theme.dp(19)
                        Layout.leftMargin: win.narrowLayout ? 0 : Theme.gap(12)
                    }
                    Text {
                        text: "Add a folder"
                        visible: !win.narrowLayout
                        color: Theme.secondary
                        font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(12)
                        Layout.fillWidth: true
                    }
                    Icon {
                        name: "plus"
                        color: Theme.muted
                        visible: !win.narrowLayout
                        Layout.preferredWidth: Theme.dp(14)
                        Layout.preferredHeight: Theme.dp(14)
                        Layout.rightMargin: Theme.gap(8)
                    }
                }
            }
        }
        }

        Row {
            id: offlineStatus
            x: Theme.dp(27)
            spacing: Theme.gap(8)
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.gap(22)
            Rectangle {
                width: Theme.dp(6)
                height: Theme.dp(6)
                radius: Theme.dp(3)
                color: win.accent
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: "Musica"
                        visible: !win.narrowLayout
                color: Theme.muted
                font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(10)
            }
        }
    }

    Item {
        id: mainArea
        visible: !win.fullscreenNowPlaying
        enabled: !win.nowPlayingOpen
        anchors {
            left: sidebar.right
            right: parent.right
            top: parent.top
            bottom: selectionBar.visible ? selectionBar.top : playerBar.top
        }
        RowLayout {
            id: topBar
            height: Theme.controlHeight(win.shortLayout ? 56 : 76)
            anchors {
                left: parent.left
                right: parent.right
                leftMargin: Theme.gap(34)
                rightMargin: Theme.gap(34)
                top: parent.top
            }
            spacing: Theme.gap(win.narrowLayout ? 8 : 16)
            IconButton {
                objectName: "backButton"
                symbol: "back"
                label: "Back (Alt+Left)"
                enabled: win.canGoBack
                onClicked: win.goBack()
            }
            Text {
                text: win.shortLayout ? win.pageTitle : "Your library"
                visible: !win.narrowLayout || win.shortLayout
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.maximumWidth: mainArea.width * 0.3
                elide: Text.ElideRight
                color: Theme.secondary
                font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(13)
                font.weight: Font.DemiBold
            }
            Item {
                Layout.fillWidth: true
            }
            Rectangle {
                Layout.preferredWidth: Math.min(mainArea.width * (win.narrowLayout ? 0.75 : 0.5), Theme.dp(420))
                Layout.fillWidth: true
                Layout.minimumWidth: Theme.dp(80)
                Layout.maximumWidth: Theme.dp(420)
                Layout.preferredHeight: Theme.controlHeight(38)
                radius: Theme.dp(20)
                color: Theme.input
                border.color: searchField.activeFocus ? Theme.border : Theme.border
                Icon {
                    x: Theme.dp(13)
                    anchors.verticalCenter: parent.verticalCenter
                    name: "search"
                    color: Theme.muted
                    width: Theme.dp(17)
                    height: Theme.dp(17)
                }
                TextField {
                    id: searchField
                    objectName: "searchField"
                    anchors {
                        fill: parent
                        leftMargin: Theme.gap(35)
                        rightMargin: Theme.gap(28)
                    }
                    padding: Theme.gap(0)
                    font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(11)
                    color: win.textColor
                    placeholderText: win.activePage === "Genres" && !win.collectionName ? "Search genres" : "Search songs, artists, albums, genres"
                    placeholderTextColor: Theme.muted
                    selectByMouse: true
                    background: Item {}
                    onTextChanged: win.searchQuery = text
                    onAccepted: if (win.visibleTracks.count)
                        win.playTrack(win.visibleTracks.get(0), win.visibleTracks.entries())
                    Accessible.name: "Search library"
                }
                IconButton {
                    width: Theme.dp(27)
                    height: Theme.dp(27)
                    padding: Theme.gap(8)
                    anchors {
                        right: parent.right
                        rightMargin: Theme.gap(4)
                        verticalCenter: parent.verticalCenter
                    }
                    symbol: "close"
                    label: "Clear search"
                    visible: searchField.text.length > 0
                    onClicked: searchField.clear()
                }
            }
            IconButton {
                objectName: "scanLibraryButton"
                symbol: "refresh"
                label: library.scanning ? library.scanStatus : "Scan library for changes"
                enabled: !library.scanning
                onClicked: library.rescan()
            }
            IconButton {
                objectName: "undoActionButton"; symbol: "undo"
                enabled: win.canUndoAction
                label: win.canUndoAction ? "Undo " + win.undoActionLabel : "Undo"
                onClicked: win.undoLastAction()
            }
            IconButton {
                objectName: "advancedSearchButton"
                visible: win.activePage === "Overview" || songsSection.visible
                symbol: "filter"
                label: "Advanced search" + (win.activeFilterCount ? " · " + win.activeFilterCount + " filters active" : "")
                checked: win.activeFilterCount > 0
                onClicked: { advancedSearch.details = win.songFilters; advancedSearch.open(); }
            }
            IconButton {
                id: compactActionsButton
                objectName: "compactLibraryActionsButton"
                visible: win.shortLayout && !(win.collectionKind === "playlist" && win.collectionName)
                symbol: "more"
                label: "Library actions"
                onClicked: compactActions.popup()
                Menu {
                    id: compactActions
                    font: Theme.controlFont
                    MenuItem { text: "Add music…"; onTriggered: fileDialog.open() }
                    MenuItem { text: "Add a folder…"; onTriggered: folderDialog.open() }
                    MenuItem { text: "Manage albums…"; onTriggered: albumEditor.start(win.collectionKind === "album" ? win.collectionId : "") }
                    MenuItem { text: "New playlist…"; onTriggered: { playlistDialog.track = null; playlistDialog.open(); } }
                    MenuItem { text: "Import playlist…"; onTriggered: importPlaylistDialog.open() }
                }
            }
            IconButton {
                id: collectionPlaylistMore
                objectName: "collectionPlaylistOptions"
                visible: win.collectionKind === "playlist" && !!win.collectionName
                symbol: "more"
                label: "Rename, export, or delete playlist"
                onClicked: win.openPlaylistMenu({id: win.collectionId, name: win.collectionName}, collectionPlaylistMore)
            }
            IconButton {
                objectName: "displaySettingsButton"
                symbol: "settings"
                    label: "Settings" + (Theme.preferences.shortcuts.settings ? " (" + Theme.preferences.shortcuts.settings + ")" : "")
                onClicked: win.openDisplaySettings()
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
                leftMargin: Theme.gap(34)
                rightMargin: Theme.gap(34)
                bottomMargin: Theme.gap(16)
            }
            clip: true
            contentWidth: width
            contentHeight: interactive ? contentColumn.implicitHeight : height
            interactive: win.overviewSummary || (win.activePage === "Smart playlists" && !win.collectionName)
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds
            maximumFlickVelocity: 12000
            flickDeceleration: 1000
            pixelAligned: false
            onContentYChanged: if (win.viewPreferencesLoaded && !win.restoringNavigation) viewSaveTimer.restart()
            LibraryScrollHandler {
                view: contentScroll
            }
            ScrollBar.vertical: ScrollBar {
                policy: contentScroll.interactive ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
            }
            ColumnLayout {
                id: contentColumn
                width: contentScroll.width
                spacing: Theme.gap(win.shortLayout ? 10 : 24)
                RowLayout {
                    id: pageHeading
                    visible: !win.shortLayout
                    Layout.fillWidth: true
                    spacing: Theme.gap(12)
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: Theme.dp(0)
                        spacing: Theme.gap(7)
                        Text {
                            objectName: "libraryScanStatusLabel"
                            visible: library.scanning || library.metadataFailures > 0
                            text: library.scanStatus
                            color: Theme.muted
                            font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(9)
                            font.weight: Font.DemiBold
                            font.letterSpacing: 1.7
                        }
                        RowLayout {
                            spacing: Theme.gap(8)
                            Text {
                                text: win.pageTitle
                                color: win.textColor
                                font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(win.narrowLayout ? 27 : 31)
                                font.weight: Font.DemiBold
                                font.letterSpacing: -0.9
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                    }
                    ActionButton {
                        objectName: "editSmartPlaylistButton"
                        visible: win.collectionKind === "customSmart" && !!win.collectionName
                        text: "Edit rules"; symbol: "edit"; primary: false
                        onClicked: { const entry = library.customSmartPlaylists.find(item => item.id === win.collectionId); if (entry) win.openSmartEditor(entry); }
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

                OverviewPane {
                    objectName: "overviewPane"
                    controller: win
                    visible: win.overviewSummary
                    Layout.fillWidth: true
                }
                Loader {
                    active: win.activePage === "Listening dashboard"
                    visible: active
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(0, contentScroll.height - (pageHeading.visible ? pageHeading.implicitHeight + contentColumn.spacing : 0))
                    sourceComponent: ListeningDashboard {
                    objectName: "listeningDashboard"
                    visible: win.activePage === "Listening dashboard"
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(0, contentScroll.height - (pageHeading.visible ? pageHeading.implicitHeight + contentColumn.spacing : 0))
                    property var frozenStatistics: ({})
                    statistics: win.uiActive && !win.nowPlayingOpen ? library.dashboard : frozenStatistics
                    onStatisticsChanged: if (win.uiActive && !win.nowPlayingOpen) frozenStatistics = statistics
                    onPlayTrack: function(track) { win.playTrack(track, [track]); }
                    onOpenArtist: function(artist) { win.openCollection("Artists", {id: artist.id, name: artist.name, subtitle: ""}); }
                }
                }
                ColumnLayout {
                    visible: win.activePage === "Smart playlists" && !win.collectionName
                    Layout.fillWidth: true
                    spacing: Theme.gap(18)
                    Text { text: "Playlists update when you add music and listen."; color: Theme.muted; wrapMode: Text.WordWrap; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(12); Layout.fillWidth: true }
                    ActionButton { objectName: "newSmartPlaylistButton"; text: "New smart playlist"; symbol: "plus"; primary: false; onClicked: win.openSmartEditor({}) }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: Math.max(1, Math.min(3, Math.floor(width / Math.max(Theme.dp(220), Theme.textPixels * 12))))
                        rowSpacing: Theme.gap(14)
                        columnSpacing: Theme.gap(14)
                        Repeater {
                            model: library.smartPlaylists.concat(library.customSmartPlaylists)
                            delegate: Button {
                                id: smartCard
                                required property var modelData
                                required property int index
                                objectName: modelData.rules !== undefined ? "customSmartCard" + (index - library.smartPlaylists.length) : "smartPlaylist" + index
                                Layout.fillWidth: true
                                Layout.preferredHeight: Math.max(Theme.dp(220), contentItem.implicitHeight + padding * 2)
                                padding: Theme.gap(20)
                                onClicked: modelData.rules !== undefined ? win.openCollection("Smart playlists", {kind: "customSmart", id: modelData.id, name: modelData.name}) : win.openSmart(modelData.id, modelData.name)
                                IconButton {
                                    id: smartMore
                                    anchors.right: parent.right; anchors.top: parent.top; anchors.margins: Theme.gap(8)
                                    symbol: "more"; label: "Smart playlist options"
                                    onClicked: { savedSmartMenu.details = smartCard.modelData; savedSmartMenu.parent = smartMore; savedSmartMenu.popup(); }
                                }
                                background: Rectangle { radius: Theme.dp(12); color: smartCard.hovered ? Theme.hover : Theme.surface; border.color: Theme.border }
                                contentItem: ColumnLayout {
                                    spacing: Theme.gap(14)
                                    Icon { name: smartCard.modelData.icon; color: Theme.accent; Layout.preferredWidth: Theme.dp(32); Layout.preferredHeight: Theme.dp(32) }
                                    Text { text: smartCard.modelData.name; color: Theme.text; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(win.narrowLayout ? 14 : 18); font.weight: Font.DemiBold; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                                    Text { text: smartCard.modelData.description; color: Theme.muted; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(11); Layout.fillWidth: true; wrapMode: Text.WordWrap }
                                    Item { Layout.fillHeight: true }
                                    Text { text: smartCard.modelData.count + " songs  →"; color: Theme.accent; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(12)}
                                }
                            }
                        }
                    }
                }

                FolderBrowserPane {
                    id: folderBrowserPane
                    objectName: "folderBrowserPane"
                    visible: win.activePage === "Folders"
                    Layout.fillWidth: true
                    controller: win
                    details: win.folderData
                    onFolderRequested: function(folder) { win.openFolder(folder); }
                }

                // Full collection pages.
                RowLayout {
                    id: groupSorting
                    objectName: "groupSortingToolbar"
                    visible: win.browsingCollections
                    Layout.fillWidth: true
                    spacing: Theme.gap(8)
                    Text {
                        text: win.visibleGroups.count + (win.activePage === "Genres"
                            ? win.visibleGroups.count === 1 ? " genre" : " genres"
                            : win.activePage === "Artists"
                            ? win.visibleGroups.count === 1 ? " artist" : " artists"
                            : win.visibleGroups.count === 1 ? " album" : " albums")
                        color: Theme.muted
                        font: Theme.controlFont
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                    }
                    Button {
                        id: groupSortButton
                        objectName: "groupSortButton"
                        font: Theme.controlFont
                        implicitWidth: contentItem.implicitWidth + padding * 2
                        implicitHeight: Math.max(Theme.controlHeight(36), contentItem.implicitHeight + padding * 2)
                        padding: Theme.gap(10)
                        hoverEnabled: true
                        onClicked: groupSortMenu.popup()
                        Accessible.name: "Sort by " + win.groupSortLabel
                        background: Rectangle { radius: Theme.dp(6); color: groupSortButton.hovered ? Theme.hover : Theme.surface; border.color: Theme.border }
                        contentItem: RowLayout {
                            spacing: Theme.gap(8)
                            Icon { name: "sort"; color: Theme.secondary; Layout.preferredWidth: Theme.dp(17); Layout.preferredHeight: Theme.dp(17) }
                            Text { text: win.groupSortLabel; color: Theme.text; font: Theme.controlFont }
                            Icon { name: "chevron"; color: Theme.secondary; Layout.preferredWidth: Theme.dp(12); Layout.preferredHeight: Theme.dp(12) }
                        }
                        Menu {
                            id: groupSortMenu
                            objectName: "groupSortMenu"
                            font: Theme.controlFont
                            palette.window: Theme.raised
                            palette.windowText: Theme.text
                            palette.highlight: Theme.accent
                            palette.highlightedText: Theme.onAccent
                            palette.text: Theme.text
                            Repeater {
                                model: win.groupSortOptions
                                delegate: MenuItem {
                                    required property var modelData
                                    objectName: "groupSortOption_" + modelData.key
                                    text: modelData.label
                                    checkable: true
                                    checked: win.groupSortKey === modelData.key
                                    onTriggered: win.setGroupSort(modelData.key)
                                }
                            }
                            MenuSeparator {}
                            MenuItem {
                                objectName: "groupSortAscending"
                                text: "Ascending"
                                checkable: true
                                checked: !win.groupSortDescending
                                onTriggered: win.setGroupSortDescending(false)
                            }
                            MenuItem {
                                objectName: "groupSortDescending"
                                text: "Descending"
                                checkable: true
                                checked: win.groupSortDescending
                                onTriggered: win.setGroupSortDescending(true)
                            }
                        }
                    }
                    IconButton {
                        objectName: "groupSortDirectionButton"
                        symbol: win.groupSortDescending ? "sortDescending" : "sortAscending"
                        label: win.groupSortDescending ? "Descending. Click for ascending." : "Ascending. Click for descending."
                        onClicked: win.setGroupSortDescending(!win.groupSortDescending)
                    }
                    IconButton {
                        objectName: "collectionViewToggle"
                        visible: win.activePage === "Albums" || win.activePage === "Artists"
                        symbol: win.collectionListView ? "grid" : "list"
                        label: win.collectionListView ? "Switch to artwork grid" : "Switch to list view"
                        onClicked: {
                            const index = Math.max(0, groupGrid.indexAt(1, groupGrid.contentY + 1));
                            const mode = win.collectionListView ? "grid" : "list";
                            if (win.activePage === "Artists") win.artistViewStyle = mode; else win.albumViewStyle = mode;
                            Qt.callLater(function() { groupGrid.positionViewAtIndex(index, GridView.Beginning); });
                        }
                    }
                }
                GridView {
                    id: groupGrid
                    objectName: "groupGrid"
                    visible: win.isGroupPage && !win.collectionName && win.visibleGroups.count > 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(0, contentScroll.height - (pageHeading.visible ? pageHeading.implicitHeight + contentColumn.spacing : 0)
                        - (groupSorting.visible ? groupSorting.implicitHeight + contentColumn.spacing : 0))
                    cellWidth: win.collectionListView ? width : Math.floor(width / Math.max(1, Math.floor(width / Theme.dp(220))))
                    cellHeight: win.collectionListView ? Math.max(Theme.dp(84), Theme.textPixels * 4.8) : Math.ceil(cellWidth + Math.max(Theme.dp(64), Theme.textPixels * 4))
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    flickableDirection: Flickable.VerticalFlick
                    maximumFlickVelocity: 12000
                    flickDeceleration: 1000
                    pixelAligned: false
                    onContentYChanged: if (win.viewPreferencesLoaded && !win.restoringNavigation) viewSaveTimer.restart()
                    LibraryScrollHandler {
                        view: groupGrid
                    }
                    reuseItems: true
                    cacheBuffer: cellHeight
                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                    }
                    model: win.restoringNavigation || !visible ? null : win.visibleGroups
                    delegate: Item {
                        id: groupCard
                        property bool pooled: false
                        GridView.onPooled: pooled = true
                        GridView.onReused: pooled = false
                        readonly property var modelData: {
                            const revision = win.visibleGroups.revision;
                            const entry = pooled ? ({}) : win.visibleGroups.get(index);
                            return entry.id ? entry : ({id: "", name: "", subtitle: "", trackCount: 0, artwork: "", artStyle: 0});
                        }
                        required property int index
                        objectName: "groupCard" + index
                        width: groupGrid.cellWidth
                        height: groupGrid.cellHeight
                        Rectangle { anchors.fill: parent; anchors.bottomMargin: 1; visible: win.collectionListView; radius: Theme.dp(7); color: groupMouse.containsMouse ? Theme.hover : Theme.surface }
                        CoverArt {
                            id: groupCover
                            pooled: groupCard.pooled
                            x: win.collectionListView ? Theme.gap(12) : 0
                            y: win.collectionListView ? (parent.height - height) / 2 : 0
                            width: win.collectionListView ? Theme.dp(54) : parent.width - 20
                            height: width
                            artwork: groupCard.modelData.artwork
                            artStyle: groupCard.modelData.artStyle
                            radius: win.activePage === "Artists" ? width / 2 : 8
                        }
                        Text {
                            id: groupTitle
                            objectName: "groupTitle" + groupCard.index
                            x: win.collectionListView ? groupCover.x + groupCover.width + Theme.gap(14) : 0
                            y: win.collectionListView ? Theme.gap(12) : groupCover.height + Theme.gap(10)
                            width: win.collectionListView ? parent.width - x - Theme.dp(65) : groupCover.width
                            text: groupCard.modelData.name
                            color: win.textColor
                            font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(13)
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Text {
                            objectName: "groupDetails" + groupCard.index
                            x: groupTitle.x
                            y: groupTitle.y + groupTitle.implicitHeight + Theme.gap(6)
                            width: groupTitle.width
                            text: (groupCard.modelData.subtitle ? groupCard.modelData.subtitle + " · " : "") + groupCard.modelData.trackCount
                                + (groupCard.modelData.trackCount === 1 ? " song" : " songs")
                                + (win.activePage === "Artists" || win.activePage === "Genres" ? " · " + win.formatCollectionDuration(groupCard.modelData.duration || 0) : "")
                            color: win.secondary
                            font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(11)
                            elide: Text.ElideRight
                        }
                        MouseArea {
                            id: groupMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onClicked: function(mouse) {
                                if (mouse.button === Qt.RightButton) {
                                    if (win.activePage === "Playlists") win.openPlaylistMenu(groupCard.modelData, groupCard);
                                    else win.openGroupMenu(groupCard.modelData, win.groupPageKind, groupCard, mouse);
                                } else win.showGroup(groupCard.modelData);
                            }
                        }
                        IconButton {
                            id: groupPlaylistMore
                            objectName: "playlistOptions" + groupCard.index
                            visible: win.activePage === "Playlists"
                            anchors.right: groupCover.right
                            anchors.top: groupCover.top
                            anchors.margins: Theme.gap(8)
                            width: Theme.dp(32)
                            height: Theme.dp(32)
                            padding: Theme.gap(8)
                            symbol: "more"
                            label: "Playlist options"
                            surfaceColor: Theme.surface
                            onClicked: win.openPlaylistMenu(groupCard.modelData, groupPlaylistMore)
                        }
                        IconButton {
                            anchors {
                                right: groupCover.right
                                bottom: groupCover.bottom
                                margins: Theme.gap(10)
                            }
                            width: Theme.dp(40)
                            height: Theme.dp(40)
                            padding: Theme.gap(11)
                            visible: groupMouse.containsMouse || hovered
                            symbol: "play"
                            label: "Play " + groupCard.modelData.name
                            surfaceColor: win.accent
                            glyphColor: Theme.onAccent
                            onClicked: win.playGroup(groupCard.modelData, win.groupPageKind)
                        }
                    }
                }

                ColumnLayout {
                    id: songsSection
                    Layout.fillWidth: true
                    spacing: Theme.gap(win.shortLayout ? 8 : 15)
                    visible: (win.activePage === "Overview" && !win.overviewSummary) || win.activePage === "Songs" || win.activePage === "Favorites" || win.activePage === "Most Played" || win.collectionName.length > 0 || (win.isGroupPage && win.visibleGroups.count === 0)
                    RowLayout {
                        id: songActions
                        Layout.fillWidth: true
                        Text {
                            text: win.activePage === "Overview" ? "Search results" : ""
                            visible: text.length > 0
                            color: win.textColor
                            font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(18)
                            font.weight: Font.DemiBold
                        }
                        Text {
                            text: library.scanning ? library.scanStatus : win.visibleTracks.count + (win.visibleTracks.count === 1 ? " song" : " songs") + (win.collectionKind === "playlist" && win.sortBy === "Order" && !win.searchQuery && !win.narrowLayout ? " · Drag the handles to reorder" : "")
                            elide: Text.ElideRight
                            Layout.minimumWidth: 0
                            color: Theme.muted
                            font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(11)
                            Layout.fillWidth: true
                        }
                        ActionButton {
                            objectName: "playAllButton"
                            text: "Play all"
                            symbol: "play"
                            implicitHeight: Math.max(Theme.controlHeight(33), contentItem.implicitHeight + Theme.gap(12))
                            enabled: win.visibleTracks.count > 0
                            onClicked: win.playAll(false)
                        }
                        ActionButton {
                            objectName: "editAlbumMetadataButton"
                            visible: win.collectionKind === "album" && !!win.collectionName
                            text: win.narrowLayout ? "Edit tags" : "Edit all metadata"
                            symbol: "edit"
                            primary: false
                            enabled: !library.metadataBusy && win.visibleTracks.count > 0
                            onClicked: batchMetadataEditor.begin(library.tracksForGroup("album", win.collectionId))
                        }
                        IconButton {
                            symbol: "shuffle"
                            label: "Shuffle all"
                            enabled: win.visibleTracks.count > 0
                            onClicked: win.playAll(true)
                        }
                        IconButton { objectName: "songColumnsButton"; symbol: "columns"; label: "Choose, resize and reorder song columns"; onClicked: win.openSongColumnSettings() }
                        IconButton {
                            objectName: "pinCollectionButton"
                            visible: !!win.collectionName
                            symbol: "pin"
                            checked: win.isPinned(win.collectionKind, win.collectionId)
                            label: checked ? "Unpin collection" : "Pin collection to sidebar"
                            onClicked: win.togglePin(win.collectionKind, win.collectionId, win.collectionName)
                        }
                        Button {
                            id: sortButton
                            enabled: win.activePage !== "Most Played"
                            implicitHeight: Theme.dp(32)
                            implicitWidth: Theme.dp(84)
                            hoverEnabled: true
                            onClicked: sortMenu.popup()
                            background: Rectangle {
                                radius: Theme.dp(6)
                                color: sortButton.hovered ? Theme.hover : "transparent"
                            }
                            contentItem: RowLayout {
                                spacing: Theme.gap(7)
                                Icon {
                                    name: "sort"
                                    color: Theme.muted
                                    Layout.preferredWidth: Theme.dp(16)
                                    Layout.preferredHeight: Theme.dp(16)
                                }
                                Text {
                                    text: win.sortBy
                                    color: Theme.secondary
                                    font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(11)
                                }
                                Icon {
                                    name: "chevron"
                                    color: Theme.muted
                                    Layout.preferredWidth: Theme.dp(12)
                                    Layout.preferredHeight: Theme.dp(12)
                                }
                            }
                            Menu {
                                id: sortMenu
                                palette.window: Theme.raised
                                palette.windowText: Theme.text
                                palette.highlight: Theme.accent
                                palette.highlightedText: Theme.onAccent
                                palette.text: Theme.text
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
                    Item {
                        id: songColumns
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.gap(15)
                        Layout.rightMargin: Theme.gap(18)
                        implicitHeight: songHeader.implicitHeight
                        clip: true
                        RowLayout {
                        id: songHeader
                        x: -songList.contentX
                        width: win.songTableWidth - Theme.gap(33)
                        spacing: Theme.gap(12)
                        CheckBox {
                            objectName: "selectAllSongs"
                            Layout.preferredWidth: Theme.dp(24)
                            Layout.preferredHeight: Theme.dp(30)
                            padding: 0
                            tristate: true
                            checkState: win.selectedVisibleCount === 0 ? Qt.Unchecked : win.selectedVisibleCount === win.visibleTracks.count ? Qt.Checked : Qt.PartiallyChecked
                            nextCheckState: function() { return win.selectedVisibleCount === win.visibleTracks.count ? Qt.Unchecked : Qt.Checked; }
                            enabled: win.visibleTracks.count > 0
                            Accessible.name: "Select all visible songs"
                            onClicked: win.selectVisibleSongs()
                            ToolTip.visible: hovered
                            ToolTip.text: "Select all visible songs"
                        }
                        Text {
                            text: "#"
                            color: Theme.muted
                            font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(10)
                            Layout.preferredWidth: Theme.dp(24)
                        }
                        Text {
                            text: "TITLE"
                            color: Theme.muted
                            font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(9)
                            font.letterSpacing: 1.3
                            Layout.fillWidth: true
                        }
                        Repeater {
                            model: win.visibleSongColumns
                            delegate: Item {
                                id: headerColumn
                                required property var modelData
                                Layout.preferredWidth: win.songColumnWidth(modelData)
                                Layout.preferredHeight: Theme.controlHeight(30)
                                Text {
                                    anchors.fill: parent; anchors.rightMargin: Theme.gap(8)
                                    verticalAlignment: Text.AlignVCenter
                                    text: headerColumn.modelData.key === "duration" ? "TIME" : headerColumn.modelData.key.toUpperCase()
                                    color: Theme.muted; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(9)
                                    elide: Text.ElideRight
                                }
                                Rectangle { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; width: 1; height: parent.height * 0.6; color: resizeHandle.containsMouse || resizeHandle.pressed ? Theme.accent : Theme.border }
                                MouseArea {
                                    id: resizeHandle
                                    objectName: "columnResize_" + headerColumn.modelData.key
                                    anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
                                    width: Theme.dp(10); hoverEnabled: true; cursorShape: Qt.SplitHCursor
                                    property real startX: 0
                                    property real startWidth: 0
                                    onPressed: function(mouse) { startX = mapToItem(win.contentItem, mouse.x, mouse.y).x; startWidth = headerColumn.modelData.width; win.pendingColumnWidth = startWidth; win.resizingColumn = headerColumn.modelData.key; }
                                    onPositionChanged: function(mouse) { if (pressed) win.pendingColumnWidth = Math.max(50, Math.min(500, startWidth + (mapToItem(win.contentItem, mouse.x, mouse.y).x - startX) / Theme.dp(1))); }
                                    onReleased: win.commitColumnWidth(headerColumn.modelData.key, win.pendingColumnWidth)
                                    onCanceled: win.resizingColumn = ""
                                }
                            }
                        }
                        Text {
                            text: "PLAYS"
                            visible: win.activePage === "Most Played"
                            color: Theme.muted
                            font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(9)
                            font.letterSpacing: 1.3
                            Layout.preferredWidth: Theme.dp(84)
                        }
                        Item {
                            Layout.preferredWidth: Theme.dp(30)
                        }
                        Item {
                            Layout.preferredWidth: Theme.dp(30)
                        }
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.dp(1)
                        color: Theme.border
                    }
                    ListView {
                        id: songList
                        objectName: "songList"
                        Layout.fillWidth: true
                        Layout.preferredHeight: win.visibleTracks.count === 0 ? 0 : Math.max(0, contentScroll.height - (pageHeading.visible ? pageHeading.implicitHeight + contentColumn.spacing : 0) - (folderBrowserPane.visible ? folderBrowserPane.implicitHeight + contentColumn.spacing : 0) - songActions.implicitHeight - songColumns.implicitHeight - 1 - songsSection.spacing * 3)
                        interactive: songsSection.visible
                        flickableDirection: Flickable.AutoFlickIfNeeded
                        contentWidth: win.songTableWidth
                        maximumFlickVelocity: 12000
                        flickDeceleration: 1000
                        pixelAligned: false
                        onContentYChanged: if (win.viewPreferencesLoaded && !win.restoringNavigation) viewSaveTimer.restart()
                        LibraryScrollHandler {
                            view: songList
                        }
                        reuseItems: true
                        clip: true
                        model: win.restoringNavigation || !songsSection.visible ? null : win.visibleTracks
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {
                            policy: songList.interactive ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                        }
                        ScrollBar.horizontal: ScrollBar { policy: songList.contentWidth > songList.width ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
                        delegate: Rectangle {
                            id: songRow
                            readonly property var track: {
                                const revision = win.visibleTracks.revision;
                                const entry = win.visibleTracks.get(index);
                                return entry.fileUrl ? entry : ({fileUrl: "", title: "", artist: "", album: "", artwork: "", artStyle: 0,
                                    favorite: false, playCount: 0, duration: 0, trackNumber: -1, discNumber: -1});
                            }
                            required property int index
                            objectName: "songRow" + index
                            width: songList.contentWidth
                            height: Theme.rowHeight(65)
                            radius: Theme.dp(6)
                            readonly property bool current: win.currentUrl === track.fileUrl
                            readonly property bool reorderable: win.collectionKind === "playlist" && win.sortBy === "Order" && !win.searchQuery
                            color: playlistDrop.containsDrag ? Theme.selected : win.selectedUrls.indexOf(track.fileUrl) >= 0 ? Theme.selected : current ? Theme.selected : rowMouse.containsMouse ? Theme.hover : "transparent"
                            DropArea {
                                id: playlistDrop
                                objectName: "playlistDrop" + songRow.index
                                anchors.fill: parent
                                enabled: songRow.reorderable
                                keys: ["playlist:" + win.collectionId]
                                onDropped: function(drop) {
                                    const after = drop.y >= height / 2;
                                    const next = after && songRow.index + 1 < win.visibleTracks.count ? win.visibleTracks.get(songRow.index + 1).fileUrl : "";
                                    win.movePlaylistSong(drop.source.entryId, after ? next : songRow.track.fileUrl);
                                    drop.acceptProposedAction();
                                }
                            }
                            Rectangle { visible: playlistDrop.containsDrag; width: parent.width; height: Theme.dp(2); y: playlistDrop.drag.y < parent.height / 2 ? 0 : parent.height - 2; color: win.accent }
                            MouseArea {
                                id: rowMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                onClicked: function (mouse) {
                                    if (mouse.button === Qt.RightButton)
                                        win.openTrackMenu(songRow.track, rowMoreButton);
                                    else if (mouse.modifiers & Qt.ControlModifier)
                                        win.toggleSelection(songRow.track.fileUrl);
                                    else
                                        win.playTrack(songRow.track, win.visibleTracks.entries());
                                }
                            }
                            RowLayout {
                                anchors {
                                    fill: parent
                                    leftMargin: Theme.gap(15)
                                    rightMargin: Theme.gap(18)
                                }
                                spacing: Theme.gap(12)
                                CheckBox {
                                    objectName: "selectSong" + songRow.index
                                    Layout.preferredWidth: Theme.dp(24)
                                    Layout.preferredHeight: Theme.dp(30)
                                    padding: 0
                                    checked: win.selectedUrls.indexOf(songRow.track.fileUrl) >= 0
                                    Accessible.name: "Select " + songRow.track.title
                                    onClicked: win.toggleSelection(songRow.track.fileUrl)
                                }
                                ReorderHandle {
                                    objectName: "playlistDrag" + songRow.index
                                    visible: songRow.reorderable
                                    Layout.preferredWidth: Theme.dp(24)
                                    Layout.preferredHeight: Theme.dp(30)
                                    entryId: songRow.track.fileUrl
                                    dragGroup: "playlist:" + win.collectionId
                                    delegateItem: songRow
                                    title: songRow.track.title
                                    view: songList
                                    onStep: function(direction) {
                                        const target = direction < 0 ? songRow.index - 1 : songRow.index + 2;
                                        if (target < 0) return;
                                        win.movePlaylistSong(entryId, target >= win.visibleTracks.count ? "" : win.visibleTracks.get(target).fileUrl);
                                    }
                                }
                                Item {
                                    visible: !songRow.reorderable
                                    Layout.preferredWidth: Theme.dp(24)
                                    Layout.preferredHeight: Theme.dp(22)
                                    Text {
                                        anchors.centerIn: parent
                                        text: win.collectionName && win.collectionKind === "album" && songRow.track.trackNumber >= 0 ? (songRow.track.discNumber > 1 ? songRow.track.discNumber + "." : "") + songRow.track.trackNumber : songRow.index + 1
                                        color: Theme.muted
                                        font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(11)
                                        visible: !songRow.current && !rowMouse.containsMouse
                                    }
                                    Icon {
                                        anchors.centerIn: parent
                                        name: songRow.current && win.isPlaying ? "pause" : "play"
                                        color: songRow.current ? win.accent : Theme.text
                                        width: Theme.dp(15)
                                        height: Theme.dp(15)
                                        visible: songRow.current || rowMouse.containsMouse
                                    }
                                }
                                CoverArt {
                                    Layout.preferredWidth: Theme.dp(40)
                                    Layout.preferredHeight: Theme.dp(40)
                                    radius: Theme.dp(5)
                                    artwork: songRow.track.artwork
                                    artStyle: songRow.track.artStyle
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: win.songTitleMinimumWidth
                                    spacing: Theme.gap(5)
                                    Text {
                                        objectName: "songTitleLabel" + songRow.index
                                        text: songRow.track.title
                                        color: songRow.current ? win.accent : Theme.text
                                        font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(12)
                                        font.weight: Font.Medium
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    ArtistLinks {
                                        objectName: "songArtistLabel" + songRow.index
                                        track: songRow.track
                                        onArtistChosen: function(credit) { win.openArtistCredit(credit); }
                                        Layout.fillWidth: true
                                    }
                                }
                                Repeater {
                                    model: win.visibleSongColumns
                                    delegate: Text {
                                        required property var modelData
                                        objectName: "songColumn_" + modelData.key + "_" + songRow.index
                                        text: win.songColumnValue(songRow.track, modelData.key)
                                        color: Theme.muted; font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(10)
                                        Layout.preferredWidth: win.songColumnWidth(modelData)
                                        horizontalAlignment: modelData.key === "duration" ? Text.AlignRight : Text.AlignLeft
                                        elide: Text.ElideRight
                                    }
                                }
                                Text {
                                    objectName: "songPlayCountLabel" + songRow.index
                                    text: songRow.track.playCount
                                    visible: win.activePage === "Most Played"
                                    color: Theme.muted
                                    font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(9)
                                    Layout.preferredWidth: Theme.dp(84)
                                }
                                IconButton {
                                    objectName: "favorite" + songRow.index
                                    symbol: "heart"
                                    label: songRow.track.favorite ? "Remove from favorites" : "Add to favorites"
                                    glyphColor: songRow.track.favorite ? "#ef4444" : Theme.muted
                                    filled: songRow.track.favorite
                                    Layout.preferredWidth: Theme.dp(30)
                                    Layout.preferredHeight: Theme.dp(30)
                                    padding: Theme.gap(8)
                                    onClicked: library.toggleFavorite(songRow.track.fileUrl)
                                }
                                IconButton {
                                    id: rowMoreButton
                                    objectName: "songOptions" + songRow.index
                                    symbol: "more"
                                    label: "Song options"
                                    Layout.preferredWidth: Theme.dp(30)
                                    Layout.preferredHeight: Theme.dp(30)
                                    padding: Theme.gap(7)
                                    onClicked: win.openTrackMenu(songRow.track, rowMoreButton)
                                }
                            }
                        }
                    }
                    ColumnLayout {
                        visible: win.visibleTracks.count === 0
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.gap(12)
                        Layout.bottomMargin: Theme.gap(23)
                        spacing: Theme.gap(9)
                        Icon {
                            name: win.searchQuery ? "search" : win.activePage === "Favorites" ? "heart" : "music"
                            color: Theme.muted
                            Layout.preferredWidth: Theme.dp(29)
                            Layout.preferredHeight: Theme.dp(29)
                            Layout.alignment: Qt.AlignHCenter
                        }
                        Text {
                            text: library.scanning ? library.scanStatus : win.searchQuery ? "No matching music" : win.activePage === "Most Played" ? "No played songs yet" : win.activePage === "Playlists" ? (win.collectionName ? "This playlist is empty" : "No playlists") : win.activePage === "Favorites" ? "No favorites" : win.activePage === "Genres" && !win.collectionName ? "No genres" : "No songs"
                            color: Theme.secondary
                            font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(13)
                            font.weight: Font.Medium
                            Layout.alignment: Qt.AlignHCenter
                        }
                        Text {
                            text: win.searchQuery ? "Try another song, artist, album, or genre." : win.activePage === "Most Played" ? "Play at least 50% of a song to add a listen." : win.activePage === "Playlists" ? "Use a song's three-dot menu to add it to a playlist." : win.activePage === "Favorites" ? "Tap the heart beside a song to keep it here." : "Add audio files or a music folder."
                            color: Theme.muted
                            font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(11)
                            Layout.alignment: Qt.AlignHCenter
                        }
                    }
                }

            }
        }
    }

    Loader {
        id: nowPlayingLoader
        objectName: "nowPlayingLoader"
        z: 2
        active: win.nowPlayingOpen
        visible: active
        anchors {
            left: win.fullscreenNowPlaying ? parent.left : sidebar.right
            right: parent.right
            top: parent.top
            bottom: win.fullscreenNowPlaying ? parent.bottom : playerBar.top
        }
        sourceComponent: NowPlayingPane {
        id: nowPlayingPane
        objectName: "nowPlayingPane"
        anchors.fill: parent
        visible: win.nowPlayingOpen
        fullscreen: win.fullscreenNowPlaying
        queueOpen: win.queueOpen
        onShareSongRequested: if (win.currentTrack) sharing.prepareSong(win.currentTrack)
        onShareScreenshotRequested: win.shareNowPlayingScreenshot()
        onFullscreenRequested: win.toggleFullscreenNowPlaying()
        track: win.currentTrack
        playing: win.isPlaying
        shuffled: win.shuffleEnabled
        repeatMode: win.repeatMode
        position: win.uiActive ? player.position : 0
        duration: player.duration
        volume: library.volume
        muted: win.muted
        seekable: player.seekable
        onCloseRequested: win.goBack()
        onArtistRequested: function(credit) { win.openArtistCredit(credit); }
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
        onLyricsRequested: win.openLyrics(win.currentTrack)
        onBookmarksRequested: win.openTrackBookmarks(win.currentTrack)
        onMenuRequested: function (anchor) {
            win.openTrackMenu(win.currentTrack, anchor);
        }
    }
    }

    Rectangle {
        id: selectionBar
        objectName: "selectionBar"
        visible: win.selectedCount > 0 && !win.nowPlayingOpen
        enabled: !win.nowPlayingOpen
        z: 2
        anchors { left: sidebar.right; right: parent.right; bottom: playerBar.top }
        height: selectionActions.implicitHeight + Theme.gap(16)
        color: Theme.selected
        border.color: Theme.border
        RowLayout {
            id: selectionActions
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: Theme.gap(18) }
            spacing: Theme.gap(8)
            IconButton { objectName: "clearSongSelection"; symbol: "close"; label: "Clear selection"; onClicked: win.selectedUrls = [] }
            Text { text: win.selectedCount + " selected"; color: Theme.text; font: Theme.controlFont; Layout.fillWidth: true; elide: Text.ElideRight }
            IconButton { objectName: "playSelectedSongs"; symbol: "play"; label: "Play selected songs"; visible: !win.narrowLayout; onClicked: win.handleBatchAction("play", win.selectedSongs()) }
            IconButton { objectName: "queueSelectedSongs"; symbol: "queue"; label: "Add selected songs to queue"; onClicked: win.handleBatchAction("queue", win.selectedSongs()) }
            ActionButton {
                objectName: "editSelectedMetadataButton"; text: win.narrowLayout ? "Edit tags" : "Edit metadata"; symbol: "edit"; primary: false
                enabled: !library.metadataBusy
                onClicked: win.handleBatchAction("metadata", win.selectedSongs())
            }
            IconButton {
                objectName: "selectedSongsOptions"; symbol: "more"; label: "Selected song actions"
                onClicked: selectedActions.popup()
                Menu {
                    id: selectedActions
                    font: Theme.controlFont
                    palette.window: Theme.raised; palette.windowText: Theme.text; palette.text: Theme.text; palette.highlight: Theme.accent
                    MenuItem { text: "Play selected"; onTriggered: win.handleBatchAction("play", win.selectedSongs()) }
                    MenuItem { text: "Play next"; onTriggered: win.handleBatchAction("next", win.selectedSongs()) }
                    MenuItem { text: "Add to queue"; onTriggered: win.handleBatchAction("queue", win.selectedSongs()) }
                    MenuItem { text: "Add to playlist…"; onTriggered: win.handleBatchAction("playlist", win.selectedSongs()) }
                    MenuItem { text: "Add to favorites"; onTriggered: win.handleBatchAction("favorite", win.selectedSongs()) }
                    MenuItem { text: "Remove from favorites"; onTriggered: win.handleBatchAction("unfavorite", win.selectedSongs()) }
                    MenuItem { text: "Remove from this playlist"; visible: win.collectionKind === "playlist"; height: visible ? implicitHeight : 0; onTriggered: win.handleBatchAction("removePlaylist", win.selectedSongs()) }
                    MenuSeparator {}
                    MenuItem { objectName: "deleteSelectedSongs"; text: "Delete selected songs…"; onTriggered: win.handleBatchAction("trash", win.selectedSongs()) }
                }
            }
        }
    }
    PlayerBar {
        id: playerBar
        objectName: "playerBar"
        visible: !win.fullscreenNowPlaying
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        controller: win
        onTransitionsRequested: playbackOptions.visible ? playbackOptions.close() : playbackOptions.open()
        onSleepRequested: sleepPopup.visible ? sleepPopup.close() : sleepPopup.open()
    }

    // Keep the view model alive across edits. Replacing a JavaScript array as
    // the ListView model resets its delegates, focus, and browsing position.
    ListModel {
        id: queueEntries
        objectName: "queueEntries"
        function synchronize(entries) {
            if (win.queueSearch.trim()) {
                const matching = new Set(library.queueSearchUrls(win.queueSearch, entries.map(entry => entry.fileUrl)));
                entries = entries.filter(entry => matching.has(entry.fileUrl));
            }
            const wanted = new Set(entries.map(entry => entry.id));
            for (let last = count - 1; last >= 0;) {
                if (wanted.has(get(last).entryId)) { --last; continue; }
                let first = last;
                while (first > 0 && !wanted.has(get(first - 1).entryId)) --first;
                remove(first, last - first + 1);
                last = first - 1;
            }
            for (let row = 0; row < entries.length; ++row) {
                const entry = entries[row];
                if (row >= count || get(row).entryId !== entry.id) {
                    let existing = row + 1;
                    while (existing < count && get(existing).entryId !== entry.id) ++existing;
                    if (existing < count) move(existing, row, 1);
                    else insert(row, {entryId: entry.id, fileUrl: entry.fileUrl});
                }
                if (get(row).fileUrl !== entry.fileUrl) setProperty(row, "fileUrl", entry.fileUrl);
            }
        }
        function indexOfEntry(id) {
            for (let i = 0; i < count; ++i) if (get(i).entryId === id) return i;
            return -1;
        }
    }
    Popup {
        id: queuePopup
        objectName: "queuePopup"
        modal: true
        dim: false
        focus: true
        readonly property real headerBottom: win.queueOpen && win.nowPlayingOpen && nowPlayingLoader.item
            ? nowPlayingLoader.item.queueButtonItem.mapToItem(win.contentItem, 0, nowPlayingLoader.item.queueButtonItem.height).y : 0
        x: win.width - width - Theme.gap(12)
        y: headerBottom ? headerBottom + Theme.gap(10) : win.height - win.playerBarExtent - height - Theme.gap(12)
        width: Math.min(Theme.dp(480), win.width - Theme.gap(24))
        height: Math.max(0, Math.min(Theme.dp(520), win.height - win.playerBarExtent - (headerBottom ? headerBottom : 0) - Theme.gap(24)))
        visible: win.queueOpen
        onOpened: { queueSearchField.clear(); scrollToCurrentSong(); }
        onClosed: win.queueOpen = false
        function scrollToCurrentSong() {
            if (win.currentQueueIndex < 0) return;
            if (win.queueSearch) { queueSearchField.clear(); Qt.callLater(scrollToCurrentSong); return; }
            Theme.preferences.settleLayout(queueList);
            queueList.cancelFlick();
            queueList.forceLayout();
            queueList.positionViewAtIndex(queueEntries.indexOfEntry(win.currentQueueId), ListView.Center);
        }
        padding: Theme.gap(win.shortLayout ? 12 : 18)
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            color: Theme.surface
            radius: Theme.dp(12)
            border.color: Theme.border
        }
        contentItem: ColumnLayout {
            spacing: Theme.gap(win.shortLayout ? 6 : 12)
            RowLayout {
                Layout.fillWidth: true
                ComboBox {
                    objectName: "activeQueueBox"
                    Layout.fillWidth: true
                    model: library.savedQueues
                    textRole: "name"
                    currentIndex: {
                        const entries = library.savedQueues;
                        return Math.max(0, entries.findIndex(entry => entry.id === library.activeQueueId));
                    }
                    onActivated: function(index) { win.activateSavedQueue(library.savedQueues[index].id); }
                    Accessible.name: "Active play queue"
                }
                ActionButton {
                    objectName: "queueCurrentSongButton"
                    Layout.preferredWidth: win.shortLayout ? Theme.dp(52) : implicitWidth
                    Layout.maximumWidth: win.shortLayout ? Theme.dp(52) : Number.POSITIVE_INFINITY
                    text: win.shortLayout ? "" : "Current song"
                    symbol: "played"
                    primary: false
                    leftPadding: Theme.gap(10)
                    rightPadding: Theme.gap(10)
                    implicitHeight: Math.max(Theme.controlHeight(32), contentItem.implicitHeight + Theme.gap(10))
                    enabled: win.currentQueueIndex >= 0
                    ToolTip.visible: hovered
                    ToolTip.text: "Go to currently playing song"
                    Accessible.name: "Go to currently playing song"
                    onClicked: queuePopup.scrollToCurrentSong()
                }
                IconButton {
                    objectName: "manageQueuesButton"
                    symbol: "queue"; label: "Manage queues · up to four"
                    onClicked: { queueOpen = false; queueManager.begin(); }
                }
                IconButton {
                    objectName: "queueUndoButton"; symbol: "undo"; label: "Undo " + win.undoActionLabel
                    enabled: win.canUndoAction; onClicked: win.undoLastAction()
                }
                IconButton {
                    objectName: "saveQueuePlaylistButton"
                    symbol: "save"; label: "Save queue as new playlist"
                    enabled: win.queueCount > 0
                    onClicked: win.saveQueueAsPlaylist()
                }
                IconButton {
                    symbol: "close"
                    label: "Close queue"
                    Layout.preferredWidth: Theme.dp(28)
                    Layout.preferredHeight: Theme.dp(28)
                    onClicked: win.queueOpen = false
                }
            }
            Text {
                visible: win.queueUrls.length === 0
                text: "Play a song to start your queue."
                color: win.secondary
                font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(12)
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.fillWidth: true
                TextField {
                    id: queueSearchField
                    objectName: "queueSearchField"
                    Layout.fillWidth: true; Layout.minimumWidth: 0
                    font: Theme.controlFont; selectByMouse: true
                    placeholderText: "Search this queue"
                    maximumLength: 512
                    onTextChanged: win.queueSearch = text
                    Accessible.name: "Search queued songs by title, artist or album"
                }
                IconButton { symbol: "close"; label: "Clear queue search"; visible: win.queueSearch.length > 0; onClicked: queueSearchField.clear() }
            }
            Label {
                visible: win.queueSearch.length > 0
                text: queueEntries.count + " matches · " + win.queueCount + " queued"
                color: Theme.muted; font: Theme.controlFont; Layout.fillWidth: true
            }
            ListView {
                id: queueList
                objectName: "queueList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                currentIndex: -1
                highlightFollowsCurrentItem: false
                LibraryScrollHandler { view: queueList }
                model: queueEntries
                delegate: ItemDelegate {
                    id: queuedTrack
                    required property string entryId
                    required property string fileUrl
                    required property int index
                    objectName: "queueRow" + index
                    readonly property var track: win.trackForUrl(fileUrl)
                    width: ListView.view.width
                    height: Theme.rowHeight(54)
                    onClicked: win.playQueueItem(entryId)
                    DropArea {
                        id: queueDrop
                        enabled: !win.queueSearch.trim()
                        anchors.fill: parent
                        keys: ["queue"]
                        onDropped: function(drop) {
                            const after = drop.y >= height / 2;
                            const next = after && queuedTrack.index + 1 < win.queueItems.length ? win.queueItems[queuedTrack.index + 1].id : "";
                            win.moveQueueItem(drop.source.entryId, after ? next : queuedTrack.entryId);
                            drop.acceptProposedAction();
                        }
                    }
                    Rectangle { visible: queueDrop.containsDrag; width: parent.width; height: Theme.dp(2); y: queueDrop.drag.y < parent.height / 2 ? 0 : parent.height - 2; color: win.accent; z: 2 }
                    background: Rectangle {
                        radius: Theme.dp(5)
                        color: queueDrop.containsDrag || queuedTrack.entryId === win.currentQueueId ? Theme.selected : queuedTrack.hovered ? Theme.hover : "transparent"
                    }
                    contentItem: RowLayout {
                        ReorderHandle {
                            objectName: "queueDrag" + queuedTrack.index
                            visible: !win.queueSearch.trim()
                            Layout.preferredWidth: Theme.dp(20)
                            Layout.preferredHeight: Theme.dp(30)
                            entryId: queuedTrack.entryId
                            dragGroup: "queue"
                            delegateItem: queuedTrack
                            title: queuedTrack.track ? queuedTrack.track.title : "Song"
                            view: queueList
                            onStep: function(direction) {
                                const target = direction < 0 ? queuedTrack.index - 1 : queuedTrack.index + 2;
                                if (target < 0) return;
                                win.moveQueueItem(entryId, target >= win.queueItems.length ? "" : win.queueItems[target].id);
                            }
                        }
                        CoverArt {
                            Layout.preferredWidth: Theme.dp(34)
                            Layout.preferredHeight: Theme.dp(34)
                            artwork: queuedTrack.track ? queuedTrack.track.artwork : ""
                            artStyle: queuedTrack.track ? queuedTrack.track.artStyle : 0
                            radius: Theme.dp(4)
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.gap(4)
                            Text {
                                text: queuedTrack.track ? queuedTrack.track.title : "Unavailable track"
                                color: queuedTrack.entryId === win.currentQueueId ? win.accent : win.textColor
                                font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(11)
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Text {
                                text: queuedTrack.track ? queuedTrack.track.artist : ""
                                color: win.secondary
                                font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(10)
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                        IconButton {
                            id: queuedMore
                            symbol: "more"
                            label: "Song options"
                            Layout.preferredWidth: Theme.dp(27)
                            Layout.preferredHeight: Theme.dp(27)
                            padding: Theme.gap(6)
                            onClicked: win.openTrackMenu(queuedTrack.track, queuedMore, queuedTrack.entryId)
                        }
                        IconButton {
                            objectName: "removeQueue" + queuedTrack.index
                            symbol: "close"
                            label: "Remove from queue"
                            Layout.preferredWidth: Theme.dp(27)
                            Layout.preferredHeight: Theme.dp(27)
                            padding: Theme.gap(7)
                            onClicked: win.removeQueueItem(queuedTrack.entryId)
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
            bottom: selectionBar.visible ? selectionBar.top : playerBar.top
            bottomMargin: Theme.gap(18)
        }
        color: Theme.raised
        border.color: Theme.border
        radius: Theme.dp(9)
        Text {
            id: toastText
            anchors {
                left: parent.left
                right: closeToast.left
                verticalCenter: parent.verticalCenter
                leftMargin: Theme.gap(16)
                rightMargin: Theme.gap(10)
            }
            text: win.toastMessage
            color: Theme.text
            font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(12)
            wrapMode: Text.WordWrap
        }
        IconButton {
            id: closeToast
            anchors {
                right: parent.right
                verticalCenter: parent.verticalCenter
                rightMargin: Theme.gap(5)
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
        objectName: "externalMusicDropArea"
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: function (drop) {
            if (drop.hasUrls) {
                win.openExternalFiles(drop.urls, !!(drop.modifiers & Qt.ShiftModifier));
                drop.acceptProposedAction();
            }
        }
        Rectangle {
            anchors {
                fill: parent
                margins: Theme.gap(10)
            }
            visible: parent.containsDrag
            color: Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, 0.92)
            radius: Theme.dp(12)
            border {
                color: win.accent
                width: Theme.dp(2)
            }
            Column {
                anchors.centerIn: parent
                spacing: Theme.gap(14)
                Icon {
                    name: "music"
                    width: Theme.dp(44)
                    height: Theme.dp(44)
                    color: win.accent
                    anchors.horizontalCenter: parent.horizontalCenter
                }
                Text {
                    text: "Drop to play"
                    color: win.textColor
                    font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(24)
                    font.weight: Font.DemiBold
                }
                Text {
                    text: "Audio files or folders · Hold Shift to add to queue"
                    color: Theme.accent
                    font.family: Theme.preferences.fontFamily; font.pointSize: Theme.textSize(12)
                    anchors.horizontalCenter: parent.horizontalCenter
                }
            }
        }
    }
}
