import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:musiqa/providers/audio_query_provider.dart';
import 'package:on_audio_query/on_audio_query.dart';
import 'package:musiqa/screens/album_details_screen.dart';

class AlbumsTab extends ConsumerWidget {
  const AlbumsTab({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final albumsAsyncValue = ref.watch(albumsProvider);

    return albumsAsyncValue.when(
      data: (albums) {
        if (albums.isEmpty) {
          return const Center(child: Text("No albums found"));
        }
        return ListView.builder(
          itemCount: albums.length,
          itemBuilder: (context, index) {
            final album = albums[index];
            return ListTile(
              leading: QueryArtworkWidget(
                id: album.id,
                type: ArtworkType.ALBUM,
                nullArtworkWidget: const Icon(Icons.album, size: 50),
              ),
              title: Text(album.album),
              subtitle: Text("${album.numOfSongs} Songs"),
              onTap: () {
                Navigator.of(context).push(
                  MaterialPageRoute(
                    builder: (context) => AlbumDetailsScreen(album: album),
                  ),
                );
              },
            );
          },
        );
      },
      loading: () => const Center(child: CircularProgressIndicator()),
      error: (err, stack) => Center(child: Text('Error: $err')),
    );
  }
}
