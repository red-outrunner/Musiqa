import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:musiqa/providers/audio_provider.dart';
import 'package:musiqa/screens/player_screen.dart';

class MiniPlayer extends ConsumerWidget {
  const MiniPlayer({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final title = ref.watch(currentSongTitleProvider);
    final isPlaying = ref.watch(isPlayingProvider);

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
            const SizedBox(
              width: 50,
              height: 50,
              child: Icon(Icons.music_note, size: 50),
            ),
            const SizedBox(width: 8),
            Expanded(
              child: Text(
                title,
                style: Theme.of(context).textTheme.titleMedium,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
              ),
            ),
            IconButton(
              icon: Icon(isPlaying ? Icons.pause : Icons.play_arrow),
              onPressed: () {
                final player = ref.read(audioPlayerProvider);
                if (isPlaying) {
                  player.pause();
                } else {
                  player.play();
                }
              },
            ),
            const SizedBox(width: 8),
          ],
        ),
      ),
    );
  }
}
