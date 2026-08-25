#!/usr/bin/env python3
from __future__ import annotations
import hashlib
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
paths = [ROOT / "engine/include/odpar/odg_host.h", ROOT / "app/lib/src/native/generated_host.dart"]
before = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
subprocess.run([sys.executable, str(ROOT / "tools/generate_host_contract.py")], check=True)
after = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
if before != after:
    print("generated host contract was stale", file=sys.stderr)
    raise SystemExit(1)
print("generated host contract: OK")
