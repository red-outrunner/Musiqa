# Musica 1.0.1

An offline music player for your own music collection, created by **weo sikho fuzile**.

[Download the latest release](https://github.com/red-outrunner/Musiqa/releases/latest)

## Download and open

- **Windows 10/11, Intel or AMD 64-bit:** download the Windows ZIP, extract the whole folder, then open `bin/musicplayer.exe`. Keep the DLLs and folders beside it. Install the [Microsoft Visual C++ x64 runtime](https://aka.ms/vs/17/release/vc_redist.x64.exe) if Windows asks for it.
- **Linux, Intel or AMD 64-bit:** download the Linux archive, extract it, and run `./Musica` inside the extracted folder. See [building from source](./BUILDING.md) if you prefer to compile it yourself.

Choose a built package under **Assets**. The automatic “Source code” downloads are for developers. ARM builds are not provided yet.

## Start listening

1. Open Musica and choose **Add a folder** in the sidebar. Select your music folder and let the first scan finish.
2. Browse **Albums**, **Artists**, **Genres**, or **Folders**, or search for a song.
3. Click a song to play its collection. Use **Play all** or **Shuffle** to start the whole page.
4. Click the cover in the bottom player bar to open **Now Playing**. Click an artist or album name to visit that page.

Library scanning is manual. After adding or moving music on disk, use the scan button to refresh it. Musica remembers your queue and listening position when you close it.

## Everyday controls

- **Queue:** the queue button opens and closes the playing list. Search it, use **×** to remove a song, or clear the search and drag songs to reorder. **Current song** jumps back to the playing track.
- **Multiple queues:** starting another collection creates a named queue. Musica keeps up to four; the oldest is replaced when full. Use the queue menu to save one as a playlist.
- **Song menus:** right-click a song or use **⋯** to play next, add to a queue or playlist, share, view information, or edit metadata.
- **Favorites:** click the heart; a red heart marks a favorite. **Most Played** counts a play after at least half the song has been heard.
- **Batch editing:** select songs with their checkboxes, then use the selection toolbar. Metadata and cover edits are written to the audio files.
- **Moving from another player:** open **Settings → Library tools → Import from another player**. Preview an M3U/M3U8, PLS, XSPF, or WPL playlist, optionally match moved files in your music folder, and import the selected songs.
- **Undo:** press **Ctrl+Z** to undo recent queue and playlist changes. File deletion and metadata writes are not covered by this undo.

## Make it yours

Open **Settings** to change the light/dark theme, text size, layout, keyboard shortcuts, and audio output. Optional controls include crossfade from **0–70 seconds**, gapless playback, a sleep timer, mono/stereo, and a **5–15 band equalizer**.

In the player’s **More → Gapless / crossfade** menu, enable **Crossfade when skipping** to blend songs when pressing Next. It uses your fade duration and is off by default; automatic crossfade works as usual. If the next song is not buffered yet, it skips normally.

ReplayGain uses existing loudness tags. Artist-credit splitting is optional. No automatic BPM/key analysis or startup library scan runs.

**Settings → Laptop and performance** contains battery saver, pause-on-disconnect, optional resume after sleep, and local startup/RAM/CPU measurements. Playback integrates with Linux MPRIS or Windows media controls. High contrast and larger icon controls are available under **Display and layout**; songs and collections also support keyboard navigation.

See [performance measurements](./PERFORMANCE.md) for the benchmark method and recorded results.

## Useful shortcuts

| Shortcut | Action |
| --- | --- |
| Space | Play or pause |
| Left / Right | Seek by your chosen 5–30 second step |
| Ctrl+Left / Ctrl+Right | Previous / next song |
| Ctrl+F | Search |
| Ctrl+O | Open audio files |
| Ctrl+, | Settings |
| Alt+Left | Back |
| F11 | Fullscreen Now Playing |
| Escape | Close the current overlay |
| Ctrl+Q | Quit |

You can also drag audio files into the window or choose Musica from your file manager’s **Open with** menu.

## Help and contributing

[Report a problem](https://github.com/red-outrunner/Musiqa/issues) · [Build from source](./BUILDING.md) · [Contribute](./CONTRIBUTING.md)

Licensed under [GPL-3.0-or-later](./LICENSE). Contributions and donations are welcome. See [third-party notices](./THIRD_PARTY_NOTICES.md) for dependency licenses.
