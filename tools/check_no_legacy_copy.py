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

# A byte-identical match is strong evidence of direct legacy reuse for authored
# project files, but not for deterministic metadata emitted by an external
# toolchain. Keep this allowlist deliberately narrow and pair each Greenfield
# path with the exact legacy path it is allowed to match. This avoids weakening
# the guard for C/Dart/Kotlin source, assets, product configuration, or docs.
STANDARD_TOOLCHAIN_EQUIVALENCE: dict[str, frozenset[str]] = {
    "app/android/gradle/wrapper/gradle-wrapper.properties": frozenset(
        {"app/flutter/android/gradle/wrapper/gradle-wrapper.properties"}
    ),
}

legacy_by_hash: dict[str, list[str]] = {}
with LEGACY.open(newline="", encoding="utf-8") as handle:
    for row in csv.DictReader(handle, delimiter="\t"):
        digest = row["sha256"]
        legacy_by_hash.setdefault(digest, []).append(row["path"])

checked = 0
matches: list[tuple[str, str]] = []
standard_equivalences: list[tuple[str, str]] = []
for path in sorted(ROOT.rglob("*")):
    if not path.is_file():
        continue
    rel = path.relative_to(ROOT).as_posix()
    if rel in EXCLUDED_FILES or any(part in EXCLUDED_PARTS for part in path.relative_to(ROOT).parts):
        continue
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    checked += 1
    for legacy_path in legacy_by_hash.get(digest, []):
        allowed_legacy_paths = STANDARD_TOOLCHAIN_EQUIVALENCE.get(rel, frozenset())
        if legacy_path in allowed_legacy_paths:
            standard_equivalences.append((rel, legacy_path))
            continue
        matches.append((rel, legacy_path))

if matches:
    print("NO-DIRECT-COPY CHECK FAILED")
    for greenfield, legacy in matches:
        print(f" - Greenfield {greenfield} is byte-identical to legacy {legacy}")
    raise SystemExit(1)

for greenfield, legacy in standard_equivalences:
    print(
        "standard toolchain equivalence: OK "
        f"({greenfield} == {legacy}; deterministic Gradle wrapper metadata)"
    )
print(
    "no-direct-copy check: OK "
    f"({checked} Greenfield files, {len(legacy_by_hash)} legacy hashes, "
    f"{len(standard_equivalences)} standard toolchain equivalence(s))"
)
