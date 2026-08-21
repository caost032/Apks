# ODPAR: Territorial Domain — current master continuation handoff

**CURRENT AUTHORITY: API 37 / ABI 9 / SAVE 25**

This is the continuation authority for the most advanced working tree. Do **not** restart from API29,
ABI6 or SAVE18. That historical handoff is preserved only under `docs/history/`.

## 1. Product doctrine

Primary product path:

```text
Android APK → Flutter/Dart host → explicit FFI → authoritative C11 runtime/render
```

- Flutter owns UI, touch, preferences, files and host lifecycle.
- C11 owns gameplay, world, inventory, territory, AI, construction, persistence and 3D rendering.
- Web/WASM is preview/testing only; never constrain native design for browser convenience.
- No WebView. No gameplay rewrite in Dart. No duplicated rules between Dart and C.
- Keep the fixed 120 Hz deterministic simulation.

## 2. Current contracts

- `ODG_API_VERSION = 37`
- `ODG_FFI_ABI_VERSION = 9`
- `ODG_SAVE_SCHEMA_VERSION = 25`
- `ODG_WORLDGEN_VERSION_CURRENT = 4` (canonical procedural resources; v3 safe natural turrets is retained as an explicit older boundary)
- readable save chain: 14 → 15 → 16 → 17 → 18 → 19 → 20 → 21 → 22 → 23 → 24 → 25
- public native symbol count currently: 149
- FFI ABI v9 discovery struct remains 112 bytes and validates construction snapshot size plus the explicit artifact paging `total_count` contract.

API/ABI in a save header are provenance, not save compatibility. Never strand a world because the UI
ABI changed. Save compatibility is governed by DataVersion/schema and deliberate migrations.

## 3. Current architecture additions

`engine/src/world_physics.c` is the shared physical authority for world occupancy, terrain and
airspace. Do not reintroduce local arena-edge assumptions or parallel collision tests in unrelated modules.

Construction is **not** a generic artifact:

- compact `odg_construction_block` remains 72 bytes;
- floor / wall / doorway / roof are typed layers;
- roof requires support;
- wall is a physical ground obstacle;
- doorway center remains open;
- SAVE20 stores `health` + `max_health` explicitly;
- support destruction collapses dependent roof;
- controlled damaged structure can be repaired with matching structural material;
- intact dismantle returns one structural module;
- damaged dismantle returns raw salvage derived from the authoritative build recipe so dismantle→place cannot heal for free.

Do not add per-block mini-inventories or artifact-sized payload to lightweight structural cells.

## 4. FFI / Flutter state

The public artifact snapshot now has a real `total_count`. Do not resurrect hidden semantics in
`reserved_u32`. `tools/check_reserved_contracts.py` guards this class of debt.

Flutter currently:

- negotiates API37/ABI9;
- paginates artifacts and construction;
- models construction shape, controller, health/max-health;
- exposes SUELO / MURO / VANO / TECHO mode selection;
- maps native interaction hints to COLOCAR / DESMONTAR / REPARAR / GOLPEAR;
- treats save API/ABI as provenance while using save schema compatibility.

Any future public POD meaning/layout change requires deliberate ABI evolution and synchronized C,
Dart, tests, web smoke and host guard changes.

## 5. Persistence chain

Important schema boundaries:

- SAVE16: nest substrate semantics;
- SAVE17: persistent runtime/worldgen provenance + respiration;
- SAVE18: lightweight construction section;
- SAVE19: construction shape/topology semantics;
- SAVE20: construction integrity semantics in bytes that were previously reserved zeros;
- SAVE21: WORLDGEN3 provenance boundary for safe natural-turret placement;
- SAVE22: WORLDGEN4 provenance boundary for canonical procedural resources;
- SAVE23: portable/manual instance IDs are restricted to the low 63-bit sequential namespace while
  deterministic procedural turret IDs reserve the high bit. Historical SAVE22 carried natural
  turrets migrate primary record + exact inventory/pickup/storage payload handle atomically.

`odg_chunks_refresh_summaries()` must remain a derived cache refresh and hash-neutral. The current
construction regression explicitly checks deterministic save/hash behavior.

Never reinterpret future values inside an older schema. Fail closed if an old schema contains a
semantic value it could never have emitted.

## 6. Bots and economy

Bots must use the same rules as the player.

Current advanced bot loops include:

- wood axe → wood pick → backpack → stone tools → smithy → iron pick;
- home fortification;
- layered construction salvage constraints;
- damaged home-structure repair;
- conquered/neutral reusable infrastructure;
- route-water detection;
- real raft reuse or wood gathering → workbench → craft raft → deploy → mount → navigate → coast dismount.

The previous hardcoded material→building-recipe helper has been removed. Use
`odg_recipe_find_output_internal()` when AI needs the authoritative recipe for an output/material.
It fails closed on ambiguous producers; never restore table-order heuristics.

## 7. Renderer / visuals

Native renderer evidence is under `artifacts/graphics_inspection_2026_08_20/` and was generated by the
C renderer, not an image generator. Construction has distinct floor/wall/doorway/roof silhouettes and
health-dependent damage treatment with diagonal fracture marks.

Do not trust old screenshots after renderer work. Regenerate captures and inspect the new files.
Keep physics and visuals semantically aligned; visual detail may be richer than collision but must not
imply a solid passage where gameplay says open, or vice versa.

## 8. Guardians / gates

Normal iteration:

```sh
make ffi-test
make ecosystem-test
make world-systems-test
make construction-test
make coherence-test
make bot-economy-test
make continuity-test
make host-check
make graph-check
make catalog-check
make content-coupling-check
make reserved-contracts-check
make docs-contract-check
```

Then:

```sh
make quick-gate
```

`quick-gate` also covers hardened native symbol surface and WASM smoke.

Strict C compile flags stay:

```text
-std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Werror
```

Do not routinely block the container with long ASan/UBSan/soak runs. Use them only when specifically
requested or when a focused memory/UB defect justifies them.

## 9. Documentation authority guardian

`tools/check_docs_contract.py` now reads API/ABI/SAVE values directly from `odpar_game.h` and rejects
stale “current tree” documentation. Current guarded files include root README/certification/status/
handoff plus architecture/embedding/Flutter docs.

Historical version references belong in CHANGELOG or `docs/history/`, not in current-contract prose.

## 10. No-gaps checklist for new systems

For each new material/item/species/vehicle/block/world mechanic, cover every applicable edge:

1. stable ID/key;
2. registry/profile;
3. capabilities/tags;
4. validation/cross-reference;
5. deterministic RNG ownership;
6. runtime behavior;
7. spawn/habitat/worldgen;
8. physical medium/collision;
9. acquisition/source;
10. inventory/payload truth;
11. crafting/processing;
12. use/sink;
13. loot/decay;
14. territory/control;
15. bot reasoning;
16. death/recovery;
17. persistence/migration;
18. floating-origin/global position;
19. renderer morphology;
20. map representation;
21. FFI/query if needed;
22. export map/WASM if public;
23. Dart layout/binding if public;
24. Flutter presentation;
25. graph edge;
26. coupling/reserved/docs guardians;
27. focused regression;
28. fresh visual evidence when visual.

A feature is not complete because C compiles.

## 11. Highest-value next directions

Continue from the current tree, not from a predetermined feature list. Prefer closing systemic gaps
over adding disconnected content. Particularly valuable areas:

- construction orientation/opening physics if implemented with explicit persistence rather than hidden state;
- more bot structural reasoning using the same shape/support rules as the player;
- richer world/ecology only when sources, sinks, AI, save and rendering all connect;
- further renderer readability/performance passes with fresh native captures;
- Android toolchain/CI validation of the actual Flutter APK;
- continued pruning of duplicated authority and cache state.

## 12. Current known non-claims

- no deployed online multiplayer backend;
- no matchmaking/accounts/server reconciliation/anti-cheat service;
- no claim of a locally produced APK on a host lacking Flutter/Android SDK/NDK;
- no long sanitizer/soak certification unless explicitly run and recorded.

## 13. Continuation principle

Use Minecraft-like engineering ideas where useful — registries, compact world records, chunk
separation, deterministic generation, resource→craft→build progression — but do not copy its cubic
aesthetic or create features just because Minecraft has them.

The goal is a larger game with **fewer contradictory authorities**. Every new system must make the
surrounding systems more coherent, not leave the next chat a half-finished island.


## Latest physical/streaming authority

- WORLDGEN4 canonical resource layout derives position, stable ID and flora species solely from seed/chunk/ordinal/kind.
- Dynamic occupancy delays materialization; it never chooses a different world position.
- Manual artifact/construction/turret placement shares global territory + surface + static + body checks.
- Fauna spawn/motion/landing is body-safe; ground wildlife blocks actors while territorial runners remain non-solid to one another so trail combat semantics are preserved.
- Flora growth may age while physically blocked but cannot enlarge through bodies/structures; it catches up when clear.
- Eggs that cannot hatch physically remain in the nest and retry rather than disappearing.
