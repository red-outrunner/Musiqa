import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:musiqa/providers/audio_query_provider.dart';
import 'package:musiqa/providers/audio_provider.dart';
import 'package:musiqa/providers/metadata_provider.dart';
import 'package:musiqa/widgets/song_tile.dart';
import 'package:on_audio_query/on_audio_query.dart';

class AlbumDetailsScreen extends ConsumerWidget {
  final AlbumModel album;

  const AlbumDetailsScreen({super.key, required this.album});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final audioQuery = ref.watch(audioQueryProvider);
    final controller = ref.read(playbackControllerProvider.notifier);

    return Scaffold(
      appBar: AppBar(
        title: Text(album.album),
      ),
      body: FutureBuilder<List<SongModel>>(
        future: audioQuery.queryAudiosFrom(
          AudiosFromType.ALBUM_ID,
          album.id,
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

          final hidden = ref.watch(hiddenSongsProvider);
          List<SongModel> songs =
              (snapshot.data ?? []).where((s) => !hidden.contains(s.id)).toList();
          if (songs.isEmpty) {
            return const Center(child: Text("No songs found in this album."));
          }

          // Sort by track number if available, else by title alphabetically.
          songs.sort((a, b) {
            final trackA = a.track ?? 0;
            final trackB = b.track ?? 0;
            if (trackA > 0 && trackB > 0) {
              return trackA.compareTo(trackB);
            }
            return a.title.compareTo(b.title);
          });

          return Column(
            children: [
              _AlbumHeader(album: album),
              const Divider(height: 1),
              Expanded(
                child: ListView.builder(
                  itemCount: songs.length,
                  itemBuilder: (context, index) {
                    final song = songs[index];
                    final trackNumber = song.track != null && song.track! > 0
                        ? song.track.toString()
                        : (index + 1).toString();

                    return SongTile(
                      song: song,
                      leading: SizedBox(
                        width: 40,
                        child: Center(
                          child: Text(trackNumber,
                              style: const TextStyle(fontWeight: FontWeight.bold)),
                        ),
                      ),
                      onTap: () => controller.playQueue(songs, index),
                    );
                  },
                ),
              ),
            ],
          );
        },
      ),
    );
  }
}

class _AlbumHeader extends StatelessWidget {
  final AlbumModel album;
  const _AlbumHeader({required this.album});

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.all(16),
      child: Row(
        children: [
          ClipRRect(
            borderRadius: BorderRadius.circular(8),
            child: QueryArtworkWidget(
              id: album.id,
              type: ArtworkType.ALBUM,
              artworkWidth: 100,
              artworkHeight: 100,
              nullArtworkWidget: const Icon(Icons.album, size: 100),
            ),
          ),
          const SizedBox(width: 16),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(album.album,
                    style: Theme.of(context).textTheme.titleLarge,
                    maxLines: 2,
                    overflow: TextOverflow.ellipsis),
                const SizedBox(height: 4),
                Text(album.artist ?? 'Unknown artist',
                    style: Theme.of(context).textTheme.bodyMedium,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis),
                Text("${album.numOfSongs} songs"),
              ],
            ),
          ),
        ],
      ),
    );
  }
}
