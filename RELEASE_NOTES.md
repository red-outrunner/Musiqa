# Musica 1.0.1

An update to Musica, the offline music player by **weo sikho fuzile**.

- Optional **Crossfade when skipping** in **More → Gapless / crossfade**, using your chosen fade duration. It is off by default.
- Now Playing controls stay in place when song titles wrap.
- Batch selections clear after successful actions, and metadata editing suggests existing artists, albums, and genres.
- Search plays the latest query immediately, even when Enter is pressed quickly.
- Repeat changes apply immediately, preventing an extra repeat when seeking near a song's end.
- Faster batch favorites and artist album updates, with more reliable artist-page scroll restoration.
- Folder matching and library refresh fixes.

Download a built package under **Assets**:

- **Windows 10/11 x64:** extract the entire Windows ZIP and open `bin/musicplayer.exe`. Windows may need the [Microsoft Visual C++ x64 runtime](https://aka.ms/vs/17/release/vc_redist.x64.exe).
- **Linux x86_64:** extract the Linux archive and run `Musica` from the extracted folder.

ARM packages are not included. The source archive matches this release, and `SHA256SUMS.txt` lists the download checksums. See the [README](https://github.com/red-outrunner/Musiqa/blob/v1.0.1/README.md) for the short usage guide.

Playback integration checks run on Linux with an audio output. Runners without an audio device skip those checks; software mixer and packaging checks still run.
