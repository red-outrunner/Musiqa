import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:musiqa/screens/main_layout.dart';
import 'package:permission_handler/permission_handler.dart';

import 'package:shared_preferences/shared_preferences.dart';
import 'package:musiqa/providers/metadata_provider.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await _requestPermissions();
  
  final prefs = await SharedPreferences.getInstance();

  runApp(
    ProviderScope(
      overrides: [
        sharedPreferencesProvider.overrideWithValue(prefs),
      ],
      child: const MusiqaApp(),
    ),
  );
}

Future<void> _requestPermissions() async {
  await [
    Permission.audio,
    Permission.storage,
  ].request();
}

class MusiqaApp extends StatelessWidget {
  const MusiqaApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Musiqa',
      theme: ThemeData(
        useMaterial3: true,
        brightness: Brightness.dark,
        colorSchemeSeed: Colors.deepPurple,
      ),
      home: const MainLayout(),
    );
  }
}
