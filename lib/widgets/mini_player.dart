import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:musiqa/providers/audio_provider.dart';
import 'package:musiqa/screens/player_screen.dart';
import 'package:on_audio_query/on_audio_query.dart';

class MiniPlayer extends ConsumerWidget {
  const MiniPlayer({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final song = ref.watch(currentSongProvider);
    final isPlaying = ref.watch(isPlayingProvider);
    final controller = ref.read(playbackControllerProvider.notifier);

    return GestureDetector(
      onTap: () {
        showModalBottomSheet(
          context: context,
          isScrollControlled: true,
          useSafeArea: true,
          builder: (context) => const PlayerScreen(),
        );
      },
      child: Container(
        height: 70,
        color: Theme.of(context).colorScheme.surfaceContainerHighest,
        child: Row(
          children: [
            const SizedBox(width: 8),
            SizedBox(
              width: 50,
              height: 50,
              child: song == null
                  ? const Icon(Icons.music_note, size: 40)
                  : QueryArtworkWidget(
                      id: song.id,
                      type: ArtworkType.AUDIO,
                      artworkBorder: BorderRadius.circular(6),
                      nullArtworkWidget: const Icon(Icons.music_note, size: 40),
                    ),
            ),
            const SizedBox(width: 12),
            Expanded(
              child: Text(
                song?.title ?? 'Not Playing',
                style: Theme.of(context).textTheme.titleMedium,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
              ),
            ),
            IconButton(
              icon: const Icon(Icons.skip_previous),
              onPressed: controller.previous,
            ),
            IconButton(
              icon: Icon(isPlaying ? Icons.pause : Icons.play_arrow),
              onPressed: controller.togglePlayPause,
            ),
            IconButton(
              icon: const Icon(Icons.skip_next),
              onPressed: controller.next,
            ),
            const SizedBox(width: 8),
          ],
        ),
      ),
    );
  }
}
