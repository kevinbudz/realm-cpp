# Implementation progress

The project goal is a complete, optimized native C++ client using Flash as its
visual and functional shell. On 2026-10-10 the user clarified that UI/playfield
should look basically the same, with performance and improvements over Flash
taking priority over exact matching. See `port-spec.md` and `../AGENTS.md`.

Exact-match results below are historical implementation evidence. Small text
coverage, subpixel, antialiasing, shadow, and rotated-arrow differences are
accepted and do not block completion. Fix actual readability/layout defects,
missing features, correctness problems, and measured performance bottlenecks.

## Current evidence

- Q08 codebase boundaries completed on 2026-10-12: CPU assets, UI and fixtures now have separate owners, headless model/background/draw interfaces no longer pull GL, and
  viewer/client share one compiled application coordinator. Eleven graphics / seven headless libraries have configure-time link and include/source ownership checks (66 headers).
  `main.cpp` is eight lines; option/script/offline-map setup and SDL/RSS lifetimes have concrete owners. Headless SDK discovery is disabled and poisoned GL/SDL public-header builds pass.
  All 30 seeded full-quality paired captures and painter/work/cache signatures match Q07 exactly; 22 CLI results and three headless workload outcomes match. Real SDL input/resize
  and 720 title-pulse frames pass on Q07, Q08 Release and graphics sanitizers. Release **59/60** (the strict atlas differential fails on independently built Q07 at the identical first
  pixel), headless ASan/UBSan **51/51** (the existing non-finite-position `world_dyes` overflow remains reported), graphics sanitizers **4/4** with only reproduced GLX driver exit
  allocations suppressed. No whole-client clean certification or speedup claim: software-renderer timing repeats have broad tails; dense S3 p95 changes +4.3%, and S3/144 Hz remains
  unqualified. Longer UI/idle/S2 controls and complete percentiles are in [Q08 evidence](client-quality/evidence/Q08-results.json); see [architecture](client-quality/architecture.md)
  and [handoff](client-quality/handoffs/Q08.md). Q09 is pending: its [exact request](client-quality/handoffs/Q09-dispatch-request.md) is prepared, but this cloud host exposes no chat-creation tool.
- Q07 texture residency and batching completed on 2026-10-12: every composed appearance, name, condition icon and floating status text used to be its own GL texture, so dense
  scenes broke the instanced batch at each one (S3 combat 15,080 draws/frame, `render.submit` 7.4 ms) and the 64 MiB per-image GPU cache sat at its cap with deterministic miss bursts.
  They now live in dynamic layers of the sprite atlas array: a headless shelf allocator with LRU and a 2-frame protection window (`TextureResidency`), appearances keyed by content
  so a resident one needs no CPU lookup, growth from 2 to at most 12 pages (GPU copy) only when the recent working set needs it, shrink at map change, per-image fallback beyond that.
  Paired 1080p runs (5 repeats): draws S3 combat 15,080 -> 4, S3 mixed-dense 18,170 -> 3,169, S2 players 3,005 -> 3; CPU p50 S3 combat 23.0 -> 11.7 ms, mixed-dense 24.5 -> 13.9
  (p95 233 -> 140), mixed-spread 18.4 -> 13.4, S2 players 2.04 -> 0.64; GPU S3 mixed-dense 12.9 -> 5.9 ms; models/Nexus/UI flat. Worst-case GPU for these bitmaps 256 MiB (was 160).
  New `dynamic_atlas` (906,018 checks: every resident slot read back byte-exact through churn, eviction and growth on both copy paths; 0 unexplained differing pixels, the rest are
  nearest-sampling ties) and `texture_residency` (485 M checks). Release **59/59**, ASan/UBSan **50/50**, graphics sanitizers **27/27**; S3/144 Hz still unqualified (first-seen
  composition bursts). See `client-quality/handoffs/Q07.md`, `client-quality/evidence/Q07-results.json`. Q08 is next; its dispatch request is written but no successor chat was created.
- Q06 real 3D model rendering completed on 2026-10-11: each placed model is now one allocation-free painter-stream `Quad` (`model_quad`: the affine screen transform of
  model space, object rotation, sprite and color) drawn by an instanced model program over geometry uploaded once (`model_geometry`, 124 KB for all 42 models); consecutive
  copies of one model are a single `glDrawArraysInstanced`, and the vertex shader applies the transform and Flash flat shade while GL back-face culling applies the same winding
  rule. `project_model` stays as the CPU reference. Paired 1080p runs (5 repeats): S3 dense models p50 6.18 -> 1.37 ms, GPU 3.29 -> 0.93 ms, S4 12.1 -> 2.5 ms, 1,000 repeated
  models 1.51 -> 0.22 ms with 1,005 -> 7 draws, allocations 27,822 -> 876 per frame; mixed S3 spread p50 20.0 -> 18.6 ms (mixed GPU +2-4 %). Equivalence: new `model_instances`
  differential (12,096 CPU cases, 344 GPU frames: 49 of 27.8 M pixels differ at edges; 7/7 mutations caught), real-Flash model reference 110 frames within 3 px and 109/110
  byte-identical to the pre-Q06 renderer, 21/21 paired state/packet/hit/marker digests identical, 8/9 1080p captures byte-identical (1 pixel). Release **57/57**, ASan/UBSan
  **49/49**, graphics sanitizers **23/23**; S3/144 Hz still unqualified (appearance/text = Q07). See `client-quality/handoffs/Q06.md`, `client-quality/evidence/Q06-results.json`.
  Q07 is next; its dispatch request is written but no successor chat was created from this session.
- Q05 hot data and resource lifetimes completed on 2026-10-11: callgrind of the S3 mixed-dense fixture showed `Particles::remove_target` was 79 % of all instructions
  (each expiring projectile scanned the 8,192-particle pool, emitters and flashes), then bounded-log erases at the front in `World::report_damage`/`notify`. Fixes: conservative
  `TargetFilter`s so removal/attach skip scans when nothing follows the object (exact `Particles::targets` for tests); flat open-addressing `EntityIndex` for id -> slot (no heap node per
  entity/projectile); `StatMap` (sorted 8-byte entries) for `Entity::stats` (`Entity` 304 -> 272 B, a 40-stat player ~1.7 -> 0.3-0.5 KB); `HitSet` for multi-hit history
  (`FlyingProjectile` 128 -> 96 B); deque/head-offset bounded logs; default dyes decoded once per definition; dense-list, id-table and combat-grid capacity released at map reset.
  Headless paired fixtures (identical state/packet/hit checksums in 12/12): S3 mixed-dense 49.7 -> 3.0 ms per tick, S3 mixed-spread 12.6 -> 4.3, churn 5.1 -> 0.43; counted heap S3
  live 24.6 -> 17.7 MB, 9,000-object populate 156,000 -> 33,025 allocations, map teardown 1.6 -> 0.16 ms and 6.1 -> 1.05 MB retained. Viewer paired runs: painter-stream digests identical
  in 11/11, S3 p95 ratios 0.86-0.90. New `world_storage` test (505,894 checks, 7 mutations caught). No `Entity` hot/cold split or ECS (not justified by measurement). Release **54/54**,
  ASan/UBSan **47/47**, graphics sanitizers **20/20**; S3/144 Hz still unqualified (name/appearance churn = Q07, minimap = Q12). See `client-quality/handoffs/Q05.md`,
  `client-quality/storage.md`, `client-quality/evidence/Q05-results.json`. Q06 is next; its dispatch request is written but no successor chat was created from this session.
  S3 follow-up (user-authorized): appearance/label caches resized to the S3 working sets (4096 entries / 64 MiB) with O(1) GPU lookups, struct cache keys, faster `ui_rectangle` and `compose`
  (both differential-tested byte for byte): S3 mixed-spread p50 168.8 -> 19.5 ms, mixed-dense 792 -> 29.8 ms, combat-cluster 225 -> 23.6 ms (identical painter streams in 15/15 scenarios; Release 56/56,
  ASan/UBSan 49/49, graphics 24/24). 144 Hz still unqualified: p95 tails are first-seen composition hitches, steady frames ~19 ms need a batched/instanced projectile path (Q06/Q07).
- Q04 visibility and world queries completed on 2026-10-11: profiling showed the camera/ordering/nearby-interaction work is only 3-5 ms of 140-800 ms S3
  frames (the rest is name/appearance work for Q07 and the minimap redraw, 5.7-7.6 ms, for Q12), so no spatial grid was added. `Scene` now looks up each visible
  entity's definition and depth once, sorts compact records and merges the pre-ordered entities with the sorted particles; `World::interactive_target` visits a
  maintained list of interactive ids; `world_dyes` scans once instead of four times. Per-frame query cost S3 3.5-4.4 -> 1.5-1.7 ms, S4 4.7 -> 2.1 ms; whole-frame p95
  unchanged. The new `visibility_culling` differential (cull-disabled oracle, 63 camera/zoom/resize configs, 30 seeds, 3 mutations caught) found and fixed a real bug:
  wide/tall models (e.g. Jacko, Large Monument) vanished up to 68 px before leaving the screen; offscreen models are now tested against their own `model_extent`. Painter
  streams are byte-identical to the previous implementation in 57 of 59 paired scenarios (the other 2 draw 2 more edge models); state/packet checksums identical in all.
  Release **53/53**, ASan/UBSan **46/46**, graphics sanitizers **19/19**; S3/144 Hz still unqualified. See `client-quality/handoffs/Q04.md`,
  `client-quality/evidence/Q04-results.json`. Q05 followed (above).
- Q03 projectile collision search completed on 2026-10-11: `Combat::update` finds enemy-targeting shots' candidates through a per-update uniform
  grid (bounded cells, power-of-two size, rebuilt from the entity list so nothing can go stale) instead of scanning every entity; target eligibility, exact-tie
  order (earliest dense entity), multi-hit history and hit packets are unchanged, proved by a lockstep grid-vs-original-scan differential test (56 checks incl.
  ties, boundaries, NaN/outliers, churn) and identical frozen/stress checksums in 12 paired scenarios x 15 runs. S3 combat cluster `combat.update` 1.2 s -> 1.7 ms,
  S4 projectiles 3.3 s -> 2.9 ms; 3000 enemies/20k shots in one 0.9-tile box still examine every enemy (548 ms vs 1382 ms). Release **51/51**, ASan/UBSan
  **45/45**, graphics sanitizers pass. Remaining S3 cost is `scene.build`/appearance/text (Q04/Q05/Q07); S3/144 Hz still unqualified. See `client-quality/handoffs/Q03.md`,
  `client-quality/evidence/Q03-results.json`. Q04 followed (above).

- Q02 stress fixtures completed on 2026-10-11: versioned deterministic S1–S4/Nexus/
  models/variety/churn/UI/mixed workloads (59 scenarios, frozen small-fixture checksums,
  render-pace independent) driving production World/Combat/Scene/HUD and a framed-packet
  Session replay, plus `realm_stress` (headless, TCP peers, 42-model library, new icon-cache
  component) and `tools/quality_stress.py` (paired/shuffled runs, semantic checks, traces,
  reports). Final Release **50/50**, ASan/UBSan **44/44**, graphics sanitizers **10/10**.
  Original-Nexus captures are pixel-identical to Q01 at 800x600/720p/1080p/4K; ordinary-app
  control p95 x1.011. Short scaling diagnostics rank `combat.target_query` first (20k shots
  ≈ 0.8 s, 40k ≈ 3.2 s CPU/frame), then appearance lookup/compose (variety-512 141.6 ms vs
  42.4 ms at 16) and name text; GPU stays 2–12 ms through S3 mixed. S3/144 Hz is still
  unqualified; no soak run. See `client-quality/stress.md`, `client-quality/evidence/
  Q02-results.json`, `client-quality/handoffs/Q02.md`. Q03 followed (above).

- Q01 performance telemetry completed on 2026-10-11: raw frame/counter JSONL,
  bounded CPU Chrome traces, optional pinned Tracy 0.11.1 and C++ allocation
  attribution, asynchronous GPU phase/frame/model-subset samples, and a readable
  report tool. Final Release **44/44** (91.74s), default ASan/UBSan **39/39**
  (79.92s), explicit graphics+allocation sanitizers **4/4** (8.99s), expanded
  40-model coverage checks pass. Telemetry preserves predecessor PNGs exactly
  at 800x600/720p/1080p/4K; visible actual drawable1920x1024. Five short matched
  diagnostic-control repetitions show overlapping disabled p95 ranges; export
  adds measured overhead. Original-map traces identify 68 warm-frame C++ requests,
  45 in model preparation. Real-time particles make fixed-scene Nexus timing
  comparisons unmatched; Q02 must supply a deterministic simulation timeline and
  production stress fixtures. No S3/144Hz or whole-client speedup claim. See
  `client-quality/telemetry.md`, `client-quality/evidence/Q01-results.json` and
  `client-quality/handoffs/Q01.md`. Q02 followed (see above); the old feature
  chain remains historical and does not dispatch.

- Q00 quality baseline completed on 2026-10-11: incremental Release build and
  **42/42 CTest** (92.66 s), ASan/UBSan build and **38/38 CTest** (78.81 s).
  Source/asset fingerprints, actual hardware/settings, three short Nexus and
  extra-quad repetitions, separate drawable-size captures and resource/feature
  audit are in `client-quality/baseline.md` and `client-quality/evidence/Q00-results.json`.
  Added `tools/quality_baseline.py`; C++ runtime and original assets unchanged.
  Strict gallery diagnostic reproduces the known 512 one-value pixel differences
  on Mesa and remains a diagnostic failure, not a pass. Hidden captures cover
  800x600, 720p, 1080p and 4K; visible 1080p request was actually 1920x1024.
  Extra quads do not establish active combat capacity. Q00-v1 freezes the
  full-quality S3/144 Hz goal, still unqualified; missing raw telemetry and
  production stress work belong to Q01/Q02. Death/fame, legends, deletion and
  native audio remain confirmed gaps. This audit supersedes stale gap/next-task
  statements in the historical sections below. See `client-quality/handoffs/Q00.md`.

- Task 02-guild-board completed on 2026-10-10: real panel routing, guild board
  view/edit/cancel/save, officer permissions, reusable Flash scrollbar, native
  asynchronous HTTP, Loading/Saving and recoverable errors. Fresh Release and
  ASan/UBSan each pass all 23 CTest suites. Thirteen real Flash/native fixtures
  have matching geometry and were reviewed at base, 720p, 1200p and 4K. Actual
  SDL/TCP/HTTP/GPU input and resize checks pass. Isolated real-server board
  read/save/readback, Unicode/form encoding and permission/error checks pass
  with Release and sanitizer native clients. See `task-handoffs/02-guild-board.md`
  for captures, commands and live versus fixture evidence.


- Task 01-dialogs completed on 2026-10-10: reusable standard Flash Dialog and
  MessageCloseDialog, drawable-resolution text, modal input and BUYRESULT 1
  Purchase Error integration. Fresh Release and ASan/UBSan builds each pass
  all 21 CTest suites. Ten real Flash/native fixtures have matching layout
  metrics and were reviewed side by side at 800x600, 1280x720, 1600x1200 and
  3840x2160; small glyph/filter/edge differences remain accepted. Actual SDL/TCP
  popup routing, resize and Close passed, as did the isolated real-server
  insufficient-funds BUY flow. See `task-handoffs/01-dialogs.md` for commands,
  captures and the distinction between live and fixture evidence.

- Native guild registrar, guild hall portal, and name changer panels/dialogs are
  implemented (2026-10-10; see the section below). All 15 CTest suites passed
  in Release and ASan/UBSan builds; affected interaction/inventory suites and
  the expanded native rendering checks passed after the final title layout fix.
- Native item tooltips are implemented (2026-10-10; see the section below).
- Before the social panels, all 13 CTest suites were validated in Release and ASan/UBSan builds.
  The sanitizer run exposed a pre-existing stale player reference in the
  merchant section of `inventory_tests.cpp`; it now reacquires the player by
  object ID after entity additions/removals. The affected inventory suite
  passed in both builds after the fix; all other suites passed the full runs.
  The optional 963-item rendering/cache/scroll checks also passed ASan/UBSan.
- Flash baseline: `17bdf079452a1c470b8895eae9cf108599e7addd`.
- Server baseline: `bb75bc1e438b465986a8ada354b6332f69e0e3a6`.
- Eleven Release and ASan/UBSan CTest suites passed after stats tab integration.
  Standalone font and HUD artwork cache stress runs also passed sanitizers.
- Core tests: 179 checks. Animation tests: 20,678 checks. Movement: 29 checks.
- Terrain test: 34,778 checks and 16,031 LRU evictions; no current-frame
  texture was overwritten and the 8,192-slot store stayed bounded.
- Independent loopback server verifies HELLO/LOAD, fragmented/coalesced input,
  world updates, PONG, GOTOACK and MOVE. It now connects via localhost to
  exercise IPv6-to-IPv4 address fallback where available.
- Isolated current real server accepted account registration, CREATE and LOAD,
  100 MOVE, 16 PLAYERSHOOT, 4 USEITEM, 3 SHOOTACK, 1 INVSWAP and 1 INVDROP
  packets. Swap, consumable use and drop each received a successful INVRESULT.
  All three sessions remained connected. Its database
  and settings were temporary. The supplied repositories remain clean.
  A fourth session now spends MP and uses a magic potion (slot 255); the
  server accepts it, refills MP and reports the stack going from 6 to 5.
  Four consecutive runs passed, one with the ASan/UBSan client. One earlier
  run timed out the 2-second inventory exercise; it now has 4 seconds.
  The inventory session now also drops the starter weapon through the
  ground-drop routing, waits for the server's soulbound bag, picks the weapon
  up with the ContainerGrid double-click path and re-equips it (3 INVSWAP,
  1 INVDROP; 6 seconds). Five consecutive runs passed. One earlier run showed
  that a stat update queued before an accepted swap can arrive ahead of its
  INVRESULT and briefly show the old slot. Flash applies server stats the same
  way, so the exercise now waits for the authoritative slot.
  A fifth session walks to the Nexus Vault Portal until it becomes the
  interaction target, sends USEPORTAL and follows RECONNECT into the Vault.
  The wire proxy now relays one connection per map. Five consecutive
  Release runs and one ASan/UBSan run passed with 6 HELLO, 5 LOAD and
  1 USEPORTAL. Three earlier back-to-back runs stopped because the test
  server exited during startup, before any client connected. Their logs were
  truncated, and it did not recur in the next five runs; the cause is unknown.
- A live Nexus frame was captured under `build/test-server/live.png`.
- 222 Nexus terrain compositions match original TileRedrawer PNGs exactly
  with the mask choice fixed to variant zero.
- `tests/animated_terrain_tests.py` now regenerates its map and source references.
  All 429 fixtures across 26 animated GroundCXML definitions match the Flash
  compositor exactly: 225 base textures, 200 transparent overlays and four
  compositions. This includes definitions with only TopAnimate.
- 48 outlined/unoutlined projectile frames matched original Point3D geometry
  on real Stage3D, across six texture/size combinations and four angles.
- A colored fixture covering every alpha byte matched Stage3D exactly after
  correcting PNG decode, premultiplied upload and SOURCE_ALPHA blending.
- All 35 filtered Nexus sprite captures and the 48 projectile captures matched
  exactly after the alpha correction.
- Combat has 31 checks, including item stat/rate/damage modifiers, ability
  MP/cooldown validation, USEITEM-before-PLAYERSHOOT ordering and authoritative HP.
- Inventory has 111 prediction, slot compatibility, packet, rollback and input
  checks, including potion stacks, container panel input/drop routing and
  the portal button, click and PortalPanel state mapping. A separate independent TCP peer verifies INVSWAP, consumable USEITEM,
  ability USEITEM/PLAYERSHOOT and INVDROP through Session, including fragmented
  rejection replies and the shared FIFO acknowledgment order.

## Fonts: implementation evidence and optional exact comparisons

- Downloads contains regular and bold Myriad files identical to the embedded
  files. SHA-256 regular: `87224cb7af1a3150172ae8190185fad761a50bd21648f8befd3a7751fc33167d`;
  bold: `0c2923e4148b20a95c813c0331f62c07c1c24e246549f78127e946b943e4fee1`.
- FontExporter uses the original SimpleText and exports glyph coverage on the
  twentieth-pixel horizontal grid, advances, line metrics and full-field samples.
- Native FontBook lays out UTF-8 and rasterizes whole fields, then applies the
  measured Flash shadow filter. The filter has horizontal and vertical
  1,2,2,2,1 taps, truncation after each pass, then alpha composition over 256.
- Coverage bundles contain compressed alpha, not hundreds of thousands of
  separate PNGs. A 32-byte binary index replaces the large exporter JSON at
  runtime. Font styles load on demand, with an O(1) LRU capped at 4,096 glyphs
  and 8 MiB decoded data. GPU storage contains complete text fields rather
  than every glyph phase.
- `build/fonts-client` has regular and bold at 12/13/14/16/18/20/22/24/26/28/30/32/36,
  including full original font Unicode coverage. Size 13 also has half-pixel
  vertical coverage for the stats tab. Size 18 bold also has vertical phases
  .2/.4/.6/.8 for wrapped PortalPanel names (`--wrapped-bold-sizes`). It
  indexes 535,744 coverage records in 17,143,808 bytes, with a 41 MiB
  compressed bundle. The previous pack is `build/fonts-client-before-portal`.
  In `tests/font_cpu_tests.py`, the 560 fields from the previous faces fail
  on exactly the same 128 fields and bounds as before. The 80 new
  vertical-phase fields have 16 failures, all in the existing categories:
  first-letter overhang and the left-aligned two-line sample.
  `build/fonts-hud` contains the earlier five-size comparison pack. Shadowed styles in the task file provide
  references; the native filter generates those from foreground glyphs.
- All 24 basic size-14 fields match both black GPU captures and complete
  transparent RGBA references exactly with the current binary loader. A stress
  run made 4,485 evictions, ended at 4,096 glyphs / 2,488,084 decoded bytes,
  and preserved those reference outputs. It also passed ASan/UBSan.
  The new twelve-size `fonts-client` pack passed the same 24 source fields
  under ASan/UBSan while evicting 16,602 glyphs; its final decoded cache was
  4,096 entries / 3,837,808 bytes.
- The current size-13/half-pixel pack also passed those 48 selected fields and
  its complete-glyph cache stress under ASan/UBSan: 20,054 evictions and a final
  cache of 4,096 glyphs / 3,513,888 decoded bytes.
- Expanded comparison: 163 of 200 full transparent fields match exactly.
  Remaining 37 involve multiline vertical subpixels, first-letter overhang
  clipping, a few individual coverage samples and one filtered size-16 field.
  Do not claim complete font parity. Font rasterization at four times the scale
  does not reproduce the original coverage by simple threshold/downsampling.
- Fonts now render the gameplay character name and XP/fame, HP and MP meters.
  Whole-field color, half-pixel placement and boost opacity are implemented.
  Functional wrapping, rich text and missing-glyph handling still need review;
  reproducing Flash's complete glyph coverage is optional.
- Size-13 half-pixel vertical coverage fixes the stats values' source placement.
  All 48 selected transparent fields (24 size-14 basic fields and 24 size-13
  half-pixel fields) match exactly. The fixed stats labels use finite original
  HUD artwork to preserve TextField clipping; this does not establish arbitrary
  clipped-field font parity. Tiny coverage differences are accepted; review
  multiline output for actual readability or layout problems.

## Gameplay HUD and minimap

### Trading (2026-10-10)

- TRADEREQUESTED shows TradeRequestPanel in the interaction panel ("NAME wants
  to trade with you", Reject/Accept, closed after 20 seconds) and adds Flash's
  `/trade` hint to chat. Accept sends the reverse REQUESTTRADE.
- TRADESTART covers the HUD below the minimap with TradePanel: both
  TradeInventory grids (names 20 bold, taglines 12, 4x3 slots at 44-pixel
  pitch), Cancel and the TradeButton. Slot backgrounds are baked from real
  TradeSlot objects (`tradefill*`, `tradeempty*`); offered slots use the gold
  fill and outline, non-tradeable slots veryDarkCT. My tradeable slots toggle on
  mouse down and send CHANGETRADE. checkTrade's space rules, messages and
  colors, the 3-second countdown bar, Waiting after ACCEPTTRADE,
  TRADEACCEPTED matching, TRADECHANGED resets, window ACTIVATE restarting the
  countdown (even without space, as Flash), TRADEDONE (chat line) and Cancel
  are implemented. Trade slots show item tooltips; party hover is suppressed.
- Flash resets TradeButton when it is added to the stage, so a trade without
  space still counts down and enables; the server rejects that trade. Native
  keeps this behavior.
- Verification: `realm_trade_inputs` renders `tests/trade_cases.json`;
  references come from PlayerTooltipExporter's `trade` task (real
  TradeInventory, TextButton, TradeButton and TradeRequestPanel). Side-by-side
  review matches layout, slot art, colors, text and button states at a glance;
  small glyph differences remain. `trade` (26 checks) passed in Release and
  ASan/UBSan with all 20 suites.
- Real server: a second fixture accepts a request, both sides accept after the
  countdown, and the server reports "Trade Successful!" (three consecutive
  full live runs). Fresh fixtures hold only soulbound gear, so that trade is
  empty; the offer path is covered by unit tests and a live viewer capture
  where the other player offered a potion.

### Chat and speech balloons (2026-10-10)

- Native TextBox: TEXT lines and client lines (PlayerMenu teleport refusals
  as `*Error*`) stack upward from the bottom-left, at most eleven, hidden
  20 seconds after arrival. PageUp first shows expired lines, then moves back
  three; PageDown returns. Wrapping follows TextBox: 596 pixels for the first
  row, 576 with a 20-pixel indent after, 16-pixel rows (16.5 with a rank
  icon) and 4 pixels between messages. ElementFormats colors, `<name>` with
  the 0x363636 separator, `#` enemy and `@` admin names, `To:` outgoing tells,
  guild text, FameUtil rank icons and the black GlowFilter(3,3,2) match.
- Input: Enter, `/`, Tab (`/tell` + recent teller; Shift for older) and G
  (`/g `) open the 598x18 bordered field on key release; Enter sends
  PLAYERTEXT and closes it. Text is limited to 128 code points. While the
  field is open no game keys reach movement, abilities or hotkeys. Escape
  closes the field (a native addition: Flash has no binding, and Escape
  otherwise leaves the game). Lines persist across maps like Flash's static
  log; storage keeps the newest 1,000 lines.
- Speech balloons (MapOverlay/SpeechBalloon): one per speaking object, with
  onText's normal/enemy/tell/guild colors, a 150-pixel wrapped field, the
  cut-edge frame with tail, 2-pixel outline and 16-pixel halo, positioned
  2 pixels above the sprite for bubbleTime seconds from first draw. They live
  in map pixels (scaled by world zoom), beneath HUD, chat and popups; at most
  32 at once, each with its own GPU texture slot.
- Verification: `realm_chat_inputs` renders `tests/chat_cases.json` and
  `tests/balloon_cases.json`; Flash references come from
  PlayerTooltipExporter's `chat` and `balloons` tasks (TextBoxLine/TextBlock
  with TextBox's layout, and real SpeechBalloon objects). Chat rows align
  with Flash (best whole-pixel offset 0 in x and 0-1 in y); wrapping matches
  on the long fixture. Balloon field widths match exactly and frames match
  within one pixel on every edge. Small glyph coverage differences remain
  (FTE CFF text versus the native Myriad renderer).
- `chat` (39 checks) passed in Release and ASan/UBSan with all 19 suites;
  scaled chat, balloon and menu fixtures also ran clean under sanitizers.
- Real server: the menu exercise now also sends PLAYERTEXT and waits for the
  echo. Ten consecutive full live runs passed; one earlier run during this
  work ended without a summary line, and its cause was not captured. A live
  viewer capture showed typed chat, the balloon above the speaker, the
  other player's line and the open input field in the Nexus.
- Not yet: the guest "Please REGISTER" mode (the server requires accounts),
  chat filtering for ignored accounts (Flash has none), the textBubbles
  option (no options screen yet) and the speech-bubble icon.

### PlayerMenu (2026-10-10)

- Clicking a PartyPanel member opens the native PlayerMenu (Menu.as /
  PlayerMenu.as): GameObjectListItem header, then Teleport (map allows it and
  the target is not invisible), Invite (local guild rank >= officer and the
  target has no guild), Lock/Unlock (starred), Trade + Ignore or Unignore.
  Options are fixed while open, as in Flash. Rows are 28 pixels apart from
  y 40, the frame is max(154, content+12) wide with 4-pixel cut corners, a
  0x363636 fill, white 1-pixel outline and the 16-pixel black halo.
- Hover tints icon and label with mouseOverCT. TeleportMenuOption greys out
  during the 10-second cooldown and reveals the white label as it expires.
- Menu.position (right/below in the left half/top third, else left/above,
  clamped to 12), roll-out and the 40-pixel distance close, CLICK semantics
  (press and release on the same option) and scaling with window height are
  implemented. PartyPanel shows no player tooltip while the menu is open.
  Clicks over the menu do not shoot.
- Actions: TELEPORT through Player.teleportTo's cooldown/invisibility rules
  (refusal text goes to stdout until chat exists), GUILDINVITE, REQUESTTRADE,
  and EDITACCOUNTLIST lists 0/1 with Party's local lock/ignore updates.
- Icons and the frame are baked from MenuOption/TextureRedrawer by
  PlayerTooltipExporter (`menuicon-*`, `menuframe`); the rebake left all
  2,098 existing HUD art records byte-identical.
- Verification: `realm_player_menu_inputs` renders the six cases in
  `tests/player_menu_cases.json`; Flash references come from the same
  exporter's `menus` task. All six frame sizes match Flash exactly, and
  side-by-side review at 1x shows the same layout, art, colors and states;
  small glyph coverage differences remain. 1.8x density renders sharp text
  with pixel-art icons. `player_menus` (29 checks) passed in Release and
  ASan/UBSan with the rest of the 18 suites.
- Real server: `realm_session --exercise-menu` runs beside a second fixture
  account (registered from 127.0.0.2, as the server allows one registration
  per address). It waits out the server's 3-second arrival invisibility,
  checks the options, sends TELEPORT (GOTO received; the immediate retry is
  refused with Flash's "11 seconds" text), lock/unlock/ignore/unignore (each
  ACCOUNTLIST reply checked) and REQUESTTRADE (the server's unique-name
  reply). Six consecutive full live runs passed after the harness accepted a
  peer reset at disconnect and allowed 10-second HTTP replies. The server
  enforces neither AllowTeleport nor a teleport cooldown; only the client does.
- Flash opens PlayerMenu from PartyPanel. PartyArrow opens PlayerGroupMenu
  and stops its mouse-down from reaching world firing (now implemented in task 05).
  Minimap PlayerGroupMenu is now implemented in task 04 below; PlayerArrow
  integration follows in task 05.

### Native guild and name interactions (2026-10-10)

- GuildHallPortal shows Guild Hall with Enter for guild members and Not In
  Guild otherwise. Click and the default interact key 0 send USEPORTAL only
  for members. Guild membership changes refresh the panel.
- GuildRegister shows Create a Guild / Create 1000 fame without membership,
  or the authoritative rank and guild name with Renounce. Creation opens a
  native name frame; renunciation requires the No, I'll stay / Yes, I'll quit
  confirmation. CREATEGUILD/GUILDRESULT and GUILDREMOVE are wired. Rejections
  stay visible and permit retry; successful renunciation follows the server's
  guild-name stat (the server sends no GuildResult for self-removal).
- NameChanger shows Choose Account Name or the current name / Change 1000
  gold. CHOOSENAME/NAMERESULT are wired. This fork never calls
  NameChanger.setRankRequired, so its required rank remains zero. The server
  charges 1000 gold for a changed name even with NAME_CHOSEN false; the
  confirmation frame states that actual cost. Input keeps the Flash limits:
  10 letters for names, 20 letters/spaces for guilds.
- Renames update Session's login name on successful NAMERESULT before a
  RECONNECT. Startup saves the new name with the existing password and private
  file permissions, and uses it when returning to character selection. A save
  failure keeps the new credentials in memory and reports the persistence
  error on return to the menus. Balance, name, guild and rank stats remain
  authoritative; dialogs do not predict them.
- Native frames block gameplay input while retaining network/world updates.
  Pending requests disable submission/cancellation; errors enable retry.
  Press/release picking, focus-loss cancellation, map/reconnect/death teardown,
  shrinking to small windows, and long-title layout are covered. A 20-character
  all-W guild name uses smaller text to fit above the action button.
- One 440x340 (598,400-byte) CPU image/atlas region serves every modal.
  Unchanged panels and frames reuse their bitmaps; 1,000 unchanged checks per
  fixture make no new rebuilds. New panels use existing fonts/currency artwork
  and simple native button shapes, with no new Flash exports.
- `interaction_panels` and `social_loopback` are part of CTest. The independent
  TCP peer verifies social packet bytes, fragmented rejection replies, retry,
  duplicate name submission suppression and renamed HELLO credentials after
  guild-portal RECONNECT. Optional native rendering/cache checks pass in
  Release and ASan/UBSan (10,069 assertions).
- `tests/social_hud_tests.py` runs the real native application against isolated
  TCP/HTTP fixture peers. All eight 800x600/1280x720 scenarios passed: guild
  creation, renunciation, name changes and portal clicks. The name scenarios
  use realm_client's saved-account startup and verify saved credentials,
  owner-only permissions and the renamed /char/list request after Escape.
  Normal-size captures were reviewed under `build/interaction-reference`.
  These are fixture peers, not new live-server purchase/guild verification.
- Measurement: the 800x600, 8x8 guild-creation fixture on Linux/NVIDIA RTX
  3050 Ti with `--benchmark` (vsync off) recorded CPU event/build/submit mean
  0.0513 ms / p95 0.0703 ms over 3,936 samples, presentation p95 0.3085 ms,
  and sampled process VmHWM 212,756 KiB (207.8 MiB). The atlas remained two
  pages / 32 MiB. This small scripted fixture includes screenshot work and
  initialization in peak memory; it is not a dense gameplay benchmark, GPU
  elapsed-time measurement, input-latency measurement or whole-client speedup.

### Native item tooltips (2026-10-10)

- Equipment, inventory, backpack, nearby loot, potion stacks, and merchant
  item icons show original item names/art and readable wrapped descriptions.
  Effects include projectile damage/range/rate, conditions, equip bonuses,
  activations, cooldowns, MP costs, fame bonuses, and use/class restrictions.
- Damage/rate/cooldown/stat modifiers come from ItemData. Matching equipped
  items provide labeled per-shot, range, rate, shot-count, cooldown, and equip
  stat comparisons. Green/red differences describe individual stats rather
  than claiming an overall DPS or upgrade verdict.
- XML DEX/VIT/WIS IDs (28/26/27) differ from this fork's wire IDs (25/23/24).
  Tooltips handle the two mappings separately. Removed containers, changed
  merchandise, tab changes, empty slots, and drags cannot leave stale item
  content. Focus loss/window leave hide the tooltip.
- One 256x580 bitmap is reserved in the existing atlas. Item/comparison
  changes rebuild and upload it; mouse movement only changes the quad
  position. A 1,000-update unchanged-hover check made zero new rebuilds.
  Long content is retained and scrolls through a bounded viewport with the
  mouse wheel; scrolling does not rerasterize the content.
- All 963 imported item definitions produced tooltip content and rendered
  through the existing native fonts/art without additional Flash exports.
  Tests cover real fixture values, modifiers, wrong classes, potion picking,
  backpack/container/merchant routing, changes to comparison gear, window
  bounds, and long-content scrolling. Expanded checks: 3,932 assertions.
- Gameplay screenshots were reviewed at 800x600 and 1280x720 on Linux/NVIDIA.
  Captures: `build/tooltip-reference/gameplay-800.png`,
  `gameplay-1280.png`, `staff.png`, `long-top.png`, and `long-bottom.png`.
  They validate readable native presentation; exact Flash pixels are optional.
- `realm_tooltip_tests` is part of CTest. Optional rendering checks:
  `./build/realm_tooltip_tests build/fonts-client build/hud-art build/tooltip-reference`.
  No live-server purchase changes or whole-client speedup are claimed here.

### Existing HUD and minimap evidence

- Character name (20 bold gray with shadow), XP/fame, HP and MP meters use
  authoritative player stats and typed class caps. Cap/boost colors, negative
  boosts, centered values, clipping and faint boost text follow StatusBar.
- The 200x600 HUD bitmap is retained. Only changed components are rasterized
  and uploaded as atlas subregions; unchanged states do neither. All seven
  fixtures match fresh CPU reconstruction after incremental changes, and GPU
  captures match those CPU images exactly through the overlay pass.
- Four of seven complete panel fixtures match original Flash exactly. Three
  retain 12–14 differing pixels around the first digit "8" and its shadow.
  These small rasterization differences are accepted under the clarified scope.
- Base class portraits, equipment and main/backpack inventory render from
  original finite UI artwork. All 140 assembled source fixtures match exactly
  in the portrait and equipment/inventory regions, across 14 classes and 533
  items. These cover empty slot silhouettes and numbers, cut corners, tier
  tags, dose labels, restricted-use colors and starter ability mana dimming.
  All 140 complete native panels also match fresh reconstruction and GPU
  output after incremental atlas uploads. Dynamic status text differences
  are accepted when readable; these comparisons do not establish complete HUD
  functionality or quality.
- `tools/bake_hud_art.py` exports an indexed 1,333,010-byte PNG bundle with
  1,758 records, including the six fixed stats labels at two horizontal phases.
  Runtime loads requested records only and uses an O(1) LRU
  bounded at 128 images / 4 MiB decoded pixels. Its complete-record stress run
  passed ASan/UBSan, made 2,891 evictions and ended at 128 images / 1,428,224
  bytes while preserving reconstruction. AIR is used only during offline baking.
- Native HUD input supports drag/swap, dropping to the world, double-click
  equip/use, shift-click consumables, ctrl-click backpack transfer, clicking
  the main/backpack tabs, and 1–8 inventory use. Drag artwork uploads only when
  its type/dim state changes; pointer movement changes its quad position.
  Drag visuals still need review at playing size. Focus loss cancels a
  drag and preserves the selected tab. Item tooltips are now implemented above.
- The nearby container panel follows GameSprite.updateNearestInteractive and
  InteractPanel. The strictly closest interactive object closer than 1 tile
  is the target. When it is a Container or OneWayContainer, its ContainerGrid
  is drawn at (14,508). The x is measured from the empty 172-pixel grid, the
  y is the panel offset of 8. Tiles use the inventory backgrounds without
  hotkey numbers, with the same restricted, quantity and tier art. Changes to
  the panel upload only the 200x100 region from y=500, and tab-panel changes
  leave it untouched. All 112 fixtures in `tests/container_hud_tests.py`
  match original Flash exactly in the 300-600 region, on fresh CPU and
  incremental GPU paths. They cover 14 classes, loot bags, gravestones, vault
  chests, gift chests, partial and empty grids, tiers and tab changes. Each
  fixture also asserts its upload regions.
  Input follows ItemGridMediator:
  - Shift-click uses the item.
  - Ctrl-click does nothing.
  - Double-click moves it to the next free inventory slot, or uses it if
    there is none.
  - Dragging swaps with any tile, subject to equipment slot types.
  - Dropping on the tab strip stores it in the next free slot.
  - Dragging a container item to the world sends nothing.
  - A player item dropped on the world swaps into the first empty slot of a
    nearby container. That bypasses the OneWayContainer check, as
    dropWithoutDestTile does. Otherwise it sends INVDROP.
  - Replacing the container cancels drags and pending clicks on its tiles.
  Not implemented yet:
  - MoneyChanger. GuildRegister, GuildHallPortal and NameChanger now have
    native panels/dialogs (see above). Merchant, ClosedVaultChest and
    GuildMerchant purchase panels also exist in the code.
  - The loot_appears sound. Container item tooltips are now implemented.
- The portal panel follows PortalPanel. The name is GameObject.getName (the
  NAME stat, otherwise the display ID); LockedPortal definitions strip one
  "Locked " prefix. It is an 18-pixel bold white field with a shadow, 188
  wide at panel origin (6,508), with y 6, or 0 once taller than 30 pixels.
  Measured from Flash TextLineMetrics:
  - Line widths are the summed advances without trailing spaces, matching
    `FontBook::measure` exactly for all 63 distinct lines.
  - Each line starts at 2 + (184 - width) / 2, floored to twips.
  - Lines wrap after the last word that fits in 184 pixels. A word wider than
    that breaks between characters. `FontBook::wrap` reproduces all 84
    fixtures' line breaks.
  - Lines after the first render 0.2 pixels below k * 20.6. This was found by
    sweeping baked vertical phases on lines two to four. Phases .0/.2 and
    1.0/1.2 render identically, which suggests a quarter-pixel vertical glyph
    grid. The rule is verified only for 18-pixel bold.
  The constant parts come from the art pack, captured from a real PortalPanel
  on stage in panel coordinates: the Enter TextButton (normal and hover) and
  the red Full/Locked fields. Enter shows when the portal is unlocked and its
  ACTIVE stat is nonzero (default true); otherwise Full or Locked shows.
  All 84 fixtures in `tests/portal_hud_tests.py` match original Flash exactly
  in the 300-600 region, on fresh CPU and incremental GPU paths. They cover all
  37 Portal definitions, server realm names with counts, single characters,
  empty, leading/trailing/double spaces, two to four lines, an unbreakable
  word, inactive and locked states, hover and upload regions. The exporter
  also asserts the Enter button rectangle (73,562,54,26), which drives the
  hit test.
  Input:
  - Hover highlights the button while the pointer is inside its cut-edge
    shape.
  - A click needs both press and release on the visible button and sends
    USEPORTAL.
  - The interact key 0 (Parameters default) also sends it, with key repeat
    and even while Full or Locked, as PortalPanel.onKeyDown does.
  The hover, shape and click semantics are inferred from TextButton and Flash
  picking rules, not compared against live Flash.
  Not handled: names go through htmlText, so a name containing `<` or `&`
  would be parsed as markup in Flash but is drawn literally here.
  GuildHallPortal now uses its own native panel (see above).
- The default PartyPanel draws at (6,508) when there is no interactive target.
  `Party` follows Party.update. It refreshes at most every 500 ms, and an
  ACCOUNTLIST forces the next refresh. Members are other players within 50
  tiles (distance squared at most 2500) or starred players, sorted by starred,
  then distance squared, then object ID, keeping at most 6. A star comes from
  ACCOUNTLIST id 0 and stays on that Player object; id 1 replaces the ignore
  list. The party resets on MAPINFO.
  Each member is drawn on the x 0/100, y 0/32/64 grid:
  - The class portrait is drawn at (x-4,y-4).
  - The name is a 13-pixel bold field, 66x20 with the default shadow, at
    (x+32,y+6). Fellow guild members get 10944349, everyone else 16572160.
  - Unstarred members get the 0.6 dark ColorTransform.
  - Members under 20% HP pulse with red offset rv*128, where
    rv = int(|sin(t/200)|*10)/10. Concatenating the dark transform leaves
    the offsets unscaled.
  - Flash skips the identity transform (pulse step 0 on a starred member).
  Flash's ColorTransform on premultiplied pixels is
  clamp(floor((p*m8*255 + offset*a*256)/65280), 0, a), with the 8.8
  multiplier m8. This was measured with an AIR probe over every alpha/channel
  pair for all 37 reachable transforms (`tools/bake_party_transforms.py`).
  The formula is off by one on 15,835 pairs. Those are stored as a 63 KiB
  sorted constant table (`assets/party-transforms.bin`, generated into
  `flash_party_transforms.hpp`). Any unmeasured transform throws.
  `FontBook::rasterize` now accepts a fixed field height.
  40 of the 41 fixtures in `tests/party_hud_tests.py` match original Flash
  exactly in the 300-600 region, on fresh CPU and incremental GPU paths. They
  cover:
  - all 14 classes;
  - empty and solo parties, ordering, the distance limit and ties;
  - guild colors;
  - all 10 pulse steps on dark and starred members;
  - zero, over-max and negative HP;
  - redraw regions;
  - a portal hiding the party.
  The remaining fixture, the bold 13-pixel name "Dying", keeps a pinned
  two-pixel difference from the known glyph-overlap coverage issue (see
  Fonts in progress). `realm_party_tests` covers the throttle, sticky stars,
  removal, the ignore list, the cap of six and reset.
  Not implemented: PartyPanel hover (which freezes the list), PlayerToolTip,
  the PlayerMenu, and acting on the ignore list.
- The ButtonPanel family covers GuildBoard ("Guild Board" / View),
  GuildChronicle ("Guild Chronicle" / View), CharacterChanger ("Change
  Characters" / Change) and ReskinVendor's ReskinPanel ("Change Skin" /
  Choose).
  - The title is drawn natively with the PortalPanel field: 18-pixel bold,
    188 wide, centered, with a shadow, at y 6.
  - ReskinPanel never sets its title's y, so its title is at 0. It also sets
    htmlText after multiline, so Flash reports an extra empty second line,
    which draws nothing.
  - The TextButtons are captured from Flash without titles, normal and hover,
    as eight additive art records (1,776 total). The 1,768 earlier records are
    byte-identical, and the previous pack is `build/hud-art-before-buttons`.
  - Measured button rectangles: View (74.5,562,51,26), Change (65,562,70,26)
    and Choose (66,562,68,26). `HudRect` keeps the half pixel for hit testing.
  All 28 fixtures in `tests/button_hud_tests.py` match original Flash exactly
  in the 300-600 region, on fresh CPU and incremental GPU paths. They cover:
  - every object of the four classes, with and without hover;
  - a rotation through all 14 player classes, including stats-tab cases;
  - an unchanged redraw and hover-only upload regions.
  The exporter also asserts the title lines and positions and every button
  rectangle.
  Input:
  - Portal and button panels share `HudState::panel_button` and
    `HudInput::button_down`/`button_up`.
  - A click needs the press and release on the same target's button; a
    replaced panel cancels it, and so does focus loss.
  - Change sends the game back to character selection, the native equivalent
    of GameSprite.closed, as Escape does.
  - At this panel-only milestone, guild board, chronicle and reskin windows
    were missing. Guild board View is now implemented in task 02 below;
    chronicle and reskin remain pending.
- A bold 13-pixel stats value ending in a bare "4" (for example DEF 24 or
  WIS 334) differs from Flash by about 7 pixels at the field's clip edge.
  The "4" crossbar ends inside the edge pixel but past the clip line; Flash
  clips the outline before antialiasing, so it draws no ink there. The
  current model takes the glyph alpha up to the clip coverage, which keeps
  some ink. A 720-field probe across both reachable stats phases, every final
  digit and eight boosts found only these 8 cases
  (`tools/stats_clip_probe.py`). Snapping the clip edge to whole pixels
  breaks dozens of other fields. The stats suite has no such values. This is
  an accepted minor rasterization difference; do not pursue subpixel glyph
  reconstruction solely to match Flash.
- A null-reference bind in `HudState::player` with no nearby container,
  found by UBSan, is fixed.
- Potion widgets (PotionInventoryView) draw on the main and backpack tabs
  from stats 59/60. Each widget has a stocked or empty cut-edge background,
  a fixed 24-pixel size-13 count field with a strength-2 shadow, and a
  46x35 icon at (13,-11) above the lower grid row. Their y is 468 because
  TabStripMediator measures the empty grid (84 pixels) before items are set;
  measuring after items would give 92-100. All 126 fixtures in
  `tests/potion_hud_tests.py` match original Flash exactly in the 300-500
  region: 14 classes, counts 0-6, 10 and 123, both tabs, and items under the
  icons. They also match fresh reconstruction and incremental GPU uploads.
  Six records were added to `build/hud-art` (1,764 total); the previous pack
  is `build/hud-art-before-potions` and its 1,758 records are byte-identical.
  The source never clears PotionModel.available, so the grayscale state is
  unreachable and not drawn.
- Potion input follows PotionSlotView and UsePotionCommand. Shift-click or a
  second MOUSE_UP within 250 ms on the same view uses a potion; leaving the
  view, tab changes and focus loss cancel the pending click. The hit area is
  the background shape plus the icon bounds, with the magic view topmost.
  The non-mouse-enabled count field is excluded. This is inferred from
  Flash picking rules and has not been compared with live Flash. F and V
  include key auto-repeat, matching KEY_DOWN. Nothing is sent with an empty
  stack or a full HP/MP meter. USEITEM sends the player position, predicts
  the stack decrement and, on rejection, restores both stacks captured
  before sending, in FIFO order with other inventory operations.
  The use_potion sound is pending with audio. Keys 1-8 still ignore
  auto-repeat, unlike the source.
- FontBook accepts a shadow strength and a fixed TextField width. The
  defaults reproduce the previous output exactly.
- Stats tab draws authoritative ATT/DEF/SPD/DEX/VIT/WIS values and boosts with
  original gray/green/red/gold priority and class caps imported once from XML.
  Click selects all available tabs; B cycles them, matching the source default.
  Tab changes cancel drags and double-click history. Equipment remains usable
  while the stats body has no inventory slots. An absent backpack cannot be
  selected, and release over a tab without pressing it does not select it.
- All 140 stats/tab fixtures across 14 classes match original Flash exactly in
  the equipment/tab region, including zero defense, capped/over-cap values,
  positive/negative boosts, varying text widths and inventory/backpack returns.
  Retained panels match fresh CPU reconstruction and incremental GPU uploads.
  A stats-only change uploads the 200x127 content region; hidden stat changes
  cause no rebuild or upload. References: `build/stats-reference/comparison.json`.
- Current font/HUD packs are installed at `build/fonts-client` and
  `build/hud-art` (1,768 records; the four new portal records are additive
  and the 1,764 earlier records are byte-identical). Previous versions are
  retained in `*-before-portal`, `*-before-potions` and `*-before-stats`.
- Inventory prediction moves/clears both item type and ItemData. INVRESULT
  restores rejected operations in send order, including ability USEITEM in
  the same FIFO. Successful operations retain prediction until server stats
  update it. Missing container rollback and map/reconnect reset are handled.
  Inventory use preserves authoritative HP, MP, conditions and equip bonuses.
  Container pickup, swap and ground-drop routing are wired to the interaction panel.
- Minimap implements remembered ground/static occupying objects, unknown
  black tiles, source zoom levels and edge clamping, player/guild/enemy/portal
  markers, camera arrow, and zoom controls. +/- and clicking the original
  zoom button positions work. Static object drops leave their remembered
  pixels until a ground update overwrites them, matching the source signals.
- World tracks minimap row revisions. Only changed rows are recolored; a
  changed camera/marker view reuses the map colors. Unchanged views avoid
  rasterization/upload. Marker vectors reuse capacity. Source PNG copies are
  released after the small texture-color cache has been populated.
- All 2,776 source ground/object colors and 24 unrotated minimap frames match
  exactly across three map sizes, four zoom requests and center/edge focus.
  Rotated arrow comparisons retain 3–8 differing pixels in each failing
  fixture; these tiny arrow differences are accepted. Outside-minimap marker
  overflow, full HUD scaling, alternate static object appearance colors,
  minimap player menus/tooltips were subsequently verified in task 04 below.
- Source getPixel32 recovery was measured over all 65,536 gray/alpha samples,
  covering 32,642 reachable internal pairs. The resulting 64 KiB constant
  lookup preserves differences from PNG export unpremultiplication.
- HUD and minimap use two quads in one overlay draw call after world scissoring;
  the Nexus capture contains 369 quads, two total draws and two atlas pages.
  Screenshots: `build/hud-reference/offline-minimap.png` and the new
  `build/hud-reference/offline-inventory.png`. The new capture includes the
  base Wizard portrait, starter equipment and inventory. This is still a
  development harness, with interaction views absent.
  `build/hud-reference/offline-potions.png` adds the potion widgets.
- All six atlas galleries remain exact after overlay renderer changes, and
  the 24 basic text fields remain exact on CPU and GPU.

## Startup sequence

`realm_client` (or `realm_viewer --client`) follows the Flash flow:
- `StartupSequence` shows `AccountLoadingScreen`.
- `WebLoadAccountTask` verifies the saved account through `/account/verify`.
- `GetCharListTask` loads `/char/list`. Rejected credentials clear the account
  and retry immediately; other errors show "Load error, retrying" and retry
  after 1 s.
- Then `TitleView`, `CharacterSelectionAndNewsScreen`, `NewCharacterScreen`,
  and finally `PlayGameCommand` arguments handed to `Session`.

Pieces:
- HTTP: `net::http_post` in realm_core.
- Char-list parsing, Flash's character sort (most recently used, then
  experience), fame stars and the saved account: `account.hpp`. The saved
  account is the SharedObject equivalent, stored as JSON with owner-only
  permissions.
- Screens and dialogs: `startup.hpp`. Menus render through one reserved
  1024x1024 atlas image as window-space overlay quads.
- MenuBackground: the embedded JM map, written twice with a 10-tile border
  (`World::load_jm_text(..., wrap_border)`). It is drawn through the live
  terrain and scene with a fixed-clip camera (`Camera::configure(position,
  angle, clip)`), scrolls at 1 tile/s and is dimmed by 0x2B2B2B at 0.8.

Verified against an isolated real server with `--script` at 800x600 and
1280x720:
- guest boot, title, guest character list and class grid;
- the register dialog, Rogue creation, the Nexus;
- Escape back to the list with the new character, entering again;
- a second launch from the saved account.

Deviations and gaps:
- Guests are asked to sign in or register when they press Play, because the
  server rejects empty-credential HELLO packets. Flash would try to play as a
  guest.
- Historical startup milestone: the `CharacterSkinView` step was skipped then; task 06 now implements it.
- Character, class and New Character icons scale the baked HUD portrait
  (4x sprite) to Flash's 5x/6x redraw sizes. They are static (no
  CharacterBox posing) and show no dyes.
- Not implemented (these show a message instead): Legends, the map editor,
  buying slots, character deletion, star and credit graphics, news icons and
  links, the list scrollbar (the mouse wheel scrolls the list), and the death
  screen (a "Your character has died." message instead).
- Under sdl2-compat, pushing synthetic `SDL_TEXTINPUT` events crashes. Scripted
  text is therefore handed to the menus directly. Real keyboard text input
  uses SDL text events.

## Correct source behavior worth preserving

- Local bullets use negative signed 32-bit IDs. Parametric motion uses signed
  remainder, including negative IDs; enemy/server shot IDs are full int32.
- Incoming status stat counts are bytes. String stats are NAME and GUILD_NAME;
  ACCOUNT_ID is numeric. This fork has no legacy packet cipher.
- Remote interpolation is 127 ms. Duplicate target ticks preserve velocity.
- MOVE is sent once per frame while ticks are pending, after local movement;
  sinking/ground effects update only after that acknowledgment.
- `GameObject.damage` displays feedback and does not subtract HP or set
  conditions locally. Server stat packets own HP and conditions. Damage cues
  now record immediate feedback for the forthcoming text/effect renderer.
- Original bitmap textures are premultiplied in GPU storage and the existing
  Stage3D shader blends with SOURCE_ALPHA. Colored translucent pixels can
  therefore be darker than conventional straight-alpha rendering. Preserve
  recognizable appearance. This records Flash's behavior; native blending
  may be simplified or improved under the current visual quality goals.
- A raw translucent PNG channel decodes to floor(channel*alpha/256), except
  opaque channels remain unchanged. PNG export unpremultiplies with a 256
  scale; its exact inverse is ceil(channel*alpha/256), except opaque channels.
- Outline projectile texture pixels stay at native pixel scale and get
  clipped by a rotated square; they are not fitted into the square.

## Remaining systems

- Q00 source audit confirms player/item/group tooltips, minimap menus, party
  arrows, settings, names/status text/icons and breath/boost bars now exist.
  Remaining client screens include death/fame, legends, character deletion;
  startup slot purchase/news art and links/scrollbar still have gaps. See the
  quality baseline for current verified coverage and limitations.
- Basic ItemData modifiers, abilities, escape, potion stacks and inventory
  prediction/rollback and the nearby container panel are implemented.
  The nearby portal panel and USEPORTAL, default PartyPanel, ButtonPanel family,
  native item tooltips, and merchant/closed-vault/guild-merchant purchase panels
  are implemented, as are GuildRegister, GuildHallPortal and NameChanger with
  their creation/renunciation/name dialogs. Guild board and chronicle are now
  implemented, along with minimap PlayerGroupMenu and teleport restrictions.
  Party arrows and their grouped tooltip/menu are now implemented (task 05 below).
  Reskin is now implemented in task 06 (MoneyChanger has no panel in this fork).
- Masks/dyes and arbitrary live sizes now use bounded lazy native composition
  (task 07). The optional fixed processed pack still loads supplied neutral PNGs.
- Real models, particles, backgrounds/post effects and status overlays are
  implemented through tasks 08–11. Full mixed-scene scaling/ordering qualification
  remains to be measured; implementation is not complete-client certification.
- Native audio/music/sound routing remains unimplemented. Settings and selected
  focus/input/map-reset behavior have tests; full reconnect/startup integration
  remains part of final qualification.
- Deterministic full-scene replay/reference captures and matched Flash/native
  CPU/GPU/memory measurements. Current timing figures are isolated harness
  results and must not be presented as a whole-client speedup.
- Packaging, cross-platform builds and runtime validation outside Linux.

## Next work

Continue with **Q01 — Add useful performance telemetry** under
`client-quality-chain.md`; the old feature chain is historical and does not
dispatch successors. Q00's frozen profile and limitations are the starting
point. PlayerMenu, chat, speech balloons and trading are implemented. `docs/flash-specs.md`
holds extracted specs for party arrows and reskin. Tasks 07–12 are implemented.
Standard dialogs and the guild board are implemented. GuildRegister/GuildHallPortal/NameChanger panels and
their native dialogs are implemented. Merchant purchases and BUYRESULT feedback (including the standard Purchase Error
dialog) are wired. MoneyChanger has no panel in this Flash fork. Native item tooltips are implemented. Check current
code before selecting work; this evidence log may lag implementation. Stats,
tab switching, potions, containers, portals, buttons, and the default party
panel already have substantial validation.

Review new UI and playfield work at normal playing size for familiar appearance,
clear text, correct interactions, and stable rendering. Fix missing content,
significant clipping, and misleading gameplay cues. Leave tiny font, shadow,
color-transform, and rotated-minimap differences accepted. Do not extend Flash
coverage probes, phase packs, or transform correction tables solely for exact
matching; prefer simpler native implementations where useful. Existing tooling
can remain as optional diagnostics.

Measure CPU/GPU frame times, memory, loading/map changes, and responsiveness
alongside feature work. Keep meaningful protocol, functionality, memory safety,
cache, and native rendering checks. Runtime must remain native; AIR may be used
offline. Routine implementation and isolated fixture accounts need no new
permission.

## Native window resize and animated text (2026-10-10)

Latest resize direction: retain UI proportions and redraw at drawable resolution,
including scaled tier tags and the title Play pulse. The pulse must stay sharp
and smooth. This explicitly replaces magnification of base-resolution text.

- Added FreeType rendering of the original regular/bold Myriad outlines at the
  actual physical density. Existing baked logical metrics retain wrapping,
  alignment and hit areas. Static 800x600 text keeps its previous reference path.
- HUD, minimap, item/player tooltips, startup menus and guild/name dialogs now
  invalidate their retained surfaces on density changes and use resizable GPU
  textures. Partial HUD/menu uploads and unchanged-content cache reuse remain.
  Original raster artwork is composited into these surfaces at target density.
- Replaced embedded scaled HUD text for hotkeys, tier tags, stat labels, portal
  and panel buttons, rank labels and merchant notices. Tier tags use the original
  tight two-pixel glow rather than the broader status-text shadow.
- Play rerasterizes at each pulse size and physical subpixel position. Glyph
  coverage carries fractional motion; the GPU quad displays native texels.
  Its texture reserves the complete pulse range and reuses storage through the
  animation. This fixes both bitmap magnification and whole-pixel stepping.
- Added scripted `resize WIDTH HEIGHT`, HiDPI window support, physical upload
  regions, fitted dialog/tooltip density and resize-aware hover invalidation.

Verification:
- Release build and all 17 CTest checks passed. Address/undefined-behavior
  sanitizer resize fixtures also passed with the original font/art packs.
- Native resize fixtures cover 0.75, 1.2, 1.5, 2 and 3.6 densities, same-content
  cache invalidation, fractional glyph motion and restored base HUD pixels.
- GPU checks resize a running window through 800x600, 1280x720, 1600x1200 and
  3840x2160, including stats, item tooltips and menu/dialog input.
- 720 Play pulse frames at those four resolutions preserve native texel size
  and reuse GPU storage. Isolated menu build/upload measured 0.17 ms median and
  0.63 ms p95 on this machine; this is not a whole-client frame-time claim.
- Guild creation/rejection/retry checks passed at 800x600 and 1280x720.
- The strict source-gallery diagnostic still reports an existing one-channel,
  one-value difference on 512 lofiEnvironment2 pixels under this Mesa backend.
  A separately compiled original renderer produced identical RGB pixels to the
  revised renderer for that fixture. No tolerance was loosened.

Enlarged native font coverage and filters differ from magnifying Flash's old
bitmap coverage. Original layout, art, colors and source label content remain;
this work does not claim new exact Flash screenshot parity at enlarged sizes.

## Standard dialogs and Purchase Error (task 01-dialogs, 2026-10-10)

- Added `Dialog` in `include/realm/dialog.hpp` / `src/dialog.cpp`. A nullable
  title, wrapped centered body, zero/one/two TextButtons, enabled/hover states,
  base alpha and offsets retain the source layout. Ordinary button clicks
  return button 0/1 to the owner; MessageClose closes on its single button.
- Source title fields do not automatically wrap: explicit newlines are
  centered independently with the original line advance. Body text wraps at
  260 design pixels. Frame heights and button positions match all ten source
  fixtures, including long paragraphs, a multiline title and loading state.
- The source outline makes box bounds 301 by frameHeight+1; stage centering
  uses those bounds and stageHeight/600 scaling. Text and retained surfaces
  redraw at physical density, with cache reuse when content/hover/size is stable.
  Missing baked vertical phases in long regular fields use native outlines.
- BUYRESULT 1 opens `Purchase Error` with the server text and Close, without
  an extra chat line. Other results keep their server/error chat behavior.
  The enclosing modal layer matches DialogsView's 0x151515/.6 backdrop and
  consumes pointer, keyboard, text and wheel input before game controls.
  Held movement, firing and abilities are blocked while visible. Map changes,
  reconnect/death and disconnect clear the dialog. Specialized forms remain.
- Fresh checks: 21/21 Release and 21/21 ASan/UBSan CTest suites; 20 dialog
  assertions; ten real Flash/native layout/capture fixtures; SDL/TCP input and
  720p/1200p/4K resize/close regression. The existing GPU resize/pulse suite
  also passed after the font change.
- The full isolated server runner passed with 309 MOVE, 16 PLAYERSHOOT,
  5 USEITEM, 3 SHOOTACK, 3 INVSWAP, 1 INVDROP and exactly one BUY (a repeated
  outstanding request was suppressed). The server returned insufficient-funds
  BUYRESULT 1; the shared native dialog rendered at 1280x720 and closed.
  PlayerMenu/chat and the prior empty automated trade also passed. This does
  not claim a completed nonempty automated trade.
- Captures: `build/dialog-reference/comparison-base.png`,
  `comparison-1280.png`, `comparison-1600.png`, `comparison-3840.png`, and
  `gpu/*.png`. The real Flash classes were compiled from the untouched source
  with AIR and drawn through CPU BitmapData; native GPU captures used Mesa
  26.2.4 / AMD RX 6700 XT. Glyph coverage, halo falloff and cut-edge sampling
  differ slightly, especially enlarged. No exact pixel parity or whole-client
  speedup is claimed. Font/HUD art bundles and backups were unchanged.

## Guild board (task 02-guild-board, 2026-10-10)

GuildBoardPanel View now opens the native board. It preserves the source masked
400x400 text, 400/426x432 layout, frame/buttons, URL color, black/.8 backdrop and
no-backdrop Loading/Saving dialogs. Officers (rank >=20) can edit multiline
UTF8 text, select/copy/cut/paste, scroll, Cancel or Save. Read-only members have
Close. Reusable Scrollbar supports wheel/page/arrow hold/thumb drag and fitting
content without NaN positions. Asynchronous HTTP uses current session
credentials; generations ignore stale responses and map teardown never waits.
Errors have Retry and Close/Back, with unsaved drafts preserved.

The server decodes board form data twice; the client protects literal +/% text
through both layers without changing the server. Fresh full Release/sanitizer
suites pass 23/23 each; 33 controller/scrollbar checks, six independent HTTP
scenarios, 13 real Flash/native visual fixtures, SDL/TCP/HTTP/GPU panel/input/
resize/Close, and isolated real-server board checks passed. Existing purchase,
resize and gameplay/server regressions also pass. See the task handoff for
exact evidence, small accepted glyph/edge differences and the headless/live
versus loopback GPU distinction. Task 03-guild-members followed this task; see the completion entry below.


## Guild chronicle / members (task 03-guild-members, 2026-10-10)

GuildChroniclePanel View now opens the native member screen: source backdrop,
MenuFrame/continue, current guild/member fame, stored order, own-row highlight,
rank names/icons, slots and reusable Scrollbar. The 430px mask, 400px page and
negative/fractional list bounds match the real Flash class. Promote/demote/remove
follow the complete Flash permission matrix and exact shared Dialog confirmations.
Actions send dedicated Session packets once and refresh from matching server
TEXT replies; rank/removal failures remain in chat and open Error/Ok. No
GUILDRESULT is expected for these server handlers. Authoritative permission
changes cancel unsent actions; leaving/changing guild and map teardown close it.

Native HTTP uses current Session credentials and the configured app host,
async workers, stale-reply generations, queued reopen and nonblocking close.
Errors offer Retry/Close. A missing confirmation times out with a refresh prompt,
without resending the mutation. SDL modal routing suppresses game input, including
held movement/firing and hover. Resized text renders at drawable density. Seven
original fame/rank/remove images live in a separate HUD supplement, preserving
the existing packs/backups. Runtime stays native; AIR is an offline export tool.

Fresh final Release and ASan/UBSan CTest suites pass **25/25** each (6.88 / 26.99 s).
The controller has 101 checks and the HTTP peer covers five independent scenarios.
**16 real Flash/native geometry fixtures** passed and were individually reviewed
at base, 720p, 1200p and 4K. SDL/TCP/HTTP/OpenGL panel/confirmation/chat/scroll/
resize/Continue checks passed with exact packet counts and no gameplay leakage.
Isolated real-server tests passed with both native build variants for offline/
online promote/demote/remove, rank and removal rejection, guild/member fame,
HTTP refresh and persisted SQLite state. Existing board/dialog/resize GPU and
full isolated gameplay/server regressions also passed, including the prior empty
trade. No nonempty-trade or whole-client speedup claim is made.

See `docs/task-handoffs/03-guild-members.md` and
`build/guild-members-reference/comparison-*.png`, `gpu/*.png`, `live*.log`.
Small glyph/halo/cut-edge differences remain, especially enlarged; continue's
native halo is slightly tighter than Flash. Corrected pending/chat/error behavior
is deliberate because Flash waits for responses this server never sends.
Task **04-minimap-menu** subsequently completed; see its entry below.


## Minimap player groups/menu (task 04-minimap-menu, 2026-10-10)

Minimap hover now groups other eligible-to-display players within a strict five
logical pixels after ray-clamping off-window markers to the edge. Local and
NoMiniMap players are excluded. Current encounter order is preserved; the menu
freezes ID snapshots, then resolves current visibility on click to choose the
first remaining visible player. No eligible targets produce the exact source
error chat. The map flag hides Teleport, and Session also enforces that flag
and the original ten-second cooldown at the send boundary.

Native PlayerGroup implements source tooltip/menu frames, original class row and
teleport art, click hint, passive rows, diamond divider, hover and cooldown label
reveal. Shared GameObjectListItem labels serve PlayerMenu too. Layout accounts
for the 44px portrait bounds. Position/rollout/distance closing, press/release,
focus cancellation, resize retention and drawable-density text are implemented.
Dense groups retain every target while bounding their off-window surface. SDL
menu input prevents firing/ability leakage; map/modal teardown clears it.

Fresh final Release and ASan/UBSan pass **27/27** each (17.13 / 39.73s), including
35 group checks and independent teleport wire/cooldown/map-flag checks. **21 real
Flash/native fixtures** match frame dimensions and were reviewed side by side
at base, 720p/1200p/4K densities. Actual SDL/TCP/OpenGL grouped/edge hover, menu,
visibility changes, cooldown refusal/expiry, no-eligible error and restricted-map
checks pass, with two exact TELEPORTs and no shot/ability leakage. Both native
build variants pass isolated real-server ordered teleport and two GOTO/GOTOACK
pairs, and the actual GuildHall MAPINFO restriction sends no teleport request.
Existing resize/pulse/dialog/board/members GPU and full gameplay/server regression
pass, including the prior empty trade. Font/HUD packs/backups and sibling repos
are preserved. No exact-pixel or whole-client performance claim is made.

See `docs/task-handoffs/04-minimap-menu.md` and
`build/player-group-reference/comparison-*.png`, `gpu/*.png`, `live*.log`. Small
glyph/halo/cut-corner differences are accepted. AIR references are offline class
captures; native playing GPU and headless live-server evidence are distinguished
in the handoff. Task **05-party-arrows** is next in the authorized chat chain.


## Edge party arrows (task 05-party-arrows, 2026-10-10)

Native PartyArrows uses the six ordered Party members, source known-square draw
radius, camera-origin ray/edge projection, HUD allowance and angle clamps.
Original green/white 8px / hovered 11px triangles merge at strict eight-pixel
radius. Hover freezes the arrow and ordered ID group; rollout, removal, resize,
focus and map/modal teardown are handled. Six small retained surfaces and group
vectors stay bounded. Invisible/NoMiniMap members retain source arrows.

The shared PlayerGroup tooltip sits inward from the arrow; its menu owns ordered
snapshots and resolves current visibility on teleport. SDL arrow mouse-down
opens the menu before world input. Held presses remain suppressed through rollout
and resize until release. Existing Session map restrictions and ten-second
cooldown remain intact. Native grouping uses current projected visible arrows
instead of Flash's stale/hidden-slot positions; enlarged tooltips use scaled
bounds to preserve the inward layout and avoid Flash's clipped 4K footer.

Final Release **28/28** (17.06s) and ASan/UBSan **28/28** (39.86s) pass, including
**35 arrow checks**. **18 real Flash/native fixtures** have matching edge/rotation
metrics and were reviewed at base, rotated/offset/zoom, 720p, 1200p and 4K sizes.
Shared group's **21** real Flash/native frame layouts were freshly checked.
Actual SDL/TCP/GPU grouped hover/menu, held-click/rollout/resize suppression,
positive firing control, live target visibility, cooldown/error and map flag
checks pass. Both final native build variants pass isolated real-server peer
movement beyond the draw radius, arrow menu selection, exactly two TELEPORTs,
two local GOTO events and cooldown refusal/expiry. Separate fresh group/server
checks verify GuildHall's false MAPINFO flag with zero TELEPORTs.

Existing minimap, resize/pulse, dialog and guild GPU regressions and the full
isolated gameplay/server regression pass, including the existing empty trade.
The sibling repos, fonts, HUD packs/backups and guild-members supplement remain
preserved. AIR class captures, native GPU fixtures and headless real-server
checks are distinguished in `docs/task-handoffs/05-party-arrows.md`. Small
antialiasing/glyph/halo differences remain; no exact-pixel or whole-client
performance claim is made. Captures/logs are in `build/party-arrow-reference`.
Task **06-skins** is next in the authorized sequential chat chain.


## Skins (task 06-skins, 2026-10-11)

Completed class-filtered owned skin selection, Classic reset, startup skin
selection and purchase dialogs, and the in-game vendor RESKIN flow. Catalog
loads all 15 source skins. Server stat 70 remains authoritative: world, HUD,
party, player tooltips/menus and minimap/arrow groups refresh the same skin.
Guest startup browsing resumes the selected class after sign-in. Modal input
and held presses stay suppressed through resize until release.

Separate original-art supplement `build/hud-art/skins` contains 110 records;
existing HUD/fonts/backups and guild-members art are preserved. Each HUD pack
has a 128-entry / 4 MiB decoded bound (combined main/skins 256 / 8 MiB).
The 200ms walking preview uploads only its bounded region, including at 4K.
The neutral size-100 `build/baked-skins` supplement contains 300 frames.
Dyes, arbitrary encountered sizes and bounded lazy appearance caching belong
to task 07; the current processed lookup still needs that extension.

Final Release CTest passes **30/30 in 19.15s**, ASan/UBSan **30/30 in 52.14s**,
including **72 skin checks** and independent native HTTP fixtures. **44 real
Flash/native fixtures** passed geometry checks and were reviewed side by side
at 800x600, 1280x720, 1600x1200 and 3840x2160. Minor glyph/halo/antialiasing
variations remain. The user accepted the appearance and requested the next chat.

Actual SDL/TCP/HTTP/GPU checks pass startup purchase/CREATE, owned/reset RESKIN,
server rejection, live portraits/group menu, guest/sign-in resume, modal
resize/press suppression and a positive firing control. Both Release and
sanitizer native sessions pass isolated real-server ownership/purchase,
CREATE, reset/owned RESKIN and three rejection probes: exactly six RESKINs,
one actor CREATE and one peer CREATE. Online purchase refuses Account in use!
without mutation. Normal vendor metadata uses startup cache because this
server also refuses char/list while online. No optimistic skin mutation.

Fresh shared player-group reference/GPU, arrow GPU, resize, dialogs, guild
board/members GPU and full real-server gameplay regressions pass. The latter
records 309 MOVE, 16 PLAYERSHOOT, five USEITEM, three SHOOTACK, three INVSWAP,
one INVDROP, and the existing completed empty trade. This does not verify a
completed nonempty trade. Sanitizer preset excludes the SDL viewer, so GPU
captures are Release evidence. Sibling Flash/server repositories remain clean.

See `docs/task-handoffs/06-skins.md`; captures and final logs live in
`build/skin-reference`. Task **07-dyes-masks** is next in the authorized chain.


## Dyes, masks and lazy appearances (task 07-dyes-masks, 2026-10-11)

Native appearance composition now handles solid dyes, all four textile image
sets, red/green masks, mirrored/extended animation frames and unbaked live sizes.
Scene falls back from optional neutral processed packs without missing-key
failures. Stats 29/30, size and skin remain authoritative across world, HUD,
party, player tooltips/menus and minimap/arrow groups. Appearance-only menu
refreshes preserve labels/order/snapshots. Merchant world art and local dye
preview follow source rules without changing stored player appearance.

CPU and GPU cache bounds are each 256 entries / 32 MiB. Weak CPU owners avoid
pinning bitmap copies in GPU entries; current-frame quads retain GPU storage
through eviction. World identity/generation clears both caches on map teardown.
Hostile huge sizes use bounded raster density with preserved world placement.
Missing cloth item/dim icons compose into the existing bounded HUD cache;
original fonts/HUD/backups, skins and guild art remain preserved.

Final Release full CTest passes **31/31 in 20.04s**, ASan/UBSan **31/31 in
67.45s**. The dye suite passes **2600 checks**, covering 2555 catalog definitions,
13 live sizes, masks/textiles, stat snapshots, merchant preview, icon fallback
and count/byte eviction. **130 actual Flash/native shader/texture fixtures**
pass dimension/interior-color checks; **24 real source portrait/group/menu
fixtures** pass frame geometry at base/720p/1200p/4K. Source pairs were reviewed;
small halo/gradient/boundary/glyph differences remain within the current visual
contract. Runtime is native; AIR remains offline reference tooling.

Actual SDL/TCP/OpenGL passes live dyes/skins/sizes, resized portraits, one
USEITEM, a positive shot, 322 appearance changes, 300 concurrent appearances
and map teardown. Post-teardown CPU and GPU each hold six entries / 211952
bytes; caches stay within their count/byte bounds during churn. Both native
Release and sanitizer variants pass isolated real-server cloth consumption,
remote observation, stored size137, live HUD/group/menu/tooltip updates,
no optimistic dyes, RESKIN retention and HTTP/SQLite persistence: exactly four
USEITEMs and one RESKIN. Disposable accounts/settings/database preserve user data.

Fresh shared skin/group/arrow/resize/dialog/board/members GPU and 21 shared group
Flash-reference checks pass. Full isolated gameplay/server regression passes
310 MOVE, 16 PLAYERSHOOT, five USEITEM, three SHOOTACK, three INVSWAP, one
INVDROP, purchase error, teleport/account lists/chat/trade and the existing
completed empty trade. This does not establish a completed nonempty trade or
whole-client speedup. Sanitizer excludes the viewer, so GPU evidence is Release.

See `docs/task-handoffs/07-dyes-masks.md` and `build/dye-reference` for captures,
logs, accepted differences and reproduction commands. Task **08-particles** is
next in the authorized sequential chat chain. Sibling repositories remain clean.

## Task 09: 3D models (2026-10-11)

Objects with a registered `<Model>` now render real geometry: 42 Flash OBJ models
compiled at build time, Z-rotation by XML `Rotation`, camera/zoom projection, Flash
back-face culling, flat `0.75 + 0.25*max(0,n.L)` shading, textured (nearest, tiled) or
`Solid*`/`Color` groups, drawn in the ordinary painter stream with no depth buffer
(as in Flash). `SpiderWeb` appears only beside a Wall and turns away from it.

Evidence: Release and sanitizer builds clean, 33/33 CTest each (new `models` suite,
49,722 checks); 110 real-Flash vs native Scene reference frames within 3 px
(`tests/model_reference_tests.py`, `build/model-reference`); isolated real server sent
4 White Fountain model objects that all projected (`tests/model_live_tests.py`).
Accepted differences and limits (CPU-path reference only, texture tiling vs Stage3D
clamp, no performance claim) are in `docs/task-handoffs/09-models.md`.
Task 10-lighting follows below; no successor chat was created from the 09 session.

## 10-lighting (2026-10-11)

Flash has no per-map lighting (only the fixed model shade from task 09), so the task covers what it draws around
the world: Drunk blur and Blind filters on the map view only, the low-breath vignette, the 10 px sidebar gradient and
the MAPINFO star/Nexus-water backgrounds. Native renders the world offscreen only while a filter is active and keeps
HUD/overlays unfiltered; sizes scale with the window. Spec: `docs/flash-specs.md` (lighting section).

Evidence: release 35/35 and sanitizer 34/34 CTest (new `postfx_rules`, `postfx`); 15 real-Flash vs native comparisons
(`tests/postfx_reference_tests.py`; filters/overlays within a few levels, stars/water geometry exact, Nexus noise matched
statistically); isolated real server made the player Drunk via Pirate Rum and the native blur followed
(`tests/postfx_live_tests.py`, release and sanitizer clients). Limits (Nexus noise not Flash's, breath/blind not live,
software path only, no performance claim) are in `docs/task-handoffs/10-lighting.md`.
Task 11-status-effects follows below; no successor chat was created from the 10 session (no `create_thread` here).

## 11-status-effects (2026-10-11)

Added the world-space status cues Flash draws around objects: condition icons, names, floating text (damage, condition
names, notifications, level/exp/fame), the local player's breath bar, and the sprite effects (Stasis greyscale, Invisible
alpha, low-health pulse, Hexed, Hallucinating). DAMAGE effects, NOTIFICATION packets and level/exp/fame stat changes now
reach a bounded floating-text queue (`World::notify`); previously damage cues were recorded and never drawn and
NOTIFICATION was parsed and dropped. Spec: `docs/flash-specs.md` (status section). The pre-existing health bars, HUD boost
colors, Healing particles and the Drunk/Blind filters were checked and kept.

Evidence: release 37/37 and sanitizer 35/35 CTest (new `status_rules`, 530 checks; `status_gpu` pixel fixtures in release);
real-Flash comparison of the 24 condition icons (max channel difference 2), status text (mean 0.8 of 255) and name bitmaps
(mean 4.5) through AIR (`tests/status_reference_tests.py`, `build/status-reference`); isolated real server delivered the
Drunk CONDITION (icon frame 43) and a Magic Potion NOTIFICATION that became MP-colored 2 s text
(`tests/status_live_tests.py`, release and sanitizer clients). Limits (no full-scene Flash capture, no performance
measurement, name glow slightly weaker, Hexed/Hallucinating not live, no QueuedStatusText) are in
`docs/task-handoffs/11-status-effects.md`. Task 12-options is next.

## 12-options (2026-10-11)

Added the Flash Options screen and the settings system behind it. `realm::Settings` holds `Parameters.data_` by Flash name (key codes and choice values), persists to
`settings.json` beside the account file (`--settings-file`; the harness keeps defaults unless given one), and every keyboard action in `main.cpp` now follows the key
its row binds (movement, rotation, ability, autofire, potions, inventory slots, mini-map zoom, escape to Nexus, camera reset, centering, performance stats, fullscreen,
chat keys). `OptionsScreen` is the six-tab screen (key mappers with "[Hit Key]", choice boxes, disabled Rotate rows, label tooltips, reset of the visible tab, back to
home); it opens with O or CharacterDetailsView's IconButton (new baked records; Options in the Nexus, Nexus elsewhere) and applies every change at once. The flags now gate
the systems Flash gates: health bars, shadows, projectile outline, text bubbles, Ally Shots/Damage/Notifications, particles (optional effects only), tier tags, trade popup,
centering, VSync/Max FPS/fullscreen and the 16 Flash cursors. Spec: `docs/flash-specs.md` ("Options screen").

Evidence: release 42/42 and sanitizer 38/38 CTest (new `options`, `preferences`, `sdl_keys`, `options_source`, `options_gpu`); the option table matches `Options.as`/`Parameters.as`
(57 rows, 35 key defaults); the real Flash `Options` rendered through AIR matches the native screen in grid geometry and at-a-glance look (21 frames, 5 tooltips,
`tests/options_reference_tests.py`); an isolated real server saw the rebound keys act on the wire (`tests/options_live_tests.py`). Limits (Escape not rebindable, compact performance
panel, quest portraits / guild invite / quality / sound stored without native counterparts, no audio yet, cursor and fullscreen not visually verified) are in
`docs/task-handoffs/12-options.md`. Task 13-death is next; no successor chat was created from this session (no `create_thread`).
