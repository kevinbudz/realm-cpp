# Flash gap audit (2026-10-13)

This audit tracks the user-visible gaps between `realm-cpp` and the sibling Flash client. The original report
concerned RankText, GuildText and NewCharacterScreen. Those features were absent before the source-layout refactor;
they were not regressions introduced by it. The follow-up implementation ports the remaining gameplay, social,
rendering, startup, tutorial and editor features. All active gaps in this audit are now closed.

Flash paths below are relative to `realm-client/src/com/company/assembleegameclient`, unless qualified otherwise.
The old `origin/fix/quest` and `origin/fix/social` branches supplied unfinished starting points. Their relevant code
was integrated into the current tree, repaired and tested while preserving the already-completed menu work.

## Coverage

| # | Gap | Result | Regression coverage |
|---|---|---|---|
| — | Nexus RankText and GuildText | Zero-star accounts default to 0 stars; an absent guild rank defaults to -1. | `hud_credit_safe_area` |
| 1 | Menu account header | RankText, GuildText, RankToolTip and SoundIcon remain on title, character, class and skin screens; the editor uses the same header. | `account_header`, startup GPU cases |
| 2 | NewCharacterScreen class boxes | Class box art, best-fame stars, class tooltip and chrome CreditDisplay are present. Lock/unlock states are unused in this Flash fork. | `account_header`, startup GPU cases |
| 3 | Zero-star player tooltip | Missing NUM_STARS_STAT now produces a visible zero-star rank. Guild-rank defaults also match Player.as. | `player_tooltips` |
| 4 | Death explosion particles | Exploding Character/Player drops emit 30 blood/palette particles before removal, including at zero HP; particle settings and non-character exclusions apply. | `particles` |
| 5 | Quest arrow and tooltips | QUESTOBJID, new-target/completion delays, edge arrows, hover freeze, QuestToolTip and optional PortraitToolTip are wired through Session and the application. Enemy portraits use GameObject portrait rendering. | `quest_arrows`, `quest_session_inputs`, `party_arrows`, `player_groups`, `flash_gap_gpu` |
| 6 | Guild invitations | INVITEDTOGUILD creates the chat line and optional 20-second accept/reject panel; Accept sends JOINGUILD. Trade requests and invitations share the HUD override slot. | `social_hud`, `quest_session_inputs`, `flash_gap_gpu` |
| 7 | Davy Jones's Locker keys | showKeyUI opens the panel only in the Locker; color notifications light its keys. Map exit, death and game close clear it. | `social_hud`, `flash_gap_gpu` |
| 8 | Idle watcher | Mouse movement/key-down reset idle time; warnings occur at 10/15 minutes and the session disconnects at 20 minutes, with Flash's 100 ms frame clamp. | `idle_watcher` |
| 9 | ConnectedWall, CaveWall, Sign and Stalagmite | CONNECT_STAT patterns/rotations, cap textures, wall dimensions, boundary black faces, cave diagonals, lighting, texture repetition, multiline text and stalagmite geometry render natively. Offline scenes infer connections from neighboring objects. | `models`, `connected_projection`, `model_instances`, `status_rules` |
| 10 | Character-select news | Original fame/Oryx/forum icons, row hover color, fame:ID navigation/back and HTTP(S) browser links are present. | `startup_news_gpu` |
| 11 | Tutorial overlay | The embedded tutorial script activates animated HUD boxes/arrows when the player is near the specified named signs, loot or portal; map changes reset animation state. | `tutorial`, `flash_gap_gpu` |
| 12 | Map editor | The title-screen editor has ground/object/region palettes with original artwork, draw/erase/sample/name tools, rectangle edits, filter/tooltips, pan/zoom, undo/redo, JM load/save and existing-character map testing. Disconnect, death and reconnect return to the preserved editor document. | `map_document`, `editor_screen`, `editor_session`, `editor_gpu` |

## Behavior decisions and source findings

- **Locker keys:** keep the user's 2026-10-13 decision: the panel belongs only to Davy Jones's Locker. It opens on
  `showKeyUI`, ignores color signals before opening, and clears on a map change away from the Locker, death or game
  close. This intentionally differs from Flash's context-wide persistence.
- **Idle disconnect:** keep the user's 2026-10-13 decision to port the 20-minute disconnect, in addition to both
  warnings. Only mouse movement and key-down reset it; clicks and the wheel alone do not.
- **CharacterBox idle attack:** `CharacterBox.as` defines `onEnterFrame`, but neither its constructor nor
  `NewCharacterScreen.as` registers it for ENTER_FRAME. The static portrait matches this fork's actual behavior;
  this is inactive Flash code, not an outstanding native gap.
- **Tutorial:** the shipped `EmbeddedData_TutorialScriptCXML.dat` contains six proximity steps. `Step.as` does not
  populate its action/text fields, and `Tutorial.draw()` is empty. The active feature is the simultaneous
  proximity-driven highlights in `onEnterFrame`, which are ported rather than inventing a sequential tutorial.
- **News URLs:** external navigation accepts HTTP(S); fame links request `/char/fame` for the selected account and
  return to character selection. Malformed fame IDs and unsupported URI schemes do not launch anything.
- **Rendering:** static randomized shapes are deterministic per object, so they do not jitter or destabilize
  fixtures. Connected geometry uses a bounded 4096-entry Scene cache, larger than a full screen of walls so it
  never cycles. A ConnectedWall cross has at most 78 expanded triangle vertices in two mesh commands; a CaveWall
  cross has at most 42 in one; a Stalagmite has nine in one.
  Projection culls hidden faces before submission. These are primitive-count bounds, not frame-time guarantees.
  The mesh path preserves Flash software rendering's affine mapping from the first, second and last face vertices;
  cave split triangles retain independent lighting. Nonrepeating bitmap fills clamp in the fragment shader.
- **Editor files:** load/save use in-client path dialogs rather than Flash FileReference. The canvas remains 256 by 256 tiles. Map testing requires a registered account with a saved character, matching the Flash editor's existing-character workflow. Input cancellation, UTF-8 name deletion, palette filtering, modal focus loss, right-drag erase and resize have regression tests.
- **Map import:** JM object names are preserved so imported signs display their text. Editor export uses a cropped
  Flash JM dictionary with big-endian signed-short indices, zlib and base64. Invalid loads preserve the document.

## Validation

- `cmake --preset release` and `cmake --build --preset release -j 8`: passed.
- `ctest --preset release`: **86/86 passed** (67 unit tests and 19 GPU tests).
- `cmake --preset sanitize` and `cmake --build --preset sanitize -j 6`: passed.
- `ctest --preset sanitize`: **66/66 passed** with AddressSanitizer and UBSan.
- `git diff --check`: passed. Changed C++ files were formatted with the repository's clang-format configuration.

The GPU tests use isolated loopback HTTP/game peers. `editor_gpu` verifies the actual Startup/RmlUi route,
paint/undo/redo, file save/load, HELLO's editor game ID and map JSON, the existing-character LOAD packet,
RECONNECT return with the document preserved, and resize. `flash_gap_gpu` verifies invitation accept/reject,
expiry, disabled popups, trade override, Locker show/light/map-exit/death reset, enemy quest tooltips, and Tutorial
sign proximity. `startup_news_gpu` verifies icons, hover and fame navigation/back.

Headless checks cover zero-star defaults, fatal drop particles and exclusions, tutorial timing, JM roundtrips
through the actual World parser, editor input/focus/gesture regressions, connected-face affine UVs, culling,
lighting, boundaries, quest packet state, invitations, keys and every idle threshold. `model_instances` also
verifies nonrepeating versus tiled mesh sampling on the GPU.

Captures are retained under `build/editor-gpu/`, `build/flash-gap-gpu/` and `build/startup-parity/news/`.
The manual `build/flash-gap-gpu/world-rendering.png` capture shows joined cave/Tomb walls, stalagmites and multiline
signs in an offline JM scene. Tests did not require or modify a production game server.

## Intentionally skipped or inactive in Flash

- MoneyChanger has no panel in this Flash fork (`progress.md`).
- QueuedStatusText is never dispatched (`flash-specs.md`).
- The guest "Please REGISTER" flow is skipped because the server requires accounts.
- Never instantiated in Flash: NotEnoughFameDialog, CharacterSlotNeedGoldDialog, CharacterSlotRegisterDialog,
  ChooseNameRegisterDialog, RegisterWebAccountDialog, AccountInfoView, TimerDisplay, GiftChest, UnpackEmbed,
  DebugDialog, GameObjectArrow and ThrownObject. GameObjectArrow's geometry helpers are now reused by active arrows.
- Astrum item-tooltip styling remains an intentional deviation (`src/ui/tooltip/ItemTooltip.cpp` uses
  `astrumtipicon`), now documented in the README.

Previously checked features remain covered by the existing suites: Legends tabs, Nexus HUD, fame ScoringBox,
class-detail stats, guild-board editing, character deletion, ShowEffect types 1–16, party panels and Options tabs.
