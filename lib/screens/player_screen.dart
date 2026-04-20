import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:musiqa/providers/audio_provider.dart';

class PlayerScreen extends ConsumerWidget {
  const PlayerScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final title = ref.watch(currentSongTitleProvider);
    final isPlaying = ref.watch(isPlayingProvider);
    final crossfadeDuration = ref.watch(crossfadeDurationProvider);

    return Scaffold(
      appBar: AppBar(title: const Text('Now Playing')),
      body: SafeArea(
        child: Column(
          children: [
            const Expanded(
              child: Padding(
                padding: EdgeInsets.all(32.0),
                child: Center(
                   // Album art placeholder
                   child: Icon(Icons.album, size: 200),
                ),
              ),
            ),
            Text(title, style: Theme.of(context).textTheme.headlineMedium),
            const SizedBox(height: 32),
            IconButton(
              iconSize: 64,
              icon: Icon(isPlaying ? Icons.pause_circle_filled : Icons.play_circle_fill),
              onPressed: () {
                final player = ref.read(audioPlayerProvider);
                if (isPlaying) {
                  player.pause();
                } else {
                  player.play();
                }
              },
            ),
            const SizedBox(height: 32),
            _buildCrossfadeControls(context, ref, crossfadeDuration),
            const SizedBox(height: 16),
          ],
        ),
      ),
    );
  }

  Widget _buildCrossfadeControls(BuildContext context, WidgetRef ref, double currentVal) {
    return Container(
      margin: const EdgeInsets.symmetric(horizontal: 16.0),
      padding: const EdgeInsets.symmetric(horizontal: 16.0, vertical: 8.0),
      decoration: BoxDecoration(
        color: Theme.of(context).colorScheme.surfaceContainerHighest,
        borderRadius: BorderRadius.circular(16)
      ),
      child: Row(
        children: [
          const Icon(Icons.compare_arrows, size: 20),
          const SizedBox(width: 8),
          const Text("Crossfade", style: TextStyle(fontWeight: FontWeight.bold)),
          Expanded(
            child: Slider(
              value: currentVal,
              min: 0,
              max: 60,
              divisions: 60,
              label: '${currentVal.toInt()}s',
              onChanged: (val) {
                ref.read(crossfadeDurationProvider.notifier).updateState(val);
              },
            ),
          ),
          Text('${currentVal.toInt()}s'),
        ],
      ),
    );
  }
}
