#!/usr/bin/env python3
"""Fail-fast guard for the Android/Flutter APK host contract.

This is intentionally structural and deterministic. It catches the exact class of host/toolchain
regressions that otherwise surface late in GitHub Actions after Flutter/Gradle downloads.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def fail(message: str) -> None:
    print(f"ANDROID BUILD CONTRACT FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def require(text: str, literal: str, label: str) -> None:
    if literal not in text:
        fail(f"{label}: missing {literal!r}")


def forbid(text: str, literal: str, label: str) -> None:
    if literal in text:
        fail(f"{label}: forbidden stale marker {literal!r}")


header = read("engine/include/odpar_game.h")
for name, expected in (
    ("ODG_API_VERSION", "37"),
    ("ODG_FFI_ABI_VERSION", "9"),
    ("ODG_SAVE_SCHEMA_VERSION", "25"),
):
    match = re.search(rf"^#define {name} UINT32_C\((\d+)\)", header, re.MULTILINE)
    if not match or match.group(1) != expected:
        fail(f"{name} must be {expected}")

wrapper = read("app/flutter/android/gradle/wrapper/gradle-wrapper.properties")
require(wrapper, "gradle-8.14.3-bin.zip", "Gradle wrapper")
forbid(wrapper, "gradle-8.10.2-bin.zip", "Gradle wrapper")

settings = read("app/flutter/android/settings.gradle")
require(settings, 'id "com.android.application" version "8.11.1" apply false', "AGP")
require(settings, 'id "org.jetbrains.kotlin.android" version "2.2.20" apply false', "Kotlin")

props = read("app/flutter/android/gradle.properties")
require(props, "android.useAndroidX=true", "Gradle properties")
require(props, "android.newDsl=false", "Gradle properties")

app_gradle = read("app/flutter/android/app/build.gradle")
for literal in (
    'id "dev.flutter.flutter-gradle-plugin"',
    "compileSdk = flutter.compileSdkVersion",
    "ndkVersion = flutter.ndkVersion",
    "sourceCompatibility = JavaVersion.VERSION_17",
    'path = file("../../CMakeLists.txt")',
):
    require(app_gradle, literal, "Android app Gradle")

host = read("app/flutter/android/app/src/main/kotlin/com/odpar/territorial_domain/MainActivity.kt")
for literal in (
    "import io.flutter.embedding.android.FlutterFragmentActivity",
    "class MainActivity : FlutterFragmentActivity()",
    "registerForActivityResult(ActivityResultContracts.OpenDocument())",
    "registerForActivityResult(ActivityResultContracts.OpenMultipleDocuments())",
    'Regex("\\\\s+")',
    "Os.open(dir.absolutePath, OsConstants.O_RDONLY, 0)",
    "return@synchronized",
    'System.loadLibrary("odpar_territorial_domain")',
):
    require(host, literal, "MainActivity")
for stale in (
    "import io.flutter.embedding.android.FlutterActivity",
    "class MainActivity : FlutterActivity()",
    'Regex("\\s+")',
    "OsConstants.O_DIRECTORY",
    "if (generation != localGeneration) continue",
    "import java.nio.ByteBuffer",
):
    forbid(host, stale, "MainActivity")

android_host = read("app/flutter/lib/src/platform/android_host.dart")
require(android_host, "bool get structurallyLoadable => !legacy && !corrupt;", "Dart AndroidHost")
forbid(android_host, "bool structurallyLoadable => !legacy && !corrupt;", "Dart AndroidHost")

runtime = read("app/flutter/lib/src/engine/game_runtime.dart")
require(runtime, "import 'dart:ffi' hide Size;", "Dart GameRuntime")
forbid(runtime, "import 'dart:ffi';", "Dart GameRuntime")

cmake = read("app/flutter/CMakeLists.txt")
for literal in (
    '"${ODPAR_ENGINE_ROOT}/src/android_bridge.c"',
    '"LINKER:-z,max-page-size=16384"',
    '"LINKER:-z,common-page-size=16384"',
    '"LINKER:--version-script=${ODPAR_EXPORT_MAP}"',
):
    require(cmake, literal, "CMake Android host")

bridge = read("engine/src/android_bridge.c")
exports = read("engine/odpar_territorial_domain.exports.map")
for symbol in (
    "Java_com_odpar_territorial_1domain_MainActivity_nativeSubmitPcm",
    "Java_com_odpar_territorial_1domain_MainActivity_nativeResetMusic",
):
    require(bridge, symbol, "JNI bridge")
    require(exports, symbol + ";", "native export map")
require(bridge, "odg_android_bridge_nonempty_translation_unit", "JNI non-Android mirror")


analysis = read("app/flutter/analysis_options.yaml")
for literal in (
    "    - android/**",
    "    - build/**",
    "    - ios/**",
    "    - linux/**",
    "    - macos/**",
    "    - web/**",
    "    - windows/**",
):
    require(analysis, literal, "Flutter analysis options")

pubspec = read("app/flutter/pubspec.yaml")
require(pubspec, "  ffi: 2.2.0", "Flutter pubspec")
require(pubspec, "  flutter_lints: 6.0.0", "Flutter pubspec")

workflow = read(".github/workflows/build-apk.yml")
for literal in (
    "uses: actions/checkout@v5",
    'flutter-version: "3.47.1"',
    "python3 tools/check_android_build_contract.py",
    "flutter analyze --no-fatal-infos",
    "flutter test",
    "flutter build apk --release --target-platform android-arm64",
    '"$BUILD_TOOLS/apksigner" verify',
    '"$BUILD_TOOLS/zipalign" -c -P 16 -v 4',
    "ODPAR-Territorial-Domain-API37-ABI9-SAVE25-APK",
):
    require(workflow, literal, "APK workflow")
forbid(workflow, "Apply verified Dart source fixes", "APK workflow")

print("ANDROID BUILD CONTRACT OK api=37 abi=9 save=25 gradle=8.14.3 agp=8.11.1 kotlin=2.2.20 flutter=3.47.1")
