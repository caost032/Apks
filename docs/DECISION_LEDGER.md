# Greenfield decision ledger — Slice 1

| ID | Decision | Reason | Revisit trigger |
|---|---|---|---|
| GF-001 | New source tree, no legacy copy | Greenfield authority G00 | Never as cleanup strategy |
| GF-002 | 60 Hz single-writer C simulation thread | G02 fixed-step product budget | Device profiling demonstrates a better fixed rate |
| GF-003 | Separate native render worker | Prevent Flutter/input stalls and live-state renderer coupling | Only if a future backend preserves snapshot ownership |
| GF-004 | Dart bridge isolate owns FFI service handle | Flutter main isolate must remain presentation/input only | A future Flutter FFI isolation mechanism proves simpler with same ownership |
| GF-005 | SurfaceProducer/ANativeWindow render presentation | Avoid full-frame FFI copies | GPU backend replacement with equal/better ownership |
| GF-006 | Host ABI generated from JSON schema | Prevent C/Dart layout drift | Schema generator itself is replaced with a stronger single source |
| GF-007 | Positive C pitch means LOOK UP | Eliminate sign ambiguity | Never; only user invert setting changes mapping |
| GF-008 | 2.5 px gesture-start slop, no legacy decay tail | Reject micro-jitter without gelatinous continuation | Device playtest tuning |
| GF-009 | Camera independent from body facing | Natural third-person look/move composition | Ruleset/gameplay explicitly requires temporary lock-on |
| GF-010 | Low raster cap 360 short / 640 long | Mobile-first software-render budget, aspect preserved | Real device p99 data |
| GF-011 | Keep proven Android matrix atomically for epoch A1 | Avoid repeating toolchain migration failures | Deliberate whole-matrix upgrade |
| GF-012 | No survival vitals in Slice 1 | Current master prompt places them in Slice 2; no fake state | Slice 2 starts after APK exit gate |
| GF-013 | No Slice 2 code before physical APK gate | Small healthy game over broad unverified engine | Slice 1 device gate passes |
| GF-014 | SurfaceProducer owns an explicit native service reference | Prevent asynchronous Android surface callbacks from outliving Dart service ownership | Backend/lifecycle model is replaced with a stronger ownership primitive |
| GF-015 | Continuous input expires after 250 ms without a fresh host packet | Suspended/stalled UI must fail neutral, never latch movement/actions forever | Physical device evidence shows threshold should be tuned |
| GF-016 | Product teardown waits for asynchronous startup to settle | Prevent texture retain from racing native owner destruction | Host lifecycle architecture is replaced |
