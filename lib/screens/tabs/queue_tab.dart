import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:musiqa/providers/audio_provider.dart';
import 'package:musiqa/widgets/song_tile.dart';
import 'package:musiqa/widgets/song_options_menu.dart';

class QueueTab extends ConsumerWidget {
  const QueueTab({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final playback = ref.watch(playbackControllerProvider);
    final controller = ref.read(playbackControllerProvider.notifier);
    final queue = playback.queue;

    if (queue.isEmpty) {
      return const Center(
        child: Text("Queue is empty.\nPick songs from Playlists, Albums or Artists.",
            textAlign: TextAlign.center),
      );
    }

    return ReorderableListView.builder(
      buildDefaultDragHandles: false,
      itemCount: queue.length,
      onReorder: controller.reorder,
      itemBuilder: (context, index) {
        final song = queue[index];
        return SongTile(
          key: ValueKey(song.id),
          song: song,
          selected: index == playback.currentIndex,
          onTap: () => controller.jumpTo(index),
          trailing: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              SongBadge(songId: song.id),
              SongOptionsMenu(song: song, queueIndex: index),
              ReorderableDragStartListener(
                index: index,
                child: const Padding(
                  padding: EdgeInsets.only(left: 4, right: 8),
                  child: Icon(Icons.drag_handle),
                ),
              ),
            ],
          ),
        );
      },
    );
  }
}
