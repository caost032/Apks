# APK ABI authority repair — 2026-08-25

This note records the build-only architectural repair discovered while producing the first Greenfield Android APK.

The APK compiler itself succeeded, but artifact verification found `libodpar_greenfield.so` under x86_64 in addition to the intended ARMv7 and ARM64 ABIs. Flutter 3.35+ configures broad release ABI filters automatically, so an `externalNativeBuild` CMake target can be configured for x86_64 even when Flutter AOT is invoked with an ARM-only `--target-platform` list.

A static `abiFilters` block in `app/android/app/build.gradle` was tested and deliberately rejected by the Greenfield architecture gate because it would create a second ABI authority. That attempted change was removed.

Final design:

- CI owns one variable: `ODPAR_FLUTTER_TARGET_PLATFORMS=android-arm,android-arm64`.
- Both universal and split Flutter commands consume that exact variable.
- CMake consumes the same variable and skips the ODPAR native target when Gradle configures an ABI not represented in the CI target-platform list.
- `build.gradle` remains free of `abiFilters`.
- `tools/check_product_contract.py` guards this single-authority topology.
- `tools/verify_android_apk.py` remains fail-closed and still requires exact artifact ABIs, signature, 16 KiB alignment, hardening, required native libraries, and exact ODG/JNI exports.

This is not a verifier bypass and does not weaken the product ABI requirement; it fixes the build so the produced artifact satisfies the existing verifier.
