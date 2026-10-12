# Deterministic native stress fixtures (Q02)

The checked-in `fixtures/client-quality/v1` index defines S1–S4 axis sweeps,
dense/spread populations, concentrated combat, static/repeated/rotating models,
legal appearance working sets, Nexus camera variants, churn, UI and mixed scenes.
Use Release performance; sanitizer numbers are correctness evidence.

Run from `/home/kevin/Desktop/RotMG/realm-cpp`:

```sh
python3 tools/quality_stress.py --list
python3 tools/quality_stress.py --scenarios nexus-idle,ui,churn,S1-mixed-dense --trace --with-peers
python3 tools/quality_stress.py --scenarios all --warmup-seconds .02 --measure-seconds .06 --repeats 5 --output build/stress-discovery
python3 tools/quality_stress.py --scenarios S2-projectiles-dense,S3-projectiles-dense,S4-projectiles-dense --output build/projectile-scaling
python3 tools/quality_stress.py --scenarios ui --compare build/realm_viewer --capture --visible --output build/paired-ui
./build/realm_stress --model-library 100 build/model-library.json build/model-library.jsonl
./build/realm_stress --icons 512 600 build/icons.json build/icons.jsonl   # working set 1-512, ticks 2-1000; run from realm-cpp
python3 tests/stress_packets_tests.py build/realm_stress build/packet-fixture
ctest --preset release
ctest --preset sanitize
```

The default runner performs 500 warmup + 3000 measured **simulation** ticks,
five repetitions. Simulation ticks are always 20 ms, independently of rendering
speed; report measured wall duration separately. This is fixed work, not a
promise that every run occupies exactly 70 wall seconds. At current overload
costs the full matrix may take hours. The explicit 1+3-tick discovery command
is a short scaling diagnostic, not sustained capacity or final qualification.
Q19 still requires sustained comparable runs and a 30-minute mixed/churn soak.
Do not interpret a short diagnostic as a 144 Hz pass.

Each run writes exact command arrays, logs, raw frame/CPU/GPU samples, optional
Chrome trace, manifest and binary hashes, dirty source fingerprints, original
map/art/font fingerprints, actual drawable/GL identity, work counts and checksums.
`results.json` compares semantic outcomes across repetitions and paired binaries;
paired order is seeded/shuffled. `report.md` shows p95 repeat variability. Actual
settings are full defaults (VSync/cap zero under `--benchmark`). Renderer queries
remain asynchronous, and capture/readback frames remain excluded. Compare the
same fixtures/assets/clock/settings on the same machine. A >5% control p95/p99
regression requires investigation, not automatic relabeling as noise.

`--capture` saves the final state separately from measured normal samples. For
screen sequences use the existing `--script` actions, now driven by fixed time
when `--stress` or `--fixture-step-ms 20` is present. The latter also supports
finite offline startup/title runs; pass disposable `--account-file` and
`--settings-file` paths. `--stress-delay` changes render pacing without changing
the simulation timeline. Hidden windows provide component/mixed rendering
measurements; visible-window checks confirm drawable/input/presentation behavior.

## What the workload represents

The test driver owns fixture inputs, timeline, replenishment and measurement.
It reuses production World/Combat/Scene/HUD/minimap/options/chat/dialog/inventory
controllers, model projection, appearance/font caches and SDL renderer. It does
not implement a second collision engine or substitute quads for actors.
Synthetic remote motion enters through real NEWTICK status interpolation every
100 ms using the existing 127 ms interval. Hostile/local/multi-hit shots produce
actual acknowledgment/hit/shoot packet bytes through Combat. Authoritative HP
remains server-owned. Playback is never live-server capacity evidence.

Remote population counts exclude the local Priest. Resident/visible/submitted
arrays separately report players, enemies, real model objects, projectiles and
other objects. Visibility uses ground centers in the camera footprint; submitted
counts use actual Scene body draw decisions, including conservative offscreen
candidates. These are deliberately different definitions. Shadows, names, bars
and terrain are extra quads. Summary triangles include model geometry (since Q06
all of a model's triangles before GPU back-face culling);
quad-area sum estimates overlapping parallelogram coverage and excludes model
area/alpha coverage. It is not an exact covered-pixel or fragment count.

Projectiles are replenished to the requested count before production collision.
Survivors, hits, cover/expiration removals, entity/particle simulation steps,
spawn/replacement/reset totals and checksummed ordered wire bytes are recorded.
Pure projectile-axis sweeps have no enemies so every requested projectile can
remain active/submitted. Combined combat has normal hit/cover removals and reports
them; it does not silently lower the manifest population or skip offscreen work.
Dense actors occupy ±2 tiles, clusters ±0.08, spread actors ±45. All dense actor
centers are inside even the 800x600 footprint initially. Full overlap creates
intentional overdraw; it is not a typical playable composition.

MODELS uses all 40 model families reachable through actual catalog objects.
Lab Tank and Candy Doughnut 4 have no legal XML object in this catalog; the
separate `--model-library` CPU component calls production projection for all 42
registered assets at changing rotations. SpiderWeb keeps its source rule and
can stay hidden without an adjacent Wall. XML rotations remain the default;
an explicit per-entity native model transform is used only by rotating fixtures.
Original geometry/materials/shading/stream ordering are unchanged.

VARIETY samples only class-compatible skins and actual dye/textile definitions,
with source size handling; its 16/512 working sets lie below/above the 256-entry
appearance stores. CHURN drops/adds separately because World applies additions
before drops within a single UPDATE. Its resets use the normal generation/teardown
path. RSS is sampled every 50 frames and at exit; CPU/GPU appearance entry/byte
peaks and final values are recorded every frame. C++ allocation traffic requires
`REALM_ALLOCATION_TRACKING=ON`; ordinary zero values are unmeasured.

MIXED combines dense combat and rendering with production HUD/labels/effects,
chat typing/history, inventory drag/tooltip, Options and a purchase-error modal.
The fixture asserts that the equipped-slot drag actually starts. Scripted resize
and existing SDL GPU/input suites cover routing, focus and held-input suppression.
`--with-peers` adds the packet peer and existing disposable HTTP screen-response
fixtures, never saved user accounts or a production server.

## Telemetry at large populations

Ordinary Q01 traces retain individual scopes and the 8192-record/drop bound.
Stress mode opts into repeated-name aggregation: one bounded record per name
per frame, `calls` and cumulative inclusive duration/allocation attribution.
Chrome trace records are anchored at the first completed occurrence; cumulative
work is **not a continuous execution span**. Do not use that display to infer
ordering or gaps between individual projectile queries. Nested inclusive zone
times still overlap. This prevents high populations from discarding the outer
combat/scene/frame costs. All exported drops remain visible; tests verify ordinary
overflow and aggregation above 8192 scopes. Allocation/RSS/upload/geometry/GPU
metrics retain Q01's limitations; there is no physical input-to-display estimate.

## Icon cache component

`realm_stress --icons WORKING_SET TICKS SUMMARY TELEMETRY` requests that many legal
item icons from the production `HudArt` CPU cache every tick (cyclic access). It
checks the entry/byte bounds (at most 256 entries and 8 MiB; the observed icon peak is 128), that working sets above
the cache evict, and that an evicted icon re-enters with an identical image checksum.
It is CPU-cache evidence only; it says nothing about GPU texture residency. Working
set 16 never evicts (16 entries, 200,704 bytes); 512 over 600 ticks holds the cache
at 128 entries / 1,605,632 bytes with 307,073 evictions, i.e. cyclic access above the
capacity misses on every request. Three repeats give identical checksums. CTest runs
`stress_icons_16` and `stress_icons_512` (Release, sanitizers, graphics sanitizers).

## Verification and measured results

Portable numbers, hashes and limits: `evidence/Q02-results.json`; exact commands,
captures and judgments: `handoffs/Q02.md`. Machine: Q00-v1 profile (AMD RX 6700 XT,
Mesa), hidden 1920x1080, full quality, VSync/cap zero. All of the following are short
component/mixed diagnostics; **none qualifies S3/144 Hz (6.94 ms).**

Final checks: Release 50/50 (94.4 s), ASan/UBSan 44/44 (88.9 s), graphics
ASan/UBSan fixtures 10/10 (29.1 s). Determinism gold (`expected-small.json`) is
frozen and independent of render pacing (`--stress-delay` 0 vs 3).

Cost discovery matrix (59 scenarios x 5 repeats; 1 warmup + 3 measured ticks, so p95 is
close to the cold-frame max). CPU p95 median, ms, dense unless noted:

| Axis | S1 | S2 | S3 | S4 |
| --- | ---: | ---: | ---: | ---: |
| players | 15.1 | 63.8 | 197.8 | 395.9 |
| enemies | 0.48 | 2.0 | 5.6 | 10.5 |
| 3D model objects | 0.88 | 3.0 | 7.5 | 12.9 |
| projectiles | 5.4 | 105.1 | 819.3 | 3,216.9 |
| mixed dense | 54.9 | 270.6 | 783.8 | 1,573.7 |
| mixed cluster | 27.5 | 438.0 | 2,086.3 | 7,773.1 |

Sustained-length repeats (100 warmup + 200 measured fixed ticks, five repeats; p95
median ms): Nexus idle/pan/zoom 0.20/0.20/0.22, Nexus rotate 0.78, repeated models
(1000) 2.06, rotating varied models 3.50, UI 4.4, variety-16 42.4, **variety-512
141.6**, churn 59.0, S1 mixed dense 65.8. Repeat spread is 1-2% for the large workloads
(variety, churn, mixed) and 20-50% for sub-5 ms ones (Nexus, UI, varied models).

Ranked current costs (zone medians, inclusive and overlapping; not additive):

1. **Projectile target query** (`combat.target_query`): 755 ms of the 819 ms S3
   projectile frame, 1.33 s inside S3 mixed cluster. 20k projectiles cost roughly
   0.8-0.9 s, 40k roughly 3.2 s, super-linear with population: the entity scan is
   the dominant capacity limit. **Resolved by Q03** (grid candidate search; `combat.update` at S3 combat cluster 1.2 s -> 1.7 ms, see handoffs/Q03.md).
2. **Actor appearance lookup/compose** (inside `scene.build`): 27 ms at 600 varied
   players with a 16 working set, 90 ms at 512 (the 256-entry store thrashes); 455 ms of
   the S3 mixed-dense frame. Q05/Q07.
3. **Player name text** (`text.rasterize`): ~25 ms per 1000 dense players, ~79 ms at
   3000, plus 8.6-26 ms texture upload. Q07/Q10.
4. Model CPU preparation/submission is cheap by comparison (3,000 models: 7.5 ms CPU,
   3.0 ms GPU); GPU p95 stays 2-12 ms through S3 mixed, so the measured limits are CPU.
   **Q06** made models GPU instances of persistent geometry: S3 dense models p50 6.2 -> 1.4 ms,
   GPU 3.3 -> 0.93 ms, 27,800 -> 876 allocations per frame (handoffs/Q06.md).
5. Visible actors overlapping fully produce 100-170k triangles at S3 mixed; spread
   layouts submit only ~18% of residents.

Churn: 29 resets in 3000 ticks hold the appearance cache at 256 entries / 4.85 MB and
RSS at 152.4 -> 154.9 MB, the same +2.5 MB seen after only 2 resets (a plateau, not
growth per reset). Same-binary paired controls: p95 ratios 0.98 (Nexus idle) and 0.87
(UI). Ordinary original-Nexus control, pre-replay vs current profile binaries (3000
frames, five shuffled repeats, identical counters): p95 x1.011, p99 x1.016, within the
5% line and repeat variability.

## Collision search diagnostics (Q03)

`Combat` selects enemy-targeting candidates from a per-update grid. `REALM_TARGET_SEARCH=scan` in the environment selects the original full scan in
the same binary for paired before/after runs; stress summaries record `target_queries`, `target_examined`, `target_builds` and `target_indexed`.
`./build/realm_combat_target_tests --profile` prints candidate counts and update cost for spread, dense, boundary and pathological populations.

## Visibility and world-query diagnostics (Q04)

`REALM_QUAD_CHECKSUM=1` makes the viewer's stress summary include `quad_stream_checksum`, an order-sensitive digest of every submitted quad and projected mesh
(origin/axes/uv/tint/kind/vertices): pair two binaries with `--compare` to prove a visibility/ordering change submitted the same painter stream. Zones added:
`world.interactive_target`, `scene.world_dyes`, `minimap.update` (nested `minimap.markers`, `minimap.render`), `party.update`. `Scene::set_culling(false)` disables camera
culling for the differential `visibility_culling` test (`./build/realm_visibility_tests`, graphics builds; `VISIBILITY_SEED`, `VISIBILITY_REPORT=1` and
`VISIBILITY_EXTENTS=1` are diagnostics); `./build/realm_world_query_tests` checks the nearby-object queries against the original scans.

`REALM_QUAD_CHECKSUM=models-as-markers` (Q06) hashes every non-model quad in full but each model entry only as a marker, so a model-submission change
(prepared meshes vs GPU instances, whose quads necessarily differ) can still prove that every other quad and every model's stream position are unchanged.
Per-frame zone/counter medians of any run (or either side of a `--compare` run, `DIR:current` / `DIR:compare`) come from `python3 tools/quality_zones.py`.

## Storage and lifetime diagnostics (Q05)

`./build/realm_world_storage_tests --profile` reports counted-heap bytes per object/projectile, live/peak bytes, allocations per steady tick and populate/teardown cost at S2/S3 shapes;
without arguments it is CTest `world_storage` (stale-reference, container differential and repeated-teardown checks). `python3 tools/quality_pair.py --before B --after build/realm_stress
--scenarios ... --output DIR` pairs two headless `realm_stress` binaries, requires identical outcomes and reports median `fixture.timeline`/`combat.update`/`world.update`. Layout, per-object
cost and reset rules: [storage.md](storage.md).

`quad_stream_checksum` no longer hashes GL texture names for surface-owned quads (they are renderer handles, not content); digests from before this change are not comparable with later ones. `python3 tools/quality_stress.py` steady-state tail: use `--warmup-seconds 4 --measure-seconds 4` and read p50 separately from p95/p99, which at S3 are dominated by first-seen appearance composition.
