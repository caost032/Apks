import 'dart:typed_data';
import 'dart:ui' as ui;

import '../engine/game_runtime.dart';
import '../native/odg_bindings.dart';
import '../platform/android_host.dart';

final class AvatarSkinService {
  AvatarSkinService(this.runtime, {AndroidHost? host}) : host = host ?? AndroidHost.instance;
  final GameRuntime runtime;
  final AndroidHost host;

  Future<void> restore() async {
    for (int face = odgAvatarFaceFront; face <= odgAvatarFaceBottom; face += 1) {
      final Uint8List? rgba = await host.loadSkin(face);
      if (rgba != null && rgba.length == odgAvatarTextureSize * odgAvatarTextureSize * 4) {
        runtime.uploadAvatarTexture(face, rgba);
      }
    }
  }

  Future<bool> pickAndApply(int face) async {
    final Uint8List? encoded = await host.pickImage();
    if (encoded == null || encoded.isEmpty) return false;
    final Uint8List rgba = await _coverCenterCrop(encoded);
    runtime.uploadAvatarTexture(face, rgba);
    await host.saveSkin(face, rgba);
    return true;
  }

  Future<Uint8List> _coverCenterCrop(Uint8List encoded) async {
    final ui.Codec codec = await ui.instantiateImageCodec(encoded);
    final ui.FrameInfo frame = await codec.getNextFrame();
    final ui.Image source = frame.image;
    try {
      final double side = source.width < source.height ? source.width.toDouble() : source.height.toDouble();
      final ui.Rect src = ui.Rect.fromLTWH((source.width - side) / 2, (source.height - side) / 2, side, side);
      final ui.PictureRecorder recorder = ui.PictureRecorder();
      final ui.Canvas canvas = ui.Canvas(recorder);
      canvas.drawImageRect(source, src, const ui.Rect.fromLTWH(0, 0, 256, 256), ui.Paint()..filterQuality = ui.FilterQuality.high);
      final ui.Image normalized = await recorder.endRecording().toImage(256, 256);
      try {
        final ByteData? data = await normalized.toByteData(format: ui.ImageByteFormat.rawRgba);
        if (data == null) throw StateError('No se pudo normalizar la textura.');
        return data.buffer.asUint8List(data.offsetInBytes, data.lengthInBytes);
      } finally { normalized.dispose(); }
    } finally { source.dispose(); codec.dispose(); }
  }
}
