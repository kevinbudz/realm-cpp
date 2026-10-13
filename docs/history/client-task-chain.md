# C++ client completion: one task per chat

Execution update, 2026-10-11: the user requested a broader client-only quality
chain. Use [client-quality-chain.md](client-quality-chain.md) for new work and
successor dispatch. The ledger and evidence below remain feature history; do
not dispatch this older sequence alongside the quality chain. Its confirmed
remaining features are represented in Q14–Q17 of the new chain.

Created 2026-10-10 at the user's request. The user explicitly wants a new
chat to start each time a task completes. This is a sequential chain of
local Codex chats, with no scheduled automation. Each chat implements and
verifies one task, records its evidence, and creates the next chat. A new
chat should actively do the work, not just restate this plan.

## Workspace and constraints

- Saved project: RotMG, project ID `4e459a47-d36d-4862-9ff2-53c2d40e669c`.
- Project directory: `/home/kevin/Desktop/RotMG`.
- C++ repository: `/home/kevin/Desktop/RotMG/realm-cpp`.
- Flash and server sources: sibling `realm-client` and `realm-server`.
- Use the existing local checkout so every successor inherits the actual
  changes and generated assets. The project directory itself is not a Git
  repository; the C++ subdirectory is.
- Read `AGENTS.md`, `docs/port-spec.md`, `docs/progress.md`, and
  `docs/flash-specs.md` before implementation. Verify claims against current
  code and the local Flash/server sources; old progress entries can lag.
- Preserve all existing uncommitted and untracked work. Do not reset,
  discard, or bulk-commit it. Keep Flash/server repositories unchanged.
- Match Flash at a glance: original layout, sizes, art, colors, text,
  interaction states and layering. Review source/native captures side by
  side. Small glyph-edge differences are accepted. Render resized text at
  drawable resolution, as required by the current port contract.
- Preserve server compatibility and client gameplay restrictions. The server
  does not enforce the map teleport flag or ten-second teleport cooldown;
  preserve the existing client checks.
- Runtime stays native. AIR is allowed for offline reference/art export.
  Preserve the existing art bundle and its backup when adding assets.
- Subagents: follow the user's global delegation rules for the tool in use:
  `~/.claude/CLAUDE.md` in Claude Code, `~/.codex/AGENTS.md` in ChatGPT/Codex
  (GPT-6 Luna roles). New user-owned
  successor chats are separate from subagents; keep their configured default
  model unless the user asks otherwise.

## Baseline

PlayerMenu, chat/history/input/speech balloons, trading, and chat-based
purchase feedback already exist. The user reported 20 passing release and
sanitizer test suites and repeated passing real-server tests. This is prior
evidence, not a claim that this setup chat reran them. Purchase result 1
now has its standard "Purchase Error" dialog (task 01; see its handoff). MoneyChanger has no panel
in this Flash fork and is out of scope.

Existing live tests isolate the server settings/database and use a second
registration address, `127.0.0.2`, because registration is limited per IP.
Continue using isolated fixture accounts; preserve the user's saved account.
An empty automated trade and a separately captured offered potion do not
establish a completed nonempty automated trade.

## Ordered backlog

Statuses are `pending`, `in progress`, `complete`, or `blocked`. Only one
task may be in progress. A task becomes complete after its acceptance checks,
not merely after code is written. Record evidence in
`docs/task-handoffs/<task-id>.md` and update this table before dispatching.

| ID | Status | Task and acceptance focus |
| --- | --- | --- |
| 01-dialogs | complete | Standard Flash Dialog/MessageCloseDialog, one/two-button states, wrapping, resize/input routing; integrate BUYRESULT result 1 Purchase Error popup. |
| 02-guild-board | complete | Reusable Flash scrollbar as needed, guild board read/edit/save, officer permissions, loading/error states and native HTTP integration. |
| 03-guild-members | complete | Guild chronicle/member list, fame/rank/slots, scrolling, promote/demote/remove confirmations, permission states and correct packet/chat feedback. |
| 04-minimap-menu | complete | Minimap player hover grouping/tooltips and PlayerGroupMenu; edge clamping, eligible teleport selection, map flag and cooldown behavior. |
| 05-party-arrows | complete | Edge-of-view party arrows, overlap merging, hover groups/menu, camera/resize behavior and preventing clicks from firing shots. |
| 06-skins | complete | Owned/class-filtered skin chooser and startup skin selection where Flash exposes it, default reset, RESKIN, rejection handling and live portrait updates. |
| 07-dyes-masks | complete | Player/object dyes and masks, live stat changes and all encountered appearance sizes; bounded lazy appearance caching with no missing-texture failures. |
| 08-particles | complete | Flash particle/effect event inventory, particles, blood/trails and visible ability/projectile effects; bounded lifetimes and map teardown. |
| 09-models | complete | Real 3D model import/rendering, textured geometry, placement, depth/occlusion and map/camera transitions against source scenes. |
| 10-lighting | complete | Environment/background lighting and source post effects, correct blending/order and source-equivalent quality behavior. |
| 11-status-effects | complete | Complete condition/status visual cues, names/status/damage text/icons, breath/boost bars and overlay ordering; authoritative state and readable combat cues. |
| 12-options | complete | Flash options screen, exposed keybindings/settings, persistence, focus/input behavior and integration with implemented systems. |
| 13-death | complete | Completed by quality Q14: [handoff](client-quality/handoffs/Q14.md), [evidence](client-quality/evidence/Q14-results.json). Packet-driven fame data, source screen, resize/input, guarded fade and refreshed character selection/re-entry. This historical ledger does not dispatch successors. |
| 14-legends | complete | Completed by quality Q15: [handoff](client-quality/handoffs/Q15.md), [evidence](client-quality/evidence/Q15-results.json). Actual endpoint/order/timespan filters, scroll/cache/fame routes, loading/empty/error/retry and SDL/Flash/4K checks. This historical ledger does not dispatch successors. |
| 15-character-delete | complete | Completed by quality Q16: [handoff](client-quality/handoffs/Q16.md), [evidence](client-quality/evidence/Q16-results.json). Flash confirmation/cancel, `/char/delete` retries/errors, refreshed list and stale-reply guards with disposable mocks. This historical ledger does not dispatch successors. |
| 16-audio | pending | Source sound/music inventory and native playback, gameplay/UI event routing, volume/mute settings, bounded resources and focus/map transitions. |
| 17-integration | pending | Audit every requested feature against Flash, repair remaining gaps, end-to-end client flow and representative rendering/audio performance/memory validation. |

The order reflects dependencies. Inspect source requirements before each
task and refine its scope in its handoff if necessary. Do not silently remove
requested features, substitute a placeholder, or certify a blocker as done.
Audio follows options so it can use established settings. The final task
audits cross-system interactions and does not expand scope to unrelated
packaging, map-editor work, or cross-platform ports.

## Per-task completion and handoff

1. Inspect the backlog, existing handoffs and current checkout. Claim only
   the requested next pending task by marking it `in progress`.
2. Confirm Flash/server behavior, implement the complete feature in the
   native runtime, and exercise its actual input/network/rendering path.
3. Run the relevant functional/protocol/render checks. For new visual work,
   generate captures from real Flash classes and native code and inspect them
   side by side at normal playing size and representative resized sizes.
   Record accepted differences honestly; do not chase glyph-edge equality.
4. Build/check release and sanitizer presets and run their full CTest suites
   before completion. Add meaningful regression checks where warranted.
   Use the isolated real-server runner for changed network/gameplay flows;
   distinguish live evidence from fixture/unit evidence. Record unavailable
   verification as a limitation rather than claiming it passed. A material
   unresolved acceptance gap keeps the task unfinished.
5. Write `docs/task-handoffs/<task-id>.md` with status, changes, source paths,
   verification commands/results, capture paths, differences/limitations,
   follow-up facts and the next task ID. Update `docs/progress.md` and any
   affected user-facing documentation. Mark the backlog row `complete` only
   when supported by the evidence.
6. Once complete, create exactly one successor chat using `create_thread`.
   Call `list_projects` first, resolve the RotMG local project at the path
   above, and use its current project ID with environment type `local`.
   Omit model/thinking overrides. This transition is explicitly authorized
   by the user's request for a new chat after each completed task.
7. Pass the successor the reusable prompt below with its next task ID/title.
   Save the successful creation receipt to
   `docs/task-handoffs/<next-task-id>-dispatch.json` (threadId or queued
   clientThreadId, hostId if supplied). Check an existing receipt before
   retrying so a failed output/retry does not start duplicate chats.
   If dispatch fails, record that failure and keep the completed work;
   do not run multiple copies or claim the successor exists.
8. Creation is asynchronous. Take one compact `wait_threads` snapshot with
   `timeoutMs: 0` if a ready threadId is available, to confirm dispatch status.
   Include its creation directive in the final answer and finish this chat;
   the successor owns further implementation. Keep completed chats available.
   No messages to other existing chats are needed.

If genuinely blocked, record the exact blocker and required input, and ask
the user in the active task chat. Do not create the successor until the task
is completed. When `17-integration` passes, report overall completion and
create no successor. No recurring automation is needed.

## Reusable successor prompt

> Continue the user's native C++ RotMG client completion chain. Implement
> task TASK_ID (TASK_TITLE) in `/home/kevin/Desktop/RotMG/realm-cpp` now.
> Read `docs/client-task-chain.md`, `AGENTS.md`, `docs/port-spec.md`,
> `docs/progress.md`, `docs/flash-specs.md`, and the previous task handoff.
> Preserve the existing uncommitted/untracked work and use the same local
> checkout. Match the real Flash classes at a glance, verify new visual work
> side by side, preserve server behavior and native runtime, and complete the
> task's acceptance checks. The user explicitly authorized a new chat after
> every completed task; after this task is implemented and verified, update
> the ledger/evidence and use create_thread to start exactly one successor
> for the next pending task with this same authorization and chain procedure.
> Keep the default model for successor chats. Use subagents per the user's
> global rules (`~/.claude/CLAUDE.md` in Claude Code, `~/.codex/AGENTS.md` in
> ChatGPT/Codex). Do not schedule an
> automation. Begin implementation, and persist until this task is complete
> or requires concrete user input. Then hand off and finish this chat.
