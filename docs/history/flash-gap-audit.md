# Flash gap audit (2026-10-13)

This audit lists user-visible Flash features that are missing from or broken in realm-cpp. It started from a report that RankText, GuildText and NewCharacterScreen were missing. The refactor did not cause these gaps: the source tokens at 8aab6a5 and at 6e990af are effectively identical. These features were never ported or never worked.

Flash paths are relative to `realm-client/src`. "Branch" names a work branch. Merged branches are listed under Fixed. The others hold unbuilt work in progress, or nothing.

## Fixed

### In-game RankText and GuildText in the Nexus (merged from `fix/ingame`)

- Flash: `game/GameSprite.as` `showSafeAreaDisplays`, `ui/RankText.as`, `ui/GuildText.as`.
- Cause: the server sends `NumStars` only when it is non-zero (`Game/Entities/Player.cs`), so a zero-star account never receives stat 27. `src/core/ApplicationLoopRender.cpp` defaulted stars to -1, and that hides RankText. Flash `Player.numStars_` defaults to 0 and `guildRank_` defaults to -1. The C++ defaults now match.
- A test in `tests/HudCreditSafeAreaTests.cpp` covers the zero-star render.

### Menu account header (merged from `fix/header`)

- Flash: `screens/AccountScreen.as`, `ui/SoundIcon.as`, `ui/tooltip/RankToolTip.as`.
- RankText and GuildText reuse `render_safe_area_display`, placed at header origin (28, 0). `parse_char_list` reads `Account/Guild`. The new `ui/tooltip/RankToolTip` is shown on rank hover. The SoundIcon is drawn in `menu_frame(true)` and toggles `playSFX` and `playPewPew`. The header appears on the title, character select, classes and skins screens.
- Tested on the branch: ctest 74/74, including the new `account_header` test.
- Unverified: the guild icon may sit about 10 px too far right compared with Flash GuildText. The EditingScreen header is not done because there is no C++ editor.

### NewCharacterScreen class boxes (merged from `fix/newchar`)

- Flash: `screens/NewCharacterScreen.as`, `screens/CharacterBox.as`, `ui/tooltip/ClassToolTip.as`.
- New in `HudArt`: the box art, class-quest stars from best fame, portraits, a CreditDisplay in the chrome, and the new `ui/tooltip/ClassToolTip`. Class-screen hover now goes through `Startup`'s hover path, which fixes a stale yellow hover on the back label.
- Tested on the branch: ctest 72/73. The `options_gpu` failure passed when run alone; the first run overlapped another worktree's ctest. The `startup_parity_gpu_tests` cases also passed.
- Remaining differences: the CharacterBox idle ATTACK animation is missing (the portrait is static). Lock and unlock states are not needed: this Flash fork declares them but never uses them.
- Merging with `fix/header` caused conflicts in `StartupInput.cpp` and `ClientTargets.cmake`; both sides were kept. The merged tree builds. ctest was not re-run after the merge.

## Open gaps

Sizes: S is small, M is medium, L is large.

| # | Gap | Flash source | C++ state | Size | Branch |
|---|---|---|---|---|---|
| ~~1~~ | Done (see Fixed): menu header: RankText at (36,4), GuildText at (92,6), RankToolTip, and the SoundIcon on every menu screen | `screens/AccountScreen.as`, `ui/SoundIcon.as`; added by TitleView, CharacterSelectionAndNewsScreen, NewCharacterScreen, CharacterSkinView and EditingScreen | Only the account info at top right exists (`src/screens/StartupViewModel.cpp` ~499) | M | merged |
| ~~2~~ | Done (see Fixed): NewCharacterScreen detail: CharacterBox locked state, stars and fame, unlock cost, class tooltip, and the chrome CreditDisplay | `screens/NewCharacterScreen.as`, `screens/CharacterBox.as`, `kabam/rotmg/ui/view/NewCharacterMediator.as` | The grid shows only a portrait and class name (`StartupViewModel.cpp` ~690) | M | merged |
| 3 | PlayerTooltip hides rank for zero-star players | `objects/Player.as` ~629, `ui/tooltip/PlayerToolTip.as` | `src/ui/tooltip/PlayerTooltip.cpp` ~50 defaults `NUM_STARS_STAT` to -1 (the same bug as the Nexus fix) | S | planned, see below |
| 4 | Death explosion particles | `objects/Character.as` `explode()` → ExplosionEffect; `GameServerConnection.as` ~978 | `src/map/World.cpp` ~466 uses `explode` only for the death sound | S | planned, see below |
| 5 | Quest arrow and quest/portrait tooltips | `map/Quest.as`, `map/partyoverlay/QuestArrow.as`, `ui/tooltip/QuestToolTip.as`, `PortraitToolTip.as`; `GameServerConnection.as` ~1971 | QUESTOBJID is decoded (`src/messaging/Messages.cpp` ~102) but nothing uses it; `showQuestPortraits` is parsed but never read | M | `fix/quest` ccc8188, unbuilt |
| 6 | Guild invite panel and invite chat line | `ui/panels/GuildInvitePanel.as`; `GameServerConnection.as` ~2013 | InvitedToGuild is decoded (`Messages.cpp` ~83) but nothing uses it; `showGuildInvitePopup` is unused | S–M | `fix/social` 61eeb14, partial and unbuilt |
| 7 | Davy Jones's Locker key HUD (only shown inside the Locker) | `GameServerConnection.as` ~1072–1091 (ShowKeySignal, ShowKeyUICommand, KeysView) | GlobalNotification is decoded (`Messages.cpp` ~68) but nothing uses it | S–M | not started, see below |
| 8 | Idle warnings after 10 and 15 minutes, disconnect after 20 | `game/IdleWatcher.as` ~44–57 | No idle tracking | S | not started, see below |
| 9 | ConnectedWall, CaveWall, Sign text and Stalagmite rendering | `objects/ConnectedObject.as`, `CaveWall.as`, `ConnectedWall.as`, `Sign.as`, `Stalagmite.as` | Only class `Wall` gets geometry (`src/map/Scene.cpp` ~609); these draw as generic sprites | M | not started, see below |
| 10 | Character-select news rows: icons, hover color, link clicks (`fame:ID` and URLs) | `screens/NewsLine.as`, `appengine/SavedNewsItem.as` ~52–59 | Shows title, tagline and time only (`StartupViewModel.cpp` ~553); also noted in `ui-parity-audit.md` | S–M | not started; touches the same files as #1 and #2 |
| 11 | Tutorial overlay | `tutorial/Tutorial.as` and its step, requirement and drawing classes; started from `GameSprite.as` ~119 on map "Tutorial" | No counterpart; reached through `/tutorial` | M–L | not started |
| 12 | Map editor | `mapeditor/` (27 files); `kabam/rotmg/ui/view/TitleMediator.as` ~75 | The "editor" button opens a "not available" dialog (`src/screens/Startup.cpp` ~298) | L | not started |

## Notes for unfinished work

The agents for these items were stopped before finishing, on 2026-10-13, to avoid running many builds at once. Their findings:

### #3 and #4: tooltip stars and death explosion (no code written)

- Only `Character` defines `explode()`, and `Player` extends it. In C++ that means class_name `Character` or `Player`; `Catalog.cpp` ~225 already uses that rule for DeathSound. Flash calls it from `dropObject` when `explode_` is set and the object is a Character. It also requires `Parameters.data_.particles`.
- ExplosionEffect spawns 30 ExplosionParticle on its first update. That is the same per-particle math as the existing `Particles::hit` (`src/objects/particles/Particles.cpp` ~322–341): lifetime 200 plus up to 100 ms, z .5, velocity ±.5 × 8, and blood or palette colors. `hit()` returns early once hp reaches 0, so it cannot be reused directly.
- Plan: move `hit()`'s per-particle body into `burst(world, entity, time, count, directional, angle, speed)`. Add `Particles::explode` (30 particles, not directional). Add `WorldParticleEffects::exploded`, gated on Character or Player. Call it in the drop loop in `World::apply(const net::Update&)` (`src/map/World.cpp` ~465), before `remove`. Test with an exploding Player drop at hp 0, plus negative cases.
- Stars and guild rank: change `PlayerTooltip.cpp` :50 to default stars to 0 and :51 to default guild rank to -1, and the `PlayerTooltip.hpp` :28 member defaults to match. Also change the guild-rank defaults of 0 to -1 at `ApplicationLayersHud.cpp` :124 and :131, `ApplicationLoopUpdate.cpp` :139 and :148, `PlayerMenu.cpp` :53, and `HudState.cpp` :107. All of these are behavior-neutral for players without a guild.

### #5: quest arrow (`fix/quest` ccc8188, written but never compiled)

- Adds `map/Quest`, `map/partyoverlay/GameObjectArrow` (shared code factored out of PartyArrows and PlayerGroup), `map/partyoverlay/QuestArrow`, `ui/tooltip/QuestToolTip` and `PortraitToolTip`, Session wiring for QUESTOBJID, the "Quest Complete!" notification and MAPINFO, render and layer hookup, and `tests/QuestArrowTests.cpp`.
- Before merging: build it, then run ctest with particular attention to `party_arrows` and `player_groups`, since the factoring could break them. Then fix the WIP commit subject. The offline `--map` mode cannot show the arrow; a screenshot needs the loopback-peer harness from `tests/party_arrow_gpu_tests.py`.

### #6–#8: guild invite, keys, idle (`fix/social` 61eeb14, partial)

- Committed: the `ui/panels/GuildInvite` state (20 s timer) and view, which use the same geometry as the trade request panel. JoinGuild already exists as an outgoing packet. Not yet wired: `Session::join_guild`, the INVITEDTOGUILD consumer gated on `showGuildInvitePopup` plus its chat line, the layer, the render, tests, and closing the invite when a trade request arrives (Flash's override slot keeps the latest one).
- Keys: the art is `kabam/rotmg/ui/view/KeysView_*Png.png` (176×36 background; 32×12 keys at x = 12 + 40i, y = 12). The server sends GlobalNotification `showKeyUI` when a player enters "Davy Jones's Locker" (`realm-server/Game/World.cs` ~625). Key pickups light keys through the `yellow`, `red`, `green` and `purple` notifications.
- Keys decision (user, 2026-10-13): show the key display only while the player is in Davy Jones's Locker. Show it on `showKeyUI`. Hide it and clear the lit keys on any map change away from the Locker (MAPINFO name is not "Davy Jones's Locker"), on death, and when the game closes. This intentionally differs from Flash, where KeysView lives on contextView and persists until GameClosedSignal. A draft `include/realm/hud/KeysView.hpp` is on `fix/social` (9248f39).
- Idle: Flash resets the timer only on MOUSE_MOVE and KEY_DOWN, and the dt clamp of 100 ms matches C++ `min(dt, .1)`. It warns in chat at 10 and 15 minutes, using the exact text from `IdleWatcher.as`, and disconnects at 20 minutes (`IdleWatcher.as` 60–64).
- Idle decision (user, 2026-10-13): port the 20-minute disconnect too, matching Flash. Planned shape: a headless `realm::IdleWatcher` in realm_game, reset from event dispatch, ticked in `update_session` while a player exists. Warnings use the error chat name, and reaching 20 minutes closes the game the way Flash does.

### #9: walls, signs and stalagmites (no code written)

- Data is in `kabam/rotmg/assets/EmbeddedData_StaticObjectsCXML.dat`: 8 CaveWall, 4 ConnectedWall, 2 Sign (Size 20) and 1 Stalagmite. The Tomb Wall uses Dot, ShortLine, L, Line, T and Cross textures plus NoWallTextureRepeat. Those nodes are bare File/Index elements, so `Catalog.cpp` needs a direct parse.
- The connection value comes from the server as CONNECT_STAT (`realm-server/Game/Entities/ConnectedObject.cs`), one byte per side: N, E, S, W from b3 to b0. Flash registers a rotation only when its pattern is new.
- Copy the software render path, since GPURender defaults to false. Each face is mapped affinely from its first, second and last vertices. Shade = 0.75 + 0.25·max(0, n·(1,3,2)/√14). A side face whose neighbor tile has no ground is drawn black. CaveWall quads split into two triangles along a random diagonal.
- Obstacle: the Quad stream only supports parallelograms. Trapezoids need the triangle path (kind 9 MeshDraw); measure its cost per object.
- Sign: rasterize with `FontBook::rasterize_centered` (white, 24 px, bold, '|' as a newline), scaled 5·size/100. The stress fixtures contain Signs, so their quad checksums will change.
- Stalagmite: the GPU path draws nothing, but the software path draws it.

## Intentionally skipped or dead in Flash

- MoneyChanger has no panel in this Flash fork (`progress.md`).
- QueuedStatusText is never dispatched (`flash-specs.md`).
- The guest "Please REGISTER" flow is skipped because the server requires accounts.
- Never instantiated in Flash: NotEnoughFameDialog, CharacterSlotNeedGoldDialog, CharacterSlotRegisterDialog, ChooseNameRegisterDialog, RegisterWebAccountDialog, AccountInfoView, TimerDisplay, GiftChest, UnpackEmbed, DebugDialog, GameObjectArrow and ThrownObject.

## Notes

- `ui-parity-audit.md` says Astrum item-tooltip styling is documented in the README. The README has no such text. The deviation is real: `src/ui/tooltip/ItemTooltip.cpp` ~102 uses `astrumtipicon`.
- Checked and working: Legends tabs, Nexus HUD, fame ScoringBox, class-detail stats, guild board editing, character deletion, ShowEffect types 1–16, PartyPanel, and the Options tabs.
