# Client benchmark and quality contract

This document defines the fixtures and evidence Q00–Q02 must establish, not
measurements already collected. Proposed population levels and thresholds are
planning defaults. Q00 records the actual machine and freezes an initial
target profile; Q19 reports the capacity achieved on that profile.

Q00 established [baseline.md](baseline.md) on 2026-10-11, including the frozen
Q00-v1 primary/stretch profile, provisional resource budgets and preliminary
component results. S3/144 Hz remains unqualified; use that baseline for Q01/Q02
and retain the workload/measurement obligations below.

## Population and workload dimensions

Count players, enemies, model objects, projectiles, and decorative particles
separately. Record resident, simulated, visible, and submitted counts. A thousand
resident objects with ten visible does not establish thousand-object rendering.

| Level | Players | Enemies | Model objects | Active projectiles | Purpose |
| --- | ---: | ---: | ---: | ---: | --- |
| S0 | Actual Nexus population | Actual | Actual | Actual | Representative map baseline. |
| S1 | 250 | 250 | 250 | 2,000 | Early scaling and development checks. |
| S2 | 1,000 | 1,000 | 1,000 | 10,000 | Initial thousand-object qualification. |
| S3 | 3,000 | 3,000 | 3,000 | 20,000 | Proposed combined capacity target. |
| S4 | Twice S3 | Twice S3 | Twice S3 | Twice S3 | Overload characterization and bounded degradation. |

Build individual-axis sweeps first, then combined populations. S3 is a goal,
not a guaranteed result on arbitrary hardware. Preserve the original goal if
an initial implementation misses it: report the failure and optimize. Any
target revision must state the measured reason; silently lowering populations,
quality, visibility, or update cadence is not a speedup.

For S1–S4 use spread-out and concentrated arrangements. Include a fixture with
all specified objects in the camera footprint, and report actual visible and
submitted counts. Also include realistic moving combat, overlapping sprites,
and a pathological cluster. Dense coverage and transparency create overdraw;
report pixel coverage and triangles as well as object counts. Use actual
registered models and real legal definitions/appearance combinations. Keep
static model geometry and changing model rotations as separate workloads.

| Fixture | Production paths exercised | Required variations |
| --- | --- | --- |
| NEXUS | Map import, terrain, camera, models, sprites, effects | Idle, pan, rotation, zoom, resize; original map hash. |
| POPULATION | World update, animation, visibility, sorting, renderer | Count sweeps, static/dynamic, spread/dense, names and HP bars. |
| COMBAT | Combat update, spatial search, hits, expiration, outbound events | Players/enemies/projectiles, multi-hit, crossings, crowded cells. |
| MODELS | Model preparation, upload, draw submission, overlap | Every model family, repeated/varied geometry, camera/object rotation. |
| VARIETY | Appearance/font/icon caches and texture residency | Dyes/skins/sizes, legal working sets below/above budgets, re-entry. |
| CHURN | Add/remove, spawn/despawn, map reset, resource destruction | Sustained replacement and repeated map transitions. |
| PACKETS | Real codecs/session dispatch with a local fixture peer | Fragmentation, coalescing, jitter, bursts, reconnect; bounded queues. |
| UI | Production layout, input, text, retained surfaces, submission | Typing, scroll, drag, tooltip, modal, resize, animated title. |
| MIXED | All relevant client subsystems together | Dense combat plus HUD, labels, effects and UI interactions. |

Build client-side fixtures and synthetic protocol peers; no server capacity
work is needed. Synthetic enemy/player motion must pass through the relevant
world/session behavior rather than bypass it. Combat fixtures must actually
invoke combat and produce checksummed hit/packet outcomes. Label every
component-only experiment explicitly. Never equate extra quads with players.

## Determinism and reproducibility

- Store versioned fixture definitions, seeds, legal object IDs, population
  settings, scripted camera/input, quality, and expected state/packet summaries.
- Use a deterministic simulation clock and event timeline independent of
  render speed for before/after comparisons. Preserve server-tick/interpolation
  semantics; do not replace gameplay timing merely to simplify the harness.
- Run a fixed duration/tick count so fast builds do not execute less game work.
  Record count changes caused by expiration and replenishment during the run.
- Use Release for performance. Record compiler, optimization/LTO, build flags,
  profiler state, commit plus dirty diff/file fingerprints, assets and map hashes.
  Preserve a local baseline snapshot/hash inventory without resetting the tree.
- Record CPU, GPU, RAM, OS, driver, drawable dimensions, UI density, window mode,
  monitor refresh, VSync, FPS cap, power mode and actual GL renderer. A software
  renderer is a different target. Hidden rendering is component evidence and
  needs a visible-window confirmation for presentation/input claims.
- Proposed measurement protocol: 10 seconds warm-up, 60 seconds measured,
  five repetitions. Keep cold-load/cache runs separate. Randomize paired run
  order when thermal/drift effects matter. A 30-minute mixed/churn soak is
  required at final qualification. Store raw samples, not only summaries.

## Metrics and provisional acceptance

Q00 should freeze a primary current-machine profile at 1920×1080 with the
familiar full visual settings and a provisional 144 Hz target (6.94 ms).
Also check 800×600 geometry, 1280×720, and 3840×2160 scaling. Treat 240 Hz
(4.17 ms) and 4K capacity as stretch results until qualified. Record physical
drawable resolution; logical window size is insufficient on HiDPI displays.

For the primary target, propose p95 frame interval within one frame budget and
p99 within two, with all required work and quality enabled. Q00 must record
whether this target is achievable at baseline; a miss is evidence, not a pass.
Record uncapped CPU/GPU capacity separately from VSync/paced user experience.
CPU and GPU timings overlap and must not be added as if they were serial work.

Collect median/p95/p99/max for frame intervals and CPU event/update/query/
build/upload/submit, GPU world/models/UI/effects, and present time. Keep GPU
queries asynchronous and timestamp samples to the actual frame. Record
allocations/bytes, texture/mesh upload bytes, draws/batches, triangles/quads,
cache hits/misses/evictions, packet bytes/queues, and optional-effect drops.
Record input-to-submission latency; call it input-to-display only with an
appropriate presentation/display measurement.

Before each optimization, predeclare the targeted metric and success threshold.
Use repeat variability to judge significance. A provisional >5% regression in
control-workload p95/p99 triggers investigation; use repeated comparable runs
to separate noise from a real regression. Do not compare incompatible hardware
or settings. Organization-only changes should show behavior equivalence and
performance within measured noise, not invent a speedup requirement.

Set explicit CPU cache, GPU texture, queue, and optional-effect budgets from
baseline measurements. Explain projected per-object memory at S3. Repeated
churn/reset must settle within those budgets without a persistent upward trend.
Do not drop authoritative state or damaging projectiles to satisfy a budget.
Optional-effect limits must be observable and tested. Overload may reduce
decorative quality under an explicit setting, preserving gameplay and input.
Full-quality and reduced-quality results must remain separate.

## Visual and behavior checks

Capture identical state/camera/time and inspect source/native side by side at
normal size and enlarged resolutions. Check world overlap, tall models,
shadows, blending, masks/dyes, text baselines/wrapping, clip boundaries, focus,
disabled/hover/pressed/loading/error states, and animation sequences. Numeric
image diffs are diagnostics, not the only acceptance judgment. Do not inflate
tolerances to hide visible defects. Keep source artwork/fonts unchanged.

Spatial-search replacements must compare against an independent reference
full scan with ties, eligibility, cell boundaries, removals and multi-hit
history. Culling must preserve offscreen collision/state and large rotated
object bounds. GPU changes must preserve painter order and relevant blending.
UI changes must retain pointer capture, modal blocking, coordinate transforms,
editing/IME behavior where supported, and no accidental firing through controls.

## Existing commands and their limits

Run from `/home/kevin/Desktop/RotMG/realm-cpp`:

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release
cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize
./build/realm_viewer --hidden --benchmark --frames 600 --sprites 10000
./build/realm_viewer --map ../realm-server/Resources/Worlds/Nexus/0.jm --fonts build/fonts-client --hud-art build/hud-art --hidden --benchmark --frames 600 --width 1920 --height 1080
python3 tests/render_tests.py build/realm_viewer
```

These commands exist today; they are preliminary checks, not the future stress
suite. Verify asset availability first. The extra-sprite path appends quads and
does not create active players/enemies/projectiles. Without `--telemetry`, output reports mean/p95 CPU and present; GPU timing is
now opt-in and the last value is unavailable when disabled. Q01 adds raw JSONL,
Chrome/Tracy CPU zones, frame-attributed asynchronous GPU phases and counters:
see [telemetry.md](telemetry.md) for working commands, overhead and metric limits.
`--benchmark` bypasses some interactive movement. Fixed scene/effect clocks do
not freeze the existing real-time particle simulation; even disabling Particles
after map import leaves existing source-equivalent emitters running. Q02 must
supply the missing deterministic production-path coverage. Capture/readback
frames and their following intervals are excluded by the Q01 reporter.

The sanitizer preset has `REALM_BUILD_VIEWER=OFF`. When a graphics change needs
graphics sanitizers, create a separate build explicitly:

```sh
cmake -S . -B build-sanitize-graphics -G Ninja -DCMAKE_BUILD_TYPE=Debug -DREALM_SANITIZERS=ON -DREALM_BUILD_VIEWER=ON
cmake --build build-sanitize-graphics
ctest --test-dir build-sanitize-graphics --output-on-failure
```

Run the affected GPU fixtures against that build as applicable. Sanitizer
timings do not establish Release performance. Q02 adds documented runnable
stress commands; never publish invented future flags as working commands.
