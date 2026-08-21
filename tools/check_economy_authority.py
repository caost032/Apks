#!/usr/bin/env python3
"""Fail closed when gameplay economy rules are duplicated outside their data authority.

The recipe table owns ingredient quantities. AI owns strategic ordering only. Repair
profiles own tool-repair costs. Construction salvage must derive raw recovery from the
same structural-module recipe rather than inventing a parallel conversion table.
"""
from __future__ import annotations

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
SIM = (ROOT / "engine/src/sim.c").read_text(encoding="utf-8")
CRAFT = (ROOT / "engine/src/crafting.c").read_text(encoding="utf-8")
CONSTRUCTION = (ROOT / "engine/src/construction.c").read_text(encoding="utf-8")


def fail(message: str) -> None:
    print(f"ECONOMY AUTHORITY FAILED: {message}", file=sys.stderr)
    raise SystemExit(1)


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        fail(f"missing function {signature}")
    brace = source.find("{", start)
    if brace < 0:
        fail(f"cannot find body for {signature}")
    depth = 0
    for i in range(brace, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1 : i]
    fail(f"unterminated body for {signature}")
    return ""


economy = function_body(SIM, "int odg_bot_economy_direction_internal")
if "bot_recipe_goal(" not in economy:
    fail("bot economy does not delegate ingredient acquisition to recipe authority")
if "bot_harvest_goal(" in economy:
    fail("bot economy directly harvests recipe ingredients instead of using bot_recipe_goal")
for name in ("wood", "stone", "iron"):
    if re.search(rf"\b{name}\s*=\s*odg_inventory_total", economy):
        fail(f"bot economy reintroduced shadow {name} ingredient totals")

raft = function_body(SIM, "int odg_bot_logistics_prepare_vehicle_internal")
if "odg_recipe_get(ODG_RECIPE_RAFT" not in raft or "bot_gather_resource_land_only" not in raft:
    fail("raft bootstrap inputs are not derived from the raft recipe with land-only gathering")
if re.search(r"\bwood\s*<\s*16u\b", raft):
    fail("raft bootstrap reintroduced the old hard-coded 16-wood recipe cost")

for obsolete in ("repair_resource_for_tier", "repair_full_cost_for_tier", "repair_station_for_tier"):
    if obsolete in CRAFT:
        fail(f"obsolete branch-based tool repair helper remains: {obsolete}")
if "g_tool_repair_profiles" not in CRAFT or "tool_repair_profiles_validate" not in CRAFT:
    fail("tool repair is not backed by a validated repair profile table")

salvage = function_body(CONSTRUCTION, "int odg_construction_dismantle_internal")
if "odg_recipe_find_output_internal" not in salvage or "recipe.ingredients[0]" not in salvage:
    fail("damaged structural salvage is not derived from authoritative build recipes")
if "recovered.type_id=ODG_ITEM_BUILDING_BLOCK" not in salvage:
    fail("intact structural modules no longer preserve reusable-module recovery")

print(
    "ECONOMY AUTHORITY OK "
    "bot_recipe_inputs=authoritative raft_inputs=authoritative "
    "tool_repair=data-driven damaged_salvage=recipe-driven"
)
