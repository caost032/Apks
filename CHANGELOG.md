# Changelog

## 2026-08-20 — API26 structural pruning checkpoint

- Centralized signed floor division across world-coordinate subsystems.
- Removed confirmed-dead runtime/presentation scratch and legacy turret constants.
- Reduced derived chunk-summary state/work to actual consumers.
- Marked retained save-layout legacy bytes as explicit compatibility tombstones.
- Generalized repair from concrete tool IDs to durability metadata.
- Activated Food Registry ground lifetimes for dropped perishables.
- Post-prune quick gate green; public API/ABI/save authority unchanged (26/6/17).


## API26 / SAVE17 systemic-coherence continuation — 2026-08-20

- Added authoritative swimming/buoyancy and bot oxygen-escape behavior for non-aquatic actors.
- Added Marsh Crocodile amphibious hostile fauna and predator/prey relations with River Fish.
- Added territorial BUILDING_BLOCK construction with wood/stone/iron variants, collision, protection, demolition and exact material recovery.
- Removed random environmental ammunition and conquest-created ammunition; normal ammunition production is Smithy + iron.
- Added LAND/WATER artifact placement profiles with depth requirements.
- Added persistent wooden raft lifecycle: craft, place, mount, navigate, dismount and recover.
- Connected bot long-route movement to the terrain navigation graph.
- Promoted API 25 -> 26; ABI remains 6 and save schema remains 17.
- Removed unconditional native/WASM recompilation from the quick gate by giving generated binaries real Make dependencies.
- Final `make quick-gate` passes in 25.78 s; graph reports 26 systems, 28 modules, 809 functions, 390 inter-module calls and 147 public symbols.

## API25 / SAVE17 WIP continuation — 2026-08-20

- Added versioned worldgen v2 plus explicit preservation of older worldgen behavior for migrated worlds.
- Added deterministic 3D subsurface geology: strata, cavities/caves, coal and iron veins, and geological exposure spawning.
- Added authoritative day/night state, oxygen/respiration and gradual drowning.
- Added aquatic fauna substrate and River Fish.
- Added conditional hostile-fauna substrate and first Night Stalker with night/outside-territory targeting.
- Added craftable LIGHT-capable torches and local-light participation in hostile spawn eligibility.
- Added wild-fauna defensive aggression substrate.
- Promoted game API 24 -> 25 and save schema 16 -> 17; FFI ABI remains 6.
- Added focused `world-systems-test`.
- Known WIP integration boundary: Flutter CMake still needs `geology.c` and `survival.c`; complete quick-gate is therefore not yet claimed.


## 15.0.0 — 2026-08-20

### Architecture

- Promoted gameplay API to 15 and FFI ABI to 2 with explicit size/feature negotiation.
- Replaced new `carried_*` expansion with generic ItemDefinition/ItemStack inventory and command queue.
- Added dynamic runtime stores for chunks, turrets, pickups, resources and artifacts.
- Added authoritative 64-bit global actor/entity positions plus floating-origin local caches.
- Converted the old 128×128 arena into a local precision/render window over 32×32 deterministic chunks.

### Gameplay

- Added 4-slot hotbar, +8 backpack equipment, stacks, metadata and durability.
- Added wood/stone/iron resources, axes, pickaxes, persistent harvesting and physical drops.
- Added Workbench, Smithy, Chest, transactional recipes, quantity/MAX crafting and repair.
- Added wood/stone/iron turrets, strict-tier reprogramming, ascension chips and persistent procedural turrets.
- Added turret DEFENSE/HARVEST modes; harvesting consumes ammunition and drops physical wood.
- Added persistent death/respawn semantics: territory and deployed infrastructure survive actor death.
- Removed the 55% terminal match condition; leaderboard now represents an ongoing claimed-domain share.
- Corrected head-vs-committed trail semantics and pre/post-movement trail contact resolution.
- Expanded bots into the same resource/crafting/tool/infrastructure economy used by the player.

### Open Domain

- Added seam-safe heightfield, deterministic chunk descriptors, biomes and bootstrap resources.
- Added ACTIVE/DIRTY/SLEEPING chunk runtime, summaries, depletion persistence and spatial indexing.
- Added save/load schema 8 with checksums and logical serialization independent from reserved heap capacity.
- Added viewport/resolution map queries over global coordinates and paged global artifact queries.
- Added on-demand remote turret camera with independent floating render origin.

### Presentation

- Player/bots are clean cube silhouettes; v14 multipart tactical runner presentation is retired.
- Added real six-face RGBA8 avatar materials and mipmaps.
- Added shared GlyphAtlas, readable turret ammunition labels and vertical tech item cards.
- Added marching-squares territory contours and continuous terrain-following trail ribbons.
- Added continuous terrain, biome material variation, selective outlines, shadows, fog/sky and microscenery.
- Reworked trees into irregular low-poly crowns rather than trunk + cube foliage.
- Added first/third-person camera profiles, C-rendered camera/avatar previews and remote views.
- Graphics continuation: softened biome/chunk colour boundaries into localized transitions, enriched distant relief/sky readability, separated foliage geometry from rock primitives, and regenerated all shipped captures from the live renderer.
- Immersive scale pass: compacted player/bot cubes and their collision halo, enlarged resource trees into multi-metre environmental anchors, enlarged stone/iron deposits with matching physical footprint, and opened the default third-person framing slightly.
- Material readability pass: wood/stone/iron turrets now have tier-specific structural silhouettes; reprogram/ascension chips separate material frame from functional circuitry and expose physical contact fingers.
- Replaced the previous close-range screenshot set with 18 fresh native-renderer inspection views covering ecology scale, three turret tiers from four angles, workbench/smithy/chest, chip/ammo family + detail views, and enlarged ore from multiple angles.

### Music

- Added Android local music queue with play/pause/seek/previous/next/shuffle/repeat/volume/audio focus.
- Added real decoded PCM path through JNI to the C MusicAnalyzer.
- Added noise-floor/normalization/envelope/onset/beat analysis and 0–150% presentation reactivity.
- Added explicit gameplay-hash isolation tests for music/presentation state.
- Bundled official **AFTERIMAGE 0.2** catalog: 7 Original Tracks + 5 Instrumental Reworks by kaost032.

### Flutter / Android

- Modular hotbar, inventory, crafting, domain map, artifacts, music and pause surfaces.
- Inventory blocks player input while the world continues; pause remains a distinct simulation state.
- Added normalized portrait/landscape control profiles and editing.
- Added camera sensitivity/profile editor and six-face skin workflow.
- Added safe autosave/background save and persistent music/control/skin settings.
- Added Game Credits and Music Credits in the Information panel.

### Verification

- Deterministic native game and FFI tests pass on the preparation host.
- Bundled music has a SHA-256 manifest and dedicated catalog gate.
- Hardened native library retains strict exported-symbol validation.
- Flutter/APK toolchain validation remains an external/CI gate where Flutter/Android SDK are installed.

## 14.0.0 — 2026-08-18

Historical v14 conquest-infrastructure release. See source history/checkpoint packages for its full notes.

## 2026-08-20 — API29 / ABI6 / SAVE18 — lightweight construction + multimodal/airspace closure

- Moved ordinary building blocks out of heavyweight generic artifacts into a compact dedicated construction store.
- Added explicit save17 -> save18 construction migration.
- Added paged construction snapshots and construction map markers with current territorial controller.
- Added bot home fortification plus conquered/neutral construction salvage/reuse via real crafting/inventory transactions.
- Removed hidden coal semantics from public reserved chunk descriptor fields.
- Formalized fauna FORAGE behavior and connected it to ecology/validation.
- Connected bots to water-aware raft steering, barrier detection, real-range mounting and coastal dismounting.
- Centralized physical flora height and added airspace clearance; flying fauna uses flight speed and climbs over canopy/obstacles instead of clipping.
- Removed additional zero-consumer internal helpers/state.
- Recorded next ABI debt explicitly: artifact paging total count must stop using a field named `reserved_u32` and be promoted to a real ABI field in the next contract revision.
