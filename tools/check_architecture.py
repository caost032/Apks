#!/usr/bin/env python3
from __future__ import annotations
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
errors: list[str] = []
forbidden_names = {"sim.c", "render.c", "game_internal.h", "realism.c", "world_everything.c"}
for p in ROOT.rglob("*"):
    if not p.is_file():
        continue
    rel = p.relative_to(ROOT).as_posix()
    if p.name in forbidden_names:
        errors.append(f"forbidden legacy-shaped source name: {rel}")
    if any(part in {"build", ".dart_tool", ".gradle"} for part in p.parts):
        continue
    if p.suffix.lower() in {".c", ".h", ".dart", ".kt", ".gradle", ".py", ".yml", ".yaml"}:
        text = p.read_text(errors="replace")
        if re.search(r"\bg_odg\b", text):
            errors.append(f"legacy global state leaked into {rel}")
        if "abiFilters" in text and rel.endswith("build.gradle"):
            errors.append(f"static Android ABI authority forbidden: {rel}")

# UI cannot reach native bindings directly; it talks to EngineBridge only.
for p in (ROOT / "app/lib/src/ui").rglob("*.dart"):
    text = p.read_text(errors="replace")
    if "/native/" in text or "dart:ffi" in text:
        errors.append(f"UI bypasses engine bridge: {p.relative_to(ROOT)}")

# Every C translation unit in engine/src must be compiled by both host Makefile and CMake.
c_files = sorted(p.relative_to(ROOT).as_posix() for p in (ROOT / "engine/src").glob("*.c"))
make = (ROOT / "Makefile").read_text()
cmake = (ROOT / "CMakeLists.txt").read_text()
for rel in c_files:
    if rel not in make:
        errors.append(f"C source not in Makefile: {rel}")
    if rel not in cmake:
        errors.append(f"C source not in CMakeLists: {rel}")

if errors:
    print("ARCHITECTURE CHECK FAILED", file=sys.stderr)
    for e in errors:
        print(f" - {e}", file=sys.stderr)
    raise SystemExit(1)
print(f"architecture check: OK ({len(c_files)} C translation units)")
