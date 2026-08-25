#!/usr/bin/env python3
from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SCHEMA = json.loads((ROOT / "host/schema/host_api.json").read_text())
EXPECTED = set(SCHEMA["functions"])

if len(sys.argv) != 2:
    raise SystemExit("usage: check_native_symbols.py <shared-library>")
lib = Path(sys.argv[1])
if not lib.is_file():
    raise SystemExit(f"missing shared library: {lib}")

proc = subprocess.run(
    ["nm", "-D", "--defined-only", str(lib)],
    check=True,
    text=True,
    stdout=subprocess.PIPE,
)
actual: set[str] = set()
for line in proc.stdout.splitlines():
    parts = line.split()
    if len(parts) >= 3:
        name = parts[-1]
        if name.startswith("odg_"):
            actual.add(name)
missing = sorted(EXPECTED - actual)
extra = sorted(actual - EXPECTED)
if missing or extra:
    if missing:
        print("missing exported host symbols:", ", ".join(missing), file=sys.stderr)
    if extra:
        print("unexpected exported odg symbols:", ", ".join(extra), file=sys.stderr)
    raise SystemExit(1)
print(f"native symbol allowlist: OK ({len(actual)} symbols)")
