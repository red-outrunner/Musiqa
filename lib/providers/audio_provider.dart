import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:just_audio/just_audio.dart';
import 'package:on_audio_query/on_audio_query.dart';
class CrossfadeDurationNotifier extends Notifier<double> {
  @override
  double build() => 3.0;

  void updateState(double value) {
    state = value;
  }
}

class IsPlayingNotifier extends Notifier<bool> {
  @override
  bool build() => false;

  void updateState(bool value) {
    state = value;
  }
}

class CurrentSongTitleNotifier extends Notifier<String> {
  @override
  String build() => 'Not Playing';

  void updateState(String value) {
    state = value;
  }
}

final crossfadeDurationProvider = NotifierProvider<CrossfadeDurationNotifier, double>(CrossfadeDurationNotifier.new);
final isPlayingProvider = NotifierProvider<IsPlayingNotifier, bool>(IsPlayingNotifier.new);
final currentSongTitleProvider = NotifierProvider<CurrentSongTitleNotifier, String>(CurrentSongTitleNotifier.new);

final audioPlayerProvider = Provider<AudioPlayer>((ref) {
  final player = AudioPlayer();
  
  // Listen to player state changes to update our Notifiers
  player.playingStream.listen((playing) {
    ref.read(isPlayingProvider.notifier).updateState(playing);
  });

  player.sequenceStateStream.listen((sequenceState) {
    if (sequenceState == null) return;
    final currentItem = sequenceState.currentSource;
    if (currentItem != null && currentItem.tag is SongModel) {
      final song = currentItem.tag as SongModel;
      ref.read(currentSongTitleProvider.notifier).updateState(song.title);
    }
  });
  
  ref.onDispose(() => player.dispose());
  return player;
});

final positionProvider = StreamProvider<Duration>((ref) {
  return ref.watch(audioPlayerProvider).positionStream;
});

final durationProvider = StreamProvider<Duration?>((ref) {
  return ref.watch(audioPlayerProvider).durationStream;
});

class AudioQueueManager {
  static Future<void> playQueue(AudioPlayer player, WidgetRef ref, List<SongModel> songs, int initialIndex) async {
    final validSongs = songs.where((s) => s.uri != null).toList();
    if (validSongs.isEmpty) return;

    // Find the actual index in the filtered list
    int safeIndex = 0;
    if (initialIndex >= 0 && initialIndex < songs.length) {
      final targetSongId = songs[initialIndex].id;
      safeIndex = validSongs.indexWhere((s) => s.id == targetSongId);
      if (safeIndex == -1) safeIndex = 0;
    }

    final audioSources = validSongs.map((s) {
      return AudioSource.uri(
        Uri.parse(s.uri!),
        tag: s,
      );
    }).toList();
    
    final playlist = ConcatenatingAudioSource(children: audioSources);
    await player.setAudioSource(playlist, initialIndex: safeIndex);
    player.play();
  }
}
