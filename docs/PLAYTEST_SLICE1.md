# Android device playtest — Slice 1 exit gate

Use a physical Android phone with the verified universal or matching split APK. This is an experience oracle, not a ceremonial smoke test.

1. Install the APK and launch cold. Confirm no crash, black permanent frame, or missing native library.
2. Hold the phone in portrait for at least two minutes. Walk forward/back/diagonal, make circles, stop/start rapidly, jump, and collide with each simple obstacle. Movement must follow the joystick relative to the camera and must not drift after release.
3. Drag look upward: the view must look upward. Drag downward: it must look downward. Tiny touch jitter should not make the camera twitch. Slow aim and fast swipes must both remain controllable.
4. Enable `Y INV`; repeat. The vertical direction must reverse exactly once. Disable it and verify natural mapping returns.
5. Move while looking independently. Camera yaw must not be forcibly snapped to body facing; body facing should converge toward actual movement.
6. Press/slide around the jump control. The same finger must not secretly own look. Use simultaneous left movement + right look + jump.
7. Rotate to landscape and back to portrait. Rendering must preserve proportions, controls must remain usable, reticle centered, and the engine must not restart into a broken state.
8. Run continuously for ten minutes with regular movement/look/jumps. Watch telemetry. Record any Flutter frame >50 ms, C overload increment, visible freeze, repeated hitch cadence, or input-age spike. A repeatable ODPAR-caused freeze fails the slice even if the app recovers.
9. Observe SIM and REN p95/p99. Simulation should remain under the 3/5 ms design targets in the standard minimal world; investigate any sustained breach before adding content.
10. While moving and while holding jump, background the app; after foregrounding, no old movement/look/action may remain latched. Repeat basic movement/look, then exit normally. No crash, stale camera jump, runaway movement, or stuck input is acceptable.

Pass only when the controls feel humanly comfortable as well as technically correct. A green unit gate cannot override a bad physical-phone result.
