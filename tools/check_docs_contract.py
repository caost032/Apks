#!/usr/bin/env python3
"""Fail when current-documentation contract markers drift from C authority.

Historical changelog entries are deliberately excluded.  This guard only covers files that
present themselves as the *current* tree/checkpoint and therefore must never advertise stale
API/ABI/save numbers after code evolves.
"""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = (ROOT / "engine/include/odpar_game.h").read_text(encoding="utf-8")


def macro(name: str) -> int:
    match = re.search(rf"^#define\s+{re.escape(name)}\s+UINT32_C\((\d+)\)", HEADER, re.MULTILINE)
    if not match:
        raise SystemExit(f"DOC CONTRACT FAIL: cannot resolve {name}")
    return int(match.group(1))


api = macro("ODG_API_VERSION")
abi = macro("ODG_FFI_ABI_VERSION")
save = macro("ODG_SAVE_SCHEMA_VERSION")

required = {
    "README.md": [
        f"Current development checkpoint: **API{api} / ABI{abi} / SAVE{save}",
    ],
    "CERTIFICATION.md": [
        f"API{api} / ABI{abi} / SAVE{save}",
        f"C11 gameplay authority: API **{api}**",
        f"FFI ABI: **{abi}**",
        f"Save schema: **{save}**",
    ],
    "CHECKPOINT_STATUS_2026-08-20.md": [
        f"Game API: **{api}**",
        f"FFI ABI: **{abi}**",
        f"Save schema written: **{save}**",
    ],
    "HANDOFF_NEXT_CHAT_2026-08-20.md": [
        f"CURRENT AUTHORITY: API {api} / ABI {abi} / SAVE {save}",
        f"`ODG_API_VERSION = {api}`",
        f"`ODG_FFI_ABI_VERSION = {abi}`",
        f"`ODG_SAVE_SCHEMA_VERSION = {save}`",
        "readable save chain: " + " → ".join(str(v) for v in range(14, save + 1)),
    ],
    "docs/ARCHITECTURE.md": [
        f"FFI ABI {abi}",
        f"API {api} / FFI ABI {abi} / save schema {save}",
    ],
    "docs/APP_EMBEDDING.md": [
        f"FFI ABI {abi} → C11 (current engine API {api})",
        f"current writer emits schema {save}",
    ],
    "docs/FLUTTER_APP.md": [
        f"| Engine API | {api} |",
        f"| FFI ABI | {abi} |",
        f"| Save writer | schema {save} |",
        f"FFI ABI {abi} / API {api}",
    ],
    "app/flutter/README.md": [
        f"native API {api} / FFI ABI {abi}",
    ],
}

errors: list[str] = []
for relative, markers in required.items():
    path = ROOT / relative
    if not path.is_file():
        errors.append(f"missing {relative}")
        continue
    text = path.read_text(encoding="utf-8")
    for marker in markers:
        if marker not in text:
            errors.append(f"{relative}: missing marker {marker!r}")


# Exact macro assignments in current documentation are normative, not historical prose.
# Catch the especially dangerous split-brain case where a document headline is current but
# a later contract table still instructs the next worker/host to use an older ABI or schema.
assignment_patterns = {
    "ODG_API_VERSION": api,
    "ODG_FFI_ABI_VERSION": abi,
    "ODG_SAVE_SCHEMA_VERSION": save,
}
for relative in required:
    path = ROOT / relative
    if not path.is_file():
        continue
    text = path.read_text(encoding="utf-8")
    for name, expected in assignment_patterns.items():
        for match in re.finditer(rf"{re.escape(name)}\s*=\s*(\d+)", text):
            actual = int(match.group(1))
            if actual != expected:
                errors.append(f"{relative}: stale {name} assignment {actual}, expected {expected}")

if errors:
    for error in errors:
        print(f"DOC CONTRACT FAIL: {error}")
    raise SystemExit(1)

print(f"DOC CONTRACT OK api={api} abi={abi} save={save} current_files={len(required)}")
