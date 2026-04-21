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
            const SizedBox(height: 16),
            _buildSeekBar(context, ref),
            const SizedBox(height: 16),
            _buildPlaybackControls(context, ref, isPlaying),
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
  Widget _buildSeekBar(BuildContext context, WidgetRef ref) {
    final positionAsync = ref.watch(positionProvider);
    final durationAsync = ref.watch(durationProvider);
    final player = ref.read(audioPlayerProvider);

    final position = positionAsync.value ?? Duration.zero;
    final duration = durationAsync.value ?? Duration.zero;
    
    double maxVal = duration.inMilliseconds.toDouble();
    double currentVal = position.inMilliseconds.toDouble();
    if (currentVal > maxVal) currentVal = maxVal;
    if (maxVal == 0) maxVal = 1;

    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 24.0),
      child: Column(
        children: [
          Slider(
            value: currentVal,
            max: maxVal,
            onChanged: (val) {
              player.seek(Duration(milliseconds: val.toInt()));
            },
          ),
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Text(_formatDuration(position)),
              Text(_formatDuration(duration)),
            ],
          ),
        ],
      ),
    );
  }

  Widget _buildPlaybackControls(BuildContext context, WidgetRef ref, bool isPlaying) {
    final player = ref.read(audioPlayerProvider);
    return Row(
      mainAxisAlignment: MainAxisAlignment.center,
      children: [
        IconButton(
          iconSize: 48,
          icon: const Icon(Icons.skip_previous),
          onPressed: () {
            if (player.hasPrevious) player.seekToPrevious();
          },
        ),
        const SizedBox(width: 16),
        IconButton(
          iconSize: 64,
          icon: Icon(isPlaying ? Icons.pause_circle_filled : Icons.play_circle_fill),
          onPressed: () {
            if (isPlaying) {
              player.pause();
            } else {
              player.play();
            }
          },
        ),
        const SizedBox(width: 16),
        IconButton(
          iconSize: 48,
          icon: const Icon(Icons.skip_next),
          onPressed: () {
            if (player.hasNext) player.seekToNext();
          },
        ),
      ],
    );
  }

  String _formatDuration(Duration d) {
    String twoDigits(int n) => n.toString().padLeft(2, "0");
    String twoDigitMinutes = twoDigits(d.inMinutes.remainder(60));
    String twoDigitSeconds = twoDigits(d.inSeconds.remainder(60));
    return "$twoDigitMinutes:$twoDigitSeconds";
  }
}
