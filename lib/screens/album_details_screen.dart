import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:musiqa/providers/audio_query_provider.dart';
import 'package:musiqa/providers/audio_provider.dart';
import 'package:musiqa/providers/metadata_provider.dart';
import 'package:on_audio_query/on_audio_query.dart';

class AlbumDetailsScreen extends ConsumerWidget {
  final AlbumModel album;

  const AlbumDetailsScreen({super.key, required this.album});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final audioQuery = ref.watch(audioQueryProvider);
    final metadata = ref.watch(metadataProvider);

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
          
          List<SongModel> songs = snapshot.data ?? [];
          if (songs.isEmpty) {
            return const Center(child: Text("No songs found in this album."));
          }

          // Sort by track number if available, else by title alphabetically
          songs.sort((a, b) {
            final trackA = a.track ?? 0;
            final trackB = b.track ?? 0;
            if (trackA > 0 && trackB > 0) {
              return trackA.compareTo(trackB);
            }
            return a.title.compareTo(b.title);
          });

          return ListView.builder(
            itemCount: songs.length,
            itemBuilder: (context, index) {
              final song = songs[index];
              final bpm = metadata.getBpm(song.id);
              final key = metadata.getKey(song.id);
              final trackNumber = song.track != null && song.track! > 0 ? song.track.toString() : (index + 1).toString();

              return ListTile(
                leading: SizedBox(
                  width: 30,
                  child: Center(child: Text(trackNumber, style: const TextStyle(fontWeight: FontWeight.bold))),
                ),
                title: Text(song.title, maxLines: 1, overflow: TextOverflow.ellipsis),
                subtitle: Text("${song.artist ?? "Unknown artist"} • $bpm BPM • Key: $key", maxLines: 1, overflow: TextOverflow.ellipsis),
                trailing: Text("${((song.duration ?? 0) / 60000).toStringAsFixed(2)} min"),
                onTap: () async {
                  final player = ref.read(audioPlayerProvider);
                  try {
                    await AudioQueueManager.playQueue(player, ref, songs, index);
                  } catch (e) {
                    debugPrint("Error playing audio: $e");
                  }
                },
              );
            },
          );
        },
      ),
    );
  }
}
