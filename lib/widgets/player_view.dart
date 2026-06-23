import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:musiqa/providers/audio_provider.dart';
import 'package:on_audio_query/on_audio_query.dart';

/// Full now-playing UI without its own Scaffold, so it can be embedded both in
/// the Now Playing tab and inside a modal bottom sheet.
class PlayerView extends ConsumerWidget {
  const PlayerView({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final song = ref.watch(currentSongProvider);
    final isPlaying = ref.watch(isPlayingProvider);
    final crossfadeDuration = ref.watch(crossfadeDurationProvider);

    return Column(
      children: [
        Expanded(
          child: Padding(
            padding: const EdgeInsets.all(32.0),
            child: Center(
              child: ClipRRect(
                borderRadius: BorderRadius.circular(16),
                child: song == null
                    ? const Icon(Icons.album, size: 200)
                    : QueryArtworkWidget(
                        id: song.id,
                        type: ArtworkType.AUDIO,
                        artworkWidth: 280,
                        artworkHeight: 280,
                        artworkBorder: BorderRadius.circular(16),
                        nullArtworkWidget: const Icon(Icons.album, size: 200),
                      ),
              ),
            ),
          ),
        ),
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 24.0),
          child: Text(
            song?.title ?? 'Not Playing',
            style: Theme.of(context).textTheme.headlineSmall,
            textAlign: TextAlign.center,
            maxLines: 2,
            overflow: TextOverflow.ellipsis,
          ),
        ),
        const SizedBox(height: 4),
        Text(
          song?.artist ?? '',
          style: Theme.of(context).textTheme.titleMedium,
          maxLines: 1,
          overflow: TextOverflow.ellipsis,
        ),
        const SizedBox(height: 16),
        _buildSeekBar(context, ref),
        const SizedBox(height: 8),
        _buildPlaybackControls(context, ref, isPlaying),
        const SizedBox(height: 24),
        _buildCrossfadeControls(context, ref, crossfadeDuration),
        const SizedBox(height: 16),
      ],
    );
  }

  Widget _buildCrossfadeControls(BuildContext context, WidgetRef ref, double currentVal) {
    return Container(
      margin: const EdgeInsets.symmetric(horizontal: 16.0),
      padding: const EdgeInsets.symmetric(horizontal: 16.0, vertical: 8.0),
      decoration: BoxDecoration(
        color: Theme.of(context).colorScheme.surfaceContainerHighest,
        borderRadius: BorderRadius.circular(16),
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
              max: 12,
              divisions: 12,
              label: '${currentVal.toInt()}s',
              onChanged: (val) {
                ref.read(crossfadeDurationProvider.notifier).updateState(val);
              },
            ),
          ),
          Text(currentVal == 0 ? 'Off' : '${currentVal.toInt()}s'),
        ],
      ),
    );
  }

  Widget _buildSeekBar(BuildContext context, WidgetRef ref) {
    final positionAsync = ref.watch(positionProvider);
    final durationAsync = ref.watch(durationProvider);
    final controller = ref.read(playbackControllerProvider.notifier);

    final position = positionAsync.value ?? Duration.zero;
    final duration = durationAsync.value ?? Duration.zero;

    double maxVal = duration.inMilliseconds.toDouble();
    double currentVal = position.inMilliseconds.toDouble();
    if (maxVal <= 0) maxVal = 1;
    if (currentVal > maxVal) currentVal = maxVal;

    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 24.0),
      child: Column(
        children: [
          Slider(
            value: currentVal,
            max: maxVal,
            onChanged: (val) {
              controller.seek(Duration(milliseconds: val.toInt()));
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
    final controller = ref.read(playbackControllerProvider.notifier);
    return Row(
      mainAxisAlignment: MainAxisAlignment.center,
      children: [
        IconButton(
          iconSize: 48,
          icon: const Icon(Icons.skip_previous),
          onPressed: controller.previous,
        ),
        const SizedBox(width: 16),
        IconButton(
          iconSize: 64,
          icon: Icon(isPlaying ? Icons.pause_circle_filled : Icons.play_circle_fill),
          onPressed: controller.togglePlayPause,
        ),
        const SizedBox(width: 16),
        IconButton(
          iconSize: 48,
          icon: const Icon(Icons.skip_next),
          onPressed: controller.next,
        ),
      ],
    );
  }

  String _formatDuration(Duration d) {
    String twoDigits(int n) => n.toString().padLeft(2, "0");
    final twoDigitMinutes = twoDigits(d.inMinutes.remainder(60));
    final twoDigitSeconds = twoDigits(d.inSeconds.remainder(60));
    return "$twoDigitMinutes:$twoDigitSeconds";
  }
}
