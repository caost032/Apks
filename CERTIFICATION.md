# ODPAR: Territorial Domain — API37 / ABI9 / SAVE25 checkpoint verification

Verification date: **2026-08-20 America/Guayaquil (-05:00)**

This records fast verification actually completed for the current working tree. It is not a claim of
a long sanitizer/soak release certification.

## Contract

- C11 gameplay authority: API **37**
- FFI ABI: **9**
- Save schema: **25**
- Save migration chain intentionally supported: **14 through 25**
- Android product architecture: Flutter/Dart → explicit FFI → native C11

## Latest focused/full fast-gate evidence

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

The complete `make quick-gate` chain above finished green after documentation synchronization. The
execution harness detached from the foreground command while the process continued, so no wall-clock
duration is claimed for this run; the component outputs above are the authoritative evidence.

No long ASan/UBSan/soak run is claimed.
