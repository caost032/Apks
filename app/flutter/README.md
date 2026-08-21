# ODPAR: Territorial Domain — Flutter host

This directory is the Android-first product host for native API 37 / FFI ABI 9.

Flutter/Dart owns UI, touch routing, preferences, save-file presentation, paged map/artifact/
construction surfaces and music UI. The C11 runtime remains the only gameplay authority. ABI9 includes construction topology/integrity snapshot validation and explicit artifact paging `total_count`; the current save writer is schema 25.

The app includes the official AFTERIMAGE 0.2 catalog (7 Original Tracks + 5 Instrumental Reworks by
kaost032) under `assets/music/afterimage_0_2/`; players may add local music separately.

Build on a machine with Flutter + Android SDK/NDK:

```sh
flutter pub get
flutter analyze
flutter test
flutter build apk --release
```
