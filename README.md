# Musiqa

A local music player for Android built with Flutter. Musiqa scans the songs on
your device, organises them by album/artist/playlist, reads BPM & musical key
from ID3 tags, and plays them back with a real **crossfade** between tracks.

## Features

- **Library browsing** — five tabs: Queue, Now Playing, Playlists, Albums, Artists.
- **Playback queue** — the Queue tab shows the *active* queue. Reorder tracks by
  holding the drag handle, and use the ⋮ menu to **Play next** or **Remove from
  queue**. Tap any row to jump to it.
- **Crossfade** — a configurable crossfade (0–12s) smoothly blends one track into
  the next. Implemented with a dual-`AudioPlayer` engine that ramps the outgoing
  track down while the incoming track ramps up (just_audio has no native
  crossfade).
- **Now Playing** — full-screen player with album art, seek bar, transport
  controls and the crossfade slider. Available both as a tab and as a bottom
  sheet from the mini player.
- **Playlists** — the primary **All Songs** list (every track on the device)
  plus any on-device playlists.
- **Albums** — album list with artwork; album detail shows tracks ordered by
  track number.
- **Artists** — artist detail shows the artist's albums in a horizontal strip,
  then every song by that artist listed alphabetically.
- **BPM & Key** — the sync button in the app bar scans the library's ID3 tags
  (`TBPM`, `TKEY`/`INITIALKEY`) and caches them. Each song shows a compact badge,
  e.g. `[A|90bpm]`.
- **Album art** throughout the player, mini player and song lists.

## Architecture

- **State management:** [Riverpod](https://riverpod.dev).
- **Audio:** [`just_audio`](https://pub.dev/packages/just_audio) (two players for
  crossfade), [`on_audio_query`](https://pub.dev/packages/on_audio_query) for the
  media library and artwork.
- **Metadata:** [`id3`](https://pub.dev/packages/id3) for BPM/key tags, cached in
  `shared_preferences`.

```
lib/
  main.dart                       App entry, permissions, ProviderScope
  providers/
    audio_provider.dart           PlaybackController (queue + crossfade engine)
    audio_query_provider.dart     Songs / albums / artists / playlists queries
    metadata_provider.dart        ID3 BPM/key scan + cache + badge formatting
  screens/
    main_layout.dart              Bottom-nav scaffold + library scan action
    player_screen.dart            Modal player + Now Playing tab
    album_details_screen.dart
    artist_details_screen.dart
    song_list_screen.dart         Reusable song list (All Songs, playlists)
    tabs/                         Queue / Playlists / Albums / Artists tabs
  widgets/
    player_view.dart              Reusable full-player UI
    mini_player.dart              Persistent mini player
    song_tile.dart                Shared song row with art + BPM/key badge
```

## Getting started

```bash
flutter pub get
flutter run                 # run on a connected device/emulator
flutter build apk --release # build a release APK
```

The release APK is written to `build/app/outputs/flutter-apk/app-release.apk`.

Musiqa requests audio/storage permissions on launch to read your music library.
