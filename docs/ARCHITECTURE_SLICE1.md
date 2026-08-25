# Slice 1 architecture

## Runtime ownership

```text
Android touch / Flutter widgets
          |
          v
UI MAIN ISOLATE
  - pointer ownership + joystick
  - natural look mapping / invert setting
  - Flutter FrameTiming p50/p95/p99
  - latest EngineSnapshot presentation
  - Texture composition
          |
          | bounded small messages; <= 1 input packet in flight
          v
DART ENGINE BRIDGE ISOLATE
  - opens libodpar_greenfield.so
  - owns OdgEngineService* lifetime
  - submits input packets
  - polls UI snapshot at 20 Hz
          |
          | generated ABI v1
          v
C ENGINE SERVICE
   +-----------------------+     +------------------------+
   | SIMULATION OWNER      | --> | immutable snapshots    |
   | one writer, fixed 60  |     +-----------+------------+
   +-----------------------+                 |
                                             v
                                  +------------------------+
                                  | RENDER WORKER          |
                                  | software 3D raster     |
                                  | never mutates gameplay |
                                  +-----------+------------+
                                              |
                                              v
                                  ANativeWindow / SurfaceProducer
                                              |
                                              v
                                         Flutter Texture
```

There is no `Ticker -> native tick -> native render -> framebuffer copy` product path. Flutter's ticker only samples already-owned touch state into a small packet; it does not wait for C.

## C ownership

`OdgEngineService` is the runtime service owner. It contains an `OdgEngine` gameplay context plus explicitly-owned synchronization/runtime state. Gameplay itself is not global. The simulation thread is the only writer of `OdgEngine`. Input arrives through a latest-state mailbox with accumulated look deltas and edge buttons, then is consumed exactly once by simulation. Continuous input older than 250 ms is neutralized before stepping: a suspended/stalled host cannot leave movement, look deltas, or held actions latched forever.

The renderer copies an `OdgRenderSnapshot` under a short mutex and never reads live gameplay stores. Resize is a render-worker request. Failure to allocate a requested raster extent reverts the request to the last valid raster rather than publishing a false size. Both simulation and render schedulers drop missed presentation/simulation debt rather than executing catch-up spirals.

The Dart bridge owns one native service reference. Each Android `SurfaceProducer` render texture explicitly retains a second native reference while lifecycle callbacks can exist. Texture teardown removes callbacks, detaches the `ANativeWindow`, releases the producer, then releases that reference. Final service memory is therefore freed only after both Dart ownership and Android presentation ownership are gone. `GameScreen` serializes asynchronous startup before product teardown, so texture creation cannot race Dart service destruction.

## Camera and movement

C defines **positive look pitch as LOOK UP**. Dart converts a finger-up delta (`dy < 0`) to positive pitch exactly once; invert-Y flips that sign exactly once when enabled. A 2.5 px gesture-start slop rejects micro-jitter without the legacy long decaying look accumulator. Pitch clamps to a safe third-person range; camera yaw remains independent of player facing. Player movement is camera-relative with acceleration/deceleration, jump/gravity, height-aware simple collision/support, and gradual body facing. Camera collision is authoritative against the same world geometry; after an obstruction clears, chase distance recovers with a critically damped response instead of snapping outward. The current `0.55` touch sensitivity is a device-playtest candidate, not a frozen product constant.

Movement pointer ownership is lower-left. Look owns other world-touch space except explicit UI control zones, so the jump control is not accidentally also a camera gesture.

## Render/presentation

Slice 1 uses a deliberately small software raster surface with aspect-preserving caps: maximum long side 640 and target short side <=360. Portrait 1080x2400 maps to 288x640; landscape 2400x1080 maps to 640x288. World collision boxes and rendered static boxes come from the same world authority.

The player presentation is a low-poly head/torso/limb rig. Gait is driven by actual traveled distance/speed rather than global tick sine as an independent animation clock.

## Telemetry

Flutter records total frame times from `FrameTiming`; C records simulation and render cost in bounded rings. The on-screen diagnostic shows p50/p95/p99 plus lifetime maxima, Flutter frames >50 ms, SIM >5 ms and RENDER >16.667 ms breach counters, C scheduler overload count, and input age. The separate lanes make isolated stalls attributable instead of letting a later percentile hide them. Quantile calculation copies the C perf rings before sorting so the simulation/render writers are not held behind telemetry work.

## Deliberate non-features

No targeting, interaction offer, tree chopping, ore, fauna, ecology, survival vitals, save system, territory, crafting, or electricity exists in Slice 1. Absence is intentional: there are no fake stubs pretending those systems are complete.
