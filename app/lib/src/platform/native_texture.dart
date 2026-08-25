import 'package:flutter/services.dart';

final class NativeTextureHost {
  NativeTextureHost._();

  static const MethodChannel _channel = MethodChannel(
    'odpar.greenfield/native_render',
  );

  static Future<int> create({
    required int serviceHandle,
    required int width,
    required int height,
  }) async {
    final int? id = await _channel.invokeMethod<int>('createTexture', <String, int>{
      'serviceHandle': serviceHandle,
      'width': width,
      'height': height,
    });
    if (id == null) throw StateError('Android did not return a texture ID.');
    return id;
  }

  static Future<void> resize({
    required int textureId,
    required int width,
    required int height,
  }) =>
      _channel.invokeMethod<void>('resizeTexture', <String, int>{
        'textureId': textureId,
        'width': width,
        'height': height,
      });

  static Future<void> release(int textureId) =>
      _channel.invokeMethod<void>('releaseTexture', <String, int>{
        'textureId': textureId,
      });
}
