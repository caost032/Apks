#!/usr/bin/env python3
from __future__ import annotations
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCHEMA = json.loads((ROOT / "host/schema/host_api.json").read_text())
C_OUT = ROOT / "engine/include/odpar/odg_host.h"
DART_OUT = ROOT / "app/lib/src/native/generated_host.dart"

TYPES = {
    "u32": ("uint32_t", "ffi.Uint32", "int", 4, 4),
    "u64": ("uint64_t", "ffi.Uint64", "int", 8, 8),
    "i16": ("int16_t", "ffi.Int16", "int", 2, 2),
    "f32": ("float", "ffi.Float", "double", 4, 4),
}

def layout(fields):
    off = 0
    max_align = 1
    offsets = {}
    for name, typ in fields:
        _, _, _, size, align = TYPES[typ]
        max_align = max(max_align, align)
        off = (off + align - 1) // align * align
        offsets[name] = off
        off += size
    size = (off + max_align - 1) // max_align * max_align
    return size, offsets

def snake_upper(name: str) -> str:
    out=[]
    for i,ch in enumerate(name):
        if i and ch.isupper() and name[i-1].islower(): out.append('_')
        out.append(ch.upper())
    return ''.join(out)

def emit_c() -> str:
    lines = [
        "/* GENERATED from host/schema/host_api.json. DO NOT EDIT. */",
        "#ifndef ODPAR_GREENFIELD_ODG_HOST_H",
        "#define ODPAR_GREENFIELD_ODG_HOST_H",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "#if defined(_WIN32)",
        "#define ODG_EXPORT __declspec(dllexport)",
        "#else",
        "#define ODG_EXPORT __attribute__((visibility(\"default\")))",
        "#endif",
        f"#define ODG_HOST_ABI_VERSION UINT32_C({SCHEMA['abi_version']})",
    ]
    for name, value in SCHEMA["constants"].items():
        lines.append(f"#define {name} UINT32_C({value})")
    lines += ["", "typedef struct OdgEngineService OdgEngineService;", ""]
    for s in SCHEMA["structs"]:
        lines.append(f"typedef struct {s['name']} {{")
        for field, typ in s["fields"]:
            ctype = TYPES[typ][0]
            lines.append(f"    {ctype} {field};")
        lines.append(f"}} {s['name']};")
        size, offsets = layout(s["fields"])
        lines.append(f"_Static_assert(sizeof({s['name']}) == {size}u, \"{s['name']} size\");")
        for field, _ in s["fields"]:
            lines.append(f"_Static_assert(offsetof({s['name']}, {field}) == {offsets[field]}u, \"{s['name']}.{field} offset\");")
        lines.append("")
    lines += [
        "ODG_EXPORT uint32_t odg_host_abi_version(void);",
        "ODG_EXPORT OdgEngineService *odg_service_create(const OdgServiceConfig *config);",
        "ODG_EXPORT void odg_service_destroy(OdgEngineService *service);",
        "ODG_EXPORT uint32_t odg_service_start(OdgEngineService *service);",
        "ODG_EXPORT void odg_service_stop(OdgEngineService *service);",
        "ODG_EXPORT uint32_t odg_service_submit_input(OdgEngineService *service, const OdgInputFrame *frame);",
        "ODG_EXPORT uint32_t odg_service_copy_ui_snapshot(OdgEngineService *service, OdgUiSnapshot *out_snapshot);",
        "ODG_EXPORT uint32_t odg_service_set_render_extent(OdgEngineService *service, uint32_t width, uint32_t height);",
        "",
        "#endif",
        "",
    ]
    return "\n".join(lines)

def emit_dart() -> str:
    lines = [
        "// GENERATED from host/schema/host_api.json. DO NOT EDIT.",
        "// ignore_for_file: non_constant_identifier_names",
        "import 'dart:ffi' as ffi;",
        "",
        f"const int odgHostAbiVersion = {SCHEMA['abi_version']};",
    ]
    for name, value in SCHEMA["constants"].items():
        camel = ''.join(word.capitalize() if i else word.lower() for i,word in enumerate(name.split('_')))
        lines.append(f"const int {camel} = {value};")
    lines.append("")
    for s in SCHEMA["structs"]:
        size, _ = layout(s["fields"])
        lines.append(f"const int expected{s['name']}Size = {size};")
        lines.append(f"final class {s['name']} extends ffi.Struct {{")
        for field, typ in s["fields"]:
            ann = TYPES[typ][1]
            dart = TYPES[typ][2]
            lines.append(f"  @{ann}()")
            lines.append(f"  external {dart} {field};")
        lines.append("}")
        lines.append("")
    syms = ",\n  ".join(repr(x) for x in SCHEMA["functions"])
    lines += ["const List<String> expectedNativeSymbols = <String>[", f"  {syms}", "];", ""]
    return "\n".join(lines).replace("'", "'")

def main():
    C_OUT.parent.mkdir(parents=True, exist_ok=True)
    DART_OUT.parent.mkdir(parents=True, exist_ok=True)
    C_OUT.write_text(emit_c())
    DART_OUT.write_text(emit_dart())

if __name__ == "__main__":
    main()
