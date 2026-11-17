# Musica 1.0.0

An offline music player for your own music collection, created by **weo sikho fuzile**.

[Download the latest release](https://github.com/red-outrunner/Musiqa/releases/latest)

## Download and open

- **Windows 10/11, Intel or AMD 64-bit:** download the Windows ZIP, extract the whole folder, then open `bin/musicplayer.exe`. Keep the DLLs and folders beside it. Install the [Microsoft Visual C++ x64 runtime](https://aka.ms/vs/17/release/vc_redist.x64.exe) if Windows asks for it.
- **Linux, Intel or AMD 64-bit:** download the Linux `.tar.gz`, extract it, then open **Musica** inside the extracted folder. From a terminal in that folder, run `./Musica`. The package targets Ubuntu 22.04 or newer and comparable Linux distributions with a desktop and working system audio.

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
- **Undo:** press **Ctrl+Z** to undo recent queue and playlist changes. File deletion and metadata writes are not covered by this undo.

## Make it yours

Open **Settings** to change the light/dark theme, text size, layout, keyboard shortcuts, and audio output. Optional controls include crossfade from **0–70 seconds**, gapless playback, a sleep timer, mono/stereo, and a **5–15 band equalizer**.

ReplayGain uses existing loudness tags. Artist-credit splitting is optional. No automatic BPM/key analysis or startup library scan runs.

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
