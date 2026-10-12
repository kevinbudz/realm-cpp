# Native performance telemetry (Q01)

Telemetry is opt-in at runtime. Ordinary Release/sanitizer builds do not download
or link a profiler and do not replace the allocator. Example from realm-cpp:

```sh
./build/realm_viewer --map ../realm-server/Resources/Worlds/Nexus/0.jm --fonts build/fonts-client --hud-art build/hud-art --hidden --benchmark --frames 1200 --width 1920 --height 1080 --scene-time 1000 --effect-time 1000 --telemetry build/nexus.jsonl --trace build/nexus-trace.json --telemetry-warmup 30
python3 tools/telemetry_report.py build/nexus.jsonl --output build/nexus-report
```

The same flags work in `realm_client`. Output directories must already exist.
`--trace` requires `--telemetry`; their paths must differ. The trace is a Chrome
trace-event JSON file (open in a compatible local trace viewer). The reporter
produces a readable Markdown table and a JSON summary with median/p95/p99/max.
It reports inclusive per-invocation microseconds and per-frame milliseconds for
CPU zones. Do not sum nested zones' times or allocation traffic.

These short offline Nexus runs exercise scene/render/HUD paths, not a connected
world/combat/session update. `--sprites` appends quads, never active players.
Q02 owns representative deterministic workloads, duration-based warmup, manifests
and the longer measurement/soak protocol. Timings are observations, not replay
checksums. Identical static runs should have equivalent work counters, not
identical durations. Hidden results do not establish display/input latency.

## Optional builds

For allocation attribution without an external profiler:

```sh
cmake -S . -B build-quality-profile -G Ninja -DCMAKE_BUILD_TYPE=Release -DREALM_ALLOCATION_TRACKING=ON
cmake --build build-quality-profile --target realm_viewer realm_allocation_tracking_tests --parallel 4
ctest --test-dir build-quality-profile -R '^allocation_tracking$' --output-on-failure
```

For Tracy CPU zones as well:

```sh
cmake -S . -B build-quality-tracy -G Ninja -DCMAKE_BUILD_TYPE=Release -DREALM_TRACY=ON -DREALM_ALLOCATION_TRACKING=ON
cmake --build build-quality-tracy --target realm_viewer --parallel 4
```

Tracy is pinned to **0.11.1**, archive SHA-256
`2c11ca816f2b756be2730f86b0092920419f3dabc7a7173829ffd897d91888a1`.
Its [BSD-3-Clause license](https://github.com/wolfpld/tracy/blob/v0.11.1/LICENSE)
and [integration instructions](https://github.com/wolfpld/tracy/blob/v0.11.1/manual/tracy.tex)
are upstream. Enabled builds link its CPU client through realm_core; localhost,
on-demand mode, no discovery broadcast and no source transfer are configured.
Use a matching 0.11.1 profiler/capture tool. The dependency stays under the
build directory; no submodule, engine/backend migration or runtime art/UI change.
A version update needs a fresh hash, build, capture and overhead check. Runtime
and binary footprint evidence is recorded in the Q01 handoff. Native traces are
available without Tracy. A connected profiler adds overhead and its own buffers;
keep its capacity results separate from ordinary client results.

Allocation hooks are linked only into viewer/client and the optional allocation
test. They count successful main-thread C++ new/new[] requests, including aligned
and nothrow forms. Traffic includes external C++ calls if they resolve through
these executable symbols. Counters exclude direct malloc/realloc, worker threads,
allocations before frame collection, and export (collection is disabled during
export). They do not measure frees, live/peak heap bytes, allocator capacity or
process RSS. No tracking map, allocation header, lock or per-allocation timestamp
is added. Zero fields with the build flag off mean **unmeasured**, not zero traffic;
metadata records this distinction.

## Sample semantics and attribution

JSONL contains `metadata`, `frame`, `zone` and delayed `gpu` records. Schema is
`realm-frame-v1`. Frame IDs identify rendered frames; `start_us` and zone
`start_us` use one steady-clock epoch. Durations are milliseconds in frames/GPU,
microseconds in CPU zones, and bytes in byte counters. Booleans serialize as 0/1.
GPU records must join by `frame_id`, never by arrival order. The reporter validates
that join. Frame IDs match render order; minimized/zero-drawable iterations are
not exported and taint the next interval. They do not skip simulation work.

`cpu_ms` runs from loop entry through renderer submission. `present_ms` measures
only SDL swap. `paced_ms` is post-swap housekeeping/cap time before export.
`interval_ms` is the unclamped start-to-start interval, including cap and export
cost; simulation retains its existing 100ms clamp. Export CPU time is not a
separate zone; use interval versus CPU/present/paced and paired off/on runs to
observe its cost. Existing stdout CPU/present summaries exclude both finite and
scripted captures; capture/readback no longer inflates present time.

`warmup` uses `--telemetry-warmup` **frames**, default 30. `capture` marks any
finite/scripted readback frame. Such frames are retained for diagnostics, excluded
from normal summaries, and mark GPU samples non-normal. Their following interval
is also invalid. Capture allocations are consequently excluded from normal
counter/zone summaries. Other first-frame/cold-load results remain separate.

`input_to_submit_ms` is null without input, otherwise the earliest input event
in that frame to submission using SDL's monotonic millisecond event timestamp.
Invalid/zero timestamps fall back to observation-to-submission. Timestamp
precision is roughly 1ms; scripted input is a fixture, not physical device data.
This is not input-to-display or photon latency. Events received during a frame
are included without changing routing, focus/modal guards or gameplay timing.

## CPU zones, GPU zones and counters

Production CPU zones cover session polling; socket polling/framing/packet decode
and dispatch; world interpolation; combat update and target queries; terrain,
visibility, entity/painter sorting and scene build; appearance lookup/composition
and GPU cache lookup; native text rasterization; startup/UI build; model projection;
texture/quad uploads; model upload/submission; renderer submit; swap/presentation.
The main frame/UI envelope uses the native recorder; function/block zones also
feed Tracy. Offline Nexus legitimately has no session/world/combat zones.

GPU timestamps use **eight fixed slots**, each with five phase boundaries and
32 model start/end pairs. Query names are created only for telemetry. All slots
are polled with `GL_QUERY_RESULT_AVAILABLE` before reading results; the final
boundary follows all model queries. A busy slot is skipped, never waited on or
overwritten. No glFinish, blocking query read, or forced exit drain is added.
Metadata reports skipped slots and pending samples at exit. Missing samples must
remain missing; they are never copied from the last result.

GPU phases partition `total_ms`: upload/setup (includes clear and renderer state
setup), world (includes painter-ordered models), post/screen effects, UI. Model
timing is an overlapping subset of model draws. Since Q06 a model draw is one
instanced draw of consecutive copies of one model (no per-model upload); the
reference `MeshDraw` path is still one draw per prepared mesh. `models_measured`
counts measured draws (at most 32), `models_total` counts model instances, so with
more draws this is a partial sample, not a total-model estimate. CPU and GPU
overlap too. Never sum their totals or add models onto world/UI.

Per-frame counters: C++ allocation traffic; supplied texture/mesh/quad upload
bytes; newly allocated texture storage bytes; draw calls; ordinary quads;
submitted triangles (two per quad; for model instances every model triangle
before GPU back-face culling, about twice the visible faces; prepared reference
meshes count their kept triangles); submitted models (instances); CPU/GPU appearance hits/misses/evictions; dynamic-atlas uploads,
evictions and fallbacks to a per-image surface (`atlas_uploads`, `atlas_evictions`, `atlas_fallbacks`, `atlas_resizes`); legacy glyph-cache
hits/misses/evictions; actual socket rx/tx bytes; decoded packets; optional particle
drops. Model geometry is uploaded once when the renderer is created (about
124 KB for all 42 models), so per-frame `mesh_upload_bytes` is zero for instances;
their per-instance data travels in the quad upload. Mesh placeholders are not
counted as ordinary quads. Post-effect fullscreen
rectangles contribute draw calls but are not in quad/triangle counters. Texture
storage bytes count GL storage requests, not residency or supplied pixels.

Gauges (last snapshot; do not sum them): resident entities, camera-candidate
visible entities (including the existing generous margins), active combat
projectiles, particles, outgoing/framing queue bytes, CPU/GPU appearance bytes,
dynamic-atlas resident bytes, shelf bytes and page count (`atlas_resident_bytes`, `atlas_shelf_bytes`, `atlas_pages`), legacy glyph bytes. `visible_entities` is the production scene candidate list,
not strict on-screen pixel visibility or submitted players. The report prints
sum/max/last for all fields; sums of gauges have no memory/work meaning.

## Bounds and still-unmeasured metrics

The native recorder stores at most **8192 CPU zones per frame**, about 320KiB,
then increments `dropped_zones`. It streams completed frames and owns no growing
session sample vector. The query pool is at most 552 query objects. The overflow
and allocation-form tests exercise the bounds and attribution. Raw files grow on
disk for the requested run; failures to open/write/flush are reported. Existing
finite-run stdout vectors and caches retain their pre-existing behavior/budgets.

Still unmeasured or partial:

- Direct malloc/realloc, worker/background HTTP allocations/timing, live heap,
  per-frame RSS, driver/GPU residency, native font glyph cache internals and
  all HUD/art/terrain cache hits. Existing Q00 OS high-water and cache tests
  remain separate evidence. The new texture counters exclude initial atlas
  construction before collection and driver-private transfers.
- Strict visible/submitted/simulated counts by player/enemy/model/projectile,
  occlusion/pixel coverage/overdraw, separate model CPU upload time, individual
  post passes, model GPU totals above 32, and shader invocation costs.
- TCP events/event-queue depth and latency beyond outgoing/framing bytes; HTTP
  bytes/queues and audio. Socket gauges are sampled at the frame boundary.
- Physical input-to-display, VSync queue latency, export as its own CPU zone,
  idle/minimized frame distribution, presentation completion on the display,
  and synchronized CPU/GPU absolute clocks. CPU frame IDs are the cross-clock
  attribution, not a fabricated common timestamp.
- Pre-loop cold asset loading and thread scheduling/call stacks unless separately
  profiled with Tracy. Counter zeros for paths absent from Nexus do not verify
  those paths under load. Q02 exercises them before selecting optimizations.

Required checks are `telemetry`, `telemetry_gpu`, and `allocation_tracking` when
that build flag is enabled, plus the chain's Release/default sanitizer suites.
Graphics sanitizers require an explicit viewer build; the default preset excludes
it. Q01 ran the actual GPU/allocator/postfx checks with that explicit build.

## Reproduction evidence and workload cautions

`build/quality-Q01/nexus-report.md` and `nexus-trace.json` are the original-map
trace/readable summary. `nexus.tracy` is a successfully captured matching-version
Tracy trace (20,003 frame marks, 1,260,052 zones). Raw logs and command receipts
are retained. All use the original map; no graphics or gameplay capacity is claimed.

The five off/on repetitions in `build/quality-Q01/emitter-free-control/overhead.json`
use a **different diagnostic map**, made by replacing only White Fountain dictionary
records with registered Cube objects in a local copy. Original files are unchanged.
That control has 743 residents / 146 camera candidates, 5 submitted models, 1058
ordinary quads, zero particles, and stable uploads/triangles. Its generator and
exact commands are in `measure.py` / `definition.json` beside the data. This is a
static overhead control, not the Q00-v1 full-quality map or a qualified stress scene.
The earlier original-map and Particles-off runs have varying work counts and are
retained as exploratory evidence, not paired performance comparisons.

The Particles setting preserves existing emitters after map import, matching the
source behavior. A deterministic simulation timeline is needed to compare the
original Nexus fairly; fixed scene/effect rendering clocks alone do not suffice.
Q02 must cover this, and specify Spawn in generated JM maps: the importer chooses
the first walkable corner otherwise (the expanded 40-model test verifies this).

Static-control five-repeat median stdout CPU p95: predecessor 0.1664ms; ordinary
current build off 0.1658ms (ranges overlap); native export on 0.1828ms; allocation
build off/on 0.1698/0.1420ms; Tracy build disconnected off/export on 0.1833/0.1907ms.
The broad ranges are in the handoff/evidence. Export adds work; the allocation
build's lower p95 is noise, not a speedup. Median mean CPU off/native-on is
0.0921/0.0977ms (+6.1%); native-on p95 is +10.3%. Raw intervals include export
cost. Three connected Tracy captures of the same static control contain 189,052
zones each; mean CPU 0.0924–0.1032ms, p95 0.1352–0.1903ms, overlapping the
unconnected results. No sub-percent active-profiler overhead claim is justified.

Short independent cold runs measured 151500/150976/163812/151860KiB process
high-water for ordinary/allocation/Tracy-disconnected/allocation+export builds.
Tracy binary is 4608840 bytes versus 4106352 ordinary (about 491KiB extra).
`footprints.json` contains commands. The main comparison runner's high-water
values sometimes include its large Python fork image; do not use those to
attribute memory overhead. No per-frame residency or long soak is established.
