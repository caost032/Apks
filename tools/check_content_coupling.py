#!/usr/bin/env python3
"""Fail when generic runtime modules grow concrete flora/fauna dependencies.

Concrete content IDs are expected in registries, tables, renderer morphology and focused
content modules. They are *not* expected in generic movement, interaction, territory,
resource or world plumbing. Keeping this boundary executable prevents a new species
from requiring edits across unrelated runtime systems.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]

GENERIC_RUNTIME_MODULES = (
    "engine/src/resources.c",
    "engine/src/sim.c",
    "engine/src/interactions.c",
    "engine/src/environment.c",
    "engine/src/territory_policy.c",
    "engine/src/entities.c",
    "engine/src/game.c",
)

FORBIDDEN_PATTERNS = (
    (re.compile(r"\bODG_FLORA_SPECIES_[A-Z0-9_]+\b"), "concrete flora species"),
    (re.compile(r"\bODG_FAUNA_SPECIES_[A-Z0-9_]+\b"), "concrete fauna species"),
)

failed = []
for rel in GENERIC_RUNTIME_MODULES:
    path = ROOT / rel
    text = path.read_text(encoding="utf-8")
    for line_no, line in enumerate(text.splitlines(), 1):
        for pattern, description in FORBIDDEN_PATTERNS:
            match = pattern.search(line)
            if match is not None:
                failed.append(
                    f"{rel}:{line_no}: {description} {match.group(0)} in generic runtime"
                )

# Nutrition is intentionally a content registry + generic consumer in one compact module.
# Its table may name concrete foods, but runtime code after the table must not special-case
# those item IDs. This catches an APPLE/RAW_MEAT branch without outlawing the registry.
nutrition = (ROOT / "engine/src/nutrition.c").read_text(encoding="utf-8")
registry_end = nutrition.find("const odg_food_definition *odg_food_definition_internal")
if registry_end < 0:
    failed.append("engine/src/nutrition.c: food registry boundary missing")
else:
    runtime = nutrition[registry_end:]
    for token in ("ODG_ITEM_APPLE", "ODG_ITEM_RAW_MEAT"):
        if re.search(rf"\b{token}\b", runtime):
            failed.append(
                f"engine/src/nutrition.c: runtime coupling to concrete food token {token}"
            )

if failed:
    print("CONTENT COUPLING FAILED")
    for row in failed:
        print(" -", row)
    sys.exit(1)

print(
    "CONTENT COUPLING OK "
    f"generic_modules={len(GENERIC_RUNTIME_MODULES)} nutrition_runtime=1 concrete-runtime-gaps=0"
)
