#!/usr/bin/env python3
"""Guard the incremental native/WASM build contract declared by PROJECT_SPINE.json.

This intentionally validates structure, not timestamps: the real dependency graph remains Make's
job, while this guard prevents a future refactor from silently returning to monolithic compilation
while the project spine still claims incremental build targets.
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAKEFILE = (ROOT / "Makefile").read_text(encoding="utf-8")
SPINE = json.loads((ROOT / "PROJECT_SPINE.json").read_text(encoding="utf-8"))


def fail(message: str) -> None:
    print(f"BUILD CONTRACT FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


runtime = SPINE.get("runtime", {})
if runtime.get("incremental_native_wasm_build_targets") is not True:
    fail("PROJECT_SPINE runtime must explicitly require incremental native/WASM build targets")

required_literals = (
    "NATIVE_OBJECTS := $(patsubst %.c,$(BUILD)/native-obj/%.o,$(ENGINE))",
    "NATIVE_DEPS := $(NATIVE_OBJECTS:.o=.d)",
    "WASM_OBJECTS := $(patsubst %.c,$(BUILD)/wasm-obj/%.o,$(ENGINE))",
    "WASM_DEPS := $(WASM_OBJECTS:.o=.d)",
    "$(BUILD)/native-obj/%.o: %.c",
    "$(BUILD)/wasm-obj/%.o: %.c",
    "$(NATIVE_LIB): $(NATIVE_OBJECTS)",
    "$(WASM_BIN): $(WASM_OBJECTS)",
    "-include $(TEST_DEPS) $(NATIVE_DEPS) $(WASM_DEPS)",
)
for literal in required_literals:
    if literal not in MAKEFILE:
        fail(f"missing incremental build marker: {literal}")

for rule in ("native-obj/%.o", "wasm-obj/%.o"):
    match = re.search(rf"^\$\(BUILD\)/{re.escape(rule)}: %.c\n(?P<body>(?:\t.*\n)+)", MAKEFILE, re.MULTILINE)
    if not match:
        fail(f"cannot inspect {rule} compile rule")
    body = match.group("body")
    if " -MMD -MP -c $< -o $@" not in body:
        fail(f"{rule} must emit dependency metadata and compile one source via $<")
    if "$(ENGINE)" in body:
        fail(f"{rule} regressed to compiling the complete engine source list")

native_link = re.search(r"^\$\(NATIVE_LIB\):[^\n]*\n(?P<body>(?:\t.*\n)+)", MAKEFILE, re.MULTILINE)
wasm_link = re.search(r"^\$\(WASM_BIN\):[^\n]*\n(?P<body>(?:\t.*\n)+)", MAKEFILE, re.MULTILINE)
if not native_link or "$(NATIVE_OBJECTS)" not in native_link.group("body"):
    fail("native link must consume native objects")
if not wasm_link or "$(WASM_OBJECTS)" not in wasm_link.group("body"):
    fail("WASM link must consume WASM objects")

if "build-contract-check" not in re.search(r"^quick-gate:.*$", MAKEFILE, re.MULTILINE).group(0):
    fail("quick-gate does not enforce build-contract-check")

print("BUILD CONTRACT OK native=incremental wasm=incremental deps=MMD+MP spine=verified")
