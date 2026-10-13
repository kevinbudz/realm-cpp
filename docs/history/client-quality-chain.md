# Native client quality: one task per chat

Created 2026-10-11. This is the execution ledger for the user's request to improve
the C++ client's performance, organization, and visual fidelity, including
thousands of players, enemies, 3D objects, and projectiles.

The user explicitly requested: "make a list of things needed to accomplish said
changes, and spin up new chats upon each task being completed." Each chat does
one task, verifies it, writes a handoff, and creates exactly one successor.
The workflow setup chat launches Q00. There is no scheduled automation.

## Scope and working rules

- Work in `/home/kevin/Desktop/RotMG/realm-cpp`, in the existing local checkout.
  `/home/kevin/Desktop/RotMG` is the project directory, not a Git repository.
- Improve the native client only. Web/WebGPU ports and server development are
  outside this chain. Existing sibling sources and map assets may be read;
  do not modify them, provision a server, or tune server capacity.
- Read `AGENTS.md`, `docs/port-spec.md`, `docs/progress.md`,
  `docs/client-quality-chain.md`, `docs/client-quality/benchmarks.md`,
  `docs/client-quality/work-packages.md`, and the previous quality handoff.
  Use `docs/flash-specs.md` and real source behavior where relevant.
- Preserve all existing modified and untracked files. Record a starting
  inventory; do not reset, discard, bulk-commit, or stash someone else's work.
- Original art, layout, colors, fonts, states, layering, and familiar appearance
  remain the visual reference. Match Flash at a glance; inspect actual captures.
  Resized/animated text must render at drawable resolution. Original pixel art
  remains pixel art. Do not chase per-pixel Flash rasterization differences.
- Preserve gameplay semantics, authoritative state, protocol behavior,
  collision rules, teleport restrictions, input guards, and stable resource
  lifetimes. Camera visibility is never permission to discard gameplay work.
- Profile before choosing optimizations. Keep changes small enough to compare
  against the previous implementation. Record regressions as well as gains.
- Libraries may be evaluated and integrated within the authorized client work.
  Record version, license, footprint, visual fit, and maintenance implications.
  A proprietary UI layer may wrap mature libraries; building a general engine
  or vector rasterizer from scratch needs a demonstrated client requirement.
- Keep the existing renderer/backend until measured needs justify a change.
  Do not make an API migration a prerequisite for this chain.
- Use client fixtures, deterministic replays, and local mock TCP/HTTP peers for
  protocol checks. Do not label mock evidence as live-server evidence.
- Subagents: follow the user's global delegation rules for the tool in use:
  `~/.claude/CLAUDE.md` in Claude Code, `~/.codex/AGENTS.md` in ChatGPT/Codex
  (GPT-6 Luna roles). Successor chats are
  user-owned chats, not subagents; omit model and reasoning overrides and keep
  their defaults.
- `docs/client-task-chain.md` remains historical feature work and acceptance
  context. This chain owns the dispatch of the tasks below. Do not also dispatch
  its old successors or create two chains that edit the same files.
- Do not message other chats, archive them, or create recurring automations.

## Ordered task ledger

Statuses: `pending`, `in progress`, `complete`, or `blocked`. Only one task may
be in progress. A dispatch receipt alone does not mean implementation is complete.
Q00 is the first task. Task details and acceptance checks are in
[work-packages.md](client-quality/work-packages.md).

| ID | Status | Task | Required result |
| --- | --- | --- | --- |
| Q00 | complete | Establish the actual client baseline | [Baseline](client-quality/baseline.md), [evidence](client-quality/evidence/Q00-results.json), [handoff](client-quality/handoffs/Q00.md); 42/42 Release, 38/38 ASan/UBSan; Q00-v1 frozen, S3/144 Hz unqualified. |
| Q01 | complete | Add useful performance telemetry | [Telemetry](client-quality/telemetry.md), [evidence](client-quality/evidence/Q01-results.json), [handoff](client-quality/handoffs/Q01.md); raw/Chrome/optional pinned Tracy, bounded asynchronous GPU attribution; 44/44 Release, 39/39 ASan/UBSan, 4/4 graphics sanitizers. |
| Q02 | complete | Build representative client stress fixtures | [Stress fixtures](client-quality/stress.md), [evidence](client-quality/evidence/Q02-results.json), [handoff](client-quality/handoffs/Q02.md); 59 deterministic scenarios (S1–S4, Nexus, models, variety, churn, UI, mixed) through production World/Combat/Scene/Session; Release 50/50, ASan/UBSan 44/44, graphics sanitizers 10/10; dominant cost `combat.target_query`; S3/144 Hz unqualified. Successor Q03 not yet dispatched (no `create_thread` in that session). |
| Q03 | complete | Scale projectile collision searches | [Handoff](client-quality/handoffs/Q03.md), [evidence](client-quality/evidence/Q03-results.json); per-update uniform grid, differential oracle `TargetSearch::scan`, identical checksums/hits/ties; `combat.update` 1.2 s -> 1.7 ms (S3 combat cluster), 3.3 s -> 2.9 ms (S4 projectiles); Release 51/51, ASan/UBSan 45/45; whole-client S3/144 Hz still unqualified (`scene.build`). Successor Q04 not yet dispatched (no `create_thread`). |
| Q04 | complete | Scale visibility and world queries | [Handoff](client-quality/handoffs/Q04.md), [evidence](client-quality/evidence/Q04-results.json); profile showed camera/ordering work is 3–5 ms of 140–800 ms S3 frames (name/appearance = Q07, minimap redraw = Q12), so no grid: per-visible-entity lookup/depth reuse, merged painter stream, `interactive_target` id list, one-pass `world_dyes` (S3 query cost 3.5–4.4 -> 1.5–1.7 ms, S4 4.7 -> 2.1 ms), and model-extent culling fix (models vanished up to 68 px early); differential `visibility_culling` + `world_queries`, 57/59 paired painter streams identical; Release 53/53, ASan/UBSan 46/46, graphics 19/19; S3/144 Hz still unqualified. Successor Q05 not yet dispatched (no `create_thread`). |
| Q05 | complete | Improve hot data and resource lifetimes | [Handoff](client-quality/handoffs/Q05.md), [layout/lifetimes](client-quality/storage.md), [evidence](client-quality/evidence/Q05-results.json); profile found `Particles::remove_target` = 79 % of S3 mixed-dense instructions (every projectile removal scanned 8,192 particles): conservative target filters, flat `EntityIndex`, `StatMap`, `HitSet`, O(1) bounded logs, capacity release at map reset; headless S3 mixed-dense 49.7 -> 3.0 ms per tick with identical outcomes in 12/12 scenarios, S3 live heap 24.6 -> 17.7 MB, populate 9,000 objects 156k -> 33k allocations; 505,894-check `world_storage`; Release 54/54, ASan/UBSan 47/47, graphics 20/20; S3/144 Hz still unqualified. Follow-up in the same session (user-authorized) cut S3 mixed-spread p50 169 -> 19.5 ms and mixed-dense 792 -> 29.8 ms via cache budgets/keys, `ui_rectangle`, `compose` (see handoff "S3 follow-up"); p95 is first-seen composition hitches. Successor Q06 not yet dispatched (no `create_thread`). |
| Q06 | complete | Scale real 3D model rendering | [Handoff](client-quality/handoffs/Q06.md), [evidence](client-quality/evidence/Q06-results.json); models are GPU instances of persistent geometry carried in the painter-ordered quad stream (one instanced draw per run of the same model, GPU transform/shade/back-face culling, `project_model` kept as CPU reference): S3 dense models p50 6.18 -> 1.37 ms, GPU 3.29 -> 0.93 ms, 27,822 -> 876 allocations/frame, S4 12.1 -> 2.5 ms, 1,000 repeated models 1,005 -> 7 draws; differential `model_instances` (1.33 M checks, 7/7 mutations caught), 110 Flash reference frames within 3 px (109/110 byte-identical to pre-Q06), 21/21 paired marker digests identical; Release 57/57, ASan/UBSan 49/49, graphics 23/23; S3/144 Hz still unqualified (Q07). Successor Q07 not yet dispatched (no `create_thread`). |
| Q07 | complete | Improve texture residency and batching | [Handoff](client-quality/handoffs/Q07.md), [evidence](client-quality/evidence/Q07-results.json); composed appearances (content-keyed), names, condition icons and status text live in dynamic layers of the sprite atlas array (headless shelf/LRU `TextureResidency`, 2-frame protection, growth 2 -> 12 pages on demand, shrink at map change, per-image fallback), so they batch with sprites: draws S3 combat 15,080 -> 4, S3 mixed-dense 18,170 -> 3,169, S2 players 3,005 -> 3; CPU p50 S3 combat 23.0 -> 11.7 ms, mixed-dense 24.5 -> 13.9 (p95 233 -> 140), mixed-spread 18.4 -> 13.4; GPU S3 mixed-dense 12.9 -> 5.9 ms; worst-case bitmap GPU 256 MiB. `dynamic_atlas` (906,018 checks: byte-exact readback of every resident slot incl. churn/growth/both copy paths, 0 unexplained pixels) and `texture_residency` (485 M checks); Release 59/59, ASan/UBSan 50/50, graphics 27/27; 1080p captures differ only by nearest-sampling ties (not re-run after growth); S3/144 Hz still unqualified (first-seen composition). Successor Q08 not yet dispatched (no `create_thread`). |
| Q08 | complete | Enforce codebase boundaries | [Architecture](client-quality/architecture.md), [handoff](client-quality/handoffs/Q08.md), [evidence](client-quality/evidence/Q08-results.json); 11 graphics / 7 headless libraries, 66 owned headers, configure/include boundaries, shared compiled viewer/client coordinator. All 30 paired seeded captures and work signatures match Q07; longer S2 p50 39.16 → 38.83 ms / p95 193.01 → 189.42; broad software-renderer tail variability, S3/144 Hz unqualified. Release 59/60 (same original Q07 atlas failure), headless ASan/UBSan 51/51 (known world_dyes overflow diagnostic), graphics 4/4 with precisely scoped baseline GLX leaks. Q09 request prepared, undispatched: no create_thread on this cloud host. |
| Q09 | complete | Select the UI and vector approach | [Decision](client-quality/ui-decision.md), [handoff](client-quality/handoffs/Q09.md), [evidence](client-quality/evidence/Q09-results.json); one pilot screen (tooltip, inventory drag, modal UTF-8/IME text field, scrolling panel, pulsing title) behind a narrow `realm::ui::Pilot` interface, implemented natively (production ItemTooltip/Scrollbar/HUD art/pulse) and with pinned RmlUi 6.3 (`REALM_RMLUI`, default OFF; FontBook font engine, nearest HudArt, custom element for animated text); identical final states at 1x/2x/1.8x. Native region re-rasterization spikes (2x scroll p95 38.0 ms, typing 14.6 ms) vs RmlUi ≤0.43 ms p95; steady p50 0.21 vs 0.28 ms; draws 4–6 vs 45–57; +4.05 MB binary. Selected RmlUi for Q10 layout/input/focus/scroll/editing with FontBook text; dense HUD/world stays on quads; ThorVG/ImGui not adopted; RCSS transform text rejected (soft). Release 61/61, RmlUi build 62/62, ASan/UBSan 51/51 (known world_dyes overflow), graphics sanitizers with RmlUi pass without suppressions. Successor Q10 not yet dispatched (no `create_thread`). |
| Q10 | complete | Implement the shared UI foundation | [Foundation](client-quality/ui-foundation.md), [handoff](client-quality/handoffs/Q10.md), [evidence](client-quality/evidence/Q10-results.json); headless `ui::Stack`/`Viewport` input owner (capture, modal cancel-below, focus, hover hand-off, resets, world gates) with every coordinator screen as a layer; `ui::RmlHost` (FontBook text, nearest HudArt, `paint()` Flash painters, `<pulse-text>`, embedded `ui/`, retained caches with counters, explicit lifetime) with the pilot ported onto it; `Renderer::draw_overlay`; RmlUi 6.3 vendored and default ON. Static frames: 0 rasterizations/uploads; one label change: 1. Release 64/64, ASan/UBSan 52/52 (known world_dyes overflow), graphics sanitizers clean without suppressions, 10 mock-peer GPU scripts pass. User narrowed a Q10–Q13 bundle to Q10. Successor Q11 not yet dispatched (no `create_thread`). |
| Q11 | complete | Apply the UI foundation to startup | [Bundle handoff](client-quality/handoffs/Q11-Q13.md), [evidence](client-quality/evidence/Q11-Q13-results.json): Rml startup snapshot/actions/text/editing, sharp Play, ordered skins/dialogs, native comparison, SDL/HTTP retry/validation/registration/CREATE checks and 4K resize. |
| Q12 | complete | Apply it to the gameplay HUD | Same bundle: retained Rml chat/tooltips/drag; dense HUD/minimap/world text stays native by Q09; regional invalidation, 30-frame static checks, authoritative mock peers and paired S1 MIXED measurements. |
| Q13 | complete | Apply it to menus and overlays | Same bundle: retained Rml menu/overlay composition through shared Stack input/modal/reset rules; established painters/controllers retained. Options/persistence, guild/player/trade/skin states and CREATE/RESKIN/firing gates pass. Release 66/66, headless sanitizers 52/52, graphics host/retained 2/2 and production bundle pass. |
| Q14 | complete | Close the death and fame flow | [Handoff](client-quality/handoffs/Q14.md), [evidence](client-quality/evidence/Q14-results.json); terminal DEATH/EOF handling, source fame values/ordered statistics/bonuses, cropped dyed portrait, retained native/Rml animation/scroll/hover, 2 s snapshot fade, bounded retries and stale-response-safe character refresh/re-entry. Actual SDL/TCP/HTTP and Flash comparisons through 4K; Release 69/69, headless sanitizers 53/53 (known unchanged world-dye overflow), graphics mocks clean with leak detection. |
| Q15 | complete | Close the legends flow | [Handoff](client-quality/handoffs/Q15.md), [evidence](client-quality/evidence/Q15-results.json): source endpoint/order/tabs/cache/scroll/fame routes, bounded retries/generations, SDL/4K/Flash captures; Release 71/71, headless 54/54 (known dye overflow), graphics leak checks pass. |
| Q16 | complete | Close character deletion | [Handoff](client-quality/handoffs/Q16.md), [evidence](client-quality/evidence/Q16-results.json): Flash confirmation/cancel, snapshot character identity, `/char/delete` with Flash's two retries on `<Error>`, Flash error text, ambiguous replies keep the row and refresh, success refreshes `/char/list`, stale replies inert (mutation-checked), RmlUi X/dialog fixed; 13 mock routes × 2 backends through 4K; Release 73/73, headless sanitizers 54/54 (known dye overflow), graphics leak checks 26/26. Moved by the user from Codex chat `01a13338-…` to Claude Code; successor Q17 not created (no `create_thread`). |
| Q17 | complete | Complete native audio integration | [Handoff](client-quality/handoffs/Q17.md), [evidence](client-quality/evidence/Q17-mixed-audio.json): Flash SFX-only inventory (no music), vendored miniaudio 0.11.25, loader thread + 48 MiB cache, 32-voice mixer, Flash gate/5-tick dedupe, all Flash cue sites, map cancel/session stop, ability latch; enabled-audio MIXED cost within noise; Release 75/75, headless sanitizers 56/56; not verified by ear. Successor Q18 not created (no `create_thread`). |
| Q18 | complete | Evaluate and implement justified concurrency | [Handoff](client-quality/handoffs/Q18.md), [evidence](client-quality/evidence/Q18-results.json): S3 cloud llvmpipe appearance compose p95 175.18 ms justified a bounded immutable-input job probe, but 18 same-binary runs yielded only 0.30% S3 end-to-end p95 gain with S3 p50 and S1 regressions; no new concurrency retained, source binary restored byte-identically. All nine paired state/packet/quad checksums matched. Release 74/75 (exact inherited Q07/Q08 software-GL atlas pixel mismatch), headless ASan/UBSan 56/56 with two inherited world-dye overflows in full output; TSan toolchain probe passed, no delivered worker race claim. Frozen Q00 Radeon target remains unqualified. Q19 remains pending and undispatched at the user's direction; [future brief](client-quality/handoffs/Q19-dispatch-request.md) retained. |
| Q19 | pending | Qualify the complete client | Published capacity envelope, visual and interaction checks, regression runner, soak results, repaired material gaps. |

Q14–Q17 are gaps recorded by the previous completion ledger. Q00 must check the
actual source; if any are already implemented, their task verifies and repairs
the current behavior rather than implementing a duplicate. They require no
server changes. Keep the corresponding old ledger accurate without using its
dispatch procedure.

## The loop for every task

1. Read the ledger and handoffs. Confirm the preceding task is complete and no
   other quality task is active. Record the starting state and claim only your
   assigned row. If another chat is actively editing the same files, record
   that conflict and resolve ownership before writing; do not undo its work.
2. Establish the task's observable problem, workload, baseline, and acceptance
   checks. Check current source, rather than treating this plan as a bug report.
   For an already satisfactory implementation, verify it and document why no
   code change is necessary. Do not force complexity into a passing path.
3. Implement and verify in bounded slices. Compare identical workloads and
   quality settings. A graphics microbenchmark is component evidence; use the
   mixed fixture before claiming a whole-client improvement.
4. Run relevant correctness and visual checks, then Release and sanitizer
   build/CTest checks before closing the task. Use real drawable-resolution
   captures for visual changes. The default sanitizer preset excludes the
   viewer; additional graphics sanitizer verification must use an explicitly
   configured graphics build, not an assumption about that preset.
5. Classify failures. Repair regressions introduced by the task. Record
   pre-existing unrelated failures with reproduction and baseline evidence;
   do not certify the whole client as passing while they remain. A missing
   required acceptance check or material unresolved task defect prevents
   completion and successor dispatch. Do not disable checks to manufacture a pass.
6. Save `docs/client-quality/handoffs/QNN.md` using the handoff template. Include
   exact commands/results, changed paths, captures, measurements, limitations,
   and the next task. Update this ledger and `docs/progress.md`. Mark `complete`
   only when the assigned checks pass with supporting evidence.
7. For Q00–Q18, create exactly one successor as described below, then finish this
   chat. The successor owns further work. Q19 creates no successor and reports
   the qualified result. If Q19 exposes a material gap, repair it in Q19 before
   claiming completion; an unfinished or blocked task is never a successful end.

## Reliable successor creation

User authorization for automatic successor chats comes from the quoted direct
request at the top, made in setup chat `01a12c5f-9be1-7a71-95b9-502ff0bf94d2`.
No repeated confirmation is needed for routine work or a successful handoff.

1. Before dispatch, check `docs/client-quality/handoffs/QNN-dispatch.json` for
   the next task. If a receipt exists, inspect that chat's status rather than
   creating a duplicate. If a prior creation outcome is ambiguous, inspect
   `list_threads` for the exact task title before retrying.
2. Write `QNN-dispatch-request.md` containing the exact successor prompt and
   intended title before calling the creation tool.
3. Call `list_projects`. Select the local RotMG project at
   `/home/kevin/Desktop/RotMG`. Prefer project ID
   `4e459a47-d36d-4862-9ff2-53c2d40e669c` if still available; resolve from current
   results if it changed. Use `create_thread` with environment type `local`.
   Do not request a worktree: the successor must inherit these uncommitted files
   and generated assets. Omit model/thinking overrides.
4. Title: `RotMG client QNN: TASK_TITLE`. Use the reusable prompt below,
   substituting next ID/title and the preceding handoff path.
5. Immediately save the successful creation receipt with `task_id`, `title`,
   `project_id`, `threadId` or `clientThreadId`, `hostId` if returned, and the
   creation time. Preserve a queued receipt; never treat its `clientThreadId`
   as a ready `threadId`. Record tool errors honestly and retain completed work.
6. Creation is asynchronous. For a ready `threadId`, call `wait_threads` once
   with `timeoutMs: 0`, retaining its cursor and status in the receipt. This
   confirms dispatch/progress, not task completion. Do not wait indefinitely
   for the successor or implement its task here.
7. Include `::created-thread{threadId="..."}` (or queued `clientThreadId`) on
   its own line in the final response. Keep predecessor chats available.

If blocked, write the blocker and exact missing input in the handoff, retain
unfinished status, and ask in the current chat. Continue independent authorized
work when possible. Do not skip to an easier successor. A dispatch failure
leaves the implementation complete but the handoff undispatched; retry safely
after checking for duplicate creation.

## Reusable successor prompt

> Implement task TASK_ID (TASK_TITLE) of the user's native C++ RotMG client
> quality chain in `/home/kevin/Desktop/RotMG/realm-cpp` now. Read `AGENTS.md`,
> `docs/port-spec.md`, `docs/progress.md`, `docs/client-quality-chain.md`,
> `docs/client-quality/benchmarks.md`, `docs/client-quality/work-packages.md`,
> and PREVIOUS_HANDOFF. This is client-only work: no web/WebGPU port or server
> changes. Preserve the existing modified/untracked work and use this local
> checkout. Keep Flash appearance at a glance, drawable-resolution text,
> original pixel art, correct gameplay/protocol behavior, and bounded memory.
> Establish evidence, implement the assigned task, and complete its acceptance
> checks; do not just restate the plan. The human user explicitly authorized
> a new chat upon each completed task in setup chat
> `01a12c5f-9be1-7a71-95b9-502ff0bf94d2`. After successful verification, write
> the quality handoff, update the ledger/progress, and create exactly one local
> successor for the next task using this same prompt and dispatch procedure.
> Keep successor model/thinking defaults. Do not also dispatch the old feature
> chain. Use subagents per the user's global rules (`~/.claude/CLAUDE.md` in
> Claude Code, `~/.codex/AGENTS.md` in ChatGPT/Codex). Do not message other
> chats or schedule automations. Persist until this
> task is complete or requires concrete user input. Q19 creates no successor.
