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
| Q07 | in progress | Improve texture residency and batching | Varied appearances/names/icons fit explicit budgets without avoidable churn or broken overlap. |
| Q08 | pending | Enforce codebase boundaries | Platform/network/game/assets/render/UI/application targets with explicit ownership and a smaller application coordinator. |
| Q09 | pending | Select the UI and vector approach | Representative comparison, documented decision, usable selected pilot; no speculative wholesale replacement. |
| Q10 | pending | Implement the shared UI foundation | Layout, transforms, input/focus, clipping, text, styling, invalidation and resource lifetime rules. |
| Q11 | pending | Apply the UI foundation to startup | Loading/title/account/character flows, sharp animation/resizing, deterministic UI checks. |
| Q12 | pending | Apply it to the gameplay HUD | Inventory/stats/minimap/chat/tooltips/world labels with small updates, correct input and dense-scene costs. |
| Q13 | pending | Apply it to menus and overlays | Options/dialogs/trade/guild/skin/player menus with consistent states, focus, clipping, scaling and teardown. |
| Q14 | pending | Close the death and fame flow | Source-equivalent client screens, packet-driven state, values, and return routing. |
| Q15 | pending | Close the legends flow | Client endpoint adapter, sorting/filtering, scrolling, loading/empty/error states. |
| Q16 | pending | Close character deletion | Source-equivalent confirmation, cancellation, endpoint handling and refreshed client state using disposable mocks. |
| Q17 | pending | Complete native audio integration | Source inventory, bounded playback/resources, sound/music settings, event and focus/map routing. |
| Q18 | pending | Evaluate and implement justified concurrency | Parallelize demonstrated independent expensive work, or document why current workloads do not benefit. |
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
