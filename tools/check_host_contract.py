#!/usr/bin/env python3
"""Fail-closed drift guard across C authority, native ABI, Flutter and WASM hosts.

The checker intentionally derives versions, public symbols and engine source membership
from their authoritative files.  It must not be updated merely because the API number
changes; adding a capability should either connect every required host edge or make this
script fail with the missing edge.
"""

from __future__ import annotations

import json
import pathlib
import re
import sys
from typing import Iterable


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def fail(message: str) -> None:
    print(f"host contract failed: {message}", file=sys.stderr)
    raise SystemExit(1)


def macro_u32(header: str, name: str) -> int:
    match = re.search(
        rf"^\s*#define\s+{re.escape(name)}\s+UINT32_C\((\d+)\)\s*$",
        header,
        re.M,
    )
    if not match:
        fail(f"cannot derive {name} from C header")
    return int(match.group(1))


def dart_const(dart: str, name: str) -> int:
    match = re.search(rf"\bconst\s+int\s+{re.escape(name)}\s*=\s*(\d+)\s*;", dart)
    if not match:
        fail(f"Dart binding omits integer constant {name}")
    return int(match.group(1))


def dart_name_for_macro(name: str) -> str:
    if not name.startswith("ODG_"):
        fail(f"cannot map non-ODG macro {name} to Dart")
    words = name[4:].lower().split("_")
    return "odg" + "".join(word[:1].upper() + word[1:] for word in words)


def mirrored_u32_constants(header: str, dart: str) -> int:
    prefixes = (
        "ODG_MATERIAL_",
        "ODG_INTERACTION_",
        "ODG_MESSAGE_",
        "ODG_CONSTRUCTION_SHAPE_",
        "ODG_ITEM_CAP_",
    )
    count = 0
    for name, raw_value in re.findall(
        r"^\s*#define\s+(ODG_[A-Z0-9_]+)\s+UINT32_C\((\d+)\)\s*$",
        header,
        re.M,
    ):
        if not name.startswith(prefixes):
            continue
        dart_name = dart_name_for_macro(name)
        value = int(raw_value)
        if dart_const(dart, dart_name) != value:
            fail(f"Dart {dart_name} does not match C {name} ({value})")
        count += 1
    if count == 0:
        fail("no mirrored C/Dart contract constants discovered")
    return count


def public_c_symbols(header: str) -> set[str]:
    # Public functions all use the odg_ namespace.  The header contains no function
    # bodies, so matching identifier + '(' gives the ABI surface without maintaining
    # a second hand-written list.
    return set(re.findall(r"\b(odg_[A-Za-z0-9_]+)\s*\(", header))


def export_map_symbols(export_map: str) -> set[str]:
    return set(re.findall(r"^\s*(odg_[A-Za-z0-9_]+);\s*$", export_map, re.M))


def dart_lookup_symbols(dart: str) -> set[str]:
    # The generic type arguments can span many lines; anchor on lookupFunction and
    # capture the literal passed immediately afterwards.
    return set(
        re.findall(
            r"lookupFunction\s*<.*?>\s*\(\s*'([^']+)'",
            dart,
            re.S,
        )
    )


def make_engine_sources(makefile: str) -> list[str]:
    match = re.search(r"^ENGINE\s*:=\s*(.+)$", makefile, re.M)
    if not match:
        fail("Makefile has no authoritative ENGINE source list")
    return [token.strip() for token in match.group(1).split() if token.strip()]


def cmake_engine_sources(cmake: str) -> set[str]:
    found = set()
    for rel in re.findall(r'"\$\{ODPAR_ENGINE_ROOT\}/([^\"]+\.c)"', cmake):
        found.add("engine/" + rel)
    return found


def require_all(actual: Iterable[str], required: set[str], what: str) -> None:
    actual_set = set(actual)
    missing = sorted(required - actual_set)
    if missing:
        fail(f"{what} missing: " + ", ".join(missing))


def main() -> int:
    header = read("engine/include/odpar_game.h")
    dart = read("app/flutter/lib/src/native/odg_bindings.dart")
    runtime = read("app/flutter/lib/src/engine/game_runtime.dart")
    flutter_sources = "\n".join(
        path.read_text(encoding="utf-8")
        for path in sorted((ROOT / "app/flutter/lib").rglob("*.dart"))
    )
    web = read("app/web/index.html")
    smoke = read("tools/wasm_smoke.mjs")
    cmake = read("app/flutter/CMakeLists.txt")
    makefile = read("Makefile")
    workflow = read(".github/workflows/android.yml")
    spine = json.loads(read("PROJECT_SPINE.json"))
    export_map = read("engine/odpar_territorial_domain.exports.map")
    android_host_dart = read("app/flutter/lib/src/platform/android_host.dart")
    android_main = read("app/flutter/android/app/src/main/kotlin/com/odpar/territorial_domain/MainActivity.kt")
    game_screen = read("app/flutter/lib/src/ui/game_screen.dart")
    ffi_layout_test = read("app/flutter/test/ffi_layout_test.dart")
    inventory_panel = read("app/flutter/lib/src/ui/inventory/inventory_panel.dart")

    api = macro_u32(header, "ODG_API_VERSION")
    abi = macro_u32(header, "ODG_FFI_ABI_VERSION")
    save_schema = macro_u32(header, "ODG_SAVE_SCHEMA_VERSION")

    if dart_const(dart, "odgApiVersion") != api:
        fail(f"Dart API does not match C authority ({api})")
    if dart_const(dart, "odgFfiAbiVersion") != abi:
        fail(f"Dart FFI ABI does not match C authority ({abi})")

    mirrored_constants = mirrored_u32_constants(header, dart)

    runtime_spine = spine.get("runtime", {})
    if runtime_spine.get("api_version") != api:
        fail(f"PROJECT_SPINE runtime API does not match C authority ({api})")
    if runtime_spine.get("ffi_abi_version") != abi:
        fail(f"PROJECT_SPINE FFI ABI does not match C authority ({abi})")
    if runtime_spine.get("save_schema_version") != save_schema:
        fail(
            "PROJECT_SPINE save schema does not match C authority "
            f"({save_schema})"
        )

    if f"wasm.odg_api_version()!=={api}" not in web:
        fail(f"web host does not gate API {api}")
    # WASM smoke derives the expected API directly from the C authority instead of
    # duplicating a numeric literal that must be manually synchronized every bump.
    if "ODG_API_VERSION" not in smoke or "actualApi !== expectedApi" not in smoke:
        fail("WASM smoke test does not derive/gate API from C authority")

    public = public_c_symbols(header)
    exports = export_map_symbols(export_map)
    missing_exports = sorted(public - exports)
    extra_exports = sorted(exports - public)
    if missing_exports:
        fail("native export map omits public C symbols: " + ", ".join(missing_exports))
    if extra_exports:
        fail("native export map exposes undeclared odg symbols: " + ", ".join(extra_exports))

    if "WASM_EXPORTS := $(shell" not in makefile or "odpar_territorial_domain.exports.map" not in makefile:
        fail("WASM exports are not derived from the native public export map")
    if "$(WASM_EXPORTS)" not in makefile:
        fail("WASM link command does not consume WASM_EXPORTS")

    dart_lookups = dart_lookup_symbols(dart)
    missing_native = sorted(dart_lookups - public)
    if missing_native:
        fail("Dart looks up non-public symbols: " + ", ".join(missing_native))

    required_dart = {
        "odg_api_version",
        "odg_ffi_abi_query",
        "odg_init",
        "odg_resize",
        "odg_reset",
        "odg_set_input",
        "odg_tick_us",
        "odg_render_frame",
        "odg_copy_framebuffer",
        "odg_copy_stats",
        "odg_copy_resources",
        "odg_item_definition_get",
        "odg_copy_artifacts",
        "odg_food_definition_count",
        "odg_food_definition_get",
        "odg_flora_species_count",
        "odg_flora_species_get",
        "odg_fauna_species_count",
        "odg_fauna_species_get",
        "odg_fauna_diet_count",
        "odg_fauna_diet_get",
        "odg_fauna_habitat_count",
        "odg_fauna_habitat_get",
        "odg_loot_table_count",
        "odg_loot_table_get",
        "odg_copy_fauna",
        "odg_copy_fauna_nests",
        "odg_world_surface_sample64",
        "odg_player_satiety_permille",
        "odg_player_trail_broken",
        "odg_weather_rain_permille",
        "odg_save_schema_version",
        "odg_save_blob_size",
        "odg_save_write",
        "odg_save_load",
        "odg_state_hash",
    }
    require_all(dart_lookups, required_dart, "Dart authoritative host calls")

    engine_sources = set(make_engine_sources(makefile))
    cmake_sources = cmake_engine_sources(cmake)
    missing_cmake = sorted(engine_sources - cmake_sources)
    if missing_cmake:
        fail("Flutter CMake omits authoritative C units: " + ", ".join(missing_cmake))
    if "engine/src/android_bridge.c" not in cmake_sources:
        fail("Flutter CMake omits Android PCM/native bridge")

    # Every C unit in engine/src that is a runtime authority must be intentionally in
    # ENGINE or be the Android-only bridge.  This prevents a new subsystem from being
    # added to desktop tests but silently omitted from production linkage.
    runtime_c_units = {
        str(path.relative_to(ROOT)).replace("\\", "/")
        for path in (ROOT / "engine/src").glob("*.c")
    }
    allowed_mobile_only = {"engine/src/android_bridge.c"}
    unowned_units = sorted(runtime_c_units - engine_sources - allowed_mobile_only)
    if unowned_units:
        fail("C runtime units are not owned by ENGINE/CMake: " + ", ".join(unowned_units))

    if re.search(r"\b(webview|WebView)\b", flutter_sources):
        fail("Flutter host contains a WebView")
    if "api.copyFramebuffer" not in runtime or "api.copyStats" not in runtime:
        fail("Flutter runtime does not use coherent copy APIs")
    if "make wasm" not in workflow or "flutter build apk --release" not in workflow:
        fail("CI does not build both Flutter packaging and the WASM preview")
    if "arm64-v8a" not in workflow:
        fail("CI does not verify the primary mobile ABI")

    # ABI-v4 must validate the newly shared POD layouts on the Dart side.  Looking for
    # both the field and sizeOf expression makes accidental deletion fail closed.
    abi_layout_tokens = {
        "resourceSnapshotSize": "OdgResourceSnapshot",
        "artifactSnapshotSize": "OdgArtifactSnapshot",
        "foodDefinitionSize": "OdgFoodDefinition",
        "floraSpeciesDefinitionSize": "OdgFloraSpeciesDefinition",
        "faunaSpeciesDefinitionSize": "OdgFaunaSpeciesDefinition",
        "faunaSnapshotSize": "OdgFaunaSnapshot",
        "faunaNestSnapshotSize": "OdgFaunaNestSnapshot",
        "surfaceSampleSize": "OdgSurfaceSample",
    }
    for field, struct_name in abi_layout_tokens.items():
        if field not in dart or f"sizeOf<{struct_name}>()" not in dart:
            fail(f"Dart ABI validation omits {field}/{struct_name}")

    # A valid native symbol can still be wired twice to the same Dart field, which is a
    # Dart compile error that symbol-surface checks alone cannot see.
    lookup_initializers = re.findall(
        r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*library\.lookupFunction",
        dart,
        re.M,
    )
    duplicate_initializers = sorted(
        name for name in set(lookup_initializers) if lookup_initializers.count(name) > 1
    )
    if duplicate_initializers:
        fail("Dart native API has duplicate lookup initializers: " + ", ".join(duplicate_initializers))

    # Named-world persistence is a cross-language product contract.  Every MethodChannel
    # operation used by Dart must have an Android handler, saves must be integrity-checked
    # and a load failure must never fall back to creating/overwriting a new world.
    world_methods = {
        "listWorlds",
        "saveWorldSlot",
        "loadWorldSlot",
        "deleteWorldSlot",
        "renameWorldSlot",
    }
    dart_channel_methods = set(
        re.findall(r"invoke(?:Map)?Method[^\n(]*\(\s*'([^']+)'", android_host_dart)
    )
    kotlin_channel_methods = set(re.findall(r'^\s*"([A-Za-z0-9_]+)"\s*->', android_main, re.M))
    require_all(dart_channel_methods, world_methods, "Dart world repository methods")
    require_all(kotlin_channel_methods, world_methods, "Android world repository handlers")
    missing_android_handlers = sorted(dart_channel_methods - kotlin_channel_methods)
    if missing_android_handlers:
        fail("Dart MethodChannel calls lack Android handlers: " + ", ".join(missing_android_handlers))
    for token in ("worldIdPattern", "blobSha256", "blobBytes", "sha256Hex", "Os.rename"):
        if token not in android_main:
            fail(f"Android world repository omits safety invariant {token}")
    for token in ("saveSchemaSupported", "structurallyLoadable", "saveSchemaVersion", "loadWorldSlot", "saveWorldSlot"):
        if token not in game_screen:
            fail(f"Flutter world browser omits compatibility/safety edge {token}")
    if "compatibleWith(" in game_screen or "apiVersion == api" in android_host_dart or "ffiAbiVersion == abi" in android_host_dart:
        fail("world compatibility incorrectly depends on API/FFI instead of C save-schema authority")
    continue_match = re.search(
        r"Future<void> _continueWorld\(WorldSlot world\) async \{(.*?)\n  \}",
        game_screen,
        re.S,
    )
    if not continue_match:
        fail("cannot inspect Flutter world-load path")
    continue_body = continue_match.group(1)
    if "startNewWorld(" in continue_body or "_createWorld(" in continue_body:
        fail("world-load failure path can destructively fall back to a new world")
    if "legacyWorldId" not in android_main or "territorial_v15.odg" not in android_main:
        fail("legacy single-save preservation path is missing")
    if f"expect(odgApiVersion, {api});" not in ffi_layout_test:
        fail(f"Flutter FFI layout test is stale for API {api}")
    if "sizeOf<OdgItemDefinition>()" not in ffi_layout_test:
        fail("Flutter FFI layout test omits OdgItemDefinition")
    for token in ("itemDefinitionGet", "sizeOf<OdgItemDefinition>()", "itemCapabilityBits"):
        if token not in (dart + runtime):
            fail(f"Flutter item-capability host edge is missing {token}")
    for token in ("odgItemCapPlace", "odgItemCapConsume", "odgItemCapPlant", "odgItemCapDrink", "placeSelected", "consumeSelected", "plantSelected", "drinkSelected"):
        if token not in inventory_panel + runtime:
            fail(f"Flutter inventory cannot reach native item capability {token}")
    for token in ("runWorldIo", "storageGeneration", '"2:$target:$nextGeneration\\n"'):
        if token not in android_main:
            fail(f"Android world repository omits generational/serialized invariant {token}")
    if "'saveSchemaVersion': world.saveSchemaVersion" in android_host_dart:
        fail("Android autosave metadata can retain a pre-migration schema instead of serialized C schema")
    for token in ("apiVersion: apiVersion", "saveSchemaVersion: saveSchemaVersion", "world.copyWith"):
        if token not in game_screen:
            fail(f"Flutter autosave does not advance committed blob metadata via {token}")

    print(
        "HOST CONTRACT OK "
        f"api={api} abi={abi} save={save_schema} "
        f"dart_symbols={len(dart_lookups)} public_c={len(public)} "
        f"mirrored_constants={mirrored_constants} engine_units={len(engine_sources)} "
        "authority=C11 webview=no"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
