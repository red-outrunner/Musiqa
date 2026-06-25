import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:musiqa/providers/audio_provider.dart';
import 'package:musiqa/providers/metadata_provider.dart';
import 'package:musiqa/widgets/song_tile.dart';
import 'package:on_audio_query/on_audio_query.dart';

/// Generic scrollable list of songs. Tapping a song loads the whole list as
/// the active playback queue, starting at that song.
class SongListScreen extends ConsumerWidget {
  final String title;
  final Future<List<SongModel>> songsFuture;

  const SongListScreen({
    super.key,
    required this.title,
    required this.songsFuture,
  });

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    return Scaffold(
      appBar: AppBar(title: Text(title)),
      body: FutureBuilder<List<SongModel>>(
        future: songsFuture,
        builder: (context, snapshot) {
          if (snapshot.connectionState == ConnectionState.waiting) {
            return const Center(child: CircularProgressIndicator());
          }
          if (snapshot.hasError) {
            return Center(child: Text('Error: ${snapshot.error}'));
          }
          final songs = snapshot.data ?? [];
          if (songs.isEmpty) {
            return const Center(child: Text('No songs found.'));
          }
          return SongListView(songs: songs);
        },
      ),
    );
  }
}

/// Reusable song list backed by an in-memory list.
class SongListView extends ConsumerWidget {
  final List<SongModel> songs;
  const SongListView({super.key, required this.songs});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final controller = ref.read(playbackControllerProvider.notifier);
    final hidden = ref.watch(hiddenSongsProvider);
    final visible = songs.where((s) => !hidden.contains(s.id)).toList();
    return ListView.builder(
      itemCount: visible.length,
      itemBuilder: (context, index) {
        final song = visible[index];
        return SongTile(
          song: song,
          onTap: () => controller.playQueue(visible, index),
        );
      },
    );
  }
}
