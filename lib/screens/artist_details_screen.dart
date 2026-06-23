import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:musiqa/providers/audio_query_provider.dart';
import 'package:musiqa/providers/audio_provider.dart';
import 'package:musiqa/screens/album_details_screen.dart';
import 'package:musiqa/widgets/song_tile.dart';
import 'package:on_audio_query/on_audio_query.dart';

/// Shows the artist's albums in a horizontal strip, then a divider, then every
/// song by the artist listed alphabetically.
class ArtistDetailsScreen extends ConsumerWidget {
  final ArtistModel artist;

  const ArtistDetailsScreen({super.key, required this.artist});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final audioQuery = ref.watch(audioQueryProvider);
    final albumsAsync = ref.watch(albumsProvider);
    final controller = ref.read(playbackControllerProvider.notifier);

    final artistAlbums = albumsAsync.maybeWhen(
      data: (albums) => albums.where((a) => a.artistId == artist.id).toList(),
      orElse: () => <AlbumModel>[],
    );

    return Scaffold(
      appBar: AppBar(title: Text(artist.artist)),
      body: FutureBuilder<List<SongModel>>(
        future: audioQuery.queryAudiosFrom(
          AudiosFromType.ARTIST_ID,
          artist.id,
          sortType: null,
          orderType: OrderType.ASC_OR_SMALLER,
          ignoreCase: true,
        ),
        builder: (context, snapshot) {
          if (snapshot.connectionState == ConnectionState.waiting) {
            return const Center(child: CircularProgressIndicator());
          }
          if (snapshot.hasError) {
            return Center(child: Text('Error: ${snapshot.error}'));
          }

          final songs = snapshot.data ?? [];
          songs.sort((a, b) =>
              a.title.toLowerCase().compareTo(b.title.toLowerCase()));

          return ListView(
            children: [
              ListTile(
                leading: const Icon(Icons.person, size: 40),
                title: Text(artist.artist,
                    style: Theme.of(context).textTheme.titleLarge),
                subtitle: Text(
                    "${artist.numberOfAlbums ?? artistAlbums.length} albums • ${artist.numberOfTracks ?? songs.length} tracks"),
              ),
              if (artistAlbums.isNotEmpty) ...[
                const Padding(
                  padding: EdgeInsets.fromLTRB(16, 8, 16, 4),
                  child: Text('Albums',
                      style: TextStyle(fontWeight: FontWeight.bold)),
                ),
                SizedBox(
                  height: 180,
                  child: ListView.builder(
                    scrollDirection: Axis.horizontal,
                    padding: const EdgeInsets.symmetric(horizontal: 8),
                    itemCount: artistAlbums.length,
                    itemBuilder: (context, index) {
                      final album = artistAlbums[index];
                      return _AlbumCard(album: album);
                    },
                  ),
                ),
              ],
              const Divider(),
              const Padding(
                padding: EdgeInsets.fromLTRB(16, 4, 16, 4),
                child: Text('All songs',
                    style: TextStyle(fontWeight: FontWeight.bold)),
              ),
              ...songs.asMap().entries.map(
                    (entry) => SongTile(
                      song: entry.value,
                      onTap: () => controller.playQueue(songs, entry.key),
                    ),
                  ),
            ],
          );
        },
      ),
    );
  }
}

class _AlbumCard extends StatelessWidget {
  final AlbumModel album;
  const _AlbumCard({required this.album});

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTap: () {
        Navigator.of(context).push(
          MaterialPageRoute(
            builder: (context) => AlbumDetailsScreen(album: album),
          ),
        );
      },
      child: SizedBox(
        width: 130,
        child: Padding(
          padding: const EdgeInsets.all(8),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              ClipRRect(
                borderRadius: BorderRadius.circular(8),
                child: QueryArtworkWidget(
                  id: album.id,
                  type: ArtworkType.ALBUM,
                  artworkWidth: 114,
                  artworkHeight: 114,
                  nullArtworkWidget: const Icon(Icons.album, size: 114),
                ),
              ),
              const SizedBox(height: 4),
              Text(album.album,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: const TextStyle(fontWeight: FontWeight.w600)),
              Text("${album.numOfSongs} songs",
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: Theme.of(context).textTheme.bodySmall),
            ],
          ),
        ),
      ),
    );
  }
}
