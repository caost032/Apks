#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import subprocess
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: check_native_hardening.py <shared-library>")
lib = Path(sys.argv[1])
if not lib.is_file():
    raise SystemExit(f"missing shared library: {lib}")

program = subprocess.run(
    ["readelf", "-lW", str(lib)],
    check=True,
    text=True,
    stdout=subprocess.PIPE,
).stdout
dynamic = subprocess.run(
    ["readelf", "-dW", str(lib)],
    check=True,
    text=True,
    stdout=subprocess.PIPE,
).stdout

errors: list[str] = []
if "GNU_RELRO" not in program:
    errors.append("GNU_RELRO missing")
stack_lines = [line for line in program.splitlines() if "GNU_STACK" in line]
if not stack_lines:
    errors.append("GNU_STACK missing")
elif any("E" in line.split()[-2] for line in stack_lines if len(line.split()) >= 2):
    errors.append("executable GNU_STACK")
if "BIND_NOW" not in dynamic and "NOW" not in dynamic:
    errors.append("immediate binding (-z now) missing")

if errors:
    print("NATIVE HARDENING FAILED", file=sys.stderr)
    for error in errors:
        print(f" - {error}", file=sys.stderr)
    raise SystemExit(1)
print("native hardening: OK (RELRO, NOW, non-exec stack)")
