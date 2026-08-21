import 'dart:ffi';

import 'package:flutter_test/flutter_test.dart';
import 'package:odpar_territorial_domain/odpar_territorial_domain_ffi.dart';

void main() {
  test('Dart structs match frozen FFI ABI v9', () {
    expect(odgApiVersion, 37);
    expect(odgFfiAbiVersion, 9);
    expect(odgTickRate, 120);
    expect(sizeOf<OdgFfiAbiInfo>(), 112);
    expect(sizeOf<OdgGameStats>(), 208);
    expect(sizeOf<OdgLeaderEntry>(), 24);
    expect(sizeOf<OdgItemDefinition>(), 48);
    expect(sizeOf<OdgFoodDefinition>(), 32);
    expect(sizeOf<OdgFluidDefinition>(), 32);
    expect(sizeOf<OdgFluidContainerDefinition>(), 32);
    expect(sizeOf<OdgFaunaSpeciesDefinition>(), 116);
    expect(sizeOf<OdgFaunaDietDefinition>(), 32);
    expect(sizeOf<OdgFaunaHabitatDefinition>(), 52);
    expect(sizeOf<OdgFaunaNestingDefinition>(), 40);
    expect(sizeOf<OdgFaunaEntry>(), 88);
    expect(sizeOf<OdgFaunaSnapshot>(), 4240);
    expect(sizeOf<OdgFaunaNestSnapshot>(), 776);
    expect(sizeOf<OdgSurfaceSample>(), 48);
    expect(sizeOf<OdgArtifactSnapshot>(), 2576);
    expect(odgFfiFeatureConstructionShapes, 1 << 41);
    expect(odgFfiFeatureConstructionDurability, 1 << 42);
    expect(sizeOf<OdgConstructionEntry>(), 48);
    expect(sizeOf<OdgConstructionSnapshot>(), 3088);
  });

  test('required feature mask remains explicit', () {
    const int required = odgFfiFeatureFramebufferCopy |
        odgFfiFeatureStatsCopy |
        odgFfiFeaturePortraitRender |
        odgFfiFeatureFixed120Hz |
        odgFfiFeatureCameraInput;
    expect(required & odgFfiFeatureFramebufferCopy, isNot(0));
    expect(required & odgFfiFeatureStatsCopy, isNot(0));
    expect(required & odgFfiFeaturePortraitRender, isNot(0));
    expect(required & odgFfiFeatureFixed120Hz, isNot(0));
    expect(required & odgFfiFeatureCameraInput, isNot(0));
  });
}
