import 'dart:io';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:musiqa/providers/audio_provider.dart';
import 'package:musiqa/providers/audio_query_provider.dart';
import 'package:musiqa/providers/metadata_provider.dart';
import 'package:on_audio_query/on_audio_query.dart';

/// Three-dot overflow menu shown next to every song. Offers song details,
/// play next, delete from device and hide from library. When [queueIndex] is
/// supplied the song lives in the active queue, so "play next" reorders the
/// queue and an extra "remove from queue" entry is shown.
class SongOptionsMenu extends ConsumerWidget {
  final SongModel song;
  final int? queueIndex;

  const SongOptionsMenu({super.key, required this.song, this.queueIndex});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final controller = ref.read(playbackControllerProvider.notifier);

    return PopupMenuButton<String>(
      icon: const Icon(Icons.more_vert),
      onSelected: (value) async {
        final messenger = ScaffoldMessenger.of(context);
        switch (value) {
          case 'details':
            await _showDetails(context, ref);
            break;
          case 'next':
            if (queueIndex != null) {
              controller.moveToNext(queueIndex!);
            } else {
              await controller.playNext(song);
            }
            messenger.showSnackBar(const SnackBar(content: Text('Playing next')));
            break;
          case 'remove':
            if (queueIndex != null) await controller.removeAt(queueIndex!);
            break;
          case 'delete':
            await _confirmDelete(context, ref, messenger);
            break;
          case 'hide':
            ref.read(hiddenSongsProvider.notifier).hide(song.id);
            messenger
                .showSnackBar(const SnackBar(content: Text('Hidden from library')));
            break;
        }
      },
      itemBuilder: (context) => [
        const PopupMenuItem(value: 'details', child: Text('Song details')),
        const PopupMenuItem(value: 'next', child: Text('Play next')),
        if (queueIndex != null)
          const PopupMenuItem(value: 'remove', child: Text('Remove from queue')),
        const PopupMenuItem(value: 'delete', child: Text('Delete')),
        const PopupMenuItem(value: 'hide', child: Text('Hide from library')),
      ],
    );
  }

  Future<void> _showDetails(BuildContext context, WidgetRef ref) async {
    final metadata = ref.read(metadataProvider);
    final bpm = metadata.getBpm(song.id);
    final key = metadata.getKey(song.id);

    String durationText() {
      final d = Duration(milliseconds: song.duration ?? 0);
      String two(int n) => n.toString().padLeft(2, '0');
      return '${two(d.inMinutes.remainder(60))}:${two(d.inSeconds.remainder(60))}';
    }

    await showDialog<void>(
      context: context,
      builder: (context) => AlertDialog(
        title: Text(song.title, maxLines: 2, overflow: TextOverflow.ellipsis),
        content: SingleChildScrollView(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              _DetailRow(label: 'Artist', value: song.artist ?? 'Unknown'),
              _DetailRow(label: 'Album', value: song.album ?? 'Unknown'),
              _DetailRow(label: 'Duration', value: durationText()),
              _DetailRow(label: 'BPM', value: bpm),
              _DetailRow(label: 'Key', value: key),
              _DetailRow(label: 'Path', value: song.data),
            ],
          ),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(context).pop(),
            child: const Text('Close'),
          ),
        ],
      ),
    );
  }

  Future<void> _confirmDelete(
      BuildContext context, WidgetRef ref, ScaffoldMessengerState messenger) async {
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('Delete song?'),
        content: Text(
          'Permanently delete "${song.title}" from this device? '
          'This cannot be undone.',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(context).pop(false),
            child: const Text('Cancel'),
          ),
          TextButton(
            onPressed: () => Navigator.of(context).pop(true),
            child: const Text('Delete'),
          ),
        ],
      ),
    );
    if (confirmed != true) return;

    try {
      final file = File(song.data);
      if (await file.exists()) await file.delete();
      // Drop it from the active queue as well, if present.
      final controller = ref.read(playbackControllerProvider.notifier);
      final queue = ref.read(playbackControllerProvider).queue;
      final qIdx = queue.indexWhere((s) => s.id == song.id);
      if (qIdx != -1) await controller.removeAt(qIdx);
      ref.invalidate(songsProvider);
      ref.invalidate(albumsProvider);
      ref.invalidate(artistsProvider);
      messenger.showSnackBar(const SnackBar(content: Text('Deleted')));
    } catch (e) {
      messenger.showSnackBar(SnackBar(content: Text('Could not delete: $e')));
    }
  }
}

class _DetailRow extends StatelessWidget {
  final String label;
  final String value;
  const _DetailRow({required this.label, required this.value});

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 4),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          SizedBox(
            width: 70,
            child: Text(label,
                style: const TextStyle(fontWeight: FontWeight.bold)),
          ),
          Expanded(child: Text(value)),
        ],
      ),
    );
  }
}
