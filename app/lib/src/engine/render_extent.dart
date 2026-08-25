import 'dart:math' as math;
import 'dart:ui' show Size;

Size chooseNativeRenderExtent(Size viewport) {
  if (viewport.isEmpty) return const Size(360, 640);
  final double shortSide = math.min(viewport.width, viewport.height);
  final double longSide = math.max(viewport.width, viewport.height);
  final double scale = math.min(
    1.0,
    math.min(360.0 / shortSide, 640.0 / longSide),
  );
  final int width = (viewport.width * scale).round().clamp(180, 640).toInt();
  final int height = (viewport.height * scale).round().clamp(180, 640).toInt();
  return Size(width.toDouble(), height.toDouble());
}
