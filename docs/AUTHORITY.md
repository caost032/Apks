# Greenfield authority contract

## Precedence

The active architecture is the Refundación Greenfield current epoch. `CURRENT_GREENFIELD/PASS_G00..G23` has precedence over `HISTORICO_REV3`. The historical REV3 files remain provenance and design evidence, not an implementation mandate. The current master prompt further fixes this first mission's scope; in particular it places health/hunger/thirst HUD in Slice 2, so Slice 1 does not invent fake survival state merely to satisfy an older roadmap sentence.

Authority package SHA-256: `f8378e8bcd19e19885f430f58ba21a8bd303d485b57c1d0695a78dabab057344`.

Legacy checkpoint SHA-256: `a6311729e43712ba53aa524a4b8e5812819ee3fd349962ddb1c21437500c46be`; read-only reference only.

## Greenfield laws carried into code

- New physical source tree; migrate capabilities, never legacy files as an architecture.
- C11 is gameplay/simulation authority. Flutter/Dart is product host. Kotlin is Android presentation plumbing only.
- One explicit `OdgEngine` composition root; no `g_odg`, no mega internal header, no new monolithic `sim.c` or `render.c`.
- UI main isolate cannot synchronously own simulation, renderer, ecology, save, or world streaming.
- Simulation has one writer at 60 Hz fixed step and does not perform catch-up spirals.
- Render consumes immutable snapshots and may drop intermediate snapshots rather than delay input.
- Natural camera convention is finger up -> look up; invert-Y is explicit and off by default.
- Reticle is future targeting ray origin/direction, but Slice 1 does not implement fake proximity interaction.
- Performance is a product invariant: p50/p95/p99 and >50 ms spikes are observable from the first APK.
- Android product ABIs are exactly armeabi-v7a + arm64-v8a; x86_64 is optional dev only.
- Build success is insufficient. APK artifact signature, ABI set, native libraries, 16 KiB page alignment, checksum, installation, and device playtest are separate gates.
- Pre-release saves/ABI may break when architecture correctness demands it; accidental compatibility debt is not preserved.

## Full input traversal

This checkpoint records a complete byte traversal of all 74 Refundación files (612,297 UTF-8 bytes) in `docs/audit/AUTHORITY_MANIFEST.tsv`. All 229 legacy files (72,669,187 bytes) were also traversed and hashed. This is intentionally audit evidence, not a copied dependency.
