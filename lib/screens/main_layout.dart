import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:musiqa/screens/tabs/queue_tab.dart';
import 'package:musiqa/screens/tabs/playlists_tab.dart';
import 'package:musiqa/screens/tabs/albums_tab.dart';
import 'package:musiqa/screens/tabs/artists_tab.dart';
import 'package:musiqa/widgets/mini_player.dart';
import 'package:musiqa/providers/audio_provider.dart';
import 'package:musiqa/providers/metadata_provider.dart';
import 'package:musiqa/providers/audio_query_provider.dart';
class MainLayout extends ConsumerStatefulWidget {
  const MainLayout({super.key});

  @override
  ConsumerState<MainLayout> createState() => _MainLayoutState();
}

class _MainLayoutState extends ConsumerState<MainLayout> {
  int _currentIndex = 0;

  final List<Widget> _tabs = [
    const QueueTab(),
    const PlaylistsTab(),
    const AlbumsTab(),
    const ArtistsTab(),
  ];

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Musiqa'),
        actions: [
          IconButton(
            icon: const Icon(Icons.sync),
            tooltip: 'Scan BPM & Keys',
            onPressed: () async {
              ScaffoldMessenger.of(context).showSnackBar(
                const SnackBar(content: Text('Scanning library for BPM and Keys...')),
              );
              final metadataProviderInstance = ref.read(metadataProvider);
              try {
                final songs = await ref.read(songsProvider.future);
                await metadataProviderInstance.scanAllSongs(songs);
                if (context.mounted) {
                  ScaffoldMessenger.of(context).showSnackBar(
                    const SnackBar(content: Text('Scan complete!')),
                  );
                }
                ref.invalidate(metadataProvider);
              } catch (e) {
                if (context.mounted) {
                  ScaffoldMessenger.of(context).showSnackBar(
                    SnackBar(content: Text('Error scanning: $e')),
                  );
                }
              }
            },
          ),
        ],
      ),
      body: SafeArea(
        child: Column(
          children: [
            Expanded(
              child: IndexedStack(
                index: _currentIndex,
                children: _tabs,
              ),
            ),
            const MiniPlayer(),
          ],
        ),
      ),
      bottomNavigationBar: NavigationBar(
        selectedIndex: _currentIndex,
        onDestinationSelected: (idx) {
          setState(() {
            _currentIndex = idx;
          });
        },
        destinations: const [
          NavigationDestination(icon: Icon(Icons.queue_music), label: 'Queue'),
          NavigationDestination(icon: Icon(Icons.playlist_play), label: 'Playlists'),
          NavigationDestination(icon: Icon(Icons.album), label: 'Albums'),
          NavigationDestination(icon: Icon(Icons.person), label: 'Artists'),
        ],
      ),
    );
  }
}
