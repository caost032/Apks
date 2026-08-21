# Embedding ODPAR: Territorial Domain v15

The supported native product boundary is Flutter/Dart → FFI ABI 9 → C11 (current engine API 37).
Hosts must call `odg_ffi_abi_query` and validate API/ABI/struct sizes/feature bits before reading POD
data. A POD field-order or field-meaning change requires an ABI bump even when `sizeof` is unchanged.

## Input and commands

- movement/look use the Q15 input contract;
- `ODG_BUTTON_INTERACT` is the universal tap/hold interaction;
- `ODG_BUTTON_DROP` drops from the authoritative selected slot;
- discrete gameplay actions use `odg_command_submit`; widgets never mutate game state directly.

## Queries

Prefer bounded copy/query APIs for inventory, interactions, recipes, resources, artifacts,
construction, ecology, surface/map, music and render buffers. Artifact and construction queries are
paged so a large persistent world cannot be silently truncated by UI buffer size.

## Thread ownership

One host runtime/controller serializes gameplay commands, snapshots, render, texture upload, map,
save/load and remote-view calls. The Android PCM bridge is the intentional synchronized exception.

## Presentation state

Camera, remote view, avatar textures and music reactivity are presentation-only. They may change
pixels but must not change `odg_state_hash()`.

## Save boundary

C returns/accepts a versioned save blob. The current writer emits schema 25 and
`odg_save_schema_supported()` recognizes the deliberate 14→23 migration chain. API/ABI in world
metadata are provenance, **not** a save-compatibility gate.

The host owns filesystem operations and atomic replacement. On load failure, corruption or an
unsupported schema, preserve original bytes and surface the error; never create-and-save a replacement
world over the failed slot.
