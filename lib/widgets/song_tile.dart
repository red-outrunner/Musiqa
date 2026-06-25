import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:musiqa/providers/metadata_provider.dart';
import 'package:musiqa/widgets/song_options_menu.dart';
import 'package:on_audio_query/on_audio_query.dart';

/// Shared list row for a song: album art, title, artist, and the BPM/key
/// badge. Callers can override [trailing] (e.g. for queue drag handles) and
/// [leading] (e.g. for track numbers).
class SongTile extends ConsumerWidget {
  final SongModel song;
  final VoidCallback? onTap;
  final Widget? trailing;
  final Widget? leading;
  final bool selected;

  const SongTile({
    super.key,
    required this.song,
    this.onTap,
    this.trailing,
    this.leading,
    this.selected = false,
  });

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    return ListTile(
      selected: selected,
      leading: leading ??
          SizedBox(
            width: 48,
            height: 48,
            child: QueryArtworkWidget(
              id: song.id,
              type: ArtworkType.AUDIO,
              artworkBorder: BorderRadius.circular(6),
              nullArtworkWidget: const Icon(Icons.music_note, size: 36),
            ),
          ),
      title: Text(song.title, maxLines: 1, overflow: TextOverflow.ellipsis),
      subtitle: Text(song.artist ?? 'Unknown artist',
          maxLines: 1, overflow: TextOverflow.ellipsis),
      trailing: trailing ??
          Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              SongBadge(songId: song.id),
              SongOptionsMenu(song: song),
            ],
          ),
      onTap: onTap,
    );
  }
}

/// Small monospace badge widget for use inside custom trailing rows.
class SongBadge extends ConsumerWidget {
  final int songId;
  const SongBadge({super.key, required this.songId});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final badge = ref.watch(metadataProvider).badge(songId);
    if (badge.isEmpty) return const SizedBox.shrink();
    return Text(
      badge,
      style: const TextStyle(fontFamily: 'monospace', fontWeight: FontWeight.w600),
    );
  }
}
