import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:on_audio_query/on_audio_query.dart';
import 'package:permission_handler/permission_handler.dart';

final audioQueryProvider = Provider((ref) => OnAudioQuery());

final songsProvider = FutureProvider<List<SongModel>>((ref) async {
  final audioQuery = ref.read(audioQueryProvider);
  final storageReady = await Permission.storage.request().isGranted || await Permission.audio.request().isGranted;
  if (storageReady) {
    return await audioQuery.querySongs(
       sortType: null,
       orderType: OrderType.ASC_OR_SMALLER,
       uriType: UriType.EXTERNAL,
       ignoreCase: true,
    );
  }
  return [];
});

final albumsProvider = FutureProvider<List<AlbumModel>>((ref) async {
  final audioQuery = ref.read(audioQueryProvider);
  final storageReady = await Permission.storage.request().isGranted || await Permission.audio.request().isGranted;
  if (storageReady) {
    return await audioQuery.queryAlbums(
       sortType: null,
       orderType: OrderType.ASC_OR_SMALLER,
       uriType: UriType.EXTERNAL,
       ignoreCase: true,
    );
  }
  return [];
});

final artistsProvider = FutureProvider<List<ArtistModel>>((ref) async {
  final audioQuery = ref.read(audioQueryProvider);
  final storageReady = await Permission.storage.request().isGranted || await Permission.audio.request().isGranted;
  if (storageReady) {
    return await audioQuery.queryArtists(
       sortType: null,
       orderType: OrderType.ASC_OR_SMALLER,
       uriType: UriType.EXTERNAL,
       ignoreCase: true,
    );
  }
  return [];
});
