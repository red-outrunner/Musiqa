import 'dart:async';
import 'dart:math';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:just_audio/just_audio.dart';
import 'package:on_audio_query/on_audio_query.dart';

/// Whether crossfade between tracks is turned on. Toggled from the player.
class CrossfadeEnabledNotifier extends Notifier<bool> {
  @override
  bool build() => false;

  void toggle() => state = !state;
  void set(bool value) => state = value;
}

final crossfadeEnabledProvider =
    NotifierProvider<CrossfadeEnabledNotifier, bool>(CrossfadeEnabledNotifier.new);

/// User-configurable crossfade length in seconds (used when enabled).
class CrossfadeDurationNotifier extends Notifier<double> {
  @override
  double build() => 5.0;

  void updateState(double value) {
    state = value;
  }
}

final crossfadeDurationProvider =
    NotifierProvider<CrossfadeDurationNotifier, double>(CrossfadeDurationNotifier.new);

/// How playback advances at the end of a track.
/// [continuous] plays through the queue once and stops; [queue] loops the whole
/// queue; [one] repeats the current track.
enum RepeatPlayMode { continuous, queue, one }

class RepeatPlayModeNotifier extends Notifier<RepeatPlayMode> {
  @override
  RepeatPlayMode build() => RepeatPlayMode.continuous;

  /// Cycles continuous -> queue -> one -> continuous.
  void cycle() {
    state = RepeatPlayMode.values[(state.index + 1) % RepeatPlayMode.values.length];
  }
}

final repeatModeProvider =
    NotifierProvider<RepeatPlayModeNotifier, RepeatPlayMode>(RepeatPlayModeNotifier.new);

/// Whether the next track is chosen at random.
class ShuffleNotifier extends Notifier<bool> {
  @override
  bool build() => false;

  void toggle() => state = !state;
}

final shuffleProvider = NotifierProvider<ShuffleNotifier, bool>(ShuffleNotifier.new);

/// Immutable snapshot of the playback queue exposed to the UI.
class PlaybackState {
  final List<SongModel> queue;
  final int currentIndex;
  final bool isPlaying;

  const PlaybackState({
    this.queue = const [],
    this.currentIndex = -1,
    this.isPlaying = false,
  });

  SongModel? get currentSong =>
      (currentIndex >= 0 && currentIndex < queue.length) ? queue[currentIndex] : null;

  PlaybackState copyWith({
    List<SongModel>? queue,
    int? currentIndex,
    bool? isPlaying,
  }) {
    return PlaybackState(
      queue: queue ?? this.queue,
      currentIndex: currentIndex ?? this.currentIndex,
      isPlaying: isPlaying ?? this.isPlaying,
    );
  }
}

final playbackControllerProvider =
    NotifierProvider<PlaybackController, PlaybackState>(PlaybackController.new);

/// Drives playback through two [AudioPlayer]s so consecutive tracks can be
/// crossfaded by ramping the outgoing player down while the incoming one
/// ramps up. just_audio has no native crossfade, hence the dual-player setup.
class PlaybackController extends Notifier<PlaybackState> {
  final List<AudioPlayer> _players = [];
  final Random _rng = Random();
  int _activeIdx = 0;
  bool _isCrossfading = false;
  int? _crossfadeTarget;
  Timer? _fadeTimer;

  final _positionCtrl = StreamController<Duration>.broadcast();
  final _durationCtrl = StreamController<Duration?>.broadcast();

  StreamSubscription<Duration>? _posSub;
  StreamSubscription<Duration?>? _durSub;
  StreamSubscription<bool>? _playSub;
  StreamSubscription<ProcessingState>? _procSub;

  AudioPlayer get _active => _players[_activeIdx];
  AudioPlayer get _idle => _players[1 - _activeIdx];

  Stream<Duration> get positionStream => _positionCtrl.stream;
  Stream<Duration?> get durationStream => _durationCtrl.stream;

  bool get hasNext =>
      state.currentIndex >= 0 && state.currentIndex + 1 < state.queue.length;
  bool get hasPrevious => state.currentIndex > 0;

  @override
  PlaybackState build() {
    _players.add(AudioPlayer());
    _players.add(AudioPlayer());
    ref.onDispose(_dispose);
    return const PlaybackState();
  }

  // ---------------------------------------------------------------- public API

  /// Replaces the queue with [songs] and starts playing from [initialIndex].
  Future<void> playQueue(List<SongModel> songs, int initialIndex) async {
    final valid = songs.where((s) => s.uri != null).toList();
    if (valid.isEmpty) return;

    int index = 0;
    if (initialIndex >= 0 && initialIndex < songs.length) {
      final targetId = songs[initialIndex].id;
      index = valid.indexWhere((s) => s.id == targetId);
      if (index == -1) index = 0;
    }

    state = state.copyWith(queue: valid, currentIndex: index, isPlaying: true);
    await _loadAndPlay(_active, valid[index]);
    _bindStreams();
  }

  /// Jumps to an existing queue position (used when tapping a queue item).
  Future<void> jumpTo(int index) async {
    if (index < 0 || index >= state.queue.length) return;
    state = state.copyWith(currentIndex: index, isPlaying: true);
    await _loadAndPlay(_active, state.queue[index]);
  }

  Future<void> togglePlayPause() async {
    if (_active.playing) {
      await _active.pause();
    } else {
      await _active.play();
    }
  }

  Future<void> next() async {
    final nextIndex = _computeNextIndex();
    if (nextIndex == null) return;
    await _goToIndex(nextIndex);
  }

  /// Resolves the index that should play after the current track, taking
  /// shuffle and repeat mode into account. Returns null when playback should
  /// stop (end of queue with no repeat).
  int? _computeNextIndex() {
    final queue = state.queue;
    if (queue.isEmpty) return null;

    if (ref.read(shuffleProvider) && queue.length > 1) {
      int idx;
      do {
        idx = _rng.nextInt(queue.length);
      } while (idx == state.currentIndex);
      return idx;
    }

    if (hasNext) return state.currentIndex + 1;
    if (ref.read(repeatModeProvider) == RepeatPlayMode.queue) return 0;
    return null;
  }

  Future<void> _goToIndex(int index) async {
    if (index < 0 || index >= state.queue.length) return;
    state = state.copyWith(currentIndex: index, isPlaying: true);
    await _loadAndPlay(_active, state.queue[index]);
  }

  Future<void> previous() async {
    // Restart current track if we're already at the start of the queue.
    if (!hasPrevious) {
      await _active.seek(Duration.zero);
      return;
    }
    final prevIndex = state.currentIndex - 1;
    state = state.copyWith(currentIndex: prevIndex, isPlaying: true);
    await _loadAndPlay(_active, state.queue[prevIndex]);
  }

  Future<void> seek(Duration position) => _active.seek(position);

  /// Reorders the queue while keeping the currently playing song selected.
  void reorder(int oldIndex, int newIndex) {
    final list = List<SongModel>.from(state.queue);
    if (newIndex > oldIndex) newIndex -= 1;
    if (oldIndex < 0 || oldIndex >= list.length) return;
    final current = state.currentSong;
    final item = list.removeAt(oldIndex);
    list.insert(newIndex.clamp(0, list.length), item);
    final newCurrent =
        current != null ? list.indexWhere((s) => s.id == current.id) : state.currentIndex;
    state = state.copyWith(queue: list, currentIndex: newCurrent);
  }

  /// Inserts a song from the library to play right after the current track.
  /// Starts a fresh queue if nothing is playing.
  Future<void> playNext(SongModel song) async {
    if (song.uri == null) return;
    if (state.queue.isEmpty) {
      await playQueue([song], 0);
      return;
    }
    final list = List<SongModel>.from(state.queue);
    final insertAt = (state.currentIndex + 1).clamp(0, list.length);
    list.insert(insertAt, song);
    state = state.copyWith(queue: list);
  }

  /// Moves an already-queued item to play right after the current track.
  void moveToNext(int index) {
    if (index < 0 || index >= state.queue.length) return;
    final list = List<SongModel>.from(state.queue);
    final current = state.currentSong;
    final item = list.removeAt(index);
    final currentIdx =
        current != null ? list.indexWhere((s) => s.id == current.id) : state.currentIndex;
    final insertAt = (currentIdx + 1).clamp(0, list.length);
    list.insert(insertAt, item);
    final newCurrent =
        current != null ? list.indexWhere((s) => s.id == current.id) : currentIdx;
    state = state.copyWith(queue: list, currentIndex: newCurrent);
  }

  Future<void> removeAt(int index) async {
    if (index < 0 || index >= state.queue.length) return;
    final list = List<SongModel>.from(state.queue);
    final removingCurrent = index == state.currentIndex;
    list.removeAt(index);

    if (list.isEmpty) {
      _cancelFade();
      await _active.stop();
      await _idle.stop();
      state = const PlaybackState();
      return;
    }

    int newCurrent = state.currentIndex;
    if (index < state.currentIndex) {
      newCurrent -= 1;
    } else if (removingCurrent && newCurrent >= list.length) {
      newCurrent = list.length - 1;
    }
    state = state.copyWith(queue: list, currentIndex: newCurrent);

    if (removingCurrent) {
      await _loadAndPlay(_active, state.queue[newCurrent]);
    }
  }

  // ----------------------------------------------------------------- internals

  Future<void> _loadAndPlay(AudioPlayer player, SongModel song) async {
    _cancelFade();
    try {
      await _idle.stop();
    } catch (_) {}
    try {
      await player.setVolume(1.0);
      await player.setAudioSource(AudioSource.uri(Uri.parse(song.uri!), tag: song));
      await player.play();
    } catch (_) {
      // Skip unplayable sources.
    }
  }

  void _bindStreams() {
    _posSub?.cancel();
    _durSub?.cancel();
    _playSub?.cancel();
    _procSub?.cancel();

    final p = _active;
    _posSub = p.positionStream.listen(_onPosition);
    _durSub = p.durationStream.listen(_durationCtrl.add);
    _playSub = p.playingStream.listen((playing) {
      if (state.isPlaying != playing) {
        state = state.copyWith(isPlaying: playing);
      }
    });
    _procSub = p.processingStateStream.listen((ps) {
      if (ps == ProcessingState.completed) _onCompleted();
    });
  }

  void _onPosition(Duration pos) {
    _positionCtrl.add(pos);
    if (_isCrossfading) return;

    final dur = _active.duration;
    if (dur == null) return;
    if (!ref.read(crossfadeEnabledProvider)) return;
    // Crossfading into the same track makes no sense for repeat-one.
    if (ref.read(repeatModeProvider) == RepeatPlayMode.one) return;
    final cf = ref.read(crossfadeDurationProvider);
    if (cf <= 0) return;
    final target = _computeNextIndex();
    if (target == null) return;

    final remaining = dur - pos;
    final window = Duration(milliseconds: (cf * 1000).round());
    if (remaining <= window && remaining > Duration.zero) {
      _startCrossfade(cf, target);
    }
  }

  Future<void> _startCrossfade(double cf, int targetIndex) async {
    if (_isCrossfading) return;
    if (targetIndex < 0 || targetIndex >= state.queue.length) return;
    _isCrossfading = true;
    _crossfadeTarget = targetIndex;

    final nextSong = state.queue[targetIndex];
    final incoming = _idle;
    final outgoing = _active;
    try {
      await incoming.setVolume(0.0);
      await incoming.setAudioSource(AudioSource.uri(Uri.parse(nextSong.uri!), tag: nextSong));
      await incoming.play();
    } catch (_) {
      _isCrossfading = false;
      _crossfadeTarget = null;
      return;
    }

    final totalSteps = (cf * 20).round().clamp(1, 100000); // 50ms steps
    int step = 0;
    _fadeTimer = Timer.periodic(const Duration(milliseconds: 50), (t) {
      step++;
      final frac = (step / totalSteps).clamp(0.0, 1.0);
      outgoing.setVolume(1.0 - frac);
      incoming.setVolume(frac);
      if (frac >= 1.0) {
        t.cancel();
        _completeCrossfade(outgoing);
      }
    });
  }

  Future<void> _completeCrossfade(AudioPlayer outgoing) async {
    _fadeTimer = null;
    try {
      await outgoing.stop();
      await outgoing.setVolume(1.0);
    } catch (_) {}
    _activeIdx = 1 - _activeIdx;
    final target = _crossfadeTarget ?? state.currentIndex;
    state = state.copyWith(currentIndex: target, isPlaying: true);
    _bindStreams();
    _isCrossfading = false;
    _crossfadeTarget = null;
  }

  void _cancelFade() {
    _fadeTimer?.cancel();
    _fadeTimer = null;
    _isCrossfading = false;
    _crossfadeTarget = null;
  }

  void _onCompleted() {
    if (_isCrossfading) return;
    if (ref.read(repeatModeProvider) == RepeatPlayMode.one) {
      _active.seek(Duration.zero);
      _active.play();
      return;
    }
    final nextIndex = _computeNextIndex();
    if (nextIndex != null) {
      _goToIndex(nextIndex);
    } else {
      _active.pause();
      _active.seek(Duration.zero);
      state = state.copyWith(isPlaying: false);
    }
  }

  void _dispose() {
    _cancelFade();
    _posSub?.cancel();
    _durSub?.cancel();
    _playSub?.cancel();
    _procSub?.cancel();
    _positionCtrl.close();
    _durationCtrl.close();
    for (final p in _players) {
      p.dispose();
    }
  }
}

// --------------------------------------------------------------- derived views

final currentSongProvider =
    Provider<SongModel?>((ref) => ref.watch(playbackControllerProvider).currentSong);

final isPlayingProvider =
    Provider<bool>((ref) => ref.watch(playbackControllerProvider).isPlaying);

final positionProvider = StreamProvider<Duration>(
    (ref) => ref.watch(playbackControllerProvider.notifier).positionStream);

final durationProvider = StreamProvider<Duration?>(
    (ref) => ref.watch(playbackControllerProvider.notifier).durationStream);
