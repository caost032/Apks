# Greenfield checkpoint verification — Slice 1

This document distinguishes what was actually executed in this container from what remains an Android product gate.

## Input authority / archaeology

- Refundación ZIP SHA-256: `f8378e8bcd19e19885f430f58ba21a8bd303d485b57c1d0695a78dabab057344`.
- Refundación traversed completely: 74 files, 612,297 bytes, all recorded in `docs/audit/AUTHORITY_MANIFEST.tsv`.
- Legacy ZIP SHA-256: `a6311729e43712ba53aa524a4b8e5812819ee3fd349962ddb1c21437500c46be`.
- Legacy traversed completely: 229 files, 72,669,187 bytes.
- Legacy vs Refundación PASS G20 inventory: 229/229 files match, 0 missing, 0 extra, 0 size mismatches, 0 SHA-256 mismatches.
- Greenfield no-direct-copy gate: 0 byte-identical Greenfield/legacy files after final source generation.

## Executed locally

- `make gate`: PASS from a clean build. Includes generated-host consistency, architecture/product guardians, no-direct-copy guardian, strict C11 compile (`-Werror`), exact 8-symbol ODG FFI allowlist, RELRO/NOW/non-executable-stack hardening, and six focused native tests.
- Focused native tests: camera, movement, perf quantiles, deterministic software raster, multithreaded EngineService/stale input, and retained service lifetime for Android surface callbacks: PASS.
- `cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Release -G Ninja` + `cmake --build build-cmake`: PASS.
- `CC=clang make asan-tests`: PASS with AddressSanitizer + UndefinedBehaviorSanitizer for all six focused native tests.
- `python3 -m py_compile tools/*.py`: PASS.

## Tooling absent locally — deliberately NOT claimed

At checkpoint time this container has C/Clang, CMake, Ninja, Java and `kotlinc`, but has **no `flutter`, `dart`, `gradle`, `sdkmanager`, `adb`, `ANDROID_HOME`, or `ANDROID_SDK_ROOT`**. Therefore:

- Flutter/Dart analysis and widget/unit tests: NOT RUN locally.
- Kotlin against the real Flutter/Android embedding: NOT COMPILED locally.
- Android NDK/CMake cross-build for ARMv7/ARM64: NOT RUN locally.
- APK: **NOT CREATED locally**.
- APK signing/zipalign/16 KiB/ABI artifact verifier: NOT RUN because no APK exists locally.
- Installation and physical-phone playtest: NOT RUN.

`.github/workflows/android-greenfield.yml` is the prepared Android-capable gate. It must run Flutter analysis/tests, universal and split ARM builds, and `tools/verify_android_apk.py` before any APK is called verified.

## Scope decision

Vertical Slice 2 is intentionally **NOT STARTED**. The Greenfield source has no targeting, tree/fruit/ore/animal interaction, survival vitals, territory, ecology, crafting, electricity or save implementation. Slice 1 is not product-complete until the Android APK/device exit gate in `PLAYTEST_SLICE1.md` passes.
