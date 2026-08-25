import 'dart:ffi' as ffi;

import 'package:flutter_test/flutter_test.dart';
import 'package:odpar_territorial_domain_greenfield/src/native/generated_host.dart';

void main() {
  test('generated Dart FFI layouts match schema contract', () {
    expect(ffi.sizeOf<OdgServiceConfig>(), expectedOdgServiceConfigSize);
    expect(ffi.sizeOf<OdgInputFrame>(), expectedOdgInputFrameSize);
    expect(ffi.sizeOf<OdgUiSnapshot>(), expectedOdgUiSnapshotSize);
  });
}
