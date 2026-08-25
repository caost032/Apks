# Android toolchain contract — Greenfield epoch A1

Slice 1 deliberately starts from the last **coherent proven matrix** documented by the API38 postmortem, not from copied project files:

| Layer | Pinned value |
|---|---|
| Java | 17 |
| Flutter | 3.47.1 stable |
| Dart (bundled) | 3.13.1 |
| Gradle | 9.3.1 |
| Android Gradle Plugin | 9.1.1 |
| Kotlin Gradle Plugin | 2.2.20 explicit |
| NDK | 28.2.13676358 |
| CMake | 3.22.1 |
| Android build-tools | 36.0.0 |
| compileSdk / targetSdk | 36 / 36 |
| minSdk | 24 |
| Product ABIs | armeabi-v7a, arm64-v8a |

The matrix may be upgraded later, but only as one tested epoch. `build.gradle` contains no `abiFilters`; the CI build command is the single product ABI selection authority.

Flutter 3.47.1 was published as stable on 2026-08-19 and bundles Dart 3.13.1; the package SDK floor therefore targets the same Dart 3.13 epoch instead of an unrelated older language baseline.

Native linker options request 16 KiB PT_LOAD page alignment. Artifact verification requires `zipalign -P 16`, `apksigner`, exact ABI membership, `libodpar_greenfield.so`, `libflutter.so`, `libapp.so`, uncompressed native loading, >=16 KiB PT_LOAD alignment for ODPAR native code, and exact ODG/JNI export allowlists.

The current milestone release build is release-optimized but uses the Android debug signing configuration **explicitly for pre-release CI/installability only**. It is not a public-release signing policy. A public release must introduce an external protected keystore contract before release freeze; that future change must not alter gameplay authority.

`tools/bootstrap_android.sh` is developer setup only: it writes `local.properties` and materializes a Gradle wrapper when the exact Gradle version is installed. CI performs the same setup; it does not patch Dart, Gradle semantics, or source before compiling.
