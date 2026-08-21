# Architecture — ODPAR: Territorial Domain v15 advanced tree

## Product authority

```text
Android APK
   ↓
Flutter / Dart host + UI + input + preferences + files
   ↓
FFI ABI 9 (versioned commands, snapshots, registries, world/map/render buffers)
   ↓
C11 authoritative game runtime @ 120 Hz
   ├─ world physics / territory / trails / actors / bots
   ├─ items / inventory / crafting / resources / artifacts / turrets
   ├─ construction topology + integrity
   ├─ survival / fluids / flora / fauna / ecology
   ├─ deterministic chunks / save migrations / strategic map
   ├─ MusicAnalyzer presentation data
   └─ software 3D renderer
```

The current C authority is API 37 / FFI ABI 9 / save schema 25. Flutter does not reproduce gameplay
rules and Kotlin contains no gameplay. Web/WASM is a preview/test host and must not constrain Android.

## Coordinates, chunks and physics

Authoritative actor/entity positions are 64-bit fixed-point global coordinates. Simulation and render
keep a 128×128 floating-origin local window. Deterministic 32×32 chunks derive from seed + global
coordinates; only visited/modified runtime state needs persistence.

`world_physics.c` is the shared physical authority for continuous terrain, obstacle occupancy,
construction collision and airspace checks. Rendering may add presentation detail, but it must not
invent a second physical world.

WORLDGEN versions are behavioral provenance, not a mutable “latest terrain” flag. v2 adds bathymetry,
v3 makes natural turret placement physically safe, and v4 makes procedural resource identity/position
canonical. SAVE21→22 migration canonicalizes already-materialized v3 resources while preserving their
harvest progress and other gameplay state.

## Data-driven gameplay

`ItemDefinition`, recipe tables, flora/fauna/food/fluid registries and artifact/construction profiles
own static semantics. Incoming stacks are normalized at inventory/world boundaries while legitimate
dynamic payload such as durability, instance identity and biological state is preserved.

Authority lookups fail closed on ambiguity. Bots query recipe authority rather than duplicating
material→recipe mappings. Generic movement/economy modules are guarded against concrete-species
coupling by `tools/check_content_coupling.py`.

## Territory, trails and structures

Ground ownership and committed trail ownership are independent. The actor head is not a committed
segment; self-trail is non-lethal. Enemy committed-trail contact is nonlethal sabotage that breaks the
active capture until the victim returns to owned territory.

Lightweight construction is keyed by persistent global position and stored separately from complex
artifacts. One cell can contain a floor layer, a wall/doorway support layer and a roof layer. Roofs
require support. Walls collide physically; floors/roofs are traversable layers and doorway centers are
open. Territorial control governs repair/dismantle/hostile damage authority.

Structural health originates in SAVE20 state. Enemy damage reduces health; destroying support collapses its roof.
Repair consumes a same-material structural module. Intact dismantling recovers the module; damaged
dismantling converts remaining integrity into recipe-derived raw salvage so dismantle→place cannot be
used as free healing.

## Persistence

C serializes versioned logical state with checksum. The current writer emits schema 25 and explicitly
supports schemas **14, 15, 16, 17, 18, 19, 20, 21, 22 and 23** through deliberate migration rules. API/ABI stored
in save metadata are provenance only and never the compatibility authority.

Important boundaries:

- SAVE16: explicit nest substrate semantics;
- SAVE17: persistent runtime/worldgen provenance and respiration;
- SAVE18: compact construction section;
- SAVE19: construction shape/topology semantics;
- SAVE20: structural health/integrity semantics;
- SAVE21: WORLDGEN3 safe-natural-turret provenance;
- SAVE22: WORLDGEN4 canonical-procedural-resource provenance.
- SAVE23: strict manual/procedural instance-ID namespace separation; historical carried natural
  turrets are rebound to a sequential ID together with every exact payload handle.

Derived chunk summaries are caches and refresh is required to be hash-neutral. Flutter/Android owns
physical files and atomic replacement; a corrupt/unsupported save must fail closed without overwriting
player data.

## FFI / Flutter

Hosts call `odg_ffi_abi_query()` before reading POD data. ABI9 validates the exact construction
snapshot size and the explicit artifact paging `total_count` layout alongside the existing contract. Bounded/paged copy APIs prevent persistent-world
entity counts from being silently truncated. Dart owns presentation models only; authoritative values
come from C snapshots/queries.

## Renderer and music

The C renderer owns terrain, water, shadows, contours, trails, resources, artifacts, construction,
flora/fauna, avatar faces, GlyphAtlas, sky/fog and remote views. Construction integrity has visible
material darkening/fracture detail but collision topology remains governed by physics.

Android decodes/output audio and submits PCM to the native MusicAnalyzer. Music state may change
presentation but is excluded from `odg_state_hash()`, AI, collision, territory, movement, crafting,
harvesting and RNG.
