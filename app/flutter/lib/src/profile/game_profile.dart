import 'dart:convert';

import '../native/odg_bindings.dart';

final class ControlAnchor {
  const ControlAnchor({required this.x, required this.y, this.scale = 1, this.opacity = 0.72});
  final double x;
  final double y;
  final double scale;
  final double opacity;

  ControlAnchor copyWith({double? x, double? y, double? scale, double? opacity}) => ControlAnchor(
        x: (x ?? this.x).clamp(0.04, 0.96).toDouble(),
        y: (y ?? this.y).clamp(0.08, 0.94).toDouble(),
        scale: (scale ?? this.scale).clamp(0.70, 1.50).toDouble(),
        opacity: (opacity ?? this.opacity).clamp(0.20, 1.00).toDouble(),
      );

  Map<String, Object> toJson() => <String, Object>{'x': x, 'y': y, 'scale': scale, 'opacity': opacity};
  factory ControlAnchor.fromJson(Object? raw, ControlAnchor fallback) {
    if (raw is! Map) return fallback;
    double number(String key, double value) => (raw[key] as num?)?.toDouble() ?? value;
    return fallback.copyWith(x: number('x', fallback.x), y: number('y', fallback.y), scale: number('scale', fallback.scale), opacity: number('opacity', fallback.opacity));
  }
}

final class ControlProfile {
  const ControlProfile({required this.joystick, required this.interact, required this.jump, required this.dash, required this.drop, required this.hotbar});
  final ControlAnchor joystick;
  final ControlAnchor interact;
  final ControlAnchor jump;
  final ControlAnchor dash;
  final ControlAnchor drop;
  final ControlAnchor hotbar;

  static const ControlProfile landscape = ControlProfile(
    joystick: ControlAnchor(x: 0.12, y: 0.79, scale: 1.0, opacity: 0.66),
    interact: ControlAnchor(x: 0.90, y: 0.81, scale: 1.0, opacity: 0.72),
    jump: ControlAnchor(x: 0.80, y: 0.82, scale: 0.86, opacity: 0.68),
    dash: ControlAnchor(x: 0.79, y: 0.68, scale: 0.82, opacity: 0.64),
    drop: ControlAnchor(x: 0.90, y: 0.66, scale: 0.78, opacity: 0.58),
    hotbar: ControlAnchor(x: 0.50, y: 0.88, scale: 1.0, opacity: 0.92),
  );
  static const ControlProfile portrait = ControlProfile(
    joystick: ControlAnchor(x: 0.19, y: 0.78, scale: 1.0, opacity: 0.66),
    interact: ControlAnchor(x: 0.84, y: 0.80, scale: 1.0, opacity: 0.72),
    jump: ControlAnchor(x: 0.69, y: 0.83, scale: 0.86, opacity: 0.68),
    dash: ControlAnchor(x: 0.69, y: 0.69, scale: 0.82, opacity: 0.64),
    drop: ControlAnchor(x: 0.84, y: 0.66, scale: 0.78, opacity: 0.58),
    hotbar: ControlAnchor(x: 0.50, y: 0.91, scale: 0.92, opacity: 0.92),
  );

  Map<String, Object> toJson() => <String, Object>{
        'joystick': joystick.toJson(), 'interact': interact.toJson(), 'jump': jump.toJson(), 'dash': dash.toJson(), 'drop': drop.toJson(), 'hotbar': hotbar.toJson(),
      };
  factory ControlProfile.fromJson(Object? raw, ControlProfile fallback) {
    if (raw is! Map) return fallback;
    return ControlProfile(
      joystick: ControlAnchor.fromJson(raw['joystick'], fallback.joystick),
      interact: ControlAnchor.fromJson(raw['interact'], fallback.interact),
      jump: ControlAnchor.fromJson(raw['jump'], fallback.jump),
      dash: ControlAnchor.fromJson(raw['dash'], fallback.dash),
      drop: ControlAnchor.fromJson(raw['drop'], fallback.drop),
      hotbar: ControlAnchor.fromJson(raw['hotbar'], fallback.hotbar),
    );
  }

  ControlProfile update(String id, ControlAnchor anchor) => switch (id) {
        'joystick' => ControlProfile(joystick: anchor, interact: interact, jump: jump, dash: dash, drop: drop, hotbar: hotbar),
        'interact' => ControlProfile(joystick: joystick, interact: anchor, jump: jump, dash: dash, drop: drop, hotbar: hotbar),
        'jump' => ControlProfile(joystick: joystick, interact: interact, jump: anchor, dash: dash, drop: drop, hotbar: hotbar),
        'dash' => ControlProfile(joystick: joystick, interact: interact, jump: jump, dash: anchor, drop: drop, hotbar: hotbar),
        'drop' => ControlProfile(joystick: joystick, interact: interact, jump: jump, dash: dash, drop: anchor, hotbar: hotbar),
        'hotbar' => ControlProfile(joystick: joystick, interact: interact, jump: jump, dash: dash, drop: drop, hotbar: anchor),
        _ => this,
      };
}

final class GameProfile {
  const GameProfile({
    this.schema = 1,
    this.landscapeControls = ControlProfile.landscape,
    this.portraitControls = ControlProfile.portrait,
    this.cameraMode = odgCameraModeMedium,
    this.cameraSensitivity = 1.0,
    this.musicReactivity = 1.0,
    this.pauseMusicWithGame = false,
  });
  final int schema;
  final ControlProfile landscapeControls;
  final ControlProfile portraitControls;
  final int cameraMode;
  final double cameraSensitivity;
  final double musicReactivity;
  final bool pauseMusicWithGame;

  ControlProfile controlsFor(bool landscape) => landscape ? landscapeControls : portraitControls;

  GameProfile copyWith({ControlProfile? landscapeControls, ControlProfile? portraitControls, int? cameraMode, double? cameraSensitivity, double? musicReactivity, bool? pauseMusicWithGame}) => GameProfile(
        schema: 1,
        landscapeControls: landscapeControls ?? this.landscapeControls,
        portraitControls: portraitControls ?? this.portraitControls,
        cameraMode: (cameraMode ?? this.cameraMode).clamp(0, odgCameraModeCount - 1).toInt(),
        cameraSensitivity: (cameraSensitivity ?? this.cameraSensitivity).clamp(0.40, 2.00).toDouble(),
        musicReactivity: (musicReactivity ?? this.musicReactivity).clamp(0.0, 1.50).toDouble(),
        pauseMusicWithGame: pauseMusicWithGame ?? this.pauseMusicWithGame,
      );

  String encode() => jsonEncode(<String, Object>{
        'control_profile_schema': 1,
        'landscape': landscapeControls.toJson(),
        'portrait': portraitControls.toJson(),
        'camera_mode': cameraMode,
        'camera_sensitivity': cameraSensitivity,
        'music_reactivity': musicReactivity,
        'pause_music_with_game': pauseMusicWithGame,
      });

  factory GameProfile.decode(String? source) {
    if (source == null || source.isEmpty) return const GameProfile();
    try {
      final Object? decoded = jsonDecode(source);
      if (decoded is! Map || decoded['control_profile_schema'] != 1) return const GameProfile();
      final GameProfile base = const GameProfile();
      return base.copyWith(
        landscapeControls: ControlProfile.fromJson(decoded['landscape'], ControlProfile.landscape),
        portraitControls: ControlProfile.fromJson(decoded['portrait'], ControlProfile.portrait),
        cameraMode: (decoded['camera_mode'] as num?)?.toInt(),
        cameraSensitivity: (decoded['camera_sensitivity'] as num?)?.toDouble(),
        musicReactivity: (decoded['music_reactivity'] as num?)?.toDouble(),
        pauseMusicWithGame: decoded['pause_music_with_game'] as bool?,
      );
    } catch (_) { return const GameProfile(); }
  }
}
