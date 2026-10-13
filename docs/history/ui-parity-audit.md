# UI parity audit — 2026-10-13

Baseline: `realm-client` Flash source at `17bdf079452a1c470b8895eae9cf108599e7addd`.
This pass addresses missing startup and gameplay UI, in both the native and default RmlUi backends. It does not certify the entire client as a pixel-identical replacement.

## Fixed

- **Account details:** ACCOUNT now opens `WebAccountDetailDialog`'s Current account frame with username, Continue, password-change navigation, and Not you? login navigation. Previously it showed a placeholder message.
- **Password change:** the three masked fields, validation, cancel/back navigation, pending input suppression, server errors, `/account/changePassword` request, and updated saved credentials are implemented. Success requires an explicit well-formed Success reply.
- **Frame dialogs:** startup account frames use the source cut corners, centered white outline, outer shadow, and DialogsView backdrop instead of square unshadowed rectangles without a backdrop.
- **CreditDisplay:** character selection and gameplay show the original fame/coin icons and 18px values, anchored to the window edge or six units left of the sidebar. Gameplay uses account FAME_STAT, distinct from CHAR_FAME_STAT. Changes and drawable density invalidate the retained images.
- **RankText and GuildText:** gameplay draws their source-sized icons and text only when MAPINFO.show_displays is true. Changes to stars, guild name, guild rank, and drawable density update the display.
- **Character selection:** restores quest-star icons, dyed/skin portraits, the slot-purchase plus and fame icons, and the real fame purchase flow. Buying and errors use standard source dialogs; success reloads authoritative slot/currency data.
- **Character list:** restores its 16x399 scrollbar; rows render into a clipped 356x430 panel so partially visible rows remain visible. Scroll range excludes the trailing four-unit spacing. Pointer hover is recomputed after scrolling.
- **Saved-character hover:** adds the MyPlayerToolTip content: original portrait/header, HP/MP, four equipment and eight inventory slots, empty-slot numbers, tier labels, class quest history, and next goal. Character XML now retains the values needed to display it. The tooltip follows the pointer, disappears outside its row or during a dialog, caches content, and redraws at drawable density.
- **Connection rendering:** an empty world before MAPINFO no longer tries to access terrain chunk zero. The live UI fixture exposed this crash.

Source anchors: `WebAccountDetailDialog.as`, `WebAccountDetailMediator.as`, `WebChangePasswordDialog.as`, `WebChangePasswordMediator.as`, `WebChangePasswordTask.as`, `Frame.as`, `DialogsView.as`, `CreditDisplay.as`, `RankText.as`, `GuildText.as`, `GameSprite.as`, `CharacterSelectionAndNewsScreen.as`, `CharacterRectList.as`, `CurrentCharacterRect.as`, `CreateNewCharacterRect.as`, `BuyCharacterRect.as`, `BuyCharacterSlotCommand.as`, and `MyPlayerToolTip.as`.

## Checks

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure -j6`: all 76 cases validated; failed cases were rerun after correcting header ownership/layout assertions and completing the client link.
- `python3 tests/startup_parity_gpu_tests.py`: disposable HTTP peers, separate temporary accounts/settings, seven scenarios per backend. Checks password validation/success/rejection and persistence, logout, slot purchase and insufficient funds, saved-character hover/movement/dismissal/2x resizing, and partially visible scrolled rows.
- `python3 tests/hud_overlay_gpu_tests.py`: independent TCP peer, both backends, showDisplays true/false, changed account currency and rank/guild stats.
- `python3 tests/hud_overlay_reference_tests.py`: captures actual Flash CreditDisplay, RankText and GuildText. All four layouts agree within the font/layout tolerance. Currency bounds match exactly; rank bounds differ by less than one unit, and guild text bounds differ by one unit. Image canvases include shadow padding separately from the unfiltered Flash layout bounds.

Captures and full logs are under `build/startup-parity`, `build/hud-overlay-gpu`, `build/hud-overlay-reference`, and `build/ui-parity-*.log`. The HTTP suite passed all 14 scenarios, the TCP suite all four combinations, and the direct Flash layout comparison all four fixtures. Pixel equality and comprehensive saved-tooltip Flash captures remain outside this pass; FreeType edge coverage is still an accepted rendering difference.

## Additional confirmed drift

- The class grid in `Startup::class_screen` does not reproduce Flash CharacterBox lock/progress states or its class-statistics tooltip. It currently allows selecting every imported class.
- Character selection news renders text but omits NewsLine icons and link interaction.
- Item tooltips still deliberately use Astrum EquipmentToolTip styling, as documented in README, rather than the current realm-client EquipmentToolTip baseline. This is a separate existing appearance deviation.
- The map editor button still opens a C++ availability message.
- The saved-character tooltip has no comprehensive direct Flash image fixture yet; ability dimming and ItemData-dependent appearances need a dedicated source comparison.

These are follow-up parity findings, not claims that all unlisted UI is equivalent.
