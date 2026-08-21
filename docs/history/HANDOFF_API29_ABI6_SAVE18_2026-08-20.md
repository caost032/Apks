# ODPAR: Territorial Domain — MASTER HANDOFF ABSOLUTO PARA EL SIGUIENTE CHAT

Checkpoint/handoff time: **2026-08-20 19:59 America/Guayaquil (-05:00)**

Status: **most advanced working tree, API29 / FFI ABI6 / SAVE18. Fast functional gate + native hardening + WASM verified.**

This file is the continuity authority for the next chat. It supersedes every older API24/API25/API26 handoff and every older checkpoint description when they disagree with current source.

---

## 0. INSTRUCCIÓN ABSOLUTA AL SIGUIENTE CHAT

Continue **only** from the source tree inside the ZIP that contains this handoff. It is the most advanced tree.

Do **not**:

- restart from an older ZIP;
- downgrade API/save to satisfy stale documentation;
- rebuild gameplay in Dart;
- turn the Android product into a WebView;
- make WASM/web the product authority;
- add content by scattering concrete-ID `if` branches through generic systems;
- reuse append-only IDs or persisted reserved bytes casually;
- claim a system complete merely because it compiles;
- run long ASan/UBSan/soak loops during ordinary iteration unless the user explicitly asks or a focused bug genuinely needs them;
- remove public ABI/save compatibility just because a symbol/field appears unused by Flutter today;
- confuse pruning with reducing the scope of the game.

Primary product architecture remains:

`Android APK -> Flutter/Dart host -> explicit FFI -> native C11 engine`

C11 remains the single authority for simulation, gameplay, world, inventory, crafting, resources, territory, ecology, fauna, survival, vehicles, construction, combat, worldgen, persistence and software rendering.

Flutter/Dart is the application shell/UI/input/preferences/files/world browser/native-window presentation.

WASM/web is a secondary preview/testing aid only.

When documentation conflicts, authority order is:

1. `engine/include/odpar_game.h`
2. actual C implementation + tests
3. `PROJECT_SPINE.json`
4. this handoff / current checkpoint status
5. older docs only as historical evidence

---

## 1. CURRENT AUTHORITATIVE CONTRACT

From `engine/include/odpar_game.h`:

- Game API: **29**
- FFI ABI: **6**
- Save schema written now: **18**
- Simulation: **120 Hz deterministic C11**
- Current fauna species: **7**
- Current item IDs: **28 usable item types + NONE**
- Current recipes: **30**
- Current flora species: **1** (apple tree, but registry architecture is general)
- Current food definitions: **3**
- Current worldgen for newly created worlds: **v2**
- Old migrated worlds preserve their historical worldgen semantics where required.

Save schemas intentionally readable remain **14, 15, 16, 17, 18** through explicit migrations.

Do not bump:

- API unless public capability/semantic surface genuinely changes;
- ABI unless public POD layout/order/**meaning** changes;
- save schema unless persistent representation/meaning genuinely changes.

A same-size POD semantic change is still an ABI change.

---

## 2. CURRENT VERIFICATION BOUNDARY

The final `make quick-gate` run after the handoff/document synchronization completed **fully green in 26.83 seconds**:

```text
FFI v6 OK api=29 portrait=720x1280 pixels=921600 hash=b5764c2ca774a78b
ECOSYSTEM OK api=29 abi=6 fauna=7 habitats=7 food=3 flora=1 save=18
WORLD SYSTEMS OK geology=caves+coal+iron water=swim+oxygen+fish+croc+raft night=stalker torch=ready construction=territory ammo=smithy api=29 save=18
CONSTRUCTION OK store=72B artifact=1048B paging=64+16 migration=17->18 api=29 save=18
COHERENCE OK items=28 sources+sinks=closed repair=capability-driven ground-food=expires construction=lightweight
BOT ECONOMY OK stages=wood-axe,wood-pick,backpack,stone-axe,stone-pick,smithy,iron-pick,home-fortification,salvage-reuse
WORLD CONTINUITY OK recenters=48 origin=(1472,-64)
HOST CONTRACT OK api=29 abi=6 save=18 dart_symbols=88 public_c=149 engine_units=28 authority=C11 webview=no
SYSTEM GRAPH OK systems=27 modules=29 functions=858 calls=449 public=149 gaps=0
AFTERIMAGE 0.2 OK originals=7 reworks=5 tracks=12 owner=kaost032
CONTENT COUPLING OK generic_modules=7 nutrition_runtime=1 concrete-runtime-gaps=0
NATIVE SYMBOL SURFACE OK public=149 extra=0
NATIVE HARDENING OK relro=full nx_stack=yes canary=yes textrel=no
WASM OK api=29 bytes=421716 framebuffer=518400 sampleHash=f05519c9 claimed=558 territory=22.0% alive=10 turrets=1
```

No long ASan/UBSan/soak certification is claimed for this checkpoint.

---

## 3. PRODUCT DOCTRINE / ANTI-DEBT RULES

### 3.1 Data-driven before concrete-ID branching

Generic runtime should ask registries/capabilities/profiles/relations, e.g.:

- item registry;
- recipe registry;
- artifact profiles;
- construction profiles;
- attack/hunting profiles;
- deposit/geology profiles;
- flora registry;
- fauna species/diet/habitat/nesting/predator-prey registries;
- fluid/container profiles;
- locomotion/physical-medium profiles;
- territory policy;
- light authority;
- surface/airspace authority.

Concrete identity is legitimate in content-specific morphology/registration/render details, not in generic economy/survival/pathfinding.

### 3.2 IDs are append-only

Never recycle:

- item IDs;
- recipe IDs;
- flora/fauna IDs;
- ABI/save IDs;
- public enum/capability bits.

Removed/retired IDs become tombstones/reserved IDs.

### 3.3 Reserved really means reserved

A previous debt where `chunk_descriptor.reserved_u32[0]` secretly carried coal count has already been removed. Current tests enforce that those descriptor reserved fields remain zero while coal remains available through internal worldgen authority.

Do not reintroduce hidden semantics into `reserved_*` fields.

### 3.4 Private dead code vs compatibility

- private + no consumer + no persisted/public role -> delete;
- public contract -> deprecate/version explicitly, never heuristic-delete;
- save bytes -> tombstone/migrate explicitly, never reinterpret casually.

### 3.5 Every content object needs a cycle

Every item/system should have meaningful edges such as:

`source -> acquire -> store/transport -> use -> consequence -> recovery/decay -> persistence`

The current coherence test already checks item source/sink closure, repair and food expiration. Expand this doctrine to vehicles, structures, species, monsters and world systems.

---

## 4. STRUCTURAL PRUNING ALREADY COMPLETED — DO NOT RESURRECT

Previous technical-debt passes already removed or centralized:

- duplicate signed-negative floor-division helpers -> one internal mathematical authority;
- confirmed-dead old turret constants;
- dead camera presentation state;
- dead remote-view presentation state;
- unused 16,384-entry old bot-parent scratch;
- unused chunk-summary territory/pickup/version metadata and repeated refresh work;
- internal helpers with only prototype+definition and zero consumers;
- an atomic music-energy cache that was written every update but never read;
- redundant Dart item-name dictionaries/getters;
- random world ammo and conquest-created ammo;
- generic item-stack metadata inconsistencies through registry normalization;
- multiple concrete content branches in generic hunting/mining/artifact logic;
- unconditional repeated native/WASM recompilation debt in normal iterative gates where dependency tracking is applicable.

Persisted obsolete fields that cannot yet be removed safely have been renamed/documented as save tombstones instead of being treated as active runtime state.

Do not resurrect these deleted paths “for convenience”.

---

## 5. WORLD / GLOBAL COORDINATES / FLOATING ORIGIN

The world is not a 128x128 map.

Authority:

- global positions are 64-bit fixed-point;
- the local 128x128 domain is a precision/render window;
- deterministic chunks are 32x32 cells;
- floating origin recenters local representation around global movement;
- rendering, movement and world sampling must derive from the same global surface authority.

`test_world_continuity` verifies **48 recenters** and current nav edge masks against the correct global region.

Never create a subsystem that stores “world position” only as local `int32` and silently becomes wrong after recenter.

---

## 6. GEOLOGY / ORES / CAVES

### Current state

Worldgen v2 includes a deterministic subsurface geological volume:

- topsoil/subsoil/rock strata;
- cave/cavity openness;
- coal and iron veins;
- cave-mouth/exposure selection;
- ore materialization tied to geology rather than random boulders on grass.

Coal exposure no longer abuses a public reserved chunk field.

### Critical incomplete boundary

Caves are **real queryable 3D geology**, but they are not yet a complete traversable multi-level actor domain.

Do not fake caves by adding a second unrelated heightfield.

### What the next chats must eventually build

1. Define a true subsurface spatial representation tied to global chunks.
2. Represent cave walkable volume, ceiling, floor, vertical transitions and entrances.
3. Make mining able to expose/modify underground passages without losing deterministic base geology.
4. Persist only modifications/deltas, not entire deterministic world volume.
5. Add cave light rules (sunlight attenuation, torch/ward/lantern contribution).
6. Add underground water/cave pools with terrain beneath them.
7. Add multi-level navigation transitions for player/bots/fauna where appropriate.
8. Add underground spawn/habitat conditions rather than hardcoded monster rooms.
9. Keep renderer and collision on the same cave authority.
10. Add focused cave continuity/save tests before broad content expansion.

Future ores (copper/gold/etc.) should primarily be new deposit/geology data + recipes/uses, not new mining branches.

---

## 7. WATER / OXYGEN / SWIMMING

### Current state

Water is a physical medium with non-flat terrain underneath it.

Non-aquatic actors have:

- oxygen;
- gradual drowning damage;
- recovery after breathing;
- transition from shallow wading to deep-water buoyancy/swimming;
- no terrestrial dash/jump while swimming;
- shore escape logic.

Bots under oxygen stress prioritize coast escape.

Ground fauna entering deep water uses emergency escape instead of passively dying.

### Current aquatic/amphibious examples

- River Fish: aquatic;
- Marsh Crocodile: amphibious/semi-aquatic.

### Future water work

- stronger swimming steering, surface/dive intent and explicit dive controls if gameplay needs them;
- underwater camera/render treatment that reflects depth but does not obscure play unfairly;
- depth-aware sound/presentation;
- aquatic food webs/schooling/predator avoidance;
- fishing/hunting tools only if they close real economy cycles;
- water plants/habitat;
- currents only if they meaningfully affect navigation and share physics authority;
- watercraft damage/repair/cargo as data-driven vehicle extensions;
- drowning/escape tests for bots/fauna across obstacles and coast geometry.

Do not make all water flat internally merely because the visible surface is level.

---

## 8. VEHICLES / WOODEN RAFT / MULTIMODAL NAVIGATION

### Current raft

The first watercraft is the **Wooden Raft**:

- crafted from wood;
- persistent physical artifact;
- water-only placement with minimum navigable depth;
- one occupant;
- physical mount/move/dismount/recover lifecycle;
- mounted actor stays above water and does not drown from merely riding;
- raft cannot navigate dry/shallow water;
- raft respects world obstacles and ignores only its own hull for self-collision;
- ownership/control interacts with territorial authority.

### Bot progress

Bots can now:

- use water steering while already mounted;
- avoid using the terrestrial nav graph as if water were walkable land;
- test direct heading and gentle ±45-degree water detours;
- dismount near reachable coast;
- detect a continuous navigable water barrier on a route;
- approach and mount an accessible empty raft using real interaction distance.

The internal mount helper was hardened so it cannot teleport a distant actor onto a raft.

### Major incomplete cycle

Bots do **not yet** fully solve:

`need route -> detect water barrier -> no raft exists -> evaluate cost -> gather wood -> craft raft -> carry -> find shore/deploy -> board -> water route -> disembark -> continue land route`

This should be a logistics planner decision, not “bot has 16 wood -> always make raft”.

Future vehicles must build on a vehicle/medium profile, not each add a new movement system. Potential future types include improved boats/canoes/cargo boats only when a new abstraction is justified.

---

## 9. DAY/NIGHT / LIGHT / HOSTILE ECOLOGY

### Current authority

Day/night is deterministic gameplay state shared with renderer. Approximate current full cycle: ~24 gameplay minutes.

Lighting is not only decorative:

- Torches are real deployable LIGHT artifacts.
- Night Shard has a meaningful ward/light role rather than being junk loot.
- hostile spawn eligibility can query local light.
- light queries already use spatial locality instead of scanning every artifact globally per sample.

### Night Stalker

Current first monster:

- monster family;
- appears at night;
- targets players outside their own territory;
- own territory functions as refuge for this species;
- spawn distance and acquisition range are coherent;
- real HP, movement, damage and attack cooldown;
- daytime target loss/despawn behavior;
- Night Shard loot.

### Future monster architecture

Do not make a giant switch of monster identities.

Build a spawn-condition/hostility profile capable of expressing:

- day phase;
- light threshold;
- biome;
- cave/surface domain;
- territory condition;
- altitude/depth;
- weather;
- group size;
- aggression type;
- prey preference;
- despawn/persistence policy;
- loot table;
- locomotion/physical medium.

Add new monsters only when they prove/extensively use those abstractions.

---

## 10. FAUNA / NESTING / PREDATOR-PREY / WILD AGGRESSION

Current fauna IDs:

1. Orchard Bird
2. Forest Deer
3. Meadow Rabbit
4. Field Fowl
5. River Fish
6. Night Stalker
7. Marsh Crocodile

Families currently include bird, mammal, aquatic, monster, reptile.

### Key abstractions already proven

- bird != automatically flying;
- egg != automatically tree nesting;
- aquatic != land locomotion;
- amphibious is a separate medium profile;
- wild animal inside territory != owned pet/resource;
- passive hunting restrictions != self-defense restrictions;
- predator-prey relation can consume prey without duplicating full human loot.

Current predator relation: `Marsh Crocodile -> River Fish`.

Natural predation maintains a prey floor and does not spawn full human-hunting loot when the predator eats.

### FORAGE cleanup

The old public bit named `GROUND_FORAGE` was misleading and unused. It now aliases a real generic `FORAGE` semantic and fauna ecology actually consumes it.

Registry validation prevents predators/monsters from receiving magical environmental calories merely because an old flag was present.

### Nesting

Substrates include TREE and GROUND, with CLIFF/STRUCTURE future-ready.

- Orchard Bird -> TREE;
- Field Fowl -> GROUND;
- Crocodile uses terrestrial nesting rules appropriate to its ecology.

Tree nest physical/render placement is derived from actual host-tree morphology rather than universal terrain+constant height.

### Flight / airspace

A major recent correction:

- FLIGHT now consumes `flight_speed`, not `ground_speed`;
- flying over water no longer accidentally becomes “ground fauna drowning emergency”;
- flora has shared physical height authority used by renderer and airspace;
- artifacts/construction/turrets have obstacle-height profiles;
- airspace clearance lets a bird go **over** an obstacle instead of either clipping through it or treating every object as an infinite 2D wall;
- a bird approaching a tall mature tree raises altitude before horizontal penetration.

### Future ecology

- more explicit prey fear/flee signals;
- nest/offspring defense;
- hunger-driven choice between environmental forage and prey;
- group/herd/flock behavior where it changes gameplay;
- breeding/carrying capacity tuning;
- domestic livestock as persistent named entities, not unlimited generic wild slots;
- predator competition only if it has actual ecosystem effect;
- aquatic schooling/predation;
- pollination/seed dispersal extensions using generic relations.

---

## 11. FLORA / FOOD / FLUIDS

### Flora

Do not reduce flora back to generic TREE.

Registry supports growth form, stages, harvest rules, fruit/seed cycles, moisture and morphology.

Apple Tree currently proves:

- seedling/sapling/young/mature/old stages;
- fruit without mandatory felling;
- windfall;
- seed payload preservation;
- planting/germination;
- controlled spacing/carrying capacity;
- chopping/crop yield through registry data;
- physical height derived from stage/form and shared with render/airspace.

Future pear/berry/crop species should be mostly data + morphology, not changes to generic nutrition/economy.

### Food

Current food definitions: apple, raw meat, raw fish.

Dropped food now actually ages/expires using Food Registry lifetimes. It no longer persists forever despite having expiry data.

Logical future gap: raw-food semantics should eventually gain a cooking/food-safety cycle if that meaning remains in content. Prefer a general cooking/processing station/profile rather than hardcoding meat vs fish.

### Fluids

Water is currently the first fluid. Fluid/container registries already exist.

Water flask/rain barrel use generic fluid concepts; future liquids should not turn into `if WATER_FLASK` branches.

Hydration and irrigation must continue to ask fluid capabilities.

---

## 12. ITEMS / RECIPES / ECONOMY

Current item IDs are append-only:

1 WOOD
2 STONE
3 IRON
4 AMMO
5 REPROGRAM_CHIP
6 ASCENSION_CHIP
7 AXE
8 PICKAXE
9 TURRET
10 WORKBENCH
11 SMITHY
12 CHEST
13 BACKPACK
14 APPLE
15 APPLE_SEED
16 BIRD_TRAP
17 LEATHER
18 RAW_MEAT
19 HUNTING_KNIFE
20 SWORD
21 WATER_FLASK
22 RAIN_BARREL
23 COAL
24 TORCH
25 NIGHT_SHARD
26 RAW_FISH
27 BUILDING_BLOCK
28 RAFT

Current recipe count: **30**.

Important economy guarantees:

- ammo no longer appears randomly in worldgen;
- conquest no longer creates ammunition out of nothing;
- normal ammo progression is `iron -> Smithy -> ammo`;
- current smithy ammo recipe: `2 iron -> 12 ammo`;
- mining outputs are deposit-profile driven;
- tool repair is capability/durability/material driven, not AXE/PICKAXE hardcoded;
- item stacks are normalized against authoritative static registry metadata;
- every registered item is currently checked for source/sink closure by coherence tests.

Never give bots free tools/materials merely to make a test pass.

---

## 13. CONSTRUCTION — LIGHTWEIGHT STORE (SAVE18)

This is one of the most important architecture changes.

### Why it changed

A structural block previously paid the full generic artifact struct cost (~**1048 B**) despite not needing chest storage/capability payload.

Construction now uses a lightweight dedicated store (~**72 B** current internal block), while complex stations/containers/vehicles remain artifacts.

### Current building block

One item `BUILDING_BLOCK`, material variant:

- wood;
- stone;
- iron.

Current shape baseline: WALL.

Properties:

- stable `instance_id`;
- global position;
- material;
- historical owner;
- current territorial controller exposed in snapshots/map;
- collision;
- persistence;
- material-dependent demolition duration;
- exact one-block recovery rather than reverse-smelting into raw material;
- paged public snapshot;
- dense internal store: removal compacts slots so memory tracks current blocks, not historical maximum.

### Save migration

Save18 introduced construction storage.

When loading save17 where building blocks were represented as old heavy artifacts, migration converts them into the lightweight construction store.

Do not write new blocks back as artifacts.

### Territory semantics

- initial placement requires own controlled territory;
- enemy cannot demolish while builder/controller still controls the cell;
- if cell becomes neutral or conquered, another actor may demolish/recover according to policy;
- map construction marker reports **current territorial controller**, not permanent historical builder ownership.

### Bot construction

Bots can now:

- craft blocks through real station/recipe economy;
- build a minimal dispersed home fortification;
- leave navigable openings instead of trapping themselves;
- salvage conquered/neutral enemy construction when it is not strategically useful;
- recover/reuse the exact module instead of generating material.

### Future structural graph — high priority

Do not add dozens of decorative blocks before these exist coherently:

1. shape/profile registry: floor, wall, roof, door/opening, possibly ramp/stair where terrain requires it;
2. support/integrity graph so floating roofs/walls are not arbitrary;
3. controlled collapse semantics and material recovery that avoid duplication exploits;
4. repair/damage by material;
5. doors with pathfinding/open-state authority;
6. ownership vs territorial control vs usage rights;
7. captured infrastructure decision: preserve/use vs salvage, not always destroy;
8. structural footprint/room concepts only when needed for gameplay (shelter/light/monster protection/stations);
9. bot planner with utility/cost/path-blocking checks;
10. renderer/map/UI tools for placing and inspecting structure modules;
11. save migrations for any structural state extension.

Ordinary structural modules should stay cheap data, not become generic entity objects again.

---

## 14. TERRITORY / TRAILS / OWNERSHIP

Current trail rule is deliberately non-lethal:

- actor leaves own territory -> active trail;
- closes trail into own territory -> capture;
- enemy cuts trail -> victim enters TRAIL BROKEN;
- victim does not die from trail cut;
- broken actor cannot start another capture while outside;
- returning home restores capture capability.

Trail sabotage and physical combat are separate systems.

Wild resources/fauna do not become private property merely because land is owned, but territory can prevent an enemy from harvesting/hunting opportunistically until they conquer access.

Construction/vehicles/artifacts must distinguish where relevant:

- historical owner;
- current territorial controller;
- right to use;
- right to demolish/salvage.

Do not collapse all four meanings into one `owner_id`.

---

## 15. COMBAT / HUNTING / DEATH / RECOVERY

### Combat

ATTACK uses profile-driven damage/cooldown rather than item-specific fallthrough.

HUNT requires compatible attack capability/profile.

Hunting uses weapon damage/durability/cooldown and target loot tables.

Wild retaliation/self-defense is separated from opportunistic hunting permission.

### Death

Actors use real HP.

Death should preserve long-term player/bot effort:

- territory/infrastructure persist independently;
- recovery backpack/cache stores recoverable inventory physically;
- transaction prevents partial/lost recovery;
- protected items from expanded inventory compact into surviving base inventory;
- a recovered container itself must not vanish when another backpack is already equipped.

Future combat balancing must keep death costly but not erase hours of work arbitrarily.

---

## 16. BOTS — PLAYER-LIKE, NOT CHEATING

Current bot priority direction:

1. critical survival;
2. oxygen escape;
3. recover important death cache;
4. food/water maintenance;
5. tools/productivity;
6. infrastructure;
7. construction/fortification/salvage;
8. territory expansion;
9. defense;
10. tactical sabotage/attack.

Existing economy milestone proves:

`wood axe -> wood pick -> backpack -> stone axe -> stone pick -> smithy -> iron pick -> home fortification -> salvage reuse`

Bots use real recipes/resources/stations.

Pickup logic no longer lets an unwanted nearby seed hide a useful pickup behind it.

Terrain nav graph is actually consumed and rebuilt correctly after floating-origin recenters.

### Next bot work

- complete raft acquisition/fabrication multimodal logistics;
- combine terrestrial nav, coast transitions, vehicle nav and post-disembark continuation;
- building utility planner: defense, path openness, station protection, salvage vs takeover;
- threat/danger-aware routing around hostile fauna/monsters;
- structure-aware pathfinding through doors/openings;
- deeper underground nav only when cave domain exists;
- sustainable hunting/food choices that respect populations;
- preserve player-like inventory constraints and no free resources.

---

## 17. RENDER / PHYSICS AUTHORITY

The C software renderer remains authoritative presentation for the 3D world.

Recent architecture has naturally created a reusable physical-volume boundary:

- terrain surface;
- water level/depth;
- flora physical height;
- artifact obstacle heights;
- construction heights;
- turret heights;
- airspace clearance.

Renderer now derives tree visual scale from the same flora physical-height authority used by airspace.

### Candidate refactor: `world_physics.c`

`sim.c` (~3.1k lines) and `render.c` (~3.38k lines) are large, but do **not** split them merely to make files smaller.

There is now a real extraction boundary that may justify a new module:

- obstacle footprint/height queries;
- world occupancy;
- airspace clearance;
- water-medium placement constraints;
- maybe shared physical morphology metrics.

If extracted:

1. preserve behavior bit-for-bit where possible;
2. add to Flutter CMake/native/WASM source lists;
3. update System Graph manifest;
4. keep renderer as consumer, not duplicate authority;
5. run focused collision/flight/raft/continuity tests before adding new features.

### Visual work still needed

Regenerate renderer captures after architecture stabilizes. Old screenshots do not certify new API29/save18 visuals.

Must inspect at minimum:

- lightweight construction in wood/stone/iron;
- fortification and captured structure markers;
- raft from multiple heights/angles;
- deep water with fish/crocodile;
- Orchard Bird over trees/water using new airspace;
- Night Stalker at night with torch/ward influence;
- cave-mouth coal/iron exposures;
- tree/ground nests;
- slope alignment;
- floating-origin transitions for flicker/pop;
- terrain beneath water/no falling through world.

Do not use image-generator output as proof of renderer quality.

---

## 18. FLUTTER / ANDROID / FFI

Android/Flutter remains the product.

Important files:

- `app/flutter/CMakeLists.txt`
- `app/flutter/lib/src/native/odg_bindings.dart`
- `app/flutter/lib/src/engine/game_runtime.dart`
- `app/flutter/lib/src/ui/game_screen.dart`
- `app/flutter/lib/src/platform/android_host.dart`

Current host guardian is green for API29/ABI6/save18.

Dart should display C state; it must not reimplement gameplay rules.

### IMPORTANT IMMEDIATE ABI DEBT — FIRST TASK FOR NEXT CHAT

There is a public semantic debt that was discovered at the end of this chat and intentionally **not** patched secretly:

```c
typedef struct {
    uint32_t struct_size;
    uint32_t count;
    uint32_t opened_artifact_id;
    uint32_t reserved_u32;
    odg_artifact_entry entries[ODG_ARTIFACT_MAX_ENTRIES];
} odg_artifact_snapshot;
```

`odg_copy_artifacts_page()` currently writes **total active artifact count** into `reserved_u32`, and Flutter reads it as `artifactTotal`.

The header even documents this semantic despite the field being named reserved.

This must be cleaned **explicitly**:

- likely bump Game API **29 -> 30**;
- bump FFI ABI **6 -> 7** because public POD field meaning/name is becoming an explicit contract even if size/order stay identical;
- rename public field to `total_count` (or equivalent clear name);
- update C snapshot fill;
- update ABI query sizes/version;
- update Dart FFI struct field and consumers;
- update host guard/FFI tests/WASM smoke/export/docs;
- preserve save schema **18** unless no persistence layout changes;
- do not silently call the rename “ABI6 compatible”.

There is a separate **internal** `odg_artifact.reserved_u32` used as rain-barrel fluid identity. Because the internal artifact struct is persisted, rename it semantically (e.g. `fluid_type_id`) only while preserving byte layout/save meaning, or perform an explicit save migration if semantics/layout change. Do not leave it named `reserved` while actively using it.

This contract sanitation is the **first recommended work** in the next chat before broad new content.

---

## 19. SAVE / WORLD SLOTS / MIGRATION

Current save schema: **18**.

Migration history includes at least:

- 14 -> 15 explicit migration;
- 15 -> 16 nest substrate semantic migration;
- 16 -> 17 worldgen/respiration-era additions;
- 17 -> 18 lightweight construction migration.

World save compatibility is based on `odg_save_schema_supported(schema)`, not API/ABI equality.

Flutter world browser/file layer must preserve:

- stable internal world ID separate from display name;
- name, seed, created/modified metadata;
- one save per world;
- SHA-256 and byte-size integrity metadata;
- fsync/rename atomic replacement;
- fail-closed handling of corrupt/incompatible saves;
- legacy single save preservation;
- no `load failed -> create new world -> overwrite old save` behavior.

Future cave/structure extensions should move further toward sectioned/versioned persistence and explicit data migrations rather than raw struct dumping forever.

---

## 20. SYSTEM GRAPH / GUARDIANS

Current graph after latest changes:

- **27 systems**
- **29 modules**
- **858 functions**
- **449 inter-module calls**
- **149 public C symbols**
- **0 currently declared graph gaps**

Zero declared gaps means declared contracts are connected; it does **not** mean there is no product debt.

Important guardians/tests:

- `tools/check_host_contract.py`
- `tools/check_content_coupling.py`
- `tools/generate_system_graph.py`
- `tools/check_native_symbols.py`
- `tools/check_native_hardening.py`
- `tools/wasm_smoke.mjs`
- `tests/test_ffi.c`
- `tests/test_ecosystem.c`
- `tests/test_world_systems.c`
- `tests/test_construction.c`
- `tests/test_coherence.c`
- `tests/test_bot_economy.c`
- `tests/test_world_continuity.c`

Expand coherence guardians to catch:

- public `reserved` fields with active semantics;
- recipes referencing invalid outputs/stations/ingredients;
- vehicle without source/use/medium/repair lifecycle;
- construction shape without placement/collision/render/persistence path;
- fauna behavior bits with no runtime consumer;
- species without diet/habitat/nesting where required;
- loot with no source/sink;
- new module omitted from Flutter/native/WASM build lists;
- stale API/save literals in docs/tests/scripts.

---

## 21. IMMEDIATE NEXT WORK — EXACT ORDER

### Phase A — Contract sanitation (do this first)

1. Fix public artifact snapshot `reserved_u32 -> total_count` explicitly.
2. Deliberately bump API29 -> API30 and ABI6 -> ABI7.
3. Keep save18 unless persistence is genuinely altered.
4. Rename internal artifact rain-barrel `reserved_u32` to a semantic fluid field without changing persisted bytes, or write an explicit migration if needed.
5. Synchronize C header, ABI query, exports, Dart layout/consumer, host guard, FFI tests, WASM smoke and Project Spine.
6. Add a regression that reserved fields stay zero unless explicitly specified by their public contract.
7. Run focused FFI + host + ecosystem + world systems + quick gate.

### Phase B — Extract physical authority only if it reduces duplication

1. Audit all occupancy/height/airspace/medium helpers.
2. If boundary is clean, create `world_physics.c/.h` internal module.
3. Move shared physical queries, not gameplay decisions.
4. Update every build list and graph manifest.
5. Verify player/bot/fauna/raft/flight behavior unchanged.
6. Delete superseded duplicate helpers after consumers migrate.

### Phase C — Finish bot multimodal logistics

1. Detect water barrier from actual world geometry.
2. Search accessible existing raft.
3. If none, estimate route benefit/cost.
4. Only then plan wood acquisition + workbench recipe.
5. Carry raft to a valid shore/water placement.
6. Mount using normal interaction range.
7. Execute water route with obstacle/depth checks.
8. Choose destination shore.
9. Dismount and resume terrestrial planner.
10. Add regression proving no teleport/free raft/no desert overproduction.

### Phase D — Construction becomes a structural system, not random blocks

1. Define shape registry/IDs append-only.
2. Add floor/wall/door/roof only as needed for a coherent shelter loop.
3. Define support/integrity.
4. Define repair/damage and controlled collapse.
5. Define door state and navigation.
6. Separate historical owner/current controller/use/demolition rights.
7. Let bots preserve useful captured infrastructure instead of always salvaging it.
8. Add build planner cost/pathing/fortification utility.
9. Persist new state through explicit save migration if required.
10. Keep blocks lightweight; only truly state-heavy objects become artifacts.

### Phase E — Real traversable caves

1. Establish multi-level cave domain tied to geology.
2. Define floor/ceiling/solid/open cells/voxels or another deterministic compact representation.
3. Support entrances and vertical transitions.
4. Add mining modifications/deltas.
5. Add underground lighting.
6. Add cave pools/water.
7. Add player/bot/fauna navigation where appropriate.
8. Add underground habitat/spawn conditions.
9. Add save deltas/migrations.
10. Add renderer captures and continuity tests.

### Phase F — Deepen water ecosystem / vehicles

1. Improve swimming/dive controls only after physical semantics are clear.
2. Add fish schooling/avoidance if it changes play.
3. Improve crocodile water/shore ambush logic without unfair instant attacks.
4. Add aquatic food/resource cycles.
5. Extend vehicle profile to cargo/damage/repair if needed.
6. Add future boats only through shared vehicle abstractions.

### Phase G — Ecology and monsters

1. Generalize spawn-condition profiles.
2. Add nest/offspring defense and prey fear relations.
3. Add predator preference/hunger/carrying capacity checks.
4. Add cave/night/weather monster conditions through data.
5. Balance damage/speed/telegraph/cooldown/loot.
6. Keep own territory refuge semantics species/profile driven, not globally assumed.
7. Never add loot with no use.

### Phase H — Economy/processing/cooking

1. Audit raw-food semantics.
2. If raw risk/processing remains meaningful, create generic cook/processing profiles.
3. Add campfire/furnace only when fuel, output and player/bot use cycles are coherent.
4. Keep coal meaningful across torch/smithing/processing without becoming mandatory everywhere.
5. Extend source/sink/coherence guard for processed items.

### Phase I — Bot civilization loop

1. Needs/utility planner remains explicit.
2. Shelter/fortification should protect meaningful infrastructure, not decorate randomly.
3. Storage/logistics should use real chests/inventory.
4. Bots should repair valuable tools/structures instead of rebuilding everything.
5. Bots should choose preserve/capture/salvage based on value.
6. Hunting should respect population sustainability and alternate food.
7. Threat response should distinguish trail sabotage, monster defense, wildlife defense and territorial combat.

### Phase J — UI/Android parity

1. Expose oxygen/day-phase/vehicle/structure state through snapshots, not Dart rules.
2. Improve construction selection/placement UX.
3. Map shows construction/controller/vehicles coherently.
4. World browser remains robust/fail-closed.
5. Maintain portrait/landscape mobile usability.
6. No WebView.
7. Android build remains native FFI first-class.

### Phase K — Renderer certification

1. Finish any world-physics extraction first.
2. Regenerate captures from **actual C renderer**.
3. Delete obsolete captures instead of mixing evidence generations.
4. Inspect multiple angles/heights/distances.
5. Check terrain/water/caves/fauna/monsters/construction/rafts/nests/night/light.
6. Check chunk/floating-origin transitions for flicker/pop.
7. Check collision/render alignment.
8. Check performance with larger construction counts.

### Phase L — Performance/debt work continuously

- profile before optimizing;
- prefer sparse/dense stores according to data behavior;
- keep spatial indices local;
- eliminate O(samples x all-artifacts) patterns;
- do not retain derived caches without consumers/invalidation logic;
- scan private symbols for zero consumers;
- centralize coordinate/math semantics;
- keep `quick-gate` focused and fast;
- split megamodules only along real authority boundaries;
- prune generated/build artifacts from checkpoints.

---

## 22. LONGER-TERM GAME COMPLETION MAP

These are **directions**, not permission to add everything at once without architecture.

### World

- more biomes driven by climate/moisture/altitude;
- deeper geology/ore distribution;
- traversable caves;
- weather beyond rain where it affects gameplay;
- water bodies/ecology;
- lighting/shelter/night danger;
- eventual remote strategic simulation LOD for far persistent entities.

### Flora

- shrubs, crops, berries, additional trees;
- moisture/soil requirements;
- harvest cycles;
- seed dispersal/pollination;
- no exponential uncontrolled world growth.

### Fauna

- aquatic, amphibious, herbivore, predator, domestic, pollinator examples only when each proves a new abstraction;
- lifecycle/age/reproduction/population caps;
- tame/pet systems remain species-capability driven;
- domestic persistent animals likely need explicit named/persistent handling.

### Construction

- structural modules;
- doors/roofs/floors/support;
- repair/damage;
- territory capture/use/salvage;
- shelter utility;
- stations/containers placed within structures;
- bot architecture use.

### Transport

- land/water transition planner;
- raft logistics;
- future boat/cargo only through common vehicle profiles;
- no vehicle as inventory speed buff.

### Survival

- hunger/hydration/oxygen;
- cooking/food processing if kept;
- shelter/light danger loops;
- recovery systems that preserve effort.

### Combat

- balanced melee;
- turrets require crafted ammo;
- wildlife/monster attack profiles;
- territory sabotage remains separate from lethal combat;
- death recovery remains transactional.

### Progression

- materials -> tools -> stations -> infrastructure -> mobility -> deeper world domains;
- no random free high-value resources that bypass crafting;
- every new material needs sources, uses and bot reasoning.

---

## 23. NO-GAPS CHECKLIST FOR EVERY NEW FEATURE

For every new item/species/material/vehicle/block/world mechanic, check every applicable edge:

1. append-only stable ID/key;
2. registry/profile entry;
3. capabilities/tags;
4. validator/cross-reference;
5. deterministic RNG ownership;
6. runtime behavior;
7. worldgen/spawn/habitat;
8. physical medium/collision;
9. source/acquisition;
10. inventory/payload preservation;
11. crafting/processing;
12. use/sink;
13. loot/decay if applicable;
14. territory/ownership/control;
15. bot planner;
16. death/recovery semantics;
17. persistence/save migration;
18. floating-origin/global coordinates;
19. renderer/morphology;
20. map representation;
21. public FFI struct/function if needed;
22. native export map;
23. WASM export/smoke;
24. Dart layout/binding;
25. Flutter presentation;
26. System Graph edge;
27. coherence/coupling guardian;
28. focused test;
29. docs/Project Spine;
30. fresh renderer evidence if visual.

A feature is not complete simply because C compiles.

---

## 24. TESTING POLICY

Normal iteration:

```bash
make ffi-test
make ecosystem-test
make world-systems-test
make construction-test
make coherence-test
make bot-economy-test
make continuity-test
make host-check
make graph-check
make content-coupling-check
```

Then:

```bash
make quick-gate
```

Keep strict C flags:

`-std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Werror`

Do not routinely block the container with ASan/UBSan/long soak. Run them only when explicitly requested or a focused bug calls for them.

Do not weaken a valid invariant merely to get green. Determine whether a failing test encodes an obsolete rule or a real regression.

---

## 25. FILES TO OPEN FIRST IN THE NEXT CHAT

### Authority / contracts

- `engine/include/odpar_game.h`
- `engine/src/game_internal.h`
- `PROJECT_SPINE.json`
- `Makefile`

### Immediate ABI sanitation

- `engine/src/artifacts.c`
- `engine/include/odpar_game.h`
- `app/flutter/lib/src/native/odg_bindings.dart`
- `app/flutter/lib/src/engine/game_runtime.dart`
- `tests/test_ffi.c`
- `tools/check_host_contract.py`
- `tools/wasm_smoke.mjs`
- `engine/odpar_territorial_domain.exports.map`

### Construction

- `engine/src/construction.c`
- `tests/test_construction.c`
- `engine/src/map.c`
- `engine/src/render.c`

### Bots/navigation/vehicles

- `engine/src/sim.c`
- `engine/src/artifacts.c`
- `tests/test_bot_economy.c`
- `tests/test_world_systems.c`

### World/ecology

- `engine/src/environment.c`
- `engine/src/geology.c`
- `engine/src/survival.c`
- `engine/src/ecology.c`
- `engine/src/fauna.c`
- `engine/src/resources.c`

### Persistence

- `engine/src/save.c`
- save migration tests in `tests/`

### Architecture guardians

- `tools/generate_system_graph.py`
- `docs/SYSTEM_GRAPH.md`
- `docs/SYSTEM_GRAPH.json`
- `docs/architecture/SYSTEM_GRAPH_MANIFEST.json`
- `tools/check_content_coupling.py`
- `tools/check_host_contract.py`

---

## 26. FIRST COMMANDS FOR THE NEXT CHAT

From the extracted root:

```bash
grep -n "ODG_API_VERSION\|ODG_FFI_ABI_VERSION\|ODG_SAVE_SCHEMA_VERSION" engine/include/odpar_game.h
make ffi-test
make ecosystem-test
make world-systems-test
make construction-test
make coherence-test
make bot-economy-test
make continuity-test
make host-check
make graph-check
```

Expected authority before making the planned ABI cleanup:

```text
API 29
ABI 6
SAVE 18
```

Then address the artifact snapshot ABI debt as Phase A above.

---

## 27. DEFINITION OF THE NEXT STABLE CHECKPOINT

Do not label a future ZIP stable/certified until at minimum:

- public artifact `total_count` debt is explicit and ABI synchronized if modified;
- no C/Dart POD mismatch;
- save schema/migrations remain correct;
- FFI test green;
- ecosystem test green;
- world-systems test green;
- construction test green;
- coherence test green;
- bot economy green;
- 48-recenter continuity green;
- host contract green;
- graph check green;
- content-coupling green;
- native symbol surface green;
- native hardening green;
- WASM smoke green;
- `make quick-gate` green or every component demonstrably green if an external command timeout interrupted only orchestration;
- Project Spine and current docs match C authority;
- build/cache/generated junk excluded from ZIP;
- handoff updated to exact new state;
- fresh visual captures generated if the checkpoint claims visual certification.

---

## 28. FINAL CONTINUITY PRINCIPLE

The objective is not “add lots of features”. It is to make the game **larger while each new feature has fewer wrong ways to connect**.

Use Minecraft-like proven ideas where useful — cheap block data, registries, data-driven content, chunk/world separation, crafting/resource progression — but do not blindly copy Minecraft gameplay or cubic aesthetics.

Every system must make sense in the loops around it:

- ores belong to geology;
- caves belong to physical underground space;
- water has depth/medium/oxygen/ecology/transport;
- animals need habitat/food/reproduction/behavior/loot/population consequences;
- monsters need conditions/telegraph/combat/loot/use;
- light matters to night ecology;
- structures need material/collision/territory/control/destruction/recovery/pathing;
- vehicles need physical existence/placement/movement/access/recovery/logistics;
- ammunition comes from economy, not random magic;
- bots use the same game rules;
- saves preserve player effort;
- Flutter presents C authority;
- renderer and physics describe the same world.

Continue aggressively, but never by creating another half-system that the next chat has to guess how to finish.
