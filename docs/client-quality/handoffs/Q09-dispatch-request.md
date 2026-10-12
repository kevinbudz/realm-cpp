# Intended title

RotMG client Q09: Select the UI and vector approach

# Successor prompt

Implement Q09 (Select the UI and vector approach) of the user's native C++ RotMG client quality chain now. Continue from the verified Q08 result and `docs/client-quality/handoffs/Q08.md`. The Q08 code is delivered in draft PR #1 (https://github.com/kevinbudz/realm-cpp/pull/1) on `codex/q08-boundaries` in kevinbudz/realm-cpp (implementation commit `bbb6f78`); ensure those changes are present before starting. Use the current RotMG checkout (`/workspace/realm-cpp` in this cloud environment, or `/home/kevin/Desktop/RotMG/realm-cpp` locally). Preserve all existing modified/untracked work and integrate any missing predecessor changes safely; do not reset, stash, discard or bulk-commit another person's work.

Read any applicable AGENTS.md/global Codex rules, docs/port-spec.md, docs/progress.md, docs/client-quality-chain.md, docs/client-quality/benchmarks.md, docs/client-quality/work-packages.md, docs/client-quality/architecture.md, and the Q08 handoff. Follow the selected environment's setup instructions; the current cloud setup is /workspace/realm-env/start.md and commands must source /workspace/realm-env/activate.sh.

Compare the existing bitmap UI/focused proprietary layer with a pinned library pilot using current official sources. Implement actual tooltip, inventory drag, modal text entry, scrolling panel and animated title text at 1x/HiDPI. Measure visual fit, input/IME/focus/clipping, integration, CPU/GPU/allocation costs, footprint, licensing and maintenance; document the decision in docs/client-quality/ui-decision.md. Keep the selected pilot usable in the client. Do not substitute an empty demo or speculate about a wholesale engine/backend replacement. Respect the Q08 ownership/dependency checks, headless game/protocol targets, native runtime, Flash appearance at a glance, drawable-resolution text, original pixel art, correct gameplay/protocol semantics and bounded memory. No server or web/WebGPU work.

Q08 records cloud-specific pre-existing failures: the strict dynamic_atlas differential fails on Q07 and Q08 at the same first pixel; the headless world_queries test emits an existing world_dyes UBSan overflow; libGLX_mesa exit allocations are reproduced on Q07 and precisely suppressed only for graphics LSan checks. Do not describe the whole client as sanitizer-clean or qualified. S3/144 Hz remains unqualified. Startup background positions are randomized; seeded stress captures are the deterministic visual comparison.

The user explicitly instructed this chat to start Q08 and, after each completed task, spin up a new chat to start Q09 and subsequent tasks. After Q09 is fully implemented and verified, write its handoff, update the ledger/progress and create exactly one Q10 successor using the same authorization and chain procedure. Keep default model/thinking settings. Do not also dispatch the old feature chain, message existing chats, archive them, or schedule an automation. Subagents are not requested; use them only if an applicable user instruction explicitly authorizes them. Q19 creates no successor. If the current host lacks chat-creation tools, record an undispatched successor request and explain the concrete limitation; do not claim that a chat exists.

Begin implementation and persist until Q09 is complete or requires concrete missing input.

# Dispatch state

Prepared by Q08. No Q09 chat has been created and no dispatch receipt exists.
The cloud host does not expose create_thread; list_projects returns:
"This app tool is no longer available through dynamic tools. Use the codex_app MCP server. If that server is unavailable on this host, task delegation is unavailable."
