# Work packages and acceptance

The ordered statuses live in [client-quality-chain.md](../client-quality-chain.md).
Every package also follows that file's build/check/handoff/dispatch procedure.
Observed costs below are source inspection findings, not measured bottlenecks.
Inspect the current implementation before changing it.

## Q00 — Establish the actual client baseline

Inventory the dirty checkout, implemented features, assets, dependency versions,
build presets, current hardware/GL renderer, existing checks and performance
facilities. Verify the claims in the previous feature ledger against source.
Build/check Release and sanitizers, repair baseline build/test issues needed
for this chain, and run existing Nexus and extra-sprite checks with logs.

Create `docs/client-quality/baseline.md` with source/asset fingerprints,
hardware/settings, existing results and their limits, missing verification,
confirmed feature gaps, memory/resource budgets and primary/stretch targets.
Record prerequisites without modifying sibling repos or personal account files.
Acceptance: another chat can reproduce the baseline; failures are classified
and required baseline checks pass. Do not claim synthetic combat capacity yet.

## Q01 — Add useful performance telemetry

Extend `main.cpp`/renderer diagnostics into raw machine-readable frame samples,
subsystem timings and counters described in the benchmark contract. Integrate
Tracy or an equivalently useful profiler through an optional pinned dependency.
Profile polling/codecs, world/combat, visibility, sort, appearances, text/UI,
model preparation, GPU upload/submit and presentation. Keep profiling optional
and measure enabled/disabled overhead. GPU queries must not stall the frame.

Acceptance: repeatable exports, correct units/sample attribution, no capture
timings mixed into normal samples, and a readable Nexus trace that identifies
where time and allocations go. Document every still-unmeasured metric.

## Q02 — Build representative client stress fixtures

Implement the contract's versioned deterministic workloads with production
world/combat/scene/UI/session paths. Reuse the existing harness where practical;
introduce clear interfaces instead of embedding a second game in the harness.
Add client fixture peers for protocol bursts and screen responses. Implement a
runner with manifests, warm-up/repeats, raw export, paired comparisons, captures,
and actual population/activity counts. Cover S1–S4 without treating S4 as an
initial FPS gate. Use controlled assets/fixtures, never saved user credentials.

Acceptance: commands exist and execute; same seed/timeline yields equivalent
state, hits and packet summaries independent of rendering speed. Every required
workload is exercised and profiled. Publish measured scaling curves and rank
current costs. Use these results to refine the subsequent changes' hypotheses;
do not remove correctness, organization, UI, or capacity obligations.

## Q03 — Scale projectile collision searches

Inspect `Combat::update`, which currently searches the entity collection for
enemy-targeting projectiles. Build a spatial grid suitable for the tile world
and query eligible candidates. Maintain it correctly across movement, add/remove,
map changes and client prediction. Reuse queries where appropriate without
storing pointers across dense-container mutations.

Acceptance: differential checks preserve target eligibility, full-scan tie
behavior, multi-hit history, collision and hit packet outcomes. Profile candidate
counts/costs for spread/dense/boundary cases. Preserve existing hit semantics;
swept collision is not automatically authorized as an optimization.

## Q04 — Scale visibility and world queries

Use spatial queries for camera and nearby-interaction selection where measured
useful. Inspect world update work and avoid repeated processing of genuinely
static data. Use conservative bounds for tall/large/rotated sprites/models,
shadows and effects. Evaluate redundant sorts/camera calculations from traces.

Acceptance: equivalent visible/order/interaction results, including equal-depth
ties and camera rotation/resize. Distant population costs scale with relevant
cells where practical. Offscreen authoritative state, projectiles and protocol
acknowledgments remain correct. Document unavoidable concentrated-cell costs.

## Q05 — Improve hot data and resource lifetimes

Inspect `World`, entity/projectile storage, temporary collections, shared
ownership, and CPU/GPU cache lifetime. Separate hot fields from cold data only
when measured useful. Reserve/reuse buffers and introduce stable handles where
needed. Keep generation/map reset rules explicit. Validate limits instead of
adding unbounded pools or arbitrary huge reserves.

Acceptance: allocation and memory evidence at S2/S3; removals and reallocations
cannot leave stale references. Churn and repeated teardown remain bounded;
sanitizers pass. Explain changed memory layout and per-object cost. A full ECS
replacement requires stronger evidence than fashionable architecture.

## Q06 — Scale real 3D model rendering

Inspect `project_model` and renderer mesh submission. Reuse persistent model
geometry; move suitable transforms/projection to shaders and evaluate instancing
or shared uploads. Preserve original fan/material groups, lighting, UV behavior,
backface rules and painter-order interleaving. Batch only compatible ordered
work; do not reorder transparent objects solely to reduce draws.

Acceptance: existing model/math/reference checks plus mixed overlap/rotation
captures pass. MODELS and MIXED show changed allocation/upload/draw costs and
capacity. Evaluate diverse models as well as repeated copies. No automatic
switch to a depth-buffered world that changes Flash overlap semantics.

## Q07 — Improve texture residency and batching

Profile appearance/name/icon working sets and draw fragmentation. Evaluate
shared atlases/arrays, content keys, incremental uploads, prewarming, packing,
and eviction with current-frame protection. Measure misses and re-entry hitches,
not only allocated bytes. Preserve masks, dyes, shadows, alpha and pixel filtering.

Acceptance: varied legal appearances at S3, above-budget churn, correct eviction
and no blank/reused-wrong textures. Publish CPU/GPU budgets and upload/draw
results. Atlas changes must preserve ordered translucency, font sharpness,
resource lifetime and all existing appearance consumers.

## Q08 — Enforce codebase boundaries

Refine CMake/public interfaces into platform, networking, game, assets, rendering,
UI and application responsibilities. Replace the broad `realm_assets` grouping
where useful, extract application coordination from `main.cpp`, remove accidental
GL dependencies from pure game/text/layout interfaces, and document ownership
and dependency direction. Share application code between viewer/client targets.

Acceptance: headless game/protocol checks stay headless; production and harness
use the same game systems; dependency targets enforce the boundaries. Build
and behavioral checks pass and MIXED costs stay within measured noise. Favor
concrete interfaces; do not add generic abstractions without a real consumer.

## Q09 — Select the UI and vector approach

Compare the existing retained bitmap approach and a focused proprietary layer
against a library pilot. Candidate primary sources: [RmlUi](https://github.com/mikke89/RmlUi),
[ThorVG](https://github.com/thorvg/thorvg), [Dear ImGui](https://github.com/ocornut/imgui),
and [Tracy](https://github.com/wolfpld/tracy). Check current official documentation
and pin selected versions. ImGui is a developer-tools candidate; a vector engine
does not provide layout/focus/editing by itself. Keep existing FreeType/font work
unless the pilot demonstrates a justified replacement or shaping addition.

Implement a real tooltip, inventory drag interaction, modal text field, scrolling
panel and animated title text at 1x/HiDPI. Compare visual resemblance, input,
text/IME, clipping, integration, allocations, CPU/GPU costs, binary footprint,
licenses and maintenance. Do not compare an empty demo with the production HUD.

Acceptance: `docs/client-quality/ui-decision.md` records measurements and the
chosen scope. The winning pilot is usable in the client and the discarded
experiment is isolated/removed without discarding others' work. Own a narrow
client UI interface even when it wraps external rendering/layout components.

## Q10 — Implement the shared UI foundation

Establish logical/drawable coordinates, anchors/layout, inverse hit transforms,
clip stack, layers, focus/pointer capture, modal input, scroll, text editing,
common widgets/style tokens, text measurement/rasterization and animation.
Use retained invalidation/dirty regions for infrequent content; isolate changing
values. Support safe teardown/reconnect and explicit resource ownership.

Acceptance: representative pilot is integrated through this foundation;
input/resize/focus/clipping/lifetime checks pass. Unchanged static panels incur
no unnecessary full rerasterization/upload, with counters proving behavior.
Choose caching/glyph/path strategies from costs and fidelity, not assumptions
that SDF text or GPU paths always look or perform better.

## Q11 — Apply the UI foundation to startup

Migrate loading, title, account/sign-in/registration, character selection/create
and skin selection incrementally. Preserve error/retry/cancel routes and data
flow. Keep the Play pulse sharp and smooth at physical resolution. Validate
through actual SDL input and controlled client HTTP fixtures.

Acceptance: source/native captures and interaction sequences at contract sizes;
all visible states, input transforms and screen lifetime checks pass. UI traces
show update/rasterization/upload costs and correct animated/static separation.

## Q12 — Apply it to the gameplay HUD

Migrate HUD meters/stats/inventory/backpack/potions, minimap, chat/history,
tooltips, drag feedback and shared world text where appropriate. Preserve
authoritative updates, item comparisons, order, scroll, hover, and no accidental
shooting through controls. Measure names/HP bars/chat during dense combat.

Acceptance: existing HUD/font/interaction checks and resized captures pass;
small changes update affected regions without rebuilding unrelated content.
MIXED includes active UI interactions and readable combat cues at high counts.
Keep optional label/effect reductions explicit and separate from full fidelity.

## Q13 — Apply it to menus and overlays

Migrate options, dialogs, trade, guild board/members, skin/player/group menus,
party arrows and related overlays. Use shared focus, stacking, modal blocking,
styles and scrolling rather than per-screen rules. Preserve dynamic permission,
cooldown, disabled, loading, error, confirmation and server-feedback states.

Acceptance: real native input/resize sequences plus source/native captures,
mock response/error flows and map/reconnect teardown. Recheck keybindings,
persistence and focus restoration. No inconsistent scale or clipped content.

## Q14 — Close the death and fame flow

Audit current implementation against the real source. Implement any remaining
packet-driven death/fame screens and displayed data, dismissal and return route
using the UI foundation. Reuse existing behavior if complete.

Acceptance: deterministic death packet fixture, correct values/order/state,
resize/input captures, and a working return to character selection with no
stale world/UI state. Client evidence must not claim server-side death changes.

## Q15 — Close the legends flow

Inspect the actual application endpoint and response contract from read-only
sources. Complete client requests, data interpretation, filters/sorting/scroll,
loading/empty/error/retry and routes using fixture responses.

Acceptance: source-compatible screen and client adapter, response edge cases,
captures and actual input checks. Report backend availability separately; do not
invent endpoint behavior or change the server to fit a client assumption.

## Q16 — Close character deletion

Implement/verify confirmation, cancellation, correct selected-character identity,
request/result/error flow and refreshed account/list state. Test with disposable
fixture characters and local HTTP peers, not the user's characters or account.

Acceptance: canceled actions issue no delete request; failed responses retain
the character; success refreshes the correct state; stale async responses do
not corrupt a new screen/session. Visual/input checks pass at contract sizes.

## Q17 — Complete native audio integration

Inventory source sound/music and event rules; use a suitable native audio
dependency after checking its official docs/license. Integrate existing volume,
mute and sound/music controls. Bound voices, decoding buffers and lifetimes;
handle focus, map/screen transitions, cancellation and missing assets.

Acceptance: controlled event/playback checks and listening verification,
settings persistence, resource teardown and burst behavior. Profile enabled
audio in MIXED; report what was actually heard versus only queued in a fixture.
Do not let audio work or disk decoding block interactive frame work.

## Q18 — Evaluate and implement justified concurrency

Use the current traces to identify independent costly jobs: examples include
asset preparation, independent batches, or background decoding. Keep GL work
on the owning context unless a verified explicit design says otherwise. Use
immutable job input/stable handles and generation-aware canceled results.
Compare single-thread and job variants on the same workloads.

Acceptance: measured end-to-end benefit and bounded queues with deterministic
outcomes, teardown/race checks and appropriate ThreadSanitizer verification
where supported. Record unavailable race verification. If traces do not justify
parallel work, complete with a measured decision and thresholds for revisiting;
adding threads is not itself success. Preserve simulation/protocol ordering.

## Q19 — Qualify the complete client

Run the full contract on the frozen hardware profile, including visible paced
play, uncapped capacity, audio, UI, MIXED S3, pathological/overload cases, cold
loading and a 30-minute churn/map-transition soak. Repair material feature,
correctness, visual, performance or memory gaps discovered. Produce a documented
regression runner with stable fixtures and meaningful checks.

Acceptance: `docs/client-quality/qualification.md` contains runnable commands,
raw evidence links, capacity by population/visibility/resolution/quality,
frame-time distributions, cache/memory behavior, visual and interaction review,
and residual limitations. Meet the frozen primary target at required capacity;
do not certify a target miss as complete. Do not claim "perfect" or support on
unmeasured hardware. Update public build/run/architecture docs and unresolved
backlogs honestly. This is the final chat; create no successor.
