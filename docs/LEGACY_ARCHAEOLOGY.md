# API38 legacy archaeology — read-only

The API38/ABI10/SAVE30 checkpoint was not used as the Greenfield starting tree. Its ZIP and the Refundación PASS G20 inventory agree exactly: **229 expected files, 229 actual files, zero missing, zero extra, zero size mismatches, zero SHA-256 mismatches**.

Every file was traversed and hashed. The complete per-file record and disposition is `docs/audit/LEGACY_MANIFEST_AND_DISPOSITION.tsv`.

## High-value concepts retained as knowledge

- Deterministic fixed-step simulation principle, but not the legacy host call shape.
- 64-bit/global coordinate and chunk working-set ideas.
- Procedural untouched world + sparse edit ideas.
- Hydrology macro/budgeted work concepts.
- Ecology stocks and WARM/COLD aggregation concepts.
- Transactional item/resource presence concepts.
- Compact structural support-graph concept.
- Native symbol/hardening/artifact verification discipline.
- The exact coherent Android toolchain matrix and APK postmortem lessons.

All such items are `REIMPLEMENT_CONCEPT`: they must be expressed through Greenfield ownership/API/invariants when their slice arrives. No source file is authoritative merely because its algorithm was useful.

## Shapes rejected now

- `engine/src/sim.c`, `engine/src/render.c`, `engine/src/game_internal.h` as monolithic ownership shapes.
- `g_odg`-style global gameplay authority.
- Flutter `Ticker` synchronously driving native simulation/render.
- Legacy input decay/sign behavior.
- Proximity-driven `InteractionHint`/primary action behavior.
- Legacy HUD/presentation and old tree/fauna visual semantics.
- Experimental SAVE28/SAVE29/SAVE30 compatibility tests as a Greenfield pre-release constraint.

## Why the old test suite is not copied

Tests protect semantics, not filenames. Legacy tests are classified as rewrite/retire/review-later. Slice 1 has new focused canaries for the new contracts. Hydrology, ecology, presence, support, world-edit, and similar invariants are recorded as valuable future rewrite candidates, but importing their old fixtures before those systems exist would fossilize the architecture we are replacing.
