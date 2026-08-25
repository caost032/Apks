# Vertical Slice 1 status

**State: source implementation complete for the locally-verifiable portion; product Definition of Done is NOT yet closed.**

## Implemented

- New physical Greenfield tree with no legacy source tree copy.
- C11 strict engine composition root and EngineService.
- Dedicated single-writer simulation owner at 60 Hz.
- Separate render worker consuming immutable snapshots.
- Bounded input mailbox, Dart-isolate input backpressure, lifecycle gesture cancellation, and a 250 ms native stale-input fail-safe.
- Minimal world authority and player physics.
- Camera yaw/pitch, clamps, natural vertical convention, invert-Y host setting, touch slop, camera-relative movement, height-aware jump/support, body-facing separation, camera collision, and critically damped camera-distance recovery.
- Low-poly player raster presentation and center reticle.
- Flutter host shell, bridge isolate, generated FFI ABI, Android SurfaceProducer plugin, retained native surface lifetime, and startup/teardown serialization.
- Native render path that does not copy a full framebuffer through Dart FFI.
- Flutter frame p50/p95/p99 + lifetime max/>50 ms counter; C sim/render p50/p95/p99 + lifetime maxima/breach counters + overload/input-age telemetry.
- Portrait/landscape aspect-preserving raster budget.
- ARMv7 + ARM64 CI build and exact artifact-verification scripts.
- Focused C canaries and static architecture/product guardians.
- Complete 74-file Refundación and 229-file legacy input audit manifests.

## Locally verified in this container

The final checkpoint verification runs `make gate` from a clean host build under strict C11 warnings-as-errors. It verifies generated C/Dart layout source consistency, architecture/product rules, zero byte-identical legacy files, exactly eight public `odg_*` host exports, RELRO/NOW/non-executable-stack hardening, and focused tests for camera, movement, performance quantiles, raster determinism, multithreaded EngineService behavior, stale-input neutralization, and retained Android callback lifetime.

The independent CMake Release build and focused Clang ASan+UBSan test gate also pass. Python guardian scripts compile successfully. Exact evidence is recorded in `CHECKPOINT_VERIFICATION.md`.

## Not verifiable in this container

Flutter, Dart, Gradle, Android SDK/NDK build tooling, emulator, and a physical Android device are absent. Therefore this checkpoint **does not claim an APK exists**, does not claim Flutter/Kotlin compilation passed, and does not claim camera feel/frame pacing passed a human playtest.

The CI workflow is prepared to compile/analyze/test Flutter, build universal ARMv7+ARM64 plus per-ABI splits, sign the pre-release artifacts, verify 16 KiB alignment and ABI/native contents, and emit SHA-256 checksums. That workflow still must actually run in an Android-capable environment.

## Exit gate before Slice 2

Slice 2 must not start until a real APK passes installation and the `PLAYTEST_SLICE1.md` canary, especially natural look direction, portrait/landscape behavior, movement feel, no periodic freeze, and the 10-minute >50 ms criterion. This is why targeting/tree/ore/animal systems are intentionally absent from this checkpoint.
