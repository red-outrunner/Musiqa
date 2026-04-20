import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:musiqa/providers/audio_query_provider.dart';
import 'package:musiqa/providers/audio_provider.dart';
import 'package:just_audio/just_audio.dart';
class QueueTab extends ConsumerWidget {
  const QueueTab({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final songsAsyncValue = ref.watch(songsProvider);

    return songsAsyncValue.when(
      data: (songs) {
        if (songs.isEmpty) {
          return const Center(child: Text("No songs found"));
        }
        return ReorderableListView.builder(
          itemCount: songs.length,
          onReorder: (oldIndex, newIndex) {
            // Logic for reordering
          },
          itemBuilder: (context, index) {
            final song = songs[index];
            return ListTile(
              key: ValueKey(song.id),
              leading: const Icon(Icons.music_note),
              title: Text(song.title, maxLines: 1, overflow: TextOverflow.ellipsis),
              subtitle: Text(song.artist ?? "Unknown artist", maxLines: 1, overflow: TextOverflow.ellipsis),
              trailing: Text("${((song.duration ?? 0) / 60000).toStringAsFixed(2)} min"),
              onTap: () async {
                final player = ref.read(audioPlayerProvider);
                ref.read(currentSongTitleProvider.notifier).updateState(song.title);
                try {
                  if (song.uri != null) {
                    await player.setAudioSource(AudioSource.uri(Uri.parse(song.uri!)));
                    player.play();
                  }
                } catch (e) {
                  debugPrint("Error playing audio: $e");
                }
              },
            );
          },
        );
      },
      loading: () => const Center(child: CircularProgressIndicator()),
      error: (err, stack) => Center(child: Text('Error: $err')),
    );
  }
}
