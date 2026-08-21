#!/usr/bin/env python3
"""Normalize and verify the recovered API37 Android release source.

The recovered source predates the exact Flutter 3.47.1 Android toolchain used by
CI.  This migration is intentionally deterministic and fail-closed: every
rewrite is either already present or must match the expected old form exactly.
Unexpected source drift aborts before Flutter/Gradle runs.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def die(msg: str) -> None:
    print(f"ANDROID RELEASE PREP FAIL: {msg}", file=sys.stderr)
    raise SystemExit(1)


def load(rel: str) -> str:
    path = ROOT / rel
    if not path.is_file():
        die(f"missing {rel}")
    return path.read_text(encoding="utf-8")


def store(rel: str, text: str, check_only: bool) -> None:
    if not check_only:
        (ROOT / rel).write_text(text, encoding="utf-8")


def replace(rel: str, old: str, new: str, check_only: bool) -> None:
    text = load(rel)
    if new in text and old not in text:
        return
    if check_only:
        die(f"{rel}: normalized form missing: {new!r}")
    if text.count(old) != 1:
        die(f"{rel}: expected legacy form exactly once: {old!r}")
    store(rel, text.replace(old, new), check_only=False)


def remove(rel: str, old: str, check_only: bool) -> None:
    text = load(rel)
    if old not in text:
        return
    if check_only:
        die(f"{rel}: stale form remains: {old!r}")
    if text.count(old) != 1:
        die(f"{rel}: stale form count is {text.count(old)}, expected 1: {old!r}")
    store(rel, text.replace(old, ""), check_only=False)


def ensure_analysis_excludes(check_only: bool) -> None:
    rel = "app/flutter/analysis_options.yaml"
    text = load(rel)
    block = (
        "  exclude:\n"
        "    - android/**\n"
        "    - build/**\n"
        "    - ios/**\n"
        "    - linux/**\n"
        "    - macos/**\n"
        "    - web/**\n"
        "    - windows/**\n"
    )
    if block in text:
        return
    if check_only:
        die(f"{rel}: platform/build excludes missing")
    marker = "analyzer:\n"
    if text.count(marker) != 1:
        die(f"{rel}: analyzer marker is not unique")
    store(rel, text.replace(marker, marker + block), check_only=False)


def normalize(check_only: bool) -> None:
    replace(
        "app/flutter/android/gradle/wrapper/gradle-wrapper.properties",
        "gradle-8.10.2-bin.zip", "gradle-8.14.3-bin.zip", check_only,
    )
    replace(
        "app/flutter/android/settings.gradle",
        'id "com.android.application" version "8.7.3" apply false',
        'id "com.android.application" version "8.11.1" apply false', check_only,
    )
    replace(
        "app/flutter/android/settings.gradle",
        'id "org.jetbrains.kotlin.android" version "2.1.0" apply false',
        'id "org.jetbrains.kotlin.android" version "2.2.20" apply false', check_only,
    )
    ensure_analysis_excludes(check_only)

    replace(
        "app/flutter/lib/src/platform/android_host.dart",
        "  bool structurallyLoadable => !legacy && !corrupt;",
        "  bool get structurallyLoadable => !legacy && !corrupt;", check_only,
    )
    replace(
        "app/flutter/lib/src/engine/game_runtime.dart",
        "import 'dart:ffi';", "import 'dart:ffi' hide Size;", check_only,
    )
    replace(
        "app/flutter/lib/src/ui/game_screen.dart",
        "StationPanel(runtime: widget.runtime, artifact: openedArtifact!, onClose: widget.runtime.closeArtifact)",
        "StationPanel(runtime: widget.runtime, artifact: openedArtifact, onClose: widget.runtime.closeArtifact)",
        check_only,
    )

    test = "app/flutter/test/input_router_test.dart"
    text = load(test)
    old = "input.setActionsEnabled(action: true, drop: true);"
    new = "input.setActionsEnabled(action: true, movement: true, drop: true);"
    if old in text:
        if check_only:
            die(f"{test}: stale movement action signature remains")
        if text.count(old) != 2:
            die(f"{test}: expected two legacy movement signatures, found {text.count(old)}")
        store(test, text.replace(old, new), check_only=False)
    elif text.count(new) != 2:
        die(f"{test}: normalized movement signature count is {text.count(new)}, expected 2")
    replace(test,
            "input.setActionsEnabled(action: false, drop: false);",
            "input.setActionsEnabled(action: false, movement: false, drop: false);", check_only)
    replace(test,
            "    expect(second.buttons & odgButtonDrop, isNot(0));\n    expect(input.sample().buttons, 0);",
            "    expect(second.buttons & odgButtonDrop, isNot(0));\n    input.pointerUp(3);\n    expect(input.sample().buttons, 0);",
            check_only)
    replace(test,
            "    input.pointerDown(8, layout.actionRect.center);\n\n    input.clear();",
            "    input.pointerDown(8, layout.actionRect.center);\n    input.pointerDown(9, layout.jumpRect.center);\n    input.pointerDown(10, layout.dashRect.center);\n    input.pointerDown(11, layout.dropRect.center);\n\n    input.clear();",
            check_only)

    replace(
        "app/flutter/pubspec.yaml",
        "  ffi: ^2.1.4", "  ffi: 2.2.0", check_only,
    )
    replace(
        "app/flutter/pubspec.yaml",
        "  flutter_lints: ^6.0.0", "  flutter_lints: 6.0.0", check_only,
    )

    kt = "app/flutter/android/app/src/main/kotlin/com/odpar/territorial_domain/MainActivity.kt"
    replace(kt, "import io.flutter.embedding.android.FlutterActivity",
            "import io.flutter.embedding.android.FlutterFragmentActivity", check_only)
    replace(kt, "class MainActivity : FlutterActivity() {",
            "class MainActivity : FlutterFragmentActivity() {", check_only)
    remove(kt, "import java.nio.ByteBuffer\n", check_only)
    replace(kt, 'Regex("\\s+")', 'Regex("\\\\s+")', check_only)
    replace(kt,
            "Os.open(dir.absolutePath, OsConstants.O_RDONLY or OsConstants.O_DIRECTORY, 0)",
            "Os.open(dir.absolutePath, OsConstants.O_RDONLY, 0)", check_only)
    replace(kt,
            "if (generation != localGeneration) continue",
            "if (generation != localGeneration) return@synchronized", check_only)

    bridge = "engine/src/android_bridge.c"
    replace(
        bridge,
        "    (void)env; (void)self; odg_music_reset();\n}\n#endif\n",
        "    (void)env; (void)self; odg_music_reset();\n}\n#else\n"
        "/* Keep strict non-Android CMake mirror builds valid without exporting code. */\n"
        "typedef int odg_android_bridge_nonempty_translation_unit;\n#endif\n",
        check_only,
    )


def verify() -> None:
    header = load("engine/include/odpar_game.h")
    expected = {
        "ODG_API_VERSION": "37",
        "ODG_FFI_ABI_VERSION": "9",
        "ODG_SAVE_SCHEMA_VERSION": "25",
    }
    for name, value in expected.items():
        m = re.search(rf"^#define {name} UINT32_C\((\d+)\)", header, re.MULTILINE)
        if not m or m.group(1) != value:
            die(f"{name} must be {value}")

    requirements = {
        "app/flutter/android/gradle/wrapper/gradle-wrapper.properties": ["gradle-8.14.3-bin.zip"],
        "app/flutter/android/settings.gradle": [
            'id "com.android.application" version "8.11.1" apply false',
            'id "org.jetbrains.kotlin.android" version "2.2.20" apply false',
        ],
        "app/flutter/android/gradle.properties": ["android.useAndroidX=true", "android.newDsl=false"],
        "app/flutter/lib/src/platform/android_host.dart": ["bool get structurallyLoadable => !legacy && !corrupt;"],
        "app/flutter/lib/src/engine/game_runtime.dart": ["import 'dart:ffi' hide Size;"],
        "app/flutter/lib/src/ui/game_screen.dart": ["artifact: openedArtifact, onClose: widget.runtime.closeArtifact"],
        "app/flutter/test/input_router_test.dart": [
            "setActionsEnabled(action: true, movement: true, drop: true)",
            "setActionsEnabled(action: false, movement: false, drop: false)",
            "input.pointerUp(3);", "layout.jumpRect.center", "layout.dashRect.center",
        ],
        "app/flutter/pubspec.yaml": ["  ffi: 2.2.0", "  flutter_lints: 6.0.0"],
        "app/flutter/android/app/src/main/kotlin/com/odpar/territorial_domain/MainActivity.kt": [
            "FlutterFragmentActivity", 'Regex("\\\\s+")',
            "Os.open(dir.absolutePath, OsConstants.O_RDONLY, 0)", "return@synchronized",
            'System.loadLibrary("odpar_territorial_domain")',
        ],
        "engine/src/android_bridge.c": ["odg_android_bridge_nonempty_translation_unit"],
        "app/flutter/CMakeLists.txt": [
            '"${ODPAR_ENGINE_ROOT}/src/android_bridge.c"',
            'LINKER:-z,max-page-size=16384', 'LINKER:-z,common-page-size=16384',
        ],
    }
    for rel, markers in requirements.items():
        text = load(rel)
        for marker in markers:
            if marker not in text:
                die(f"{rel}: missing {marker!r}")

    stale = {
        "app/flutter/android/app/src/main/kotlin/com/odpar/territorial_domain/MainActivity.kt": [
            "import io.flutter.embedding.android.FlutterActivity",
            "class MainActivity : FlutterActivity() {",
            "OsConstants.O_DIRECTORY", "if (generation != localGeneration) continue",
            "import java.nio.ByteBuffer",
        ],
        "app/flutter/lib/src/platform/android_host.dart": [
            "bool structurallyLoadable => !legacy && !corrupt;",
        ],
        "app/flutter/lib/src/engine/game_runtime.dart": ["import 'dart:ffi';"],
        "app/flutter/lib/src/ui/game_screen.dart": ["artifact: openedArtifact!"],
        "app/flutter/test/input_router_test.dart": [
            "setActionsEnabled(action: true, drop: true)",
            "setActionsEnabled(action: false, drop: false)",
        ],
    }
    for rel, markers in stale.items():
        text = load(rel)
        for marker in markers:
            if marker in text:
                die(f"{rel}: stale marker remains {marker!r}")

    print("ANDROID RELEASE SOURCE OK api=37 abi=9 save=25 gradle=8.14.3 agp=8.11.1 kotlin=2.2.20")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="verify only; do not rewrite")
    args = parser.parse_args()
    normalize(check_only=args.check)
    verify()


if __name__ == "__main__":
    main()
