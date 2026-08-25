# Slice 1 performance budget

Performance is an admission gate, not cleanup work.

## Baseline budgets

- Simulation authority: fixed 60 Hz.
- Simulation target: p95 < 3 ms, p99 < 5 ms in the standard scenario.
- Product target: 60 FPS at the selected native raster extent.
- Ten-minute Android canary: zero ODPAR-attributable freezes >50 ms.
- No catch-up spiral: if the simulation owner is more than two steps late, it records overload and drops debt instead of executing an unbounded burst.
- Render may consume only the latest immutable snapshot; stale intermediate render snapshots have no right to delay input.
- UI snapshot sampling is 20 Hz, not per pixel/per frame FFI traffic.
- Continuous host input older than 250 ms becomes neutral; a host stall cannot turn one old joystick packet into indefinite movement.

## Measured lanes from first APK

1. Flutter total frame time p50/p95/p99 and count >50 ms.
2. C simulation p50/p95/p99.
3. C render p50/p95/p99.
4. Lifetime maximum and breach counters for SIM >5 ms and RENDER >16.667 ms.
5. Flutter lifetime maximum plus >50 ms frame count.
6. Simulation overload count.
7. Age of the latest accepted input packet.

The diagnostic HUD is temporary engineering telemetry, not a gameplay HUD. Device evidence determines later render-quality policy; no interpolation may be used to hide a measured simulation stall.
