import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:musiqa/providers/audio_query_provider.dart';
import 'package:musiqa/screens/song_list_screen.dart';
import 'package:on_audio_query/on_audio_query.dart';

class PlaylistsTab extends ConsumerWidget {
  const PlaylistsTab({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final audioQuery = ref.watch(audioQueryProvider);
    final playlistsAsync = ref.watch(playlistsProvider);

    return ListView(
      children: [
        // Primary, always-present list of everything on the device.
        ListTile(
          leading: const CircleAvatar(child: Icon(Icons.library_music)),
          title: const Text('All Songs',
              style: TextStyle(fontWeight: FontWeight.bold)),
          subtitle: const Text('Every track on this device'),
          onTap: () {
            Navigator.of(context).push(
              MaterialPageRoute(
                builder: (context) => SongListScreen(
                  title: 'All Songs',
                  songsFuture: ref.read(songsProvider.future),
                ),
              ),
            );
          },
        ),
        const Divider(),
        ...playlistsAsync.when(
          data: (playlists) => playlists
              .map(
                (playlist) => ListTile(
                  leading: const Icon(Icons.playlist_play, size: 40),
                  title: Text(playlist.playlist),
                  subtitle: Text("${playlist.numOfSongs} Songs"),
                  onTap: () {
                    Navigator.of(context).push(
                      MaterialPageRoute(
                        builder: (context) => SongListScreen(
                          title: playlist.playlist,
                          songsFuture: audioQuery.queryAudiosFrom(
                            AudiosFromType.PLAYLIST,
                            playlist.id,
                          ),
                        ),
                      ),
                    );
                  },
                ),
              )
              .toList(),
          loading: () => const [
            Padding(
              padding: EdgeInsets.all(16),
              child: Center(child: CircularProgressIndicator()),
            ),
          ],
          error: (err, _) => [
            Padding(padding: const EdgeInsets.all(16), child: Text('Error: $err')),
          ],
        ),
      ],
    );
  }
}
