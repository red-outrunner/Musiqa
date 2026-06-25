import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:musiqa/screens/tabs/queue_tab.dart';
import 'package:musiqa/screens/tabs/playlists_tab.dart';
import 'package:musiqa/screens/tabs/albums_tab.dart';
import 'package:musiqa/screens/tabs/artists_tab.dart';
import 'package:musiqa/screens/player_screen.dart';
import 'package:musiqa/screens/settings_screen.dart';
import 'package:musiqa/widgets/mini_player.dart';
class MainLayout extends ConsumerStatefulWidget {
  const MainLayout({super.key});

  @override
  ConsumerState<MainLayout> createState() => _MainLayoutState();
}

class _MainLayoutState extends ConsumerState<MainLayout> {
  int _currentIndex = 0;

  final List<Widget> _tabs = [
    const QueueTab(),
    const NowPlayingTab(),
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
            icon: const Icon(Icons.settings),
            tooltip: 'Settings',
            onPressed: () {
              Navigator.of(context).push(
                MaterialPageRoute(builder: (context) => const SettingsScreen()),
              );
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
            // Hide the mini player on the Now Playing tab so the full-size
            // album art has room to breathe.
            if (_currentIndex != 1) const MiniPlayer(),
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
          NavigationDestination(icon: Icon(Icons.play_circle_outline), label: 'Now'),
          NavigationDestination(icon: Icon(Icons.playlist_play), label: 'Playlists'),
          NavigationDestination(icon: Icon(Icons.album), label: 'Albums'),
          NavigationDestination(icon: Icon(Icons.person), label: 'Artists'),
        ],
      ),
    );
  }
}
