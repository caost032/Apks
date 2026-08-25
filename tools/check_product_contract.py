#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
errors: list[str] = []

ui_files = list((ROOT / "app/lib/src/ui").rglob("*.dart"))
for path in ui_files:
    text = path.read_text(errors="replace")
    rel = path.relative_to(ROOT)
    if "dart:ffi" in text or "odg_service_" in text:
        errors.append(f"Flutter UI owns native gameplay call: {rel}")

bridge = (ROOT / "app/lib/src/engine/engine_bridge.dart").read_text()
if "Isolate.spawn" not in bridge:
    errors.append("EngineBridge must own a Dart worker isolate")
if "ffi.DynamicLibrary" in (ROOT / "app/lib/src/ui/game_screen.dart").read_text():
    errors.append("GameScreen may not open FFI libraries")
game_screen = (ROOT / "app/lib/src/ui/game_screen.dart").read_text()
if "Ticker" not in game_screen:
    errors.append("Slice 1 must sample touch on Flutter cadence without owning simulation")
if game_screen.count("Future<void> _shutdownProduct() async") != 1:
    errors.append("GameScreen must have exactly one product teardown owner")
if "await _startupFuture" not in game_screen:
    errors.append("product teardown must serialize after asynchronous startup")
if "cancelActiveGestures" not in game_screen:
    errors.append("Android lifecycle must clear active continuous input")

input_text = (ROOT / "app/lib/src/input/game_input_controller.dart").read_text()
if "final double naturalPitch = -delta.dy" not in input_text:
    errors.append("natural vertical look mapping is not finger-up => positive pitch")
if "_invertY ? -naturalPitch : naturalPitch" not in input_text:
    errors.append("invert-Y must be a single explicit sign inversion")

android_build = (ROOT / "app/android/app/build.gradle").read_text()
for expected in (
    'compileSdk = 36',
    'targetSdk = 36',
    'ndkVersion = "28.2.13676358"',
    'version = "3.22.1"',
):
    if expected not in android_build:
        errors.append(f"Android toolchain contract missing: {expected}")
if "abiFilters" in android_build:
    errors.append("build.gradle may not become a second ABI authority")

settings = (ROOT / "app/android/settings.gradle").read_text()
for expected in (
    'com.android.application" version "9.1.1"',
    'org.jetbrains.kotlin.android" version "2.2.20"',
):
    if expected not in settings:
        errors.append(f"Gradle plugin epoch missing: {expected}")

native_plugin = (ROOT / "app/android/app/src/main/kotlin/com/odpar/territorial_domain/greenfield/NativeRenderPlugin.kt").read_text()
for expected in ("nativeRetainService", "nativeReleaseService", "createSurfaceProducer"):
    if expected not in native_plugin:
        errors.append(f"Android surface lifetime contract missing: {expected}")

pubspec = (ROOT / "app/pubspec.yaml").read_text()
if "uses-material-design: true" not in pubspec:
    errors.append("Material icon font must be bundled while Slice 1 uses Flutter Icons")

manifest = (ROOT / "app/android/app/src/main/AndroidManifest.xml").read_text()
if "screenOrientation" in manifest:
    errors.append("Slice 1 must not hard-lock screen orientation")

cmake = (ROOT / "CMakeLists.txt").read_text()
if "max-page-size=16384" not in cmake or "common-page-size=16384" not in cmake:
    errors.append("native Android target must request 16 KiB ELF page alignment")

# Detect known legacy synchronous entry points if they ever leak back into UI.
for path in (ROOT / "app/lib/src/ui").rglob("*.dart"):
    text = path.read_text(errors="replace")
    rel = path.relative_to(ROOT)
    if any(name in text for name in ("tickUs(", "renderFrame(", "tickAndRender(")):
        errors.append(f"legacy synchronous UI=>sim/render entry point in {rel}")

if errors:
    print("PRODUCT CONTRACT FAILED")
    for error in errors:
        print(f" - {error}")
    raise SystemExit(1)
print("product contract: OK (worker ownership, natural camera, Android epoch, 16 KiB)")
