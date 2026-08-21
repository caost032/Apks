# ODPAR: Territorial Domain — v15 Open Domain

**Created & developed by kaost032**

Current development checkpoint: **API37 / ABI9 / SAVE25 (WORLDGEN5 advanced systemic checkpoint)**. See `CHECKPOINT_STATUS_2026-08-20.md` for the exact verification boundary.

ODPAR: Territorial Domain is a persistent 3D territory game built around one deterministic C11
authority running at 120 Hz. Flutter/Dart is the Android product host and UI; FFI is the versioned
contract; the dependency-free C renderer owns the 3D world. Browser/WASM remains a secondary preview
and test surface only.

## Product loop

Explore → claim territory → harvest → craft → build → defend/repair → travel → return to a persistent world.

- Open Domain has no 55% terminal condition. Territory share is a leaderboard signal.
- The player and nine deterministic bots use the same inventory, crafting, structures, vehicles and
  resource rules; bots do not receive free rafts, repairs or construction materials.
- World/entity positions are authoritative 64-bit fixed-point. The 128×128 local window is only a
  precision/render cache over deterministic 32×32 chunks, not the world boundary.
- Modified chunks, depleted resources, territory, trails, pickups, artifacts and construction persist.
- Actors remain clean cubes; Minecraft-like techniques are used for data/chunk efficiency, not to
  force a Minecraft visual style.

## Current systemic capabilities

- Generic item registry, 4-slot hotbar and equippable backpack expansion to 12 slots.
- Workbench/Smithy crafting, proportional tool repair, persistent chests and physical drops.
- Wood/stone/iron progression, tools, turrets, reprogram/ascension chips and ammunition economy.
- Survival: hunger, hydration, oxygen, water depth, swimming/buoyancy and drowning.
- Ecology: flora lifecycle, food, fluids, fauna habitats/diets/reproduction/nesting, aquatic and night fauna.
- Mobility: persistent wooden raft with real placement, mount, steering, coast dismount and bot logistics.
- Construction uses a compact sparse store rather than heavyweight artifacts. SAVE19 adds typed
  floor/wall/doorway/roof topology; SAVE20 adds structural integrity, enemy damage, repair and roof
  collapse. Damaged dismantling returns recipe-derived raw salvage instead of silently becoming a
  pristine stack item.
- Shared `world_physics.c` authority owns terrain/occupancy/airspace queries used across gameplay.
- WORLDGEN3 makes natural turret locations physically valid while preserving legacy v1/v2 worlds;
  WORLDGEN4 gives every procedural resource a canonical `(seed, chunk, ordinal, kind)` identity,
  position and flora species so depletion, occupancy and streaming cannot relocate the world.
- SAVE23 separates portable/manual instance IDs from the high-bit namespace reserved for deterministic
  procedural turrets and migrates historical SAVE22 carried-natural-turret handles transactionally.
- Runtime ecology is body-safe: fauna avoids solids/bodies, birds land only on valid ground, flora
  growth waits for expansion clearance, and blocked egg hatching retains eggs instead of deleting them.

## Renderer

The native renderer provides continuous height-field terrain, water, shadows, atmosphere, fog,
territory contours, trail ribbons, resources, flora/fauna, artifacts, construction morphology,
construction damage fractures, avatar face textures, GlyphAtlas, item cards and remote views.
Music analysis is presentation-only and is excluded from gameplay hash, AI, collision, crafting,
territory and RNG.

## Android / Flutter

`app/flutter/` is the primary product host. It provides multitouch routing, HUD/hotbar, inventory,
crafting, map, artifacts, construction shape selection, world save presentation, controls/settings,
avatar skins and local music. No WebView and no parallel Dart gameplay implementation are used.

The current native contract is **API37 / FFI ABI9 / SAVE25**. FFI v9 validates construction snapshots and the explicit artifact paging `total_count` contract. Save compatibility is governed by the save schema,
not by equality with the creator's API/ABI metadata.

## Featured music — AFTERIMAGE 0.2

**7 Original Tracks · 5 Instrumental Reworks · Music catalog by kaost032**

The 12 official MP3s are under `app/flutter/assets/music/afterimage_0_2/` and SHA-256 checked against
`docs/AFTERIMAGE_0_2_CATALOG.json`.

## Fast verification

```sh
make quick-gate             # daily C/FFI/world/Flutter-contract/native/WASM gate
make test                   # broader deterministic gameplay regression + FFI
make catalog-check          # bundled music manifest
make host-check             # C ↔ Dart/Flutter authority contract
make docs-contract-check    # current docs ↔ C API/ABI/save authority
make native                 # hardened shared library + exact symbol surface
```

Long sanitizer/soak gates remain available (`make asan`, `make soak`) but are intentionally not run
as a routine iteration blocker.

For APK creation use `app/flutter/` or the Android GitHub Actions workflow. This preparation host does
not claim a locally built APK unless a complete Flutter/Android SDK/NDK toolchain is present.

## Scope not claimed

Current play is local against deterministic bots. Deployed online multiplayer, matchmaking, account
services, server reconciliation and anti-cheat backend are not claimed.
