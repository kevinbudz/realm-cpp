# 12-options — complete

Implemented and verified 2026-10-11. Next: **13-death** (not started; no successor chat was created from this session, see "Chain hand-off").

## Scope and findings

Flash's `Options` screen (`ui/options/*`) has six tabs of KeyMapper and ChoiceOption rows over `Parameters.data_` (spec: `docs/flash-specs.md`,
"Options screen"). Before this task every control was a hard-coded SDL key in `main.cpp`, there was no settings file, and the flags the screen
controls (Health Bars, Particles, Ally Shots/Damage/Notifications, ...) were unconditional or unused (`Particles::set_enabled` had no caller).
Flash reaches the screen only in game: the `O` key and, in the Nexus, the HUD's Options button (every other map shows a Nexus button there);
it has no entry from the title or character screens, and neither does the native client.

## Implementation

- `include/realm/settings.hpp`, `src/settings.cpp` (realm_data): the option table in Flash's add order (labels, tooltips, choice lists, spacers,
  disabled-by), `Settings` (Flash key codes and choice values by Flash parameter name; `set_key` unsets the previous owner exactly as
  `Parameters.setKey`, including the row-less `escapeToNexus2`; `reset_tab` resets only the visible tab as Flash does), JSON persistence with an
  atomic write, validation of every loaded value (a corrupt file yields the defaults; unknown names and invalid values are dropped), and
  `Preferences`, the flag set the game systems read. `KeyCodes.CharCodeStrings` names are included.
- `include/realm/options.hpp`, `src/options.cpp` (realm_assets): `OptionsScreen`, a full-window retained bitmap in the 800x600 design space like the
  guild screens (shared `ui_menu_backdrop`): tabs, grid, key boxes with "[Hit Key]" blinking every 400 ms, choice boxes, disabled Rotate rows,
  label tooltips, continue / reset to defaults / back to home, press-and-release click semantics, focus loss and resize handling.
- `include/realm/text_tooltip.hpp`, `src/text_tooltip.cpp`: `TextToolTip` and `ToolTip.position()` (used by the option tooltips and the HUD button).
- `include/realm/sdl_keys.hpp`: SDL <-> Flash key codes and held-key polling by binding.
- Game integration:
  - `main.cpp`: every keyboard action is the key its row binds (move, rotate, ability, autofire, reset camera angle, centering, performance
    stats, interact, potions, inventory slots, mini-map zoom, escape to Nexus, options, switch tabs, fullscreen, and the chat keys: open, command, tell,
    guild chat, scroll). The Options screen is a modal above the HUD (below dialogs): it consumes keys and mouse and suppresses movement, shooting
    and abilities. Settings are loaded at startup (`--settings-file`, default beside the account file; the harness keeps defaults unless a file or
    account file is named), applied after each change (VSync, window mode, cursor, max FPS, world flags) and saved immediately; the camera angle is
    saved on exit.
  - `World::preferences` (+`set_preferences`, `allows_notification`): hp bars and shadows (`Scene`), projectile outline (non-outlined projectiles use
    the plain texture), Ally Damage (`report_damage`), Ally Notifications (NOTIFICATION, level/EXP/Fame text and level-up particles of other players),
    Ally Shots (`Combat`: SERVERPLAYERSHOOT of others, ALLYSHOOT), Particles (`Particles::set_enabled` now gates exactly Flash's optional effects: hits,
    wall impacts, trails, Healing, object-texture effects; ShowEffect, AoE and level-up always play), Show Tier Tag (`HudState::inventory.tiers`),
    Draw Text Bubbles and Show Trade Popup (`main.cpp`).
  - HUD: `HudState::Header` draws CharacterDetailsView's IconButton (Options in the Nexus, `game_id() == -1`; Nexus elsewhere, which escapes) from four new baked
    records (`iconbutton-options|nexus[hover]`, exported from the real `IconButton` by `PlayerTooltipExporter`'s bake task; pack rebaked with all 2,376 earlier
    records byte-identical, previous pack `build/hud-art-before-options`), hover tint and the "Hotkey: KEY" tooltip.
  - Cursor (`CursorSet`): the 16 cursors of `cursorsEmbed` (32x32, hot spot 15,15) read from the Flash source tree; default 4 as in Flash.
  - Performance stats: a top-left panel (FPS, frame and submit times, draws, resident memory on Linux).

## Verification

- Build/CTest: release `build/` **42/42**, sanitizer `build-sanitize/` **38/38** (the viewer-only `options_gpu`, `status_gpu`, `sdl_keys` need the viewer,
  which the sanitize preset disables). New: `options` (127 checks: defaults, conflicts, persistence, corrupt files, every screen interaction), `preferences`
  (24), `sdl_keys` (141), `options_source`, `options_gpu`. Logs: `build/options-ctest-release.log`, `build/options-ctest-sanitize.log`,
  `build/options-sanitize-build.log` (no warnings from the new code).
- Source conformance: `tests/options_source_tests.py` parses `Options.as`, `Parameters.as` and `KeyCodes.as`: 57 rows (labels, tooltips, choice labels/values,
  order, spacers, disabled-by, defaults) and 35 key defaults match the native table. The only omitted row is Hardware Acceleration.
- Real Flash reference: `python3 tests/options_reference_tests.py` (AIR; `tools/flash-reference/OptionsExporter.as` instantiates the real `Options`,
  clicks tabs and hovers through the real classes, and resizes the window) compares 21 frames (all six tabs, changed values, rotation on, hover states,
  option tooltips, 1280x720, 1600x1200, 1920x1080, 1000x600) and 5 TextToolTips. Grid geometry identical; 800x600 frames mean difference 1.5-2.5/255, hover cases
  identical to their base frames (448 / 202 / 1,706 hover pixels in both); resized frames 1.6-8.4 (glyph edges and one pixel placement); tooltip frames
  1.0-2.4. Output and side-by-sides: `build/options-reference/{flash,native,comparison-*.png}`.
- Viewer, real input path: `python3 tests/options_gpu_tests.py` (hidden window, scripted clicks/keys/holds): the HUD button opens the screen, hover tints it and shows the
  tooltip, continue/Escape/O close it with the world untouched, a rebound Options key opens it, rebinding Move Right to J through the screen moves the player and
  D stops working (also after reloading the saved file), rotation is off by default and gates Q, greyed rows ignore clicks, Health Bars removes exactly the 42x8 bar
  and survives a restart, Draw Shadows, reset affects only the visible tab, the performance panel, resize with the screen open.
- Real server: `python3 tests/options_live_tests.py ../realm-server/bin/Release/net8.0/RotMG.dll` (isolated server, disposable account, the real viewer through a packet
  counting proxy): autofire shoots only on the configured key (PLAYERSHOOT 7 / 0 / 7), the open screen eats keys (0 shots), a seeded potion is used only on
  the bound inventory key (also bound through the screen), a rebound movement key keeps the session healthy, and the HUD Nexus button / Insert send ESCAPE
  exactly once outside the Nexus (Realm), while in the Nexus that click opens Options and sends none. Log: `build/options-live/live-release.log`.
- Regression runs after the change: dialog, guild board, guild members, skin, party arrow, player group, particle, dye and resize GPU suites and the
  inventory/social/tooltip/button/portal/potion/container/stats HUD suites pass (`build/options-regress-summary.log`). `sellable_hud_tests`, `hud_tests`, `minimap_tests` and `font_gpu_tests` still report exact-pixel Flash differences (sellable panel bounds around x 124-143, y 266-284,
  panel text, minimap frames, glyph edges). Their fixtures build `HudState` without a header button or only compare regions that the header button (HUD x 160-200, y 190-230) does not
  touch, and the code they exercise was not edited, so they are not caused by this task; I did not run them before the change to prove it. They are optional diagnostics per `docs/port-spec.md`.
- Captures: `build/options/{hud,hud-hover,controls,gameplay}-{800x600,1280x720,1920x1080}.png`, `build/options-gpu/*.png`.

## Behavior changes to know about

- Defaults now follow Flash: Insert and F5 escape to Nexus (was R); R resets the camera angle; Q/E rotate only with Allow Camera Rotation (off by default);
  `=`/`-` (not numpad +/-) zoom the minimap; `0` is the interact key; `C` no longer toggles centering in the client (the Toggle Centering row is unset by
  default; `C` remains in the development gallery).
- The Particles option no longer clears live particles when switched off and no longer stops existing object emitters (Flash leaves existing
  ParticleEffects alone); ShowEffect/AoE effects are not gated.

## Differences and limitations (honest)

- Escape cancels a key capture and closes the screen (Flash binds Escape as an ordinary key and uses it to leave fullscreen); native Escape in game leaves the game, so
  it is not rebindable here.
- The performance panel is a compact text panel, not Flash's `FrameProfilerView` graph.
- Stored but without a native counterpart yet: Show Quest Portraits (no quest arrow in the native client), Show Guild Invite Panel (the native client does not process
  INVITEDTOGUILD at all), Quality (Flash only changes UI vector quality), Play Sound Effects / Play Weapon Sounds (no audio until task 16, which should read
  `Preferences::play_sfx` / `play_pew_pew`). Hardware Acceleration is omitted (the renderer is always the GPU path).
- Text at resized window sizes is redrawn at drawable resolution, so glyph edges and an occasional one pixel offset differ from Flash (accepted by the port contract).
  Option boxes use the same shadow model as the other screens; the half-alpha 12 px TitleMenuOption shadow is approximated.
- The header button and tooltip were compared through the baked art and source geometry, not a side-by-side full HUD capture of Flash (the HUD reference exporters do not
  include CharacterDetailsView).
- Not verified: the OS cursors (hidden windows have none; the cursor-creation path ran without error), fullscreen switching, Max FPS/VSync timing, and the sanitizer
  build against the viewer (it is disabled in that preset).
- No frame time/memory measurement. The Options bitmap is one retained texture rebuilt on interaction (about 8 MiB at 1080p), only while open.

## Chain hand-off

`create_thread` is not available in this session, so no successor chat exists and there is no `13-death-dispatch.json`. Start 13-death from
`docs/client-task-chain.md`. Facts for it: the settings system is `realm::Settings`/`OptionsScreen` (`docs/flash-specs.md`, "Options screen"); `Session::game_id()`
exists; `SessionState::dead` is entered on the DEATH packet and `main.cpp` returns to the character screen with "Your character has died." (no death/fame screen
exists yet). Preserve the uncommitted work, sibling repositories, the hud-art pack (4 new records; backups `build/hud-art-before-options`, earlier packs) and the
input guards. This session used `haiku-explorer` subagents for read-only source inventories (CLAUDE.md delegation).
