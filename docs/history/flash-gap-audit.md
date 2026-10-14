# Flash gap audit (2026-10-13)

This audit lists user-visible Flash features that are missing from or broken in realm-cpp. It started from a report that RankText, GuildText and NewCharacterScreen were missing. The refactor did not cause these gaps: the source tokens at 8aab6a5 and at 6e990af are effectively identical. These features were never ported or never worked.

Flash paths are relative to `realm-client/src`. "Branch" names a local work branch. Branches other than `fix/ingame` were still in progress when this was written and are not merged.

## Fixed

### In-game RankText and GuildText in the Nexus (merged from `fix/ingame`)

- Flash: `game/GameSprite.as` `showSafeAreaDisplays`, `ui/RankText.as`, `ui/GuildText.as`.
- Cause: the server sends `NumStars` only when it is non-zero (`Game/Entities/Player.cs`), so a zero-star account never receives stat 27. `src/core/ApplicationLoopRender.cpp` defaulted stars to -1, and that hides RankText. Flash `Player.numStars_` defaults to 0 and `guildRank_` defaults to -1. The C++ defaults now match.
- A test in `tests/HudCreditSafeAreaTests.cpp` covers the zero-star render.

## Open gaps

Sizes: S is small, M is medium, L is large.

| # | Gap | Flash source | C++ state | Size | Branch |
|---|---|---|---|---|---|
| 1 | Menu header: RankText at (36,4), GuildText at (92,6), RankToolTip, and the SoundIcon on every menu screen | `screens/AccountScreen.as`, `ui/SoundIcon.as`; added by TitleView, CharacterSelectionAndNewsScreen, NewCharacterScreen, CharacterSkinView and EditingScreen | Only the account info at top right exists (`src/screens/StartupViewModel.cpp` ~499) | M | `fix/header` |
| 2 | NewCharacterScreen detail: CharacterBox locked state, stars and fame, unlock cost, class tooltip, and the chrome CreditDisplay | `screens/NewCharacterScreen.as`, `screens/CharacterBox.as`, `kabam/rotmg/ui/view/NewCharacterMediator.as` | The grid shows only a portrait and class name (`StartupViewModel.cpp` ~690) | M | `fix/newchar` |
| 3 | PlayerTooltip hides rank for zero-star players | `objects/Player.as` ~629, `ui/tooltip/PlayerToolTip.as` | `src/ui/tooltip/PlayerTooltip.cpp` ~50 defaults `NUM_STARS_STAT` to -1 (the same bug as the Nexus fix) | S | `fix/explode` |
| 4 | Death explosion particles | `objects/Character.as` `explode()` → ExplosionEffect; `GameServerConnection.as` ~978 | `src/map/World.cpp` ~466 uses `explode` only for the death sound | S | `fix/explode` |
| 5 | Quest arrow and quest/portrait tooltips | `map/Quest.as`, `map/partyoverlay/QuestArrow.as`, `ui/tooltip/QuestToolTip.as`, `PortraitToolTip.as`; `GameServerConnection.as` ~1971 | QUESTOBJID is decoded (`src/messaging/Messages.cpp` ~102) but nothing uses it; `showQuestPortraits` is parsed but never read | M | `fix/quest` |
| 6 | Guild invite panel and invite chat line | `ui/panels/GuildInvitePanel.as`; `GameServerConnection.as` ~2013 | InvitedToGuild is decoded (`Messages.cpp` ~83) but nothing uses it; `showGuildInvitePopup` is unused | S–M | `fix/social` |
| 7 | Davy Jones's Locker key HUD | `GameServerConnection.as` ~1072–1091 (ShowKeySignal, ShowKeyUICommand, KeysView) | GlobalNotification is decoded (`Messages.cpp` ~68) but nothing uses it | S–M | `fix/social` |
| 8 | Idle warnings after 10 and 15 minutes | `game/IdleWatcher.as` ~44–57 | No idle tracking | S | `fix/social` |
| 9 | ConnectedWall, CaveWall, Sign text and Stalagmite rendering | `objects/ConnectedObject.as`, `CaveWall.as`, `ConnectedWall.as`, `Sign.as`, `Stalagmite.as` | Only class `Wall` gets geometry (`src/map/Scene.cpp` ~609); these draw as generic sprites | M | `fix/walls` |
| 10 | Character-select news rows: icons, hover color, link clicks (`fame:ID` and URLs) | `screens/NewsLine.as`, `appengine/SavedNewsItem.as` ~52–59 | Shows title, tagline and time only (`StartupViewModel.cpp` ~553); also noted in `ui-parity-audit.md` | S–M | not started; touches the same files as #1 and #2 |
| 11 | Tutorial overlay | `tutorial/Tutorial.as` and its step, requirement and drawing classes; started from `GameSprite.as` ~119 on map "Tutorial" | No counterpart; reached through `/tutorial` | M–L | not started |
| 12 | Map editor | `mapeditor/` (27 files); `kabam/rotmg/ui/view/TitleMediator.as` ~75 | The "editor" button opens a "not available" dialog (`src/screens/Startup.cpp` ~298) | L | not started |

## Intentionally skipped or dead in Flash

- MoneyChanger has no panel in this Flash fork (`progress.md`).
- QueuedStatusText is never dispatched (`flash-specs.md`).
- The guest "Please REGISTER" flow is skipped because the server requires accounts.
- Never instantiated in Flash: NotEnoughFameDialog, CharacterSlotNeedGoldDialog, CharacterSlotRegisterDialog, ChooseNameRegisterDialog, RegisterWebAccountDialog, AccountInfoView, TimerDisplay, GiftChest, UnpackEmbed, DebugDialog, GameObjectArrow and ThrownObject.

## Notes

- `ui-parity-audit.md` says Astrum item-tooltip styling is documented in the README. The README has no such text. The deviation is real: `src/ui/tooltip/ItemTooltip.cpp` ~102 uses `astrumtipicon`.
- Checked and working: Legends tabs, Nexus HUD, fame ScoringBox, class-detail stats, guild board editing, character deletion, ShowEffect types 1–16, PartyPanel, and the Options tabs.
