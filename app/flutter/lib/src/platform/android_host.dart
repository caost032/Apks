import 'dart:async';
import 'dart:io';
import 'dart:typed_data';

import 'package:flutter/services.dart';

final class WorldSlot {
  const WorldSlot({
    required this.id,
    required this.name,
    required this.seed,
    required this.createdAtMs,
    required this.updatedAtMs,
    required this.apiVersion,
    required this.ffiAbiVersion,
    required this.saveSchemaVersion,
    this.legacy = false,
    this.corrupt = false,
  });

  final String id;
  final String name;
  final int seed;
  final int createdAtMs;
  final int updatedAtMs;
  final int apiVersion;
  final int ffiAbiVersion;
  final int saveSchemaVersion;
  final bool legacy;
  final bool corrupt;

  bool get structurallyLoadable => !legacy && !corrupt;

  WorldSlot copyWith({
    String? name,
    int? updatedAtMs,
    int? apiVersion,
    int? ffiAbiVersion,
    int? saveSchemaVersion,
    bool? corrupt,
  }) => WorldSlot(
        id: id,
        name: name ?? this.name,
        seed: seed,
        createdAtMs: createdAtMs,
        updatedAtMs: updatedAtMs ?? this.updatedAtMs,
        apiVersion: apiVersion ?? this.apiVersion,
        ffiAbiVersion: ffiAbiVersion ?? this.ffiAbiVersion,
        saveSchemaVersion: saveSchemaVersion ?? this.saveSchemaVersion,
        legacy: legacy,
        corrupt: corrupt ?? this.corrupt,
      );

  factory WorldSlot.fromHost(Map<String, Object?> map) {
    int parseInt(Object? value, [int fallback = 0]) {
      if (value is int) return value;
      return int.tryParse(value?.toString() ?? '') ?? fallback;
    }
    return WorldSlot(
      id: map['id']?.toString() ?? '',
      name: map['name']?.toString() ?? 'Mundo',
      seed: parseInt(map['seed'], 1),
      createdAtMs: parseInt(map['createdAtMs']),
      updatedAtMs: parseInt(map['updatedAtMs']),
      apiVersion: parseInt(map['apiVersion']),
      ffiAbiVersion: parseInt(map['ffiAbiVersion']),
      saveSchemaVersion: parseInt(map['saveSchemaVersion']),
      legacy: map['legacy'] == true,
      corrupt: map['corrupt'] == true,
    );
  }
}

final class AndroidHost {
  AndroidHost._();
  static final AndroidHost instance = AndroidHost._();
  static const MethodChannel _channel = MethodChannel('odpar/territorial_domain/android');

  bool get supported => Platform.isAndroid;

  Future<List<WorldSlot>> listWorlds() async {
    if (!supported) return const <WorldSlot>[];
    final List<Object?>? raw = await _channel.invokeMethod<List<Object?>>('listWorlds');
    final List<WorldSlot> worlds = <WorldSlot>[];
    for (final Object? entry in raw ?? const <Object?>[]) {
      if (entry is! Map<Object?, Object?>) continue;
      final Map<String, Object?> map = <String, Object?>{
        for (final MapEntry<Object?, Object?> e in entry.entries)
          e.key.toString(): e.value,
      };
      final WorldSlot slot = WorldSlot.fromHost(map);
      if (slot.id.isNotEmpty) worlds.add(slot);
    }
    worlds.sort((WorldSlot a, WorldSlot b) => b.updatedAtMs.compareTo(a.updatedAtMs));
    return List<WorldSlot>.unmodifiable(worlds);
  }

  Future<void> saveWorldSlot({
    required WorldSlot world,
    required Uint8List bytes,
    required int apiVersion,
    required int ffiAbiVersion,
    required int saveSchemaVersion,
  }) async {
    if (!supported) return;
    await _channel.invokeMethod<bool>('saveWorldSlot', <String, Object?>{
      'id': world.id,
      'name': world.name,
      'seed': world.seed.toString(),
      'createdAtMs': world.createdAtMs,
      'apiVersion': apiVersion,
      'ffiAbiVersion': ffiAbiVersion,
      'saveSchemaVersion': saveSchemaVersion,
      'bytes': bytes,
    });
  }

  Future<Uint8List?> loadWorldSlot(String id) async {
    if (!supported) return null;
    return _channel.invokeMethod<Uint8List>('loadWorldSlot', <String, Object?>{'id': id});
  }

  Future<void> deleteWorldSlot(String id) async {
    if (!supported) return;
    await _channel.invokeMethod<bool>('deleteWorldSlot', <String, Object?>{'id': id});
  }

  Future<WorldSlot?> renameWorldSlot(String id, String name) async {
    if (!supported) return null;
    final Map<Object?, Object?>? raw = await _channel.invokeMapMethod<Object?, Object?>(
      'renameWorldSlot',
      <String, Object?>{'id': id, 'name': name},
    );
    if (raw == null) return null;
    return WorldSlot.fromHost(<String, Object?>{
      for (final MapEntry<Object?, Object?> e in raw.entries) e.key.toString(): e.value,
    });
  }

  Future<void> saveSettings(String json) async {
    if (!supported) return;
    await _channel.invokeMethod<bool>('saveSettings', <String, Object?>{'json': json});
  }

  Future<String?> loadSettings() async {
    if (!supported) return null;
    return _channel.invokeMethod<String>('loadSettings');
  }

  Future<Uint8List?> pickImage() async {
    if (!supported) return null;
    return _channel.invokeMethod<Uint8List>('pickImage');
  }

  Future<void> saveSkin(int face, Uint8List rgba) async {
    if (!supported) return;
    await _channel.invokeMethod<bool>('saveSkin', <String, Object?>{'face': face, 'rgba': rgba});
  }

  Future<Uint8List?> loadSkin(int face) async {
    if (!supported) return null;
    return _channel.invokeMethod<Uint8List>('loadSkin', <String, Object?>{'face': face});
  }

  Future<Map<String, Object?>> pickMusic() async {
    if (!supported) return const <String, Object?>{};
    final Map<Object?, Object?>? raw = await _channel.invokeMapMethod<Object?, Object?>('pickMusic');
    return _stringMap(raw);
  }

  Future<Map<String, Object?>> musicCommand(String command, {Object? value}) async {
    if (!supported) return const <String, Object?>{};
    final Map<String, Object?> args = <String, Object?>{'command': command};
    if (command == 'seek') args['positionMs'] = value;
    if (command == 'volume') args['value'] = value;
    if (command == 'shuffle' || command == 'repeat') args['value'] = value;
    final Map<Object?, Object?>? raw = await _channel.invokeMapMethod<Object?, Object?>('musicCommand', args);
    return _stringMap(raw);
  }

  Future<Map<String, Object?>> musicState() async {
    if (!supported) return const <String, Object?>{};
    final Map<Object?, Object?>? raw = await _channel.invokeMapMethod<Object?, Object?>('musicState');
    return _stringMap(raw);
  }

  static Map<String, Object?> _stringMap(Map<Object?, Object?>? raw) {
    if (raw == null) return const <String, Object?>{};
    return <String, Object?>{for (final MapEntry<Object?, Object?> e in raw.entries) e.key.toString(): e.value};
  }
}
