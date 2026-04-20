import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:just_audio/just_audio.dart';

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
  
  ref.onDispose(() => player.dispose());
  return player;
});
