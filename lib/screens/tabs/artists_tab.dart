import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:musiqa/providers/audio_query_provider.dart';
import 'package:musiqa/screens/artist_details_screen.dart';

class ArtistsTab extends ConsumerWidget {
  const ArtistsTab({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final artistsAsyncValue = ref.watch(artistsProvider);

    return artistsAsyncValue.when(
      data: (artists) {
        if (artists.isEmpty) {
          return const Center(child: Text("No artists found"));
        }
        return ListView.builder(
          itemCount: artists.length,
          itemBuilder: (context, index) {
            final artist = artists[index];
            return ListTile(
              leading: const Icon(Icons.person, size: 50),
              title: Text(artist.artist),
              subtitle: Text("${artist.numberOfAlbums} Albums | ${artist.numberOfTracks} Tracks"),
              onTap: () {
                Navigator.of(context).push(
                  MaterialPageRoute(
                    builder: (context) => ArtistDetailsScreen(artist: artist),
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
