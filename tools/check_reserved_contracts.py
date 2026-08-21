#!/usr/bin/env python3
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]

def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding='utf-8')

def fail(msg: str) -> None:
    print(f'RESERVED CONTRACT FAIL: {msg}', file=sys.stderr)
    raise SystemExit(1)

header = read('engine/include/odpar_game.h')
internal = read('engine/src/game_internal.h')
artifacts = read('engine/src/artifacts.c')
interactions = read('engine/src/interactions.c')
chunks = read('engine/src/chunks.c')
map_c = read('engine/src/map.c')
environment = read('engine/src/environment.c')
construction = read('engine/src/construction.c')
save_c = read('engine/src/save.c')

def struct_body(text: str, name: str) -> str:
    end_marker = '} ' + name + ';'
    end = text.find(end_marker)
    if end < 0:
        fail(f'{name} declaration missing')
    start = text.rfind('typedef struct {', 0, end)
    if start < 0:
        fail(f'{name} typedef start missing')
    return text[start + len('typedef struct {'):end]

body = struct_body(header, 'odg_artifact_snapshot')
if 'uint32_t total_count;' not in body:
    fail('artifact snapshot total_count is not explicit')
if 'reserved_u32' in body:
    fail('artifact snapshot regressed to a semantically active reserved field')

ibody = struct_body(internal, 'odg_artifact')
if 'uint32_t fluid_type_id;' not in ibody:
    fail('persisted artifact fluid identity is not semantically named')
if re.search(r'\breserved_u32\b', ibody):
    fail('internal persisted odg_artifact still contains reserved_u32')


construction_public = struct_body(header, 'odg_construction_entry')
for required in ('uint32_t health;', 'uint32_t max_health;'):
    if required not in construction_public:
        fail(f'construction public integrity field missing: {required}')
if 'reserved_u32' in construction_public:
    fail('construction public entry regressed to reserved integrity semantics')

construction_internal = struct_body(internal, 'odg_construction_block')
for required in ('uint32_t health;', 'uint32_t max_health;', 'uint32_t reserved_u32;'):
    if required not in construction_internal:
        fail(f'construction persisted layout field missing: {required}')
if 'reserved_u32[' in construction_internal:
    fail('construction persisted integrity semantics must not hide in a reserved array')
if 'a->reserved_u32!=0u' not in construction.replace(' ', ''):
    fail('construction validation does not fail closed on nonzero compatibility slot')
if 'b->reserved_u32=0u' not in save_c.replace(' ', ''):
    fail('SAVE18/19 -> SAVE20 construction migration does not zero compatibility slot')

for rel, text in [('engine/src/artifacts.c', artifacts), ('engine/src/interactions.c', interactions)]:
    if '->reserved_u32' in text:
        fail(f'{rel} actively reads/writes a reserved field')

if 'reserved_u32' in chunks:
    fail('chunks.c must not smuggle worldgen semantics through public reserved fields')

if 'uint32_t save_reserved_weather_u32;' not in internal:
    fail('frozen weather compatibility slot declaration missing')
if 'save_reserved_weather_u32' in environment:
    fail('weather runtime must derive epoch from tick, not mutate the frozen save slot')
sim = read('engine/src/sim.c')
if 'g_odg.save_reserved_weather_u32=0u' not in sim.replace(' ', ''):
    fail('new worlds do not explicitly zero the frozen weather save slot')

for required in ('m->reserved_u32=0u', 'sample->reserved_u32=0u'):
    if required not in map_c.replace(' ', ''):
        fail('map public reserved outputs are not explicitly zeroed')

print('RESERVED CONTRACTS OK artifact_total=explicit artifact_fluid=semantic construction_integrity=explicit weather_reserved=inactive chunk_reserved=unused map_reserved=zero')
