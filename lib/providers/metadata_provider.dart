import 'dart:io';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:id3/id3.dart';
import 'package:on_audio_query/on_audio_query.dart';

final sharedPreferencesProvider = Provider<SharedPreferences>((ref) => throw UnimplementedError());

final metadataProvider = Provider<MetadataProvider>((ref) {
  return MetadataProvider(ref.read(sharedPreferencesProvider));
});

class MetadataProvider {
  final SharedPreferences prefs;
  
  MetadataProvider(this.prefs);

  Future<void> scanAllSongs(List<SongModel> songs) async {
    for (var song in songs) {
      if (song.data.isEmpty) continue;
      
      try {
        final file = File(song.data);
        if (!await file.exists()) continue;
        
        final bytes = await file.readAsBytes();
        MP3Instance mp3instance = MP3Instance(bytes);
        if (mp3instance.parseTagsSync()) {
           Map<String, dynamic>? metaTags = mp3instance.getMetaTags();
           if (metaTags != null) {
              final tags = metaTags; // id3 package exposes the dict
              
              // Depending on ID3 version, TBPM / TKEY might be under a dictionary or raw
              // Let's try grabbing standard or raw tags
              String bpm = 'Unknown';
              String key = 'Unknown';
              
              if (tags.containsKey('TBPM')) bpm = tags['TBPM'].toString();
              if (tags.containsKey('TKEY')) key = tags['TKEY'].toString();
              if (tags.containsKey('TKEY') == false && tags.containsKey('INITIALKEY')) key = tags['INITIALKEY'].toString();

              if (bpm != 'Unknown') await prefs.setString('bpm_${song.id}', bpm);
              if (key != 'Unknown') await prefs.setString('key_${song.id}', key);
           }
        }
      } catch (e) {
        // Ignore files that fail to parse
      }
    }
  }

  String getBpm(int songId) => prefs.getString('bpm_$songId') ?? 'Unknown';
  String getKey(int songId) => prefs.getString('key_$songId') ?? 'Unknown';

  /// Compact tag shown next to a song, e.g. `[A|90bpm]`. Returns an empty
  /// string when neither value is known.
  String badge(int songId) {
    final bpm = getBpm(songId);
    final key = getKey(songId);
    final hasBpm = bpm != 'Unknown';
    final hasKey = key != 'Unknown';
    if (!hasBpm && !hasKey) return '';
    final k = hasKey ? key : '?';
    final b = hasBpm ? '${bpm}bpm' : '?bpm';
    return '[$k|$b]';
  }
}
