#!/usr/bin/env python3
from __future__ import annotations

import csv
import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LEGACY = ROOT / "docs/audit/LEGACY_MANIFEST_AND_DISPOSITION.tsv"
EXCLUDED_PARTS = {".git", ".dart_tool", ".gradle", "build", "build-cmake", "build-asan", "__pycache__"}
EXCLUDED_FILES = {
    "app/android/local.properties",
    "docs/audit/NO_DIRECT_COPY_CHECK.txt",
    "CHECKPOINT_MANIFEST.sha256",
    "CHECKPOINT_STATE.json",
}

legacy_by_hash: dict[str, list[str]] = {}
with LEGACY.open(newline="", encoding="utf-8") as handle:
    for row in csv.DictReader(handle, delimiter="\t"):
        digest = row["sha256"]
        legacy_by_hash.setdefault(digest, []).append(row["path"])

checked = 0
matches: list[tuple[str, str]] = []
for path in sorted(ROOT.rglob("*")):
    if not path.is_file():
        continue
    rel = path.relative_to(ROOT).as_posix()
    if rel in EXCLUDED_FILES or any(part in EXCLUDED_PARTS for part in path.relative_to(ROOT).parts):
        continue
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    checked += 1
    for legacy_path in legacy_by_hash.get(digest, []):
        matches.append((rel, legacy_path))

if matches:
    print("NO-DIRECT-COPY CHECK FAILED")
    for greenfield, legacy in matches:
        print(f" - Greenfield {greenfield} is byte-identical to legacy {legacy}")
    raise SystemExit(1)

print(f"no-direct-copy check: OK ({checked} Greenfield files, {len(legacy_by_hash)} legacy hashes)")
