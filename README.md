# Musica

A native Qt 6 music player for local audio, created by **weo sikho fuzile**. Version **5.2.0** adds queue/playlist undo, advanced search filters, configurable song columns, album/artist list views, remembered page preferences, ReplayGain playback, queue search, and an optional shuffle preference for songs not heard recently.

Version **5.1.1** uses the supplied feather-M logo, replaces Overview's limited song preview with a library summary, and fixes scrolling through Overview search results. Artist navigation responds only to the displayed name, including separate shared credits and the Song info dialog.

Version **5.1.0** adds Windows build and packaging support, output-device selection for system equalizers, mono/stereo controls, and a customizable **5–15 band equalizer**. Settings has a section picker and a direct audio shortcut in the player bar. The unreliable mini player has been removed; playback uses the main window and fullscreen Now Playing.

Musica also includes folder browsing, pinned collections, custom smart playlists, offline lyrics, moved-file repair, backup/restore, duplicate review, track bookmarks, drag-and-drop/Open With, a paged library with manual scanning, album/artist/genre sorting, four named queues, portable metadata editing, sharing, Linux desktop controls, gapless playback, crossfades, and sleep timers.

Version **4.10.1** displays song durations and playback positions of one hour or more as **h:mm:ss**, such as `1:05:09`, in song lists, the bottom player bar, Now Playing, and Song info. Shorter times retain **m:ss** formatting. Long duration labels have room to display the hours.

Version **4.7.1** fixes search selection bookkeeping: matching selections are
counted from identifiers without copying every song’s metadata into JavaScript.
Result counts are published after filtering completes, and selecting all visible
songs preserves selections hidden by the search. Regression checks exercise
both model backends and repeated searches with 1,200 selected songs during playback.

## Use the player

Run the compiled app from the project directory:

```sh
./build/musicplayer
```

In VS Code, you can also use **Terminal → Run Task → Musica: Run player** after building.

The window title shows the running version. Close older player windows before launching the rebuilt executable so that you are checking the current fixes.

Musica opens your last saved library immediately without scanning folders or reading song tags. Click **Scan library** (the circular arrow beside search) when you want to check your configured folders for added, removed, or changed files; your system Music folder is included by default. **Add music** and **Add a folder** explicitly request a library update. Dropping or opening files plays the selection, reusing saved entries or importing only the requested new locations. Imported locations, favorites, metadata, and volume are saved between launches.

- **Overview:** song/album/artist/genre totals, resume listening, Favorites/Most Played/Recently added shortcuts, and a small album shelf. The summary scrolls as needed. Searching switches to full, paged song results that support mouse, touchpad, and touch scrolling; clear search to return to the summary.
- **Songs:** search and sort the library by title, artist, or album.
- **Albums / Artists:** open a collection to browse and play its songs. Right-click a card for **Play all**, **Shuffle**, **Play next**, **Add to queue**, **Add to playlist**, favorites, batch metadata editing, folder access, or deletion. Album pages default to disc and track order, and their menus also offer album merge/split.
- **Genres:** browse saved genre tags, song counts, and total duration. Open a genre to play its songs, or right-click its card for queue, playlist, favorites, and batch-edit actions.
- **Folders:** browse the saved directory hierarchy with Up/Back, child folders, and songs from the chosen folder and its descendants.
- **Favorites:** click a song's heart to keep it close. Saved hearts fill red everywhere, including the bottom bar and Now Playing.
- **Most Played:** an automatic playlist ranked by qualified listens. A song qualifies after at least 50% has played during one play.
- **Playlists:** create named playlists, drag songs into your preferred order, rename/delete playlists, and import/export M3U playlists.
- **Smart playlists:** Recently added, Never played, and Rarely played update automatically.
- **Listening dashboard:** recent sessions, weekly favorites, top artists, listening time, and a seven-day activity chart.
- **Now Playing:** click the bottom-bar album cover for large artwork, song details, and playback controls. Click the artist or album name to open that collection. Background clicks and scrolling stay within Now Playing, keeping covered album/playlist tracks from interrupting playback.
- **Playback:** play/pause, previous/next, seek, shuffle, repeat off/all/one, queue, mute, volume, and gapless transitions.
- **Crossfade:** the crossing-lines button in the bottom bar opens a saved on/off switch and 0–70 second duration.
- **Audio settings:** the headphones button opens output-device and mono/stereo controls. Choose **Equalizer** in Settings for custom bands and presets.
- **Sleep timer:** the moon button stops after a chosen number of minutes or at the end of the current song.
- **Metadata & covers:** **⋯ → Edit metadata & cover…** edits the audio file itself, so saved tags and embedded artwork accompany the song when copied elsewhere.
- **Sharing:** **⋯ → Share song…** copies the audio file for pasting or saves a copy. Linux desktops with the email portal can also open an email composer with an attachment. The Share button in Now Playing captures its artwork, details, and controls as a PNG.
- **Multiple queues:** starting an album, artist, playlist, or search creates a queue named for that source. Keep up to four; new playback replaces the oldest when full. Use the queue's Save button to make a playlist, or **Manage queues** to switch, rename, or remove queues.

## Practical library tools

**Undo:** use the curved arrow beside search, the Undo button in the queue header, or **Ctrl+Z** to undo the latest queue or playlist edit. Up to 20 actions remain available during the app session: queue insertion/removal/reordering, playlist creation/deletion/rename, playlist membership changes, and playlist reordering. Text fields keep their normal text-editing Undo. An existing custom Ctrl+Z binding takes priority until you assign another key in Settings. Undo in an inactive queue restores that queue without changing current playback. Undo in the active queue keeps the playing entry when it still exists; otherwise it restores the saved selection and position paused. Starting a new source in an emptied queue retires that queue's previous undo entries. File deletion and metadata writes are outside this history.

**Advanced search:** use the filter button beside search. Combine title, artist, album, or genre text with an inclusive release-year range, duration in minutes, and favorites/non-favorites. All entered rules must match, along with the ordinary search box and current collection. Blank fields impose no restriction. Invalid ranges keep the dialog open without applying changes. **Reset** clears the filters. Filtering uses saved metadata and the paged database; it never opens music files or starts a scan. Overview shows full search results whenever filters are active, even with an empty search box.

**Song columns:** use the columns button above the song list, or **Settings → Display and layout → Song columns…**. Show/hide and reorder Album, Duration, Year, and Format, and set widths from **50–500 logical pixels**. Drag a header's right edge to resize directly. Title/artist, selection, favorites, and song actions remain available. Wide tables scroll horizontally using the scrollbar, touchpad sideways motion, or **Shift + mouse wheel**; column headers stay aligned. The default layout hides Album in narrow windows; an explicitly customized layout keeps your selected columns available by horizontal scrolling.

**Album/artist views:** the list/grid button beside collection sorting switches between artwork grids and compact rows. Albums and Artists remember their styles independently. List rows retain artwork thumbnails, playback, right-click menus, song counts, and the existing sorting options.

**Remembered browsing:** Musica saves the last page, per-page searches/filters/song sorting, collection sorting, list/grid choices, and vertical/horizontal scroll offsets. Returning to a page or reopening the app restores those preferences; window size can clamp an offset to fit the current content. Up to 100 visited page/collection states are retained. Playback still restores paused, and browsing restoration does not scan folders or read audio tags.

**Search the queue:** type in **Search this queue** to match queued titles, artists, albums, and genres. Matches retain their original entry identifiers, including repeated songs. Playing or removing a result targets that exact queue entry. Search leaves the full playback order intact. Reordering is available after clearing search; **Current song** clears the filter and reveals the playing entry.

**Shuffle preference:** enable **Settings → Playback → Prefer songs not played recently when shuffling**, and choose **1–90 local calendar days**. Songs with at least ten seconds of recorded listening in that period, or a qualified play, are shuffled after other songs. Every queued entry remains present; an explicitly selected song still starts first. This uses saved listening history without audio analysis.

**Folder browser:** open **Folders** in the sidebar. Choose a saved music location and browse child folders with **Up** and **Back**. Each folder shows its saved song count; use its Play button or open it to search, sort, select, and play its songs. The list includes descendants. Browsing reads the saved database, while **Add music** or **Scan library** discovers external changes.

**Pinned collections:** right-click an album, artist, or genre card and choose **Pin to sidebar**, or use the pin icon inside a collection. Playlist and smart-playlist menus also offer pinning; folder rows have a pin button. Pins appear above the main navigation and reopen the collection directly. Use their **×** or the collection's pin control to unpin. Names follow saved collection edits. Up to 64 pins are supported.

**Custom smart playlists:** open **Smart playlists → New smart playlist**. Combine an exact genre, an inclusive year range, favorites/non-favorites, and minimum/maximum play counts. All selected rules must match. Blank rules include all songs; a maximum of zero plays selects never-played songs. Rules use existing genre and DATE/year tags and listening counts, without analysis. Open a custom card to play matching songs, and use its **⋯ → Edit rules** to rename, edit, or delete it. Built-in Recently added/Never played/Rarely played remain available. Up to 64 custom lists are supported.

**Offline lyrics:** use **Lyrics** in Now Playing, or a song's **⋯ → Offline lyrics…**. Musica reads embedded lyrics or a same-basename `.lrc`/`.txt` beside the song. **Open lyrics file** attaches a local file to that song; **Use song lyrics** removes the attachment. LRC timestamps and offsets drive highlighting and scrolling; choose a line to play/seek to its timestamp. Manual scrolling pauses following until **Follow playback** is pressed. Lyrics are loaded on request, use no network services, and do not rewrite audio files.

**Track bookmarks:** use **Bookmarks** in Now Playing or **⋯ → Track bookmarks…** on any song. Save the current position or enter `m:ss`/`h:mm:ss` and a name. Choose a bookmark to play from there; edit or remove individual bookmarks with their row controls. Bookmarks remain in Musica settings and follow moved-file repair.

**Equalizer:** open **Settings → Equalizer**, enable it, and choose **Flat**, **Bass**, **Vocal**, **Headphones**, or **Custom**. EQ starts disabled. Select **5–15 bands** and adjust each gain from **−12 to +12 dB**. Frequency centers span 31.25 Hz–16 kHz; scroll horizontally to reach additional bands in narrow windows. Editing a gain selects Custom. Changing the number of bands interpolates the custom curve; choosing a preset keeps that curve available. **Reset curve** flattens the custom curve. Count, gains, preset, and enabled state persist and are included in backups. Filter changes and bypass use short transitions without restarting playback, with headroom and output bounds to limit clipping.

## Audio output and external equalizers

Open **Settings → Audio output**, or click the headphones button in the bottom player bar. **Output device** offers the system default and the audio devices reported by Qt. A selected device is remembered. If it disconnects, Musica uses the system default until the chosen device returns. Device changes preserve the current track and position. **Channel mode** switches between **Stereo** and **Mono**; mono mixes the left and right channels into both outputs. Changing channel mode or built-in EQ does not reopen the audio device.

Musica uses normal shared system audio output, allowing configured system effects and virtual devices to process its stream. This is output routing rather than hosting external effect plugins inside Musica.

- **Linux / EasyEffects:** leave the hardware device as your desktop's default, as required by the [EasyEffects setup guidance](https://github.com/wwmm/easyeffects). Enable processing for Musica in EasyEffects, or select its virtual output specifically for Musica using the output-device picker. Avoid making EasyEffects' virtual devices the global system default.
- **Windows / Equalizer APO:** configure the APO for the device selected in Musica, following the [Equalizer APO documentation](https://sourceforge.net/p/equalizerapo/wiki/Documentation/). Musica does not request ASIO or WASAPI exclusive output; [Equalizer APO requires a path that keeps Windows system effects active](https://sourceforge.net/projects/equalizerapo/). Other Windows system equalizers or virtual outputs can be selected when Windows exposes them as an audio device.

External effects must be installed and enabled in their own software. Turn off Musica's built-in EQ when you want only the external EQ's curve. Physical EasyEffects/APO setups have not been exercised on this development machine.

**ReplayGain:** in **Settings → Audio output**, choose **Off**, **Track**, or **Album**. Musica reads the standard `REPLAYGAIN_TRACK_GAIN/PEAK` and `REPLAYGAIN_ALBUM_GAIN/PEAK` tags for the current and upcoming songs only, on a background worker. Album mode falls back to track gain when album gain is absent. Untagged/invalid gains leave the song unchanged. Peak tags constrain boosts to avoid clipping; remaining processed samples are bounded. Each crossfade voice uses its own tags, and changes ramp without restarting playback. Mode preferences persist and are included in backups. This implements playback of [existing ReplayGain metadata](https://wiki.hydrogenaudio.org/index.php?title=Revised_ReplayGain_specification); it neither computes loudness values nor modifies the files, and adds no startup scan.

## Repair, backups, and duplicate files

Open these tools from the bottom of **Settings → Library tools**.

- **Repair moved files:** enter the old folder path and choose the new folder. **Preview repair** compares relative file paths and saved file sizes, lists matches, and reports unmatched files. **Repair matching songs** relinks the validated preview without moving or editing audio. Favorites, playlists, all four queue sessions, play counts, history, bookmarks, and folder pins follow the new paths. Existing destination records and stale previews are rejected. This matches paths/sizes, rather than audio fingerprints.
- **Backup and restore:** **Create backup…** saves a `.musica-backup` JSON bundle containing saved library metadata, app/display/playback settings, queues, playlists, pins, custom rules, bookmarks, favorites, listening history, and lyric attachment links. **Restore backup…** validates and previews the bundle before confirmation, then restores it with playback paused. Failed database writes roll back the current library. Audio files, cover-cache files, and attached lyric files are not copied into the bundle; keep those files separately and repair paths when restoring on another machine. Backups are limited to 256 MB and 250,000 song records.
- **Duplicate finder:** **Find duplicates** runs only when requested. It groups byte-identical files using SHA-256 after filtering by file size. Review each group, select the file to keep, then confirm moving the other copies to the system Trash. Musica rechecks their content before deletion; changed/unreadable files stay in place. Different encodings or different embedded tags are not treated as identical copies. Hashing runs off the interface thread and is cancelled when the application exits.

## Drag-and-drop and Open With

Drop local audio files or folders onto the player to open and play them in incoming order, removing repeated file selections. Hold **Shift** while dropping to add them to the current queue without starting or interrupting playback. **Ctrl+O** opens an **Open and play music** file picker; **Add music** remains available for importing without starting a new queue.

Installed desktop integration advertises supported audio MIME types so file managers can offer **Open With → Musica**. File arguments may be local paths or encoded `file:` URLs, including spaces, Unicode, `%`, and `#`. A second launch forwards its entire selection to the existing player and raises that window. Requests arriving during startup wait until the interface is ready. Already saved songs open immediately; new files are imported in the background without walking unrelated library folders. Explicitly opening a folder imports that folder. No network streams or automatic startup scans are introduced.

## Sort albums and artists

On **Albums**, use the sort menu above the cards to choose **Recently added**, **Artist name**, or **Title**. Recently added uses the newest library-addition date among the album's songs; adding another song to an existing album moves that album forward. It starts newest first, while artist names and titles start A–Z.

On **Artists**, choose **Artist name**, **Number of songs**, or **Hours**. Song count starts largest first. Hours starts longest first and measures the combined duration of that artist's songs in the library, rather than listening history. Cards show song counts and total hours/minutes. Shared credits follow the artist-separation preference, counting each song once per credited artist.

The arrow beside the menu reverses the order. **Ascending** and **Descending** are also available inside the menu. Each page retains its chosen sort while browsing, searching, and returning from a collection. Song order inside an album or playlist remains independent of the card sort.

Existing saved libraries gain the required totals from their stored metadata on upgrade. Sorting uses the paged database and does not start a library scan or audio analysis.

## Genres

Open **Genres** in the sidebar. Choose **Genre name**, **Number of songs**, **Hours**, or **Recently added** above the cards. Name starts A–Z; song count and hours start largest first; recently added starts newest first. Use the arrow or the menu's **Ascending / Descending** options to reverse the order. Recently added measures the newest song added to each genre, and Hours measures the combined song duration.

Each card shows its song count and duration. Genre names match without case or extra-space differences, so `Jazz`, `jazz`, and ` Jazz ` share one group. Punctuation is preserved: a tag such as `R&B/Soul` remains one genre. Empty genre tags appear under **Unknown genre**. Existing nonempty labels, including older cached `Other` labels, are retained.

Click a card to browse its songs. **Play all** creates a queue named after that genre, and the Back button returns to the genre list with its sort retained. Right-click a card for **Play all**, **Shuffle**, **Play next**, **Add to queue**, **Add to playlist**, favorites, **Edit all metadata & cover…**, folder access, or deletion with confirmation. Genre edits use the existing manual, offline metadata editor and update genre membership immediately. If every song moves to a new genre while editing the open collection, the page follows the edited genre.

Search finds genre cards by name and also matches genre tags when searching songs. Existing libraries gain genre groups from the saved database without opening audio files or scanning folders. Normal startup and browsing keep the library paged; only an explicit import or scan discovers external tag changes.

## Lightweight operation and keyboard settings

The VS Code project now configures **Release** builds for everyday listening.
The appearance and audio features are retained. The display continues to use
the desktop font and scaling; readable text is preserved.

- **Artwork:** song rows, the bottom bar, and queue reuse thumbnails at matching
  physical sizes. The shared decoded-image cache defaults to **32 MiB**, and the
  thumbnail disk budget defaults to **128 MiB**. Now Playing requests a larger
  image. Thumbnail decoding and disk cleanup run off the interface thread.
- **Screens:** metadata editors, album management, playlist/song dialogs,
  Settings, sharing, the queue manager, the listening dashboard, and Now Playing
  are created when opened. Closed panels release their controls and held data.
- **Hidden windows:** minimized-window progress displays stop following frequent
  position changes. Dashboard statistics update only while its screen is loaded
  and visible; playback, listening history, sleep timing, and desktop controls
  remain active.
- **Scrolling:** album/artist cards reuse delegates, pooled cards release their
  artwork, and displayed entries refresh from their current indexes.

In **Settings**, enable **Reduce decorative motion** to simplify hover and color
fades. Mouse and touchpad scrolling keep their smooth behavior.

**Artwork cache and storage** shows thumbnail disk usage and decoded image
cache usage. Set an **8–256 MiB** memory budget and a **32–2048 MiB** thumbnail
storage budget. **Refresh usage** updates the figures; **Clear thumbnails**
removes disposable thumbnails, which rebuild when needed. **Clean abandoned
audio buffers** removes buffers older than one day whose owning Linux process
is no longer running. Music files, embedded covers, saved library entries,
and buffers belonging to running players are kept. The artwork budget covers
the shared image cache; the interface and other app data also use memory.

Under **Keyboard shortcuts**, select a command and press its replacement key
combination. Changes apply immediately and survive restarting. Conflicts are
rejected with an explanation. A letter or number needs Ctrl, Alt, or Meta so
typing stays available. You can unassign a command or **Restore default
shortcuts**. Escape, Tab, and Backspace retain their navigation behavior.

## Paged library and migration

The saved library now lives in a `*.library.sqlite` database beside the app's
settings. The existing saved library and tag cache migrate automatically,
without rescanning music. Favorites, playlists, album grouping, track ordering,
listening counts, and artist-separation preferences are retained.

Search and sorting run against the database. Views keep identifiers and cache
up to **three pages of 64 full entries each**, loading track details as you
browse. The total song count and full scroll range remain available, including
jumping to the bottom of a large library. Playing a whole collection explicitly
loads the entries needed to form that queue.

Scanning and edits that regroup the library temporarily load a working copy;
it is released after the operation becomes idle. Playing, favoriting, and
qualifying a listen can update individual stored songs without loading the
whole library. No background BPM/key analysis or automatic library scan is
introduced. Keep the database, settings, and queue-state file together when
backing up the player after closing it.

## Settings and artist credits

Open **Settings** with the sliders button or **Ctrl+,**. Choose **Teal dark** or **Teal light**. **Adapt accent colors to album artwork** is optional and samples only the current cover in the background; it does not scan songs. Missing or unusable artwork falls back to teal. Turn the option off to restore teal at any time.

Under **Playback · keyboard seeking**, choose a **5–30 second** step for Left/Right. Ctrl+Left/Right still changes tracks. Typing fields and other sliders keep their normal arrow-key behavior.

**Library · artist credits → Show individual artists in shared credits** starts disabled. Enable it and choose the separators to recognize: comma **`,`**, semicolon **`;`**, and vertical bar **`|`**. For example, `Artist A, Artist B; Artist C | Artist D` appears under all four artists. A song is counted once on each artist’s page, and albums retain their grouping and disc/track order. Repeated names in the same credit are deduplicated. Ampersands and “feat.” are left intact. Disable comma splitting for names such as `AC, DC`.

Click an individual artist name in a song row, the bottom bar, or Now Playing to open that person’s page. The song menu’s **Go to artist** offers a chooser for shared credits. Artist dashboard entries follow the same setting; a shared song contributes to each credited artist while overall listening time is counted once. Preference changes apply to saved tags immediately, without scanning or rewriting audio files.

**About Musica** is at the bottom of Settings, with the creator credit, project link, and license information. The project repository is [red-outrunner/Musiqa](https://github.com/red-outrunner/Musiqa).

## Share songs and Now Playing

Song sharing uses the original local audio file, retaining its embedded metadata and artwork. **Copy audio file** puts a file reference on the desktop clipboard; paste it into an app that accepts attachments or a file manager. **Save a copy…** writes an identical copy to a destination you select. **Email as attachment…** opens the Linux desktop portal’s email composer, where you choose recipients and send. It requires a compatible desktop portal and email application; Copy and Save remain available without them. Musica does not upload or send automatically.

In Now Playing, use **Share → Now Playing screenshot…**, or the song menu’s matching action, to capture just the Now Playing panel. It excludes the desktop, library behind it, and the bottom bar. Preview, copy the image for pasting, save a PNG, or attach it to an email. Temporary captures are removed when Musica exits; save a copy if you want to retain one.

Playback starts from the current view. For example, **Play all** on Favorites plays your favorites, and selecting a song inside an album queues that album. Shuffle visits each queued song before repeating. Previous restarts the current song after three seconds; otherwise it selects the previous song.

Most Played includes only songs with a positive qualified play count, ordered from highest to lowest; equal counts are ordered by song title. A qualified listen updates its row and ranking immediately, without waiting for a metadata batch, library scan, or periodic refresh. Song names and displayed counts follow the current ranked indexes during sorting and scrolling.

Most Played records one listen per play after half the song has actually played. Pauses and skipped sections do not count, and replaying the same passage within one play does not increase coverage. Starting the song again can earn another listen. Play counts and playlist contents are saved between launches. Seeking near the end alone will not add the song to Most Played.

Opening the queue from the bottom bar or Now Playing scrolls to the exact currently playing entry, including when a song appears more than once. Click **Current song** in the queue header to return to it after browsing. While the queue stays open, removals, reordering, and playback advances leave your browsing position alone; removing the last entry may move the view only enough to fit the shortened list.

The queue's **×** removes that one queue entry. Duplicate entries can be removed independently, and removing an upcoming song leaves the current song playing. Removing the current entry selects another available song; an empty queue stops playback. Drag the dotted handle to reorder entries without changing the current song or its position. Both the active queue and the original order used when disabling shuffle are saved.

## Back navigation and fullscreen Now Playing

A **Back** arrow stays in the top bar on every library page, including short windows. It returns through the pages and album, artist, playlist, and smart-playlist collections you visited, restoring their search, sorting, selection, and scroll position. **Alt+Left** also goes back; **Backspace** does the same while you are not editing text. The arrow is disabled at the start of the history. Music continues while you browse.

Now Playing uses the same Back arrow to return to the library. Following its artist or album links creates one navigation step, so Back returns to Now Playing. Navigation history belongs to the current app session.

Press **F11** from the library or Now Playing to open fullscreen Now Playing, or click the expand icon in its header. On keyboards using media keys by default, use **Fn+F11**. Fullscreen fills the window with the artwork and Now Playing controls, with the sidebar and bottom player bar hidden. Playback, seek, favorites, artist/album links, and the queue remain available there.

Press **F11** again, the fullscreen button, or **Escape** to leave fullscreen while keeping Now Playing open. Its Back arrow returns to the library. The previous normal or maximized window mode is restored. If the queue is open, Escape closes it first. Closing the app from fullscreen still quits and saves your listening position.

Queue, playback-transition, and sleep-timer buttons work as toggles: click once to open, then again to close. Clicking outside dismisses the panel.

## Display settings and readable text

Open **Settings** beside search and refresh, or press **Ctrl+,**. Musica uses the font family and point size reported by your desktop through Qt. Text updates when the system font changes, and Qt handles monitor scaling, including high-DPI displays. Choose **Automatic**, **Compact**, or **Comfortable** layout density. Automatic adjusts spacing to the available window; Compact uses less space, while Comfortable gives rows and controls more room. Density does not reduce the chosen text size.

Set **Text size** from **100–200%** for additional reading comfort. At 100%, the desktop font is the reference size; headings and supporting labels retain their hierarchy. Changes apply immediately to the library, dialogs, and Now Playing, and are saved between launches. **Use system defaults** restores Automatic density and 100% text size. These small preferences are stored in `display.ini` beside the application's normal settings.

The fixed section picker at the top of Settings jumps directly to Appearance, Playback, Audio output, Equalizer, Artist credits, Display and layout, Artwork cache, Keyboard shortcuts, Library tools, or About. Settings fits within the window and keeps Close reachable while its contents scroll.

Narrow windows use a sidebar of icons with tooltips and stack playback controls into rows. Short windows show the page title in the top bar and provide **Library actions** through its three-dot button. Playlist actions remain beside the top-bar controls. Album grids adjust their column count, large text expands song rows, and settings and Now Playing details can scroll when needed. Normal windows are kept within the current monitor's available work area after resolution or monitor changes.

## Edit portable metadata and cover art offline

Open a song's **⋯ → Edit metadata & cover…** from the song list, queue, or Now Playing. Edit title, artist, album, album artist/collective, genre, release date/year, composer, track/disc numbers and totals, and comments. The editor reads the tags stored in the file. Only fields you change are saved.

Use **Choose image…** to select a JPEG or PNG from your computer. Its original image data and resolution are embedded as the front cover; the image is not linked to an external path. Images can be up to 20 MB and 40 megapixels. **Remove artwork** removes embedded artwork, while **Keep original artwork** cancels a pending artwork change. Text-only edits retain existing embedded covers. A nearby folder image can still appear as a library fallback when a song has no embedded cover.

Click **Save to file** to commit the changes. Copy the edited audio file to another machine and its tags and embedded cover travel with it; no Musica settings folder or image file needs to be copied. Other players may need to refresh their library or artwork cache. Editing and artwork selection are entirely manual and offline, with no online metadata or image lookup.

Musica stages edits in a temporary copy beside the song, verifies the saved tags and cover, checks the audio properties and original revision, and atomically replaces the original. The audio is not re-encoded. Read-only files, unsupported tag values, invalid cover images, stale edits, and failed writes report an error while keeping the original. **Cancel** discards the draft. Tags, grouping, track order, artwork, and search results refresh after saving, while the song URL, playlists, favorites, and listening history remain associated with the file. Editing album tags resets that song's library-only album grouping override.

Editable containers include **MP3, FLAC, M4A/MP4, Ogg Vorbis, Opus, WAV, AIFF, and WMA**. The writer uses native ID3, Vorbis/FLAC, MP4, and ASF tag storage, and synchronizes existing legacy WAV/AIFF/WMA text metadata. Individual players differ in the fields and artwork formats they display, particularly WAV/AIFF. Raw `.aac` files do not have a supported portable tagging container; the editor reports this rather than silently storing changes only in Musica. Unedited tags already stored in the audio file are preserved.

## Select songs and edit a batch

Check the box at the far left of a song row to select it without interrupting playback. The header checkbox selects or clears all visible songs; a partial check indicates that only some are selected. Ctrl-clicking a row also toggles its selection. Navigation clears the selection; searching preserves selected songs, including any hidden by the search.

A selection bar appears above the player controls with **Add to queue**, **Edit metadata**, and a **⋯** menu for playing the selection, **Play next**, adding to playlists or favorites, removing favorites, removing songs from the current playlist, and deleting songs. Batch queue actions preserve the displayed song order. Album/artist menu actions use disc/track order. Deletion first confirms the songs and moves only their audio files to the system Trash; failures leave those files and library entries in place.

Inside an album, **Edit all metadata** beside **Play all / Shuffle** opens the batch editor for the whole album, even when a search filters the displayed songs. For a subset, select songs and use **Edit metadata** on the selection bar.

The batch editor shows shared tag values and **Multiple values** for differing tags. Check each field you want to apply to every selected song, then enter its value. Unchecked fields retain each song's own tags; a checked blank field clears that tag. Leave title and track number unchecked to preserve individual song names and album order. Artwork defaults to **Keep each song's artwork**; choose a local image to replace the selected songs' embedded covers, or explicitly remove their covers.

**Save to files** writes and verifies each song individually using the same portable native tag writer as the single-song editor. Tags, covers, album grouping, and the saved library update directly, without starting another library scan. Progress and per-file failures are shown. Successful files remain saved when another file fails; reload the remaining songs to review fresh values before retrying. Cancel leaves every file unchanged.

## Gapless playback and crossfade

Click the **crossing-lines button** beside the queue in the bottom player bar to open **Playback transitions**. Gapless playback is enabled by default; crossfade is optional and starts disabled. Enable crossfade and set its duration with the slider or editable seconds box from **0 to 70 seconds**. A duration of **0** plays without an overlap. These preferences survive restarting.

Audio decoding, mixing, output feeding, and sleep deadlines run on a dedicated audio thread. Five-second session checkpoints, library settings writes, and busy interface work cannot block the audio feed. The output has a 200 ms scheduling buffer; seeking flushes buffered audio, and pause/volume controls act directly on the audio thread. Position and listening events are passed back to the interface with their original playback timestamps. Saving a session also collects pending track transitions and listening events so it records the correct song and position.

Playback uses one continuous audio output and predecodes the upcoming queue entry. With crossfade off, the next song begins at the previous song's final decoded sample. With crossfade on, the outgoing and incoming songs overlap with equal-power fades and peak limiting. The song display and queue selection switch when the incoming song starts. Both songs' actual playback contributes to listening history during the overlap. Short songs reduce the fade to fit the available audio. Changes to fade settings apply to upcoming transitions.

The mixer removes gaps introduced by opening a new output stream; silence recorded into the audio itself remains. An unreadable or insufficiently buffered next song can still delay a transition. PCM is buffered in temporary files and removed when released, limiting RAM use to the working buffers. Long tracks use additional temporary disk space.

## Sleep timer and window controls

Click the **moon button** to set a sleep duration of **1–720 minutes**, choose **At the end of this song**, or cancel the timer. The remaining time is shown in the timer panel. A timed stop pauses at the current position for later resumption. End-of-song mode finishes the current song and suppresses crossfading, repeat, and automatic advance into another song. Sleep timers do not restart when the application reopens.

The window's **×** button, **Ctrl+Q**, and the Linux desktop **Quit** command exit Musica, stop audio, and save the current listening position. Opening another audio file or using desktop **Raise** restores the main window, preserving its maximized or fullscreen mode.

## Resume listening

Reopening restores your queue, the exact selected entry (including duplicate songs), playback position, shuffle, repeat, mute, and volume. Playback waits for you to press Play. Missing files are skipped. Session changes are saved after a short delay, playback position is checkpointed every five seconds, and closing the window saves the latest position. The queue is restored from the saved library without a startup scan.

Starting playback from an album, artist, playlist, Favorites, Most Played, smart playlist, or the song library creates a **new named queue**. Searches use **Search: your query**; collections use their own names. The initial empty queue, or an empty queue you opened manually, is filled first. There are **four queue slots**: when they are full, starting another source replaces the oldest queue at the top of the list. Switching or renaming a queue does not change its age. Next/previous, selecting an entry in the playing queue, **Play next**, and **Add to queue** keep the active queue.

The queue panel’s selector switches between saved queues. Each preserves its own songs, duplicates, order, selected entry, position, shuffle, and repeat. Switching saves the outgoing queue, restores the incoming one, and continues playing if playback was running. **Play all** and **Shuffle** save the outgoing queue before setting up the new one. Open **Manage queues** to create an empty queue when a slot is available, rename a queue, delete one with confirmation, or remove every queue except the active one. At least one queue is kept. Deleting or replacing a queue keeps its music files.

Use the **Save queue as new playlist** icon in the queue header, or **Manage queues → Save playing queue as playlist**, to save the whole queue, including songs already played, in its displayed order. The dialog starts with the queue's name and lets you choose another playlist name. Playlists contain each song once, so repeated queue entries are saved at their first appearance; the dialog indicates when this applies. Saving uses the queue as it was when the dialog opened and keeps playback and the active queue unchanged.

Queue state and listening history are stored atomically in a small `*.queues.json` file beside the library settings. JSON encoding and writes run in the background; five-second checkpoints no longer synchronize the large library cache on the interface thread. The old single queue and play counts migrate automatically. Keep this state file with your settings when making a local backup; it is excluded from Git.

## Manage playlists

Use **Playlists → New playlist** or a song's **⋯ → Add to playlist**. Each playlist card and open playlist has a **⋯** menu with **Rename**, **Export M3U playlist**, and **Delete playlist**. Deleting a playlist asks for confirmation and keeps its music files.

Playlist pages start in **Order** sorting. Drag the dotted handles to arrange songs, or focus a handle and use Up/Down. The order is saved independently for each playlist. Clear search and choose **Order** to enable reordering after searching or choosing a different sort.

Use **Import M3U** on the Playlists page to load `.m3u` or `.m3u8` files. UTF-8 names, relative paths, absolute paths, and local `file:` URLs are supported. Readable local songs outside your library are imported automatically; missing files, streams, and unsupported entries are skipped with a message. Repeated paths are deduplicated. Exports use UTF-8 extended M3U with song descriptions and relative paths, retaining the playlist's saved order. Exporting a playlist does not copy its audio files.

## Correct album grouping

Open **Albums → Manage albums**. Select **Merge albums**, check the albums to combine, and enter the album name and album artist. For a split, choose **Split an album** and either select songs for a new album, group by track artist or folder, or make an album for each song. Opening Manage albums inside an album preselects that album for splitting.

Click **Preview changes** to review the resulting albums and their disc/track ordering, then **Apply changes**. **Edit selection** returns to the choices. **Undo last edit** restores the previous grouping; up to 20 edits remain available after restarting. Changes are saved for the selected files and survive rescanning. Each song retains its performer, and audio files and tags are untouched. A preview is rejected if the library's album membership changes before applying it.

## Smart playlists and listening dashboard

- **Recently added:** songs first discovered in the last 30 days, newest first. Existing songs receive their first discovery date when first scanned by this version.
- **Never played:** songs with no qualified plays yet.
- **Rarely played:** songs with 1–4 qualified plays, lowest counts first.

The dashboard shows listening over the last seven local calendar days, weekly favorites ranked by qualified plays, artists ranked by total listening time, and recent listening sessions. Clicking a song plays it; clicking an artist opens their page. Listening time includes audio heard after the 50% threshold and repeated passages, while pauses and seek jumps add no time. Qualification still requires distinct coverage of half the song.

Dated history starts with listening in this version; earlier play counts are preserved without inventing past dates or durations. History stays locally in your settings. Musica retains the latest 500 listening sessions, 90 days of daily detail, and cumulative listening-time totals.

## Linux desktop controls

Musica implements the [MPRIS interface](https://specifications.freedesktop.org/mpris/latest/), so compatible Linux desktops and media-key utilities can control play/pause, previous/next, seeking, volume, shuffle, and repeat, and display song information and artwork. The service is `org.mpris.MediaPlayer2.Musica` at `/org/mpris/MediaPlayer2`. Normal desktop launches reuse the running player through a local per-user socket. Integration is available automatically when a session D-Bus is running. The CMake install includes the desktop launcher and icon.

Each song's **⋯** menu offers **Play now**, **Play next**, **Add to queue**, **Add to playlist**, favorites, artist/album navigation, **Edit metadata & cover…**, **Song info**, **Show in folder**, and **Delete song**. The same menu is available in Now Playing and in the queue. Deleting a song asks for confirmation and moves its file to the system Trash, also removing it from the library, queue, favorites, and saved playlists. If the system cannot move it to Trash, Musica reports the error and keeps the file.

You can also import paths at launch:

```sh
./build/musicplayer "/path/to/music" "/path/to/song.flac"
```

## Build in VS Code

Prerequisites: a C++17 compiler, CMake 3.21 or newer, and Qt 6.3 or newer with Core, Gui, Qml, Quick, QuickControls2, Multimedia, Concurrent, Sql, Network, and Test development components. Linux MPRIS additionally uses Qt DBus; Windows builds do not link DBus and explicitly require Qt SVG. The runtime also needs the SQLite Qt SQL driver, Qt Quick Controls, Qt Quick Dialogs, Qt Multimedia QML modules, and Qt SVG image plugin. The `ffmpeg` command-line executable is used when available for fast playback predecoding; otherwise Musica uses Qt's audio decoder, which may buffer more slowly during early seeks. Linux MPRIS integration tests use `dbus-daemon` to create an isolated session bus.

Metadata editing additionally requires **TagLib 2 or newer** and its development headers (`taglib-devel` on Fedora, `libtag2-dev` on distributions providing that package). CMake accepts TagLib's package configuration, or pkg-config on Linux. An unpacked Linux development package is also accepted at `build/taglib-sdk/usr/include/taglib`, linked against the installed TagLib runtime. Metadata portability tests use `ffmpeg` and `ffprobe` as independent audio/tag readers.

For native Windows `.exe` builds, matching MSVC/Qt/TagLib SDKs, DLL deployment, ZIP packaging, and Open With registration, follow [Windows builds](./WINDOWS_BUILD.md). A Windows CI workflow is prepared. No Windows executable has been built or validated on this Linux development machine, which has no Windows Qt/TagLib SDK.

Open this folder in VS Code with CMake Tools installed, select a Qt 6 kit, then run **CMake: Configure** and **CMake: Build**. The application target is `musicplayer`, and the default build also includes `musica_tests`. Build output goes to the configured CMake build directory (normally `build/`).

[.vscode/settings.json](./.vscode/settings.json) sets `CMAKE_BUILD_TYPE` to
`Release`. Set that configuration value to `Debug` for debugging, then configure
and build again with CMake Tools. [CMakePresets.json](./CMakePresets.json) also
provides Release and Debug presets for preset-based workflows; these require
CMake 3.21 or newer. An unpacked TagLib SDK in the original build directory can
be used by either configuration.

## Checks

Version 5.2 adds **musica_usability** for combined filters with both model backends, cached metadata queries, playlist undo, duplicate queue matching, inactive-queue restoration, saved page/column preferences, shortcut compatibility, and audible-history shuffle selection. Audio checks read actual ReplayGain tags without modifying the file, verify sample gains, album fallback, peak bounds, ramps and independent crossfade gains, and reject invalid backup modes atomically. UI checks exercise filter validation, queue search/removal/undo, column reordering/resizing, Shift-wheel scrolling, list views, and restoration after reopening with large text. **musica_ui_regressions** focuses on old-database migration, small/high-DPI layouts, popup toggles/fullscreen, and queue browsing stability.

Version 5 adds focused suites for cached folder/custom-rule queries, pins/bookmarks, repair/backup rollback, exact duplicate validation, LRC/embedded lyrics, real EQ sample responses, single-instance batch forwarding, and targeted file opening. Version 5.1 extends audio checks to 5/10/15-band responses, custom-curve retention, mono mixing, clipping bounds, persistence, and invalid-backup rejection. Platform checks cover Unicode TagLib filenames and working-directory-independent instance endpoints. UI checks exercise folder navigation, pinned collections, custom rules, bookmarks with large text, offline lyric seeking, live preference restoration, audio controls, and Settings section navigation. Overview checks send real mouse-wheel and touchpad events through a 180-song search and reach the last result at normal and enlarged text sizes. Artist-link checks click names and blank space for solo/shared credits in Now Playing, the footer, song rows, and Song info. All file-opening fixtures use temporary WAV files; unknown files are imported without discovering unrelated new files in saved folders. Existing playback continuity, navigation, metadata portability, queue, and high-DPI suites remain part of the full run.

Run **CMake: Run Tests** in VS Code. The tests use temporary audio files and isolated settings, run without a display, and cover recursive imports, duplicate files, metadata duration, persisted favorites and volume, invalid imports, QML startup, library search, playback controls, and queue navigation. Regression checks also cover collective releases, track numbering, cache reuse, scrolling through 160 albums / 1,600 songs, red favorite hearts, Now Playing navigation, duplicate queue removal, playlist persistence, song info, confirmation cancellation, deletion of a controlled temporary file, and the 50% listening threshold with pause/seek exclusions and saved play counts.

Lightweight regressions migrate a 3,000-song library and verify bounded paging,
search, favorites, listening qualification, and smart counts without a resident
full library. Artwork checks cover cache reuse, memory bounds, real rendering
of filenames containing spaces/Unicode/percent signs, and clearing thumbnails.
Shortcut checks verify conflicts, persistence, actual key capture, playback
with remapped keys, and restoring defaults. Screen checks verify that closed
editors, Settings, Now Playing, and the dashboard release their objects.

Ranking regressions change counts across existing rows, promote newly played songs in a 1,000-song library, verify visible titles and count labels against the sorted model, exercise direct page switches and scrolling with reused delegates, and confirm that artwork-only updates do not trigger sorting.

Version 4.6 checks additionally cover four independent queue sessions and restart migration, background saves for a 10,000-entry queue, light/dark preference persistence and artwork fallback, seek bounds and keyboard controls, batched delegate refreshes, unchanged audio copies, Now Playing image capture, optional artist separators and duplicate credits, actual mouse clicks on the second credited artist, and the About credit. Email composition depends on the Linux desktop portal and is not opened by automated tests.

Navigation checks cover Back on short album/artist/playlist pages, history, search/sort/scroll restoration, text editing with Backspace, repeated popup-button toggles, fullscreen controls and maximized-window restoration, and quitting from fullscreen in a separate process.

Queue regressions use 120 entries with repeated songs, normal and large text, consecutive removals at the bottom, removals above the visible area and of the playing entry, playback advances while browsing, both queue-opening buttons, jumping to middle/first/last entries, empty queues, and real drag/reorder and session restoration.

Automatic-queue checks cover source names for albums, artists, playlists, Most Played, and searches; filling the initial empty slot; replacing the oldest queue even after switching back to it; and retaining queue order, position, shuffle, and repeat across switching and restarting. Saving a reordered queue verifies playlist order, repeated-song handling, validation before creation, and uninterrupted queue state.

Collection-sort regressions exercise title, artist, date, numeric song count, and duration in both directions with both model backends. They cover live total updates, search, old-database migration without music files or scanning, actual sort-menu and direction-button clicks, returning from albums, and controls at small window sizes with large text.

Genre checks cover case/spacing/Unicode normalization, intact punctuation, missing tags, totals, cached-database migration without scanning, genre search, and filtering with both model backends. Interface checks exercise the sidebar, all four sorts, reverse ordering, right-click actions, named playback queues, batch genre edits, and Back navigation at normal and large text sizes. The same interaction checks run at double display scaling; **musica_genres** is also available as a focused CMake test.

Additional checks cover saved-library startup without filesystem discovery, legacy cache migration, explicit scans after external file changes, album/artist right-click actions, ordered batch queue insertion, checkbox selection, portable batch tag and cover edits, stale-file partial failures, bulk favorites/playlists/Trash, and the batch editor at large text sizes.

Additional checks cover session restore with duplicate shuffled entries and missing files, real pointer drags in queues and playlists, saved manual album edits and undo across rescans/restarts, M3U round trips with Unicode and relative paths, smart-playlist rules, dated dashboard aggregation, playlist rename/delete flows, and real MPRIS requests over an isolated D-Bus connection.

Playback checks verify PCM joins without inserted silence, equal-power mixed samples, the 70-second cap, independent counts for both songs in a real crossfade, timed/end-of-song sleep without automatic advance, main-player audio controls, and mouse/touchpad response through the large-library fixture.

Continuity regressions use a 5,000-entry library cache and deliberately block the interface for 600 ms during saves around five and ten seconds. They verify that audio is still submitted and processed without new output underruns. A separate check blocks the interface across a crossfade and a sleep deadline, verifies the saved queue entry and listening count, and ensures delayed notifications cannot undo a pause or seek.

Window regressions check main-window restoration, minimized/maximized/fullscreen states, desktop Raise, and removal of the old mini-player controls and shortcut. Separate-process tests close the main window during idle, playback, minimized, and fullscreen states and invoke desktop Quit; each must terminate the app and preserve the listening session. Upgrade checks retire old analysis settings without invalidating the normal tag cache.

Metadata tests cover MP3, FLAC, M4A, Vorbis, Opus, WAV, AIFF, and WMA. They verify Unicode tags and attached artwork using `ffprobe`, compare decoded-audio hashes before/after editing, copy each edited file into a fresh library with separate settings, preserve unrelated tags, and exercise stale/read-only/invalid-image failures, artwork removal, cancel, live UI refresh, and playback continuity during a save.

For a startup check or screenshot:

```sh
./build/musicplayer --smoke-test
./build/musicplayer --screenshot preview.png
```

On a machine without a display, prefix those commands with `QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software`. These options load the real interface and exit; the smoke check fails if the QML engine reports a warning.

## Keyboard shortcuts

| Shortcut | Action |
| --- | --- |
| Space | Play / pause, while not editing text |
| Right / Left | Seek forward / backward by the configured 5–30 second step |
| Ctrl+Right / Ctrl+Left | Next / previous track |
| Ctrl+F | Search the library |
| Ctrl+O | Open and play audio files |
| Ctrl+Z | Undo the latest queue/playlist edit while not editing text |
| Ctrl+, | Open Settings |
| Ctrl+Q | Quit Musica and save the current listening position |
| Alt+Left | Go back through visited pages |
| Backspace | Go back while not editing text |
| F11 / Fn+F11 | Toggle fullscreen Now Playing |
| Escape | Close the open popup, exit fullscreen, or close Now Playing; otherwise leave search |
| Up / Down on a reorder handle | Move a queue or playlist entry |

These are the defaults. The main playback/navigation bindings can be changed
in **Settings → Keyboard shortcuts**.

## Library scanning and scrolling

Scanning is manual. Reopening restores the saved song list and cached tags, including older caches, without walking folders, checking every file, or probing metadata. New or externally changed files appear after you click **Scan library** or explicitly import music. A library scan already running when you close the app stays stopped on the next launch. Tag edits and deletions update the saved list directly; neither starts a new scan.

During a requested scan, the header distinguishes **Finding music files…** from **Reading tags: completed / total**. Tags are cached as they are read, including failures, so one damaged file does not restart the scan. Playback and browsing remain available during indexing. Older tag caches are upgraded during your next requested scan.

Albums are matched by normalized title, then by evidence that their tracks belong to one release:

- A shared album artist or collective name.
- The same album directory, including its `CD1` / `CD2` subfolders, or identical cover artwork.
- Compilation credits such as “Compiled By” or “Various Artists”.
- A distinctive matching title with complementary disc/track numbers across different performers, without conflicting track positions.

Matching ignores capitalization, extra whitespace, punctuation, and compilation-credit suffixes. Generic names such as “Greatest Hits” need stronger evidence than a title or track-number match. Tracks from unrelated albums with conflicting numbering remain separate unless their artwork, folder, or album-artist tags identify the same release. Each song keeps its performer name. A merged album displays its collective name when available, otherwise **Various artists**. This changes Musica's library view; the audio files and their tags are not rewritten.

Album, artist, and song views update individual entries without resetting the page as tags load. Artwork and duration updates do not trigger sorting; the list order is checked only when its sorting fields change. Each page has one active scroll area. High-resolution touchpad motion responds immediately with a 1.8× distance gain while keeping gesture direction and OS momentum. Mouse notches move at least 144 pixels (or 30% of the viewport), with acceleration for rapid repeated notches and a reset on direction changes. Touch flicks have a higher velocity limit and lower deceleration. Cover encoding runs in the background. **Songs** shows the full library; Overview has a bounded summary and switches to the full scrolling results list while searching.

Metadata batches refresh delegate data once per batch. Mouse notches animate over 100 ms with easing; touchpad pixel movement remains immediate. Five-second queue/history saves run separately from the large library cache. Queue headers fit on one row, and only the actively dragged queue/playlist delegate is retained while dragging beyond the viewport; normal row virtualization remains in use.

## Album track order

Opening an album selects **Track** sorting automatically. Tracks are ordered by disc, then track number, so track 2 precedes track 10. Embedded track-number tags take priority over numbered filenames. When tags are missing, names such as `02 Artist - Song.flac` provide the track number, and `CD2` / `Disc 2` folders or `2-01 Song.flac` filenames provide the disc. Unnumbered tracks follow numbered tracks on their disc.

The album's play button builds its queue in the same release order, and **Play all** follows the current view's sorting. You can still choose **Title**, **Artist**, or **Album** from the sort menu. Imported locations, favorites, and saved metadata remain available after restarting; older cached metadata is refreshed during a requested scan to read track-number tags.

## Audio and metadata

Recognized extensions: MP3, FLAC, WAV, OGG, OGA, OPUS, M4A, AAC, AIF, AIFF, and WMA. Codec availability depends on your Qt Multimedia backend. Tags, duration, and embedded cover art are read asynchronously and cached; nearby `cover.jpg`, `cover.png`, `folder.jpg`, or `folder.png` images are also recognized. Until tags load, titles come from filenames and albums from folders. Files without artwork receive a built-in illustration. No streaming account or internet connection is required.

Implementation: [main.cpp](main.cpp) starts the app, [tracklibrary.cpp](tracklibrary.cpp) scans local files and coordinates persistence, [filetags.cpp](filetags.cpp) reads and safely writes native metadata and artwork, [metadataediting.cpp](metadataediting.cpp) coordinates asynchronous edits and cache refresh, [components/MetadataEditor.qml](components/MetadataEditor.qml) and [components/BatchMetadataEditor.qml](components/BatchMetadataEditor.qml) provide the editors, [libraryfeatures.cpp](libraryfeatures.cpp) manages playlists, saved sessions, album overrides, and dashboard history, [albumgrouping.cpp](albumgrouping.cpp) reconciles releases and track order, [listeninghistory.cpp](listeninghistory.cpp) measures actual playback and qualified listens, [musiclibrary.cpp](musiclibrary.cpp) tracks overlapping listens, [audiomixer.cpp](audiomixer.cpp) buffers and mixes PCM, [playbackengine.cpp](playbackengine.cpp) exposes playback controls and state to QML, [playbackworker.cpp](playbackworker.cpp) owns decoding, continuous audio, and sleep timers on the audio thread, [librarymodels.cpp](librarymodels.cpp) maintains filtered library entries, [mprisservice.cpp](mprisservice.cpp) handles Linux desktop controls, [main.qml](main.qml) coordinates playback and navigation, and [qml.qrc](qml.qrc) bundles the interface and artwork.

Musica links dynamically to [TagLib](https://taglib.org/), an audio metadata library available under the MPL or LGPL. See [TagLib's licensing information](https://taglib.org/api/) for its upstream terms.

## License and contributing

Copyright (C) 2026 **weo sikho fuzile** and contributors. Musica’s own source,
documentation, and original artwork are licensed under **GPL-3.0-or-later**.
See [LICENSE](./LICENSE), [CONTRIBUTING.md](./CONTRIBUTING.md), and
[THIRD_PARTY_NOTICES.md](./THIRD_PARTY_NOTICES.md). Contributors retain their
copyright and contribute under the same license.

Donations and paid distribution are allowed. The GPL permits commercial use
and sales; accepting donations does not by itself require buying a Qt
commercial license. When distributing open-source Qt builds, comply with the
applicable GPL/LGPL requirements, including corresponding source, license
notices, and any required installation/relinking information. Separate Qt
commercial agreements, if used, have their own terms. See
[Qt’s obligations](https://www.qt.io/development/open-source-lgpl-obligations)
and [GNU’s guidance on selling free software](https://www.gnu.org/philosophy/selling.en.html).

The GitHub repository is [red-outrunner/Musiqa](https://github.com/red-outrunner/Musiqa).
The source includes code, documentation, application artwork, and tests.
Build output, libraries, local player state, screenshots, credentials, and
temporary music directories are excluded by [.gitignore](./.gitignore).
