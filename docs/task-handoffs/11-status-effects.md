# 11-status-effects — complete

Implemented and verified 2026-10-11. Next: **12-options** (not started; no successor chat was created from this
session, see "Chain hand-off").

## Scope and findings

Flash draws these status cues around objects (spec: `docs/flash-specs.md`, "Status effects, names and floating text"):
condition icons, names, health and breath bars, floating `CharacterStatusText` (damage, condition names, notifications,
level/exp/fame), and sprite-level effects (Stasis, Invisible, low-health pulse, Hexed, Hallucinating). Before this task the
native client had only the health bars, HUD stat-boost colors, Healing particles and the Drunk/Blind/breath post effects.
`World::report_damage` recorded damage cues that nothing drew, and the NOTIFICATION packet was parsed and dropped.

## Implementation

- `include/realm/conditions.hpp`: Flash condition IDs, `bit = 1 << (id-1)`, names, icon frames, `condition_icon_frames`
  (ascending bit order, `(time/500) % n` frame cycling).
- `World` (`world.hpp/.cpp`): `StatusEvent`, `notify()`, `take_status_events()` (bounded to 64 undrained). `report_damage`
  now emits the `GameObject.damage` texts (condition names 500 ms apart, "Immune", `-N` red/purple). `status()` emits
  "Level Up!", "+N EXP", "+N Fame" from stat changes of players seen before. `Session` turns NOTIFICATION into
  `world.notify(object, text, color & 0xffffff, 2000)`.
- `include/realm/status_effects.hpp`, `src/status_effects.cpp`: `StatusArt` (bounded LRU of rasterized labels, 160 entries /
  8 MiB, keyed by text, color and density; fixed cache of condition icons), `StatusTexts` (queue of 48, Flash timing and
  position), `breath_bar_back_color`, `low_health_pulse`. Text is rasterized at the world zoom, like speech balloons.
- `Appearances::condition_icon`: the 16x16 icon (frame at 4,4, gradient subtract, outline, 0.3-alpha glow).
- `Scene` (`scene.cpp`): after the sprite, in Flash order: condition icons, name (other players, or `ShowName` objects), health bar,
  and the local player's breath bar (only while the breath stat exists). Sprite effects: Stasis sets a flag the sprite shader
  turns into the greyscale matrix (`renderer.cpp`), Invisible players draw at 40% alpha, players under 20% health pulse
  red, Hexed players use a stable `Hexable` object texture (`Catalog::hex_transforms`), Hallucination swaps ordinary objects to
  a stable `lofiChar8x8` tile. `Scene::status_surface` keeps GPU copies of labels and icons (<= 256).
- `main.cpp`: drains status events, updates `StatusTexts`, and appends the floating text to the overlay after speech balloons
  and below the HUD, chat and popups (overlay storage 48 -> 128 quads). Viewer fixtures (with `--map`): `--hp`, `--ally`,
  `--ally-name`, `--notify TEXT[:RRGGBB]`, `--damage`, `--damage-effects 3,5`, `--status-age`, `--scene-time`, `--ghost`
  (plus the existing `--condition`, `--breath`, `--effect-time`).

## Verification

- Build/CTest: release `build/` **37/37** (new `status_rules`, `status_gpu`), sanitizer `build-sanitize/` **35/35**
  (`status_rules`; `status_gpu` needs the viewer, which the sanitize preset disables). Logs: `build/status-ctest-release.log`,
  `build/status-ctest-sanitize.log`, `build/status-build-sanitize.log` (only the pre-existing test warnings).
- `status_rules` (530 checks, fonts + Flash root): condition bits equal the existing magic numbers, icon frame order/cycling, damage
  and condition text (stagger, Immune, armor-piercing purple, ordering), level/exp/fame rules (first sighting silent, level wins
  over exp, fame only at level 20), notification queue bounds, Flash text position/drift/timing, label cache keys, density,
  eviction bounds, icon alpha map, bar colors.
- `status_gpu` (`tests/status_gpu_tests.py`, real viewer and renderer, pixel checks): Stasis exactly grey and confined to the
  sprite; Invisible alpha 0.4 (10th percentile 0.400 of the sprite pixels; the unchanged shadow raises the rest); low-health
  pulse direction; six icons 96 px wide centered on and just above the sprite; one icon about 16 px; white name outline
  under other players and none for the local player; breath bar geometry and exact 0x26CAFF fill/0x545454 back for 50%;
  notification and damage text colors, centering, above the sprite, 20 px of drift at half life; Hexed and Hallucinating stable
  across runs and changing both players.
- Real Flash reference: `python3 tests/status_reference_tests.py` (AIR; `tools/flash-reference/StatusExporter.as`, built with
  `tools/build_flash_reference.py StatusExporter --java-home <jdk>`) exports ConditionEffect's own cached icons and the
  SimpleText/GlowFilter bitmaps the source builds for status text and names. Against `build/realm_status_inputs`: 24
  icons max channel difference 2 (mean 0.08 of 255); six status texts mean 0.77 (worst 2.6, a 1 px kerning shift on
  "Quest Complete!"); three names mean 4.5 (glow slightly weaker). Output: `build/status-reference/{flash,native}`,
  `compare-text.png`, `compare-text2.png`.
- Real server: `python3 tests/status_live_tests.py ../realm-server/bin/Release/net8.0/RotMG.dll` (isolated server, disposable
  account; also with `--client build-sanitize/realm_status_session_inputs`): a seeded Pirate Rum turned on Drunk and the
  player's icon list gained exactly frame 43; after the ability spent mana, a seeded Magic Potion made the server send
  NOTIFICATION "+30" (0xff6084e0) which was queued as MP-colored, 2000 ms text. USEITEM=3 (rum, ability, potion).
  `postfx_live_tests.py` still passes.
- Captures: `build/status/{icons,text,contact-800x600,contact-1280x720,contact-1920x1080,contact-resized,halluc,hexed}.png`.

## Differences and limitations (honest)

- No full-scene Flash capture exists for objects with status cues (the reference exporters render a class at a time); the
  in-world layout (icon/name/bar positions, text offsets) follows the source code and the numeric pixel checks, and the art
  was compared per asset. Condition-name text positions, drift and timing are from `CharacterStatusText.as`, not captured.
- Native glyph runs start at the field origin and Flash's start after the 2 px gutter; `StatusArt` adds 2 px (and 1 px vertical for
  the 16 pt name) measured against `SimpleText` in AIR. Name glow is a little lighter than `GlowFilter(0,1,3,3,2,1)`.
- Icon outline/glow reimplement `GlowRedrawer.outlineGlow` with a 3x3 dilation and a six-wide box blur, not Flash's filter
  code; the measured difference is at most 2 levels.
- Floating texts are drawn above speech balloons; Flash interleaves them by creation order. Sizes use the world zoom.
- Hexed and Hallucinating have pixel fixtures but no live-server evidence (no seeded item was exercised); the Flash random choice
  is replaced by a stable per-object hash. Blind and breath still cannot be produced by this server (task 10).
- `QueuedStatusText` is not reproduced: Flash maps it but nothing dispatches it. Quest-complete handling is not needed.
- `Parameters.allyNotifs` and `hpBars` default to on and are always on natively; their Options toggles belong to task 12.
- Not measured: frame time/memory impact. Memory is bounded by construction (160 labels / 8 MiB, 48 texts, 256 GPU surfaces);
  names rebuild a cache key per named entity per frame.

## Chain hand-off

`create_thread` is not available in this session, so no successor chat exists and there is no `12-options-dispatch.json`. Start
12-options from `docs/client-task-chain.md`. Facts for it: Flash `Options.as` has `Health Bars` (`hpBars`), `Particles` and `Ally
Notifications` (`allyNotifs`) choices that gate the cues above (`allyNotifs` off drops other players' notifications, level-ups
and exp text); the native bars/particles/text are currently unconditional. `Scene` draws bars and names unconditionally in the
draw lambda, and `main.cpp` queues status events from `World::take_status_events()`. Preserve the uncommitted work, sibling
repositories, models (09), lighting (10), particles, dyes, skins, authoritative stats, the teleport flag/cooldown, snapshots
and input guards. This session used `haiku-explorer` subagents for read-only source inventories at the user's direct request;
the chain's older "no subagents / GPT-6 Luna" note applies to the earlier Codex chats.
