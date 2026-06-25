import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:musiqa/providers/audio_query_provider.dart';
import 'package:musiqa/providers/metadata_provider.dart';

class SettingsScreen extends ConsumerStatefulWidget {
  const SettingsScreen({super.key});

  @override
  ConsumerState<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends ConsumerState<SettingsScreen> {
  bool _scanning = false;

  Future<void> _scan({required bool bpm, required bool key}) async {
    if (_scanning) return;
    setState(() => _scanning = true);
    final label = bpm ? 'BPM' : 'Keys';
    final messenger = ScaffoldMessenger.of(context);
    messenger.showSnackBar(SnackBar(content: Text('Scanning library for $label...')));
    try {
      final songs = await ref.read(songsProvider.future);
      await ref
          .read(metadataProvider)
          .scanAllSongs(songs, scanBpm: bpm, scanKey: key);
      ref.invalidate(metadataProvider);
      messenger.showSnackBar(SnackBar(content: Text('$label scan complete!')));
    } catch (e) {
      messenger.showSnackBar(SnackBar(content: Text('Error scanning $label: $e')));
    } finally {
      if (mounted) setState(() => _scanning = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Settings')),
      body: ListView(
        children: [
          if (_scanning) const LinearProgressIndicator(),
          const Padding(
            padding: EdgeInsets.fromLTRB(16, 16, 16, 4),
            child: Text('Library', style: TextStyle(fontWeight: FontWeight.bold)),
          ),
          ListTile(
            leading: const Icon(Icons.speed),
            title: const Text('Scan BPM'),
            subtitle: const Text('Read BPM tags from every track'),
            enabled: !_scanning,
            onTap: () => _scan(bpm: true, key: false),
          ),
          ListTile(
            leading: const Icon(Icons.piano),
            title: const Text('Scan Keys'),
            subtitle: const Text('Read musical key tags from every track'),
            enabled: !_scanning,
            onTap: () => _scan(bpm: false, key: true),
          ),
        ],
      ),
    );
  }
}
