import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:musiqa/widgets/player_view.dart';

/// Now-playing screen used as a modal bottom sheet from the mini player.
class PlayerScreen extends ConsumerWidget {
  const PlayerScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    return Scaffold(
      appBar: AppBar(title: const Text('Now Playing')),
      body: const SafeArea(child: PlayerView()),
    );
  }
}

/// Now-playing as a top-level tab (no nested Scaffold; MainLayout supplies it).
class NowPlayingTab extends StatelessWidget {
  const NowPlayingTab({super.key});

  @override
  Widget build(BuildContext context) {
    return const PlayerView();
  }
}
