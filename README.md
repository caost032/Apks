# ODPAR: Territorial Domain — Greenfield

This is the **new Greenfield source tree**. It is not a cleaned copy of API38 and must never be merged back into the legacy tree as a development strategy.

## Authority

1. Refundación Greenfield package: SHA-256 `f8378e8bcd19e19885f430f58ba21a8bd303d485b57c1d0695a78dabab057344`.
2. Current passes `G00..G23` inside that package.
3. The current master prompt for this Greenfield start, where it gives a more specific slice boundary.
4. Legacy API38 checkpoint: read-only archaeology only, SHA-256 `a6311729e43712ba53aa524a4b8e5812819ee3fd349962ddb1c21437500c46be`.

The legacy checkpoint was verified against PASS G20: **229/229 files match size and SHA-256, with zero missing, extra, or mismatched files**. See `docs/audit/LEGACY_MANIFEST_AND_DISPOSITION.tsv`.

## Slice 1 product shape

`Flutter main isolate` owns touch, widgets, frame-time observation, and composition of the latest texture/snapshot. It never ticks gameplay and never calls C FFI directly.

`Dart engine bridge isolate` owns the FFI service handle, submits small input packets with bounded in-flight backpressure, and samples immutable UI snapshots at 20 Hz.

`C EngineService` owns a 60 Hz single-writer simulation thread and a separate render thread. The renderer consumes immutable render snapshots. Android presentation goes through `ANativeWindow` attached to a Flutter `SurfaceProducer` texture; the full framebuffer is not copied through Dart FFI.

## Local gate

`make gate` checks generated C/Dart host contracts, architecture/product constraints, the no-direct-legacy-copy invariant, the native export allowlist/hardening, and focused C11 canaries for camera, movement, perf ring, raster, EngineService, stale-input neutralization, and retained Android surface lifetime.

The current container does **not** contain Flutter, Dart, Gradle, or the Android SDK, so no APK is claimed from this checkpoint. The Android CI workflow is prepared to build and verify universal ARMv7+ARM64 and both splits. See `docs/SLICE1_STATUS.md`.

## Scope boundary

Slice 2 has **not** started. Targeting, tree interaction, fruit, ore, animal interaction, and survival vitals remain out until Slice 1 passes a real Android APK/device gate.
