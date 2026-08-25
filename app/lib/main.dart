import 'package:flutter/material.dart';

import 'src/ui/game_screen.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(const OdparGreenfieldApp());
}

class OdparGreenfieldApp extends StatelessWidget {
  const OdparGreenfieldApp({super.key});

  @override
  Widget build(BuildContext context) {
    return const MaterialApp(
      debugShowCheckedModeBanner: false,
      home: GameScreen(),
    );
  }
}
