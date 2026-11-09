# Musica

A native Qt 6 music player for the music you already own. A quiet dark interface, mint accents, album artwork, and a player that stays with you as you browse. Version **4.2.0** makes either window's close button exit the app and removes tempo/key analysis and matching. It includes dedicated-thread audio playback, an offline metadata editor, gapless playback, customizable crossfades, an independent mini player, sleep timers, and fast library scrolling.

## Use the player

Run the compiled app from the project directory:

```sh
./build/musicplayer
```

In VS Code, you can also use **Terminal → Run Task → Musica: Run player** after building.

The window title shows the running version. Close older player windows before launching the rebuilt executable so that you are checking the current fixes.

Musica scans your system Music folder, including subfolders. Use **Add music** to import individual files, **Add a folder** to import a collection, or drop files and folders onto the window. Imported locations, favorites, metadata, and volume are saved between launches. The refresh button checks for added or removed files.

- **Overview:** your collection at a glance.
- **Songs:** search and sort the library by title, artist, or album.
- **Albums / Artists:** open a collection to browse and play its songs. Album pages default to disc and track order.
- **Favorites:** click a song's heart to keep it close. Saved hearts fill red everywhere, including the bottom bar and Now Playing.
- **Most Played:** an automatic playlist ranked by qualified listens. A song qualifies after at least 50% has played during one play.
- **Playlists:** create named playlists, drag songs into your preferred order, rename/delete playlists, and import/export M3U playlists.
- **Smart playlists:** Recently added, Never played, and Rarely played update automatically.
- **Listening dashboard:** recent sessions, weekly favorites, top artists, listening time, and a seven-day activity chart.
- **Now Playing:** click the bottom-bar album cover for large artwork, song details, and playback controls. Click the artist or album name to open that collection. Background clicks and scrolling stay within Now Playing, keeping covered album/playlist tracks from interrupting playback.
- **Playback:** play/pause, previous/next, seek, shuffle, repeat off/all/one, queue, mute, volume, and gapless transitions.
- **Crossfade:** the crossing-lines button in the bottom bar opens a saved on/off switch and 0–70 second duration.
- **Mini player:** the small-window button opens an always-on-top window with artwork, song details, seek, playback controls, and favorites.
- **Sleep timer:** the moon button stops after a chosen number of minutes or at the end of the current song.
- **Metadata & covers:** **⋯ → Edit metadata & cover…** edits the audio file itself, so saved tags and embedded artwork accompany the song when copied elsewhere.

Playback starts from the current view. For example, **Play all** on Favorites plays your favorites, and selecting a song inside an album queues that album. Shuffle visits each queued song before repeating. Previous restarts the current song after three seconds; otherwise it selects the previous song.

Most Played records one listen per play after half the song has actually played. Pauses and skipped sections do not count, and replaying the same passage within one play does not increase coverage. Starting the song again can earn another listen. Play counts and playlist contents are saved between launches. Seeking near the end alone will not add the song to Most Played.

The queue's **×** removes that one queue entry. Duplicate entries can be removed independently, and removing an upcoming song leaves the current song playing. Removing the current entry selects another available song; an empty queue stops playback. Drag the dotted handle to reorder entries without changing the current song or its position. Both the active queue and the original order used when disabling shuffle are saved.

## Edit portable metadata and cover art offline

Open a song's **⋯ → Edit metadata & cover…** from the song list, queue, or Now Playing. Edit title, artist, album, album artist/collective, genre, release date/year, composer, track/disc numbers and totals, and comments. The editor reads the tags stored in the file. Only fields you change are saved.

Use **Choose image…** to select a JPEG or PNG from your computer. Its original image data and resolution are embedded as the front cover; the image is not linked to an external path. Images can be up to 20 MB and 40 megapixels. **Remove artwork** removes embedded artwork, while **Keep original artwork** cancels a pending artwork change. Text-only edits retain existing embedded covers. A nearby folder image can still appear as a library fallback when a song has no embedded cover.

Click **Save to file** to commit the changes. Copy the edited audio file to another machine and its tags and embedded cover travel with it; no Musica settings folder or image file needs to be copied. Other players may need to refresh their library or artwork cache. Editing and artwork selection are entirely manual and offline, with no online metadata or image lookup.

Musica stages edits in a temporary copy beside the song, verifies the saved tags and cover, checks the audio properties and original revision, and atomically replaces the original. The audio is not re-encoded. Read-only files, unsupported tag values, invalid cover images, stale edits, and failed writes report an error while keeping the original. **Cancel** discards the draft. Tags, grouping, track order, artwork, and search results refresh after saving, while the song URL, playlists, favorites, and listening history remain associated with the file. Editing album tags resets that song's library-only album grouping override.

Editable containers include **MP3, FLAC, M4A/MP4, Ogg Vorbis, Opus, WAV, AIFF, and WMA**. The writer uses native ID3, Vorbis/FLAC, MP4, and ASF tag storage, and synchronizes existing legacy WAV/AIFF/WMA text metadata. Individual players differ in the fields and artwork formats they display, particularly WAV/AIFF. Raw `.aac` files do not have a supported portable tagging container; the editor reports this rather than silently storing changes only in Musica. Unedited tags already stored in the audio file are preserved.

## Gapless playback and crossfade

Click the **crossing-lines button** beside the queue in the bottom player bar to open **Playback transitions**. Gapless playback is enabled by default; crossfade is optional and starts disabled. Enable crossfade and set its duration with the slider or editable seconds box from **0 to 70 seconds**. A duration of **0** plays without an overlap. These preferences survive restarting.

Audio decoding, mixing, output feeding, and sleep deadlines run on a dedicated audio thread. Five-second session checkpoints, library settings writes, and busy interface work cannot block the audio feed. The output has a 200 ms scheduling buffer; seeking flushes buffered audio, and pause/volume controls act directly on the audio thread. Position and listening events are passed back to the interface with their original playback timestamps. Saving a session also collects pending track transitions and listening events so it records the correct song and position.

Playback uses one continuous audio output and predecodes the upcoming queue entry. With crossfade off, the next song begins at the previous song's final decoded sample. With crossfade on, the outgoing and incoming songs overlap with equal-power fades and peak limiting. The song display and queue selection switch when the incoming song starts. Both songs' actual playback contributes to listening history during the overlap. Short songs reduce the fade to fit the available audio. Changes to fade settings apply to upcoming transitions.

The mixer removes gaps introduced by opening a new output stream; silence recorded into the audio itself remains. An unreadable or insufficiently buffered next song can still delay a transition. PCM is buffered in temporary files and removed when released, limiting RAM use to the working buffers. Long tracks use additional temporary disk space.

## Mini player and sleep timer

Click the **small-window button** in the bottom bar, or press **Ctrl+M**, to open the mini player. It is an independent window that requests always-on-top placement and shares the same queue and playback state. The full player is minimized and stays available from the taskbar. Use the mini player's expand button, press **Ctrl+M** again, or restore the full player from the taskbar to return. Desktop media controls' **Raise** action also restores the full player. If the mini window is unexpectedly hidden, Musica restores the full player automatically. Reopening places the mini player within the current screen, including after a monitor is disconnected. Its heart, previous/next, play/pause, and seek controls work on the same song. The **×** button in either window, **Ctrl+Q**, and the desktop **Quit** command exit Musica, stop audio, and save the current listening position.

Click the **moon button** to set a sleep duration of **1–720 minutes**, choose **At the end of this song**, or cancel the timer. The remaining time is shown in the timer panel and mini player. A timed stop pauses at the current position for later resumption. End-of-song mode finishes the current song and suppresses crossfading, repeat, and automatic advance into another song. Sleep timers do not restart when the application reopens.

## Resume listening

Reopening restores your queue, the exact selected entry (including duplicate songs), playback position, shuffle, repeat, mute, and volume. Playback waits for you to press Play. Missing files are skipped. Session changes are saved after a short delay, playback position is checkpointed every five seconds, and closing the window saves the latest position. The startup scan finishes discovering files before restoring the queue; it does not have to finish reading every tag.

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

Musica implements the [MPRIS interface](https://specifications.freedesktop.org/mpris/latest/), so compatible Linux desktops and media-key utilities can control play/pause, previous/next, seeking, volume, shuffle, and repeat, and display song information and artwork. The service is `org.mpris.MediaPlayer2.Musica` at `/org/mpris/MediaPlayer2`; additional instances use a process-specific service suffix. Integration is available automatically when a session D-Bus is running. The CMake install includes the desktop launcher and icon.

Each song's **⋯** menu offers **Play now**, **Play next**, **Add to queue**, **Add to playlist**, favorites, artist/album navigation, **Edit metadata & cover…**, **Song info**, **Show in folder**, and **Delete song**. The same menu is available in Now Playing and in the queue. Deleting a song asks for confirmation and moves its file to the system Trash, also removing it from the library, queue, favorites, and saved playlists. If the system cannot move it to Trash, Musica reports the error and keeps the file.

You can also import paths at launch:

```sh
./build/musicplayer "/path/to/music" "/path/to/song.flac"
```

## Build in VS Code

Prerequisites: a C++17 compiler, CMake 3.16 or newer, and Qt 6.3 or newer with Core, Gui, Qml, Quick, QuickControls2, Multimedia, Concurrent, DBus, and Test development components. The runtime also needs the Qt Quick Controls, Qt Quick Dialogs, Qt Multimedia QML modules and the Qt SVG image plugin. The `ffmpeg` command-line executable is used when available for fast playback predecoding; otherwise Musica uses Qt's audio decoder, which may buffer more slowly during early seeks. Integration tests use `dbus-daemon` to create an isolated session bus.

Metadata editing additionally requires **TagLib 2 or newer** and its development headers (`taglib-devel` on Fedora, `libtag2-dev` on distributions providing that package). CMake normally discovers it through pkg-config. An unpacked development package is also accepted at `build/taglib-sdk/usr/include/taglib`, linked against the installed TagLib runtime. Metadata portability tests use `ffmpeg` and `ffprobe` as independent audio/tag readers.

Open this folder in VS Code with CMake Tools installed, select a Qt 6 kit, then run **CMake: Configure** and **CMake: Build**. The application target is `musicplayer`, and the default build also includes `musica_tests`. Build output goes to the configured CMake build directory (normally `build/`).

## Checks

Run **CMake: Run Tests** in VS Code. The tests use temporary audio files and isolated settings, run without a display, and cover recursive imports, duplicate files, metadata duration, persisted favorites and volume, invalid imports, QML startup, library search, playback controls, and queue navigation. Regression checks also cover collective releases, track numbering, cache reuse, scrolling through 160 albums / 1,600 songs, red favorite hearts, Now Playing navigation, duplicate queue removal, playlist persistence, song info, confirmation cancellation, deletion of a controlled temporary file, and the 50% listening threshold with pause/seek exclusions and saved play counts.

Additional checks cover session restore with duplicate shuffled entries and missing files, real pointer drags in queues and playlists, saved manual album edits and undo across rescans/restarts, M3U round trips with Unicode and relative paths, smart-playlist rules, dated dashboard aggregation, playlist rename/delete flows, and real MPRIS requests over an isolated D-Bus connection.

Playback checks verify PCM joins without inserted silence, equal-power mixed samples, the 70-second cap, independent counts for both songs in a real crossfade, timed/end-of-song sleep without automatic advance, always-on-top mini-player controls, mouse/touchpad response through the large-library fixture.

Continuity regressions use a 5,000-entry library cache and deliberately block the interface for 600 ms during saves around five and ten seconds. They verify that audio is still submitted and processed without new output underruns. A separate check blocks the interface across a crossfade and a sleep deadline, verifies the saved queue entry and listening count, and ensures delayed notifications cannot undo a pause or seek.

Mini-player regressions check independent window ownership, always-on-top and taskbar flags, expand controls, taskbar restoration, desktop Raise, Ctrl+M from either window, unexpected hiding, and recovery from an off-screen position. Separate-process tests close the main or mini window during playback and invoke desktop Quit from either mode; each must terminate the app and preserve the listening session. Upgrade checks retire old analysis settings without invalidating the normal tag cache.

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
| Ctrl+Right / Ctrl+Left | Next / previous track |
| Ctrl+F | Search the library |
| Ctrl+O | Add audio files |
| Ctrl+M | Switch between the mini player and full player |
| Ctrl+Q | Quit Musica and save the current listening position |
| Escape | Close the queue or Now Playing and leave search |
| Up / Down on a reorder handle | Move a queue or playlist entry |

## Library scanning and scrolling

The header distinguishes **Finding music files…** from **Reading tags: completed / total**. Tags are cached as they are read, including when a file has no readable metadata, so one damaged file does not restart the scan. Playback and browsing remain available during indexing. After upgrading from the original scanner, older cached tags are refreshed once; imported locations and favorites are preserved.

Albums are matched by normalized title, then by evidence that their tracks belong to one release:

- A shared album artist or collective name.
- The same album directory, including its `CD1` / `CD2` subfolders, or identical cover artwork.
- Compilation credits such as “Compiled By” or “Various Artists”.
- A distinctive matching title with complementary disc/track numbers across different performers, without conflicting track positions.

Matching ignores capitalization, extra whitespace, punctuation, and compilation-credit suffixes. Generic names such as “Greatest Hits” need stronger evidence than a title or track-number match. Tracks from unrelated albums with conflicting numbering remain separate unless their artwork, folder, or album-artist tags identify the same release. Each song keeps its performer name. A merged album displays its collective name when available, otherwise **Various artists**. This changes Musica's library view; the audio files and their tags are not rewritten.

Album, artist, and song views update individual entries without resetting the page as tags load. Artwork and duration updates do not trigger sorting; the list order is checked only when its sorting fields change. Each page has one active scroll area. High-resolution touchpad motion responds immediately with a 1.8× distance gain while keeping gesture direction and OS momentum. Mouse notches move at least 144 pixels (or 30% of the viewport), with acceleration for rapid repeated notches and a reset on direction changes. Touch flicks have a higher velocity limit and lower deceleration. Cover encoding runs in the background. Use **Songs** for the full scrolling track list; Overview shows a short preview.

## Album track order

Opening an album selects **Track** sorting automatically. Tracks are ordered by disc, then track number, so track 2 precedes track 10. Embedded track-number tags take priority over numbered filenames. When tags are missing, names such as `02 Artist - Song.flac` provide the track number, and `CD2` / `Disc 2` folders or `2-01 Song.flac` filenames provide the disc. Unnumbered tracks follow numbered tracks on their disc.

The album's play button builds its queue in the same release order, and **Play all** follows the current view's sorting. You can still choose **Title**, **Artist**, or **Album** from the sort menu. Imported locations, favorites, and saved metadata remain available after restarting; older cached metadata is refreshed once to read track-number tags.

## Audio and metadata

Recognized extensions: MP3, FLAC, WAV, OGG, OGA, OPUS, M4A, AAC, AIF, AIFF, and WMA. Codec availability depends on your Qt Multimedia backend. Tags, duration, and embedded cover art are read asynchronously and cached; nearby `cover.jpg`, `cover.png`, `folder.jpg`, or `folder.png` images are also recognized. Until tags load, titles come from filenames and albums from folders. Files without artwork receive a built-in illustration. No streaming account or internet connection is required.

Implementation: [main.cpp](main.cpp) starts the app, [tracklibrary.cpp](tracklibrary.cpp) scans local files and coordinates persistence, [filetags.cpp](filetags.cpp) reads and safely writes native metadata and artwork, [metadataediting.cpp](metadataediting.cpp) coordinates asynchronous edits and cache refresh, [components/MetadataEditor.qml](components/MetadataEditor.qml) provides the editor, [libraryfeatures.cpp](libraryfeatures.cpp) manages playlists, saved sessions, album overrides, and dashboard history, [albumgrouping.cpp](albumgrouping.cpp) reconciles releases and track order, [listeninghistory.cpp](listeninghistory.cpp) measures actual playback and qualified listens, [musiclibrary.cpp](musiclibrary.cpp) tracks overlapping listens, [audiomixer.cpp](audiomixer.cpp) buffers and mixes PCM, [playbackengine.cpp](playbackengine.cpp) exposes playback controls and state to QML, [playbackworker.cpp](playbackworker.cpp) owns decoding, continuous audio, and sleep timers on the audio thread, [librarymodels.cpp](librarymodels.cpp) maintains filtered library entries, [mprisservice.cpp](mprisservice.cpp) handles Linux desktop controls, [main.qml](main.qml) coordinates playback and navigation, and [qml.qrc](qml.qrc) bundles the interface and artwork.

Musica links dynamically to [TagLib](https://taglib.org/), an audio metadata library available under the MPL or LGPL. See [TagLib's licensing information](https://taglib.org/api/) for its upstream terms.
