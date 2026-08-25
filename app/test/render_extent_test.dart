import 'dart:ui' show Size;

import 'package:flutter_test/flutter_test.dart';
import 'package:odpar_territorial_domain_greenfield/src/engine/render_extent.dart';

void main() {
  test('portrait render budget preserves aspect without exceeding 640', () {
    final Size extent = chooseNativeRenderExtent(const Size(1080, 2400));
    expect(extent, const Size(288, 640));
  });

  test('landscape render budget preserves aspect without exceeding 640', () {
    final Size extent = chooseNativeRenderExtent(const Size(2400, 1080));
    expect(extent, const Size(640, 288));
  });
}
