# ODPAR — Living System Graph

> Generated from the current source tree. Do not hand-edit this file; edit `docs/architecture/SYSTEM_GRAPH_MANIFEST.json` or the code and run `make graph`.

**28 systems · 30 C modules · 958 functions · 553 cross-module calls · 149 public C symbols · 0 detected gaps.**

## Rule for future changes

A new species/item/tool/terrain/weather/interaction is not complete when it merely compiles. Its identity, capabilities, registry references, simulation authority, persistence (when stateful), renderer/host exposure (when observable), validation and tests must form an intentional path in this graph. Numeric ABI IDs are append-only and must never be recycled.

## Systems

- **platform** — Platform + deterministic memory — `engine/src/platform.c`
- **entity_store** — Dynamic entity stores + spatial index — `engine/src/entities.c`
- **game_api** — Public C API + ABI — `engine/src/game.c`, `engine/include/odpar_game.h`
- **items** — Items + capabilities — `engine/src/items.c`
- **registry** — Cross-registry validation — `engine/src/content_registry.c`
- **territory_policy** — Territorial access policy — `engine/src/territory_policy.c`
- **surface** — Terrain + water + biome + weather authority — `engine/src/environment.c`, `engine/src/chunks.c`
- **world_physics** — Shared terrain height + occupancy + airspace physics — `engine/src/world_physics.c`
- **geology** — Subsurface strata + caves + ore-body authority — `engine/src/geology.c`
- **survival** — Respiration + persistent environmental survival state — `engine/src/survival.c`
- **nutrition** — Food + satiety + hydration + gradual damage — `engine/src/nutrition.c`
- **fluids** — Fluid registry + typed containers — `engine/src/fluids.c`
- **flora** — Flora lifecycle + fruit + seed + irrigation — `engine/src/ecology.c`, `engine/src/resources.c`
- **fauna** — Fauna lifecycle + habitat + diet + reproduction — `engine/src/fauna.c`
- **interactions** — Player/bot interaction dispatch — `engine/src/interactions.c`
- **construction** — Lightweight territorial construction modules — `engine/src/construction.c`
- **artifacts** — Stations + storage + death recovery — `engine/src/artifacts.c`
- **crafting** — Recipes + repair + material progression — `engine/src/crafting.c`
- **persistence** — Save/load + persistent world state — `engine/src/save.c`
- **map** — Map queries + spatial world inspection — `engine/src/map.c`
- **simulation** — Actors + bots + territory + combat + movement — `engine/src/sim.c`
- **renderer** — Native 3D presentation — `engine/src/render.c`
- **textures** — Texture/skin presentation store — `engine/src/texture_store.c`, `engine/src/glyphs.c`
- **music** — Music reactive presentation — `engine/src/music_fx.c`
- **flutter_host** — Flutter/Dart APK host — `app/flutter/lib/src/native/odg_bindings.dart`, `app/flutter/lib/src/engine/game_runtime.dart`, `app/flutter/CMakeLists.txt`, `app/flutter/lib/src/platform/android_host.dart`, `app/flutter/lib/src/ui/game_screen.dart`, `app/flutter/android/app/src/main/kotlin/com/odpar/territorial_domain/MainActivity.kt`
- **wasm_host** — WASM preview host — `Makefile`, `app/web/index.html`, `tools/wasm_smoke.mjs`
- **spine** — Project contract manifest — `PROJECT_SPINE.json`
- **architecture_guards** — Architecture anti-coupling guards — `tools/check_content_coupling.py`, `tools/check_host_contract.py`, `tools/generate_system_graph.py`

## Required integration edges

- **registry → items** — all registered content references valid item identities/capabilities — evidence: `odg_item_definition_internal`
- **flora → surface** — growth/irrigation use the same terrain moisture authority — evidence: `odg_environment_surface_local`
- **flora → territory_policy** — wild flora is exploitable only with territorial access — evidence: `odg_territory_allows_environment_action`
- **fauna → surface** — spawn/migration/foraging/drinking are constrained by the authoritative habitat surface — evidence: `odg_environment_surface_local`, `odg_fauna_habitat_internal`
- **fauna → items** — diets and loot resolve item identities — evidence: `odg_item_definition_internal`
- **nutrition → items** — food is item-driven, not flora-specific — evidence: `odg_food_definition_internal`
- **interactions → nutrition** — consume action uses generic food authority — evidence: `odg_actor_consume_food_internal`
- **interactions → flora** — gather/plant/irrigate enter through interaction dispatcher — evidence: `odg_ecology_gather_fruit`, `odg_ecology_plant_selected`, `odg_ecology_irrigate_nearest`
- **interactions → fauna** — hunting/taming interaction reaches fauna authority — evidence: `odg_fauna_hunt`, `odg_fauna_build_hint`
- **simulation → surface** — movement uses terrain authority rather than renderer geometry — evidence: `odg_environment_surface_local`, `odg_terrain_height_fx`
- **simulation → nutrition** — actor survival ticks through generic nutrition — evidence: `odg_nutrition_tick`
- **simulation → fauna** — fauna lifecycle runs in authoritative world tick — evidence: `odg_fauna_tick`
- **simulation → flora** — flora lifecycle runs in authoritative world tick — evidence: `odg_ecology_tick`
- **simulation → artifacts** — death can create persistent recovery container — evidence: `odg_artifact_create_death_cache`
- **renderer → surface** — visual ground/water/orientation use same terrain height/normal — evidence: `odg_world_height_milli64`, `odg_environment_normal_local_q15`
- **persistence → entity_store** — save/load restores dynamic stores transactionally — evidence: `odg_entities_reserve_resources`, `odg_entities_reserve_artifacts`
- **flutter_host → fauna** — APK consumes fauna + nest snapshots and habitat definitions — evidence: `odg_copy_fauna`, `odg_copy_fauna_nests`, `odg_fauna_habitat_get`
- **flutter_host → surface** — APK can inspect authoritative terrain/weather state — evidence: `odg_world_surface_sample64`, `odg_weather_rain_permille`
- **flutter_host → nutrition** — APK exposes actor satiety/hydration without duplicating survival rules — evidence: `odg_player_satiety_permille`, `odg_player_hydration_permille`
- **flutter_host → simulation** — APK sees non-lethal trail-break state — evidence: `odg_player_trail_broken`
- **wasm_host → game_api** — preview export surface is derived from native ABI allowlist — evidence: `WASM_EXPORTS`, `odpar_territorial_domain.exports.map`
- **persistence → fauna** — fauna and nests survive save/load rather than respawning as decoration — evidence: `fauna must persist in save prefix`, `nests must persist in save prefix`
- **persistence → nutrition** — actor HP/satiety/trail recovery survive save/load — evidence: `actors must persist in save prefix`
- **persistence → surface** — weather epoch/rain state survives save/load — evidence: `weather must persist in save prefix`
- **persistence → territory_policy** — claimed territory survives save/load — evidence: `territory must persist in save suffix`
- **flutter_host → persistence** — APK stores independent named world slots, verifies integrity, and asks the C save-schema authority whether a world is loadable; API/ABI are provenance only — evidence: `listWorlds`, `saveWorldSlot`, `loadWorldSlot`, `blobSha256`, `structurallyLoadable`, `saveSchemaSupported`
- **architecture_guards → flora** — transversal gameplay cannot hardcode concrete flora species/items — evidence: `CONTENT COUPLING OK`
- **registry → fluids** — fluid/container registries are cross-validated before world start — evidence: `odg_fluid_definition_count`, `odg_fluid_container_definition_count`
- **nutrition → fluids** — actor drinking resolves typed fluids/containers through the fluid registry — evidence: `odg_fluid_definition_internal`, `odg_fluid_container_definition_internal`
- **interactions → fluids** — fill/drink/irrigate dispatch uses typed fluid payloads and container capabilities — evidence: `odg_fluid_container_definition_internal`, `odg_fluid_payload_make_internal`
- **simulation → fluids** — bot survival drinks real carried fluids and navigates to physical water sources — evidence: `odg_fluid_container_definition_internal`, `odg_actor_drink_selected_internal`
- **flutter_host → fluids** — APK consumes fluid and container definitions from the C authority — evidence: `odg_fluid_definition_get`, `odg_fluid_container_definition_get`
- **geology → surface** — cave mouths and subsurface depth are anchored to the authoritative surface/water world — evidence: `odg_world_surface_sample64`
- **flora → geology** — surface mineral exposures are materialized only when backed by deterministic subsurface ore bodies — evidence: `odg_geology_surface_exposure_internal`
- **survival → surface** — respiration queries authoritative water depth instead of renderer state — evidence: `odg_environment_surface_local`
- **simulation → survival** — respiration and environmental damage advance in the authoritative world tick — evidence: `odg_survival_tick`
- **persistence → survival** — worldgen version and respiration state survive save/load through save schema migrations — evidence: `g_odg_persistent_runtime`
- **flutter_host → survival** — APK reads oxygen/worldgen state from C without duplicating survival rules — evidence: `odg_player_oxygen_permille`, `odg_worldgen_version`
- **flutter_host → geology** — APK can inspect authoritative cave/ore strata through the C API — evidence: `odg_world_geology_material64`, `odg_world_cave_openness_permille64`
- **registry → construction** — construction content/profile consistency is validated at registry initialization — evidence: `odg_construction_profiles_validate_internal`
- **construction → territory_policy** — placement and dismantling rights derive from territorial control — evidence: `odg_chunk_owner_at_global_cell`
- **construction → surface** — construction placement rejects water/steep invalid surfaces — evidence: `odg_environment_surface_local`
- **interactions → construction** — universal interaction dispatch places and dismantles lightweight construction — evidence: `odg_construction_build_hint_internal`, `odg_construction_execute_hold_internal`
- **world_physics → construction** — shared world occupancy treats lightweight construction as physical geometry — evidence: `odg_construction_position_blocked_internal`
- **persistence → construction** — save20 persists compact construction topology and durability while explicit migrations preserve legacy artifact-backed blocks — evidence: `odg_construction_import_legacy_artifact_internal`, `odg_construction_max_health_internal`
- **renderer → construction** — native renderer draws authoritative compact construction state — evidence: `render_construction_nodes`
- **world_physics → surface** — shared terrain physics interpolates deterministic global world height — evidence: `odg_world_height_milli64`
- **simulation → world_physics** — actor movement and camera consume shared physical occupancy geometry — evidence: `odg_position_clear_internal`, `odg_world_circle_aabb_overlap_internal`

## Detected gaps

- ✅ None. All declared integration invariants have evidence.

## Machine-readable companions

- `SYSTEM_GRAPH.json`: systems, generated function inventory, module edges and gaps.
- `SYSTEM_GRAPH.dot`: system/integration diagram.
- `FUNCTION_GRAPH.dot`: generated C module/function call edges.
- `SYSTEM_GRAPH.svg`: rendered diagram when Graphviz is available.
