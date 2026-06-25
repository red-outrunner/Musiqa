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
    final crossfadeEnabled = ref.watch(crossfadeEnabledProvider);
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
        const SizedBox(height: 16),
        _buildCrossfadeControls(context, ref, crossfadeEnabled, crossfadeDuration),
        const SizedBox(height: 16),
      ],
    );
  }

  Widget _buildCrossfadeControls(
      BuildContext context, WidgetRef ref, bool enabled, double currentVal) {
    return Container(
      margin: const EdgeInsets.symmetric(horizontal: 16.0),
      padding: const EdgeInsets.symmetric(horizontal: 16.0, vertical: 4.0),
      decoration: BoxDecoration(
        color: Theme.of(context).colorScheme.surfaceContainerHighest,
        borderRadius: BorderRadius.circular(16),
      ),
      child: Column(
        children: [
          Row(
            children: [
              const Icon(Icons.compare_arrows, size: 20),
              const SizedBox(width: 8),
              const Text("Crossfade",
                  style: TextStyle(fontWeight: FontWeight.bold)),
              const Spacer(),
              Switch(
                value: enabled,
                onChanged: (val) =>
                    ref.read(crossfadeEnabledProvider.notifier).set(val),
              ),
            ],
          ),
          // Only let the length be tuned while crossfade is on.
          if (enabled)
            Row(
              children: [
                const SizedBox(width: 8),
                Expanded(
                  child: Slider(
                    value: currentVal,
                    min: 1,
                    max: 12,
                    divisions: 11,
                    label: '${currentVal.toInt()}s',
                    onChanged: (val) {
                      ref.read(crossfadeDurationProvider.notifier).updateState(val);
                    },
                  ),
                ),
                Text('${currentVal.toInt()}s'),
              ],
            ),
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
    final shuffle = ref.watch(shuffleProvider);
    final repeatMode = ref.watch(repeatModeProvider);
    final accent = Theme.of(context).colorScheme.primary;

    // Each repeat mode gets its own glyph; "off"/continuous is dimmed.
    final IconData repeatIcon;
    switch (repeatMode) {
      case RepeatPlayMode.one:
        repeatIcon = Icons.repeat_one;
        break;
      case RepeatPlayMode.queue:
        repeatIcon = Icons.repeat;
        break;
      case RepeatPlayMode.continuous:
        repeatIcon = Icons.trending_flat;
        break;
    }
    final repeatTooltip = switch (repeatMode) {
      RepeatPlayMode.continuous => 'Play continuously',
      RepeatPlayMode.queue => 'Repeat queue',
      RepeatPlayMode.one => 'Repeat song',
    };

    return Row(
      mainAxisAlignment: MainAxisAlignment.center,
      children: [
        IconButton(
          iconSize: 26,
          tooltip: 'Shuffle',
          color: shuffle ? accent : null,
          icon: const Icon(Icons.shuffle),
          onPressed: () => ref.read(shuffleProvider.notifier).toggle(),
        ),
        const SizedBox(width: 8),
        IconButton(
          iconSize: 44,
          icon: const Icon(Icons.skip_previous),
          onPressed: controller.previous,
        ),
        const SizedBox(width: 8),
        IconButton(
          iconSize: 64,
          icon: Icon(isPlaying ? Icons.pause_circle_filled : Icons.play_circle_fill),
          onPressed: controller.togglePlayPause,
        ),
        const SizedBox(width: 8),
        IconButton(
          iconSize: 44,
          icon: const Icon(Icons.skip_next),
          onPressed: controller.next,
        ),
        const SizedBox(width: 8),
        IconButton(
          iconSize: 26,
          tooltip: repeatTooltip,
          color: repeatMode == RepeatPlayMode.continuous ? null : accent,
          icon: Icon(repeatIcon),
          onPressed: () => ref.read(repeatModeProvider.notifier).cycle(),
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
