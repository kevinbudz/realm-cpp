# 04-minimap-menu: complete

Completed 2026-10-10 in the existing local checkout. Next task:
**05-party-arrows**. Sequential-chat authorization and dispatch procedure remain
in `docs/client-task-chain.md`. No subagents, automations, commits or resets
were used. Existing uncommitted/untracked work, Flash/server sources, font/HUD
packs and backups (including the guild-members supplement) were preserved.

## Implementation and source behavior

`Minimap::hover_players` projects players through the current zoom and map-edge
translation, ray-clamps off-window markers to the same 192px square as drawing,
and groups them at a strict distance squared <25. Local, non-player and
NoMiniMap objects are excluded. The result preserves current World entity
encounter order, without ID/distance/eligibility sorting. A menu freezes that
ordered list of IDs and row data; movable/dropped entities leave no dangling
pointers. Teleport resolves each ID against current authoritative state and
selects the first remaining visible player. An all-invisible/removed group
produces *Error* "No players are eligible for teleporting" and no request.

`include/realm/player_group.hpp` / `src/player_group.cpp` implement reusable
PlayerGroupToolTip and PlayerGroupMenu surfaces. The common long-version
GameObjectListItem label helper now serves PlayerMenu and PlayerGroup; existing
class portraits and normal/hover/inactive teleport icon art are reused. Tooltips
have the half-alpha source fill, cut frame, white outline, halo, 32px rows and
centered click hint. Menus have the opaque frame, optional Teleport at (8,8),
source-width diamond divider at (6,40), and passive rows at y52 or y4. The
44px portrait bounds and negative teleport icon top are included in frame size.
Short/unnamed rows retain the source footer-centering behavior.

Position follows Menu/ToolTip's left-half/top-third rule, with a 12px minimum.
Menus close on rollout or distance >40px. Press/release must both hit Teleport;
row/frame clicks do nothing. Cooldown clicks still close the menu and produce
Session's refusal chat, as in Flash. Inactive icon/text and the progressively
revealed white label match TeleportMenuOption (whose bar has no separate shadow).
Text redraws at drawable density. Hover movement and unchanged data/cooldown
pixels reuse the image; resizing retains an open menu and cancels stale presses.

Dense groups keep every target in the ordered list, while their surface reserves
at most 644 logical pixels vertically: the visible 600px window plus shadow
padding. Oversized source groups extend below the window and have no scrollbar;
native clips the same invisible rows rather than allocating arbitrarily tall
GPU textures. Unit coverage includes 200 rows. No gameplay list cap is added.

`src/main.cpp` routes group menu input above the HUD, opens it on a minimap
click, suppresses hover while it is open, excludes its frame/held press from
shooting and abilities, and clears decorations for modals/map/reconnect/death/
disconnect. Zoom controls remain functional. Minimap and party menus replace
one another. Window/focus transitions cancel presses and hide stale tooltips.
Runtime remains C++/SDL/OpenGL; AIR is an offline reference tool only.

Session's send boundary now enforces MAPINFO.allow_teleport as well as its
existing ten-second cooldown and target visibility. It also rejects self and
non-player/missing targets. The UI omits Teleport on restricted maps; an attempted
direct call returns "Teleporting is not allowed in this map." without consuming
cooldown. This server enforces neither the map flag nor the client cooldown;
server code and protocol remain unchanged.

## Authoritative sources

Paths under `realm-client/src`:

- `kabam/rotmg/minimap/view/{MiniMap,MiniMapZoomButtons}.as`
- `com/company/assembleegameclient/ui/tooltip/{PlayerGroupToolTip,ToolTip}.as`
- `com/company/assembleegameclient/ui/menu/{PlayerGroupMenu,Menu,MenuOption,TeleportMenuOption}.as`
- `com/company/assembleegameclient/ui/{GameObjectListItem,LineBreakDesign}.as`
- `com/company/assembleegameclient/objects/Player.as` (isTeleportEligible,
  teleportTo, msUtilTeleport and MS_BETWEEN_TELEPORT)
- `kabam/rotmg/ui/PlayerGroupMenuMediator.as` (unable-to-teleport chat)

Server: `Networking/GameServer.Social.cs`, `Game/Entities/Player.Guild.cs`,
`Game/Manager.cs` and `Resources/Worlds/Worlds.xml`. GuildHall actually advertises
AllowTeleport=false. Flash revision `17bdf079452a1c470b8895eae9cf108599e7addd`;
server revision `bb75bc1e438b465986a8ada354b6332f69e0e3a6`.

## Verification

From `/home/kevin/Desktop/RotMG/realm-cpp`:

```sh
cmake --preset release
cmake --build --preset release -j 4
ctest --preset release --output-on-failure
cmake --preset sanitize
cmake --build --preset sanitize -j 4
ctest --preset sanitize --output-on-failure
python3 tests/player_group_reference_tests.py
python3 tests/player_group_gpu_tests.py
python3 tests/player_group_live_tests.py ../realm-server/bin/Release/net8.0/RotMG.dll
python3 tests/player_group_live_tests.py ../realm-server/bin/Release/net8.0/RotMG.dll --client build-sanitize/realm_teleport_session_inputs
python3 tests/ui_resize_gpu_tests.py
python3 tests/dialog_gpu_tests.py
python3 tests/guild_board_gpu_tests.py
python3 tests/guild_members_gpu_tests.py
python3 tests/live_server_tests.py ../realm-server/bin/Release/net8.0/RotMG.dll
git diff --check
```

- Final **Release 27/27** (17.13s) and **ASan/UBSan 27/27** (39.73s) pass.
  `player_groups` has **35 checks**: strict radius, NoMiniMap/self exclusion,
  map translation/zoom, edge direction/grouping, unsorted order, dropped and
  invisible target resolution, passive rows, cooldown redraw reuse, pointer
  cancellation, position/rollout/distance, resized text and dense surface bounds.
- New independent `teleport_loopback` checks exact TELEPORT bodies, visibility
  updates, cooldown refusal/expiry, GOTO/GOTOACK and a valid target on a map that
  forbids teleport. No refused request is sent. Both presets run it.
- **21 real Flash/native fixtures** have identical measured frame dimensions
  and were reviewed side by side: one/grouped/unnamed tooltips, menu, hover,
  full/mid cooldown, no-teleport menu, and 1.2/2/3.6 densities. Exporter uses
  unmodified PlayerGroupToolTip/PlayerGroupMenu/TeleportMenuOption; only an offline
  Player subclass supplies a fixed msUtilTeleport value. Sibling sources stay clean.
- Actual SDL/TCP/OpenGL fixture checks pass for grouped/edge hover, minimap
  click, menu/row picking, first visible target, cooldown refusal and expiry,
  all-invisible error, restricted-map passive menu, rollout and 720p/1200p/4K
  resize. Exactly two TELEPORT requests target the expected second then first
  player after live condition changes. There is exactly one legitimate MOVE
  reply to the injected NEWTICK and no shot/ability/social packet leakage.
  Native captures also show the exact no-eligible and cooldown refusal chat.
- Isolated **real-server Release and sanitizer** clients each pass staggered
  player-arrival visibility, ordered selection, immediate cooldown refusal,
  expiry/second teleport and two authoritative GOTO/GOTOACK pairs. Wire observers
  verify exactly two TELEPORTs. A separate native session in the real GuildHall
  receives its false map flag, refuses a direct teleport and sends zero TELEPORTs.
  Disposable registered accounts and temporary settings/SQLite preserve user data.
- Existing native GPU resize/pulse, Purchase Error/Dialog, Guild Board and
  Guild Members suites pass. Full isolated gameplay regression passes with
  309 MOVE, 16 PLAYERSHOOT, 5 USEITEM, 3 SHOOTACK, 3 INVSWAP, 1 INVDROP,
  vault portal/reconnect, Purchase Error, PlayerMenu/chat and the existing empty
  trade. No nonempty-trade or whole-client speedup claim is made.
- `git diff --check` passes; sibling Flash/server working trees are clean.
  No material task-04 acceptance gap remains.

An initial real-server attempt exited during .NET HttpEndPointListener startup
with ArgumentNullException, before a client connected; clean later runs pass.
An initial harness attempt overlapped binary linking and could not launch it;
subsequent live runs wait for the build. Initial fixture packet/stat IDs and
final-ACK flushing were corrected in the test harness before the final checks.
These are not hidden successful runs or server/runtime fixes.

## Captures and limits

`build/player-group-reference/comparison-*.png` contains all 21 source/native
pairs; `flash/metrics.json` and `native/metrics.json` contain matching dimensions.
`gpu/*.png` contains native playing-window tooltip/menu/edge/refusal/close
captures at 800x600, 1280x720, 1600x1200 and 3840x2160; `menu-3840-detail.png`
is a readable crop. `live.log` and `live-sanitize.log` record isolated-server checks.

Source is AIR CPU BitmapData output from real classes, not a live Flash
GameSprite. Native playing captures use Mesa 26.2.4 / AMD RX 6700 XT. Small
font coverage, halo falloff and cut-corner sampling differences remain, especially
when enlarged. Native enlarged text is deliberately rerasterized rather than
magnifying Flash's baked field. Layout, original art, content, colors and states
match at a glance. No exact pixel parity or full-scene performance claim is made.

## Next task facts

Implement **05-party-arrows**: edge-of-view party arrows, overlap merging,
hover groups/menu, camera/resize behavior and click suppression. Reuse PlayerGroup,
player_group_targets and first_teleport_player; its menu owns ID snapshots and
Session preserves map restrictions/cooldown. Party-arrow input must precede
world firing. Common row portrait art is still the existing class pack; upcoming
skin/dye tasks must update shared player presentation consistently.

Read the chain, port contract, progress/specs and this handoff. Preserve the
same checkout and packs. Carry the user's new-chat-after-completion authorization
and exact-one-successor/default-model procedure forward; task 17 creates none.
Subagents are not requested; if later authorized, each must be GPT-6 Luna.

## Successor dispatch

Created exactly one local successor for task 05 with the configured default
model, after resolving the RotMG project and checking for an existing receipt.
Thread ID: `01a12930-fbba-7ea0-bb1b-cb5a7676c489`, host `local`.
Receipt: `docs/task-handoffs/05-party-arrows-dispatch.json`. The successor owns
further implementation; this completed chat remains available.
