# ODPAR: Territorial Domain — advanced working-tree state

Checkpoint continuation date: **2026-08-20 America/Guayaquil (-05:00)**

This file describes the current most advanced tree. Historical API29/ABI6/SAVE18 handoff text was
moved to `docs/history/HANDOFF_API29_ABI6_SAVE18_2026-08-20.md` so it cannot be mistaken for current authority.

## Authoritative contract

- Game API: **37**
- FFI ABI: **9**
- Save schema written: **25**
- Save schemas intentionally readable: **14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25** through explicit migration logic.
- Android product authority: Flutter/Dart host → explicit FFI → native C11.
- Web/WASM: secondary preview/test host only.

## Important advances now present

- Artifact paging has an explicit `total_count`; no public `reserved_*` field carries hidden meaning.
- Rain-barrel fluid identity is explicitly named in persisted state.
- Shared `world_physics.c` centralizes terrain/occupancy/airspace authority.
- Construction is a compact sparse store with SAVE19 typed floor/wall/doorway/roof topology.
- SAVE20 adds structural health, hostile damage, same-material repair and support-dependent roof collapse.
- Construction health is visible through FFI snapshots and through native renderer damage treatment.
- Repair hints correctly win over placement when a damaged controlled structure is targeted.
- Bots preserve/repair home infrastructure using real inventory/crafting authority.
- Bots can acquire/craft/deploy/mount a real raft when navigation requires water mobility.
- Chunk-summary refresh is hash-neutral; derived summaries no longer mutate authoritative logical state.
- Redundant mutable weather epoch state was removed as an authority; deterministic weather derives from tick.
- Construction damaged-dismantle exploit is closed: intact modules are reusable, damaged modules produce
  recipe-derived raw salvage instead of becoming pristine stack items.
- Bot construction recipe lookup uses recipe authority rather than a duplicated material→recipe hardcode.
- `make quick-gate` now includes a documentation-contract guardian so current docs cannot advertise stale API/ABI/save versions.
- SAVE21 / WORLDGEN3 explicitly versions physically safe natural-turret placement; legacy worldgen v1/v2 remains frozen.
- SAVE22 / WORLDGEN4 canonicalizes procedural resource position, stable identity and flora species, so depleted or temporarily blocked nodes never shift after streaming.
- SAVE23 reserves the instance-ID high bit for deterministic procedural turrets. Historical SAVE22
  portable natural turrets are rebound to sequential IDs with their exact payload handles during load.
- Static materialization defers around dynamic bodies instead of relocating world content.
- Fauna spawn and locomotion are body-safe; flying fauna cannot snap down into solids, and airborne birds do not create fake ground colliders.
- Flora stage expansion is collision-safe and catches up after blockers leave; congested egg hatching preserves unhatched eggs for retry.

## Verification boundary

Focused component suite after the latest gameplay changes is green:

```text
FFI v9 OK api=34 portrait=720x1280 pixels=921600 hash=1c8e58c68b5c6bdf
ECOSYSTEM OK api=34 abi=9 fauna=7 habitats=7 food=3 flora=1 motion=body-safe+landing-safe flora-growth=collision-safe save=22
WORLD SYSTEMS OK geology=caves+coal+iron water=swim+oxygen+fish+croc+raft+body-safe night=stalker torch=ready construction=territory+body-safe turret=coastal-majority+worldgen-v3-safe+stream-defer+global-domain+body-safe resources=worldgen-v4-canonical+depletion-persistent+stream-defer ammo=smithy api=34 save=22
BOOTSTRAP OK seeds=2,3,17,19,31,41 actors=safe+nonoverlap resources=3wood+2stone+surface workbench=1+safe chips=4+safe turrets=safe+reserved api=34 save=22
TERRITORY COMBAT OK identity=actor trail-vs-ground=symmetric self-trail=nonlethal respawn=safe-ground+fail-closed api=34 save=22
CONSTRUCTION OK store=72B artifact=1048B paging=64+16 migration=17->22 shapes=floor+wall+doorway+roof durability=damage+repair+collapse salvage=recipe-driven dynamic=body-safe api=34 save=22
COHERENCE OK items=28 sources+sinks=closed repair=capability-driven ground-food=expires construction=lightweight
BOT ECONOMY OK stages=wood-axe,wood-pick,backpack,stone-axe,stone-pick,smithy,iron-pick,home-fortification,salvage-reuse,layered-salvage,structure-repair,raft-logistics
WORLD CONTINUITY OK recenters=48 origin=(1472,-64)
SOAK OPEN-DOMAIN OK ticks=720 hash=2a4a775cd8613164 cells=49 alive=10 turrets=4 resources=210 artifacts=10 pickups=4
HOST CONTRACT OK api=34 abi=9 save=22 dart_symbols=88 public_c=149 mirrored_constants=54 engine_units=29 authority=C11 webview=no
SYSTEM GRAPH OK systems=28 modules=30 functions=916 calls=505 public=149 gaps=0
AFTERIMAGE 0.2 OK originals=7 reworks=5 tracks=12 owner=kaost032
CONTENT COUPLING OK generic_modules=7 nutrition_runtime=1 concrete-runtime-gaps=0
ECONOMY AUTHORITY OK bot_recipe_inputs=authoritative raft_inputs=authoritative tool_repair=data-driven damaged_salvage=recipe-driven
RESERVED CONTRACTS OK artifact_total=explicit artifact_fluid=semantic construction_integrity=explicit weather_reserved=inactive chunk_reserved=unused map_reserved=zero
DOC CONTRACT OK api=34 abi=9 save=22 current_files=8
NATIVE SYMBOL SURFACE OK public=149 extra=0
NATIVE HARDENING OK relro=full nx_stack=yes canary=yes textrel=no
WASM OK api=34 bytes=449649 framebuffer=518400 sampleHash=f1ff317b claimed=563 territory=22.3% alive=10 turrets=1
```

The component orchestration above completed in 29.26 s. A final `make quick-gate` is run again after
current-document synchronization. No long ASan/UBSan/soak campaign is claimed.

## Current priorities for further continuation

1. Keep C/FFI/Flutter/save/docs authority synchronized; do not reuse reserved fields for semantics.
2. Continue construction toward richer physically accurate openings/orientation only with an explicit persistence model.
3. Broaden bot use of structural shapes without giving AI privileged build rules.
4. Keep ecology/world additions registry-driven and connect acquisition → use → persistence → AI → renderer.
5. Continue native visual inspection from freshly generated captures rather than old screenshots.
6. Validate/build APK in a real Flutter + Android SDK/NDK environment or CI; do not fake a local APK claim.
