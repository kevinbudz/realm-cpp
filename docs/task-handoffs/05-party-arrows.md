# 05-party-arrows: complete

Implemented 2026-10-10 in the existing local checkout. Next task:
**06-skins**. The user's sequential-chat authorization and dispatch procedure
remain in `docs/client-task-chain.md`. No subagents, automations, commits or
resets were used. Existing uncommitted/untracked work, sibling Flash/server
repositories, fonts, HUD packs/backups and the guild-members supplement are
preserved. Runtime remains C++/SDL/OpenGL; AIR is only an offline reference tool.

## Implementation and confirmed source behavior

`include/realm/party_arrows.hpp` / `src/party_arrows.cpp` retain the six Party
slots and their ordered ID groups. Party's existing starred/distance/ID ordering
and 500ms membership refresh remain authoritative. Arrows use the source
Map.draw known-square radius around its vertically offset world center to
identify undrawn members, then GameObjectArrow's camera-origin ray intersection
with the playfield rectangle (excluding the HUD). Inside targets produce no
edge arrow. Camera translation, rotation, centered/offset placement, zoom and
window/HUD resizing feed the projection each frame. Invisible and NoMiniMap
members still have arrows, as in Flash; live visibility controls teleporting.

Triangles have the original 3px inset, 8px normal / 11px hover sizes, green
0x3FC752 fill, white one-pixel stroke and black shadow. Source edge/corner angle
clamps keep the triangle pointing inward while indicating the member's direction.
Six retained 48px arrow surfaces upload only when rotation/hover/density changes;
ordered vectors retain their capacity between frames. There is no entity pointer
retention, unbounded arrow image cache or per-target texture allocation.

Groups merge at strict distance squared <64 in map pixels, in party slot order.
The hovered slot freezes its position and full group until rollout, matching
PartyOverlay's hover behavior. Removed primary IDs cancel that freeze safely.
Resize/focus/map transitions cancel stale hover; map changes, reconnect, death,
disconnect and modals clear the decorations. A hovered group reuses PlayerGroup
rows/art/text and places its tooltip inward along GameObjectArrow's 26px anchor.
Text redraws at drawable density and menu rows own ordered snapshots.

Arrow mouse-down immediately opens PlayerGroupMenu and consumes input before
HUD/world firing. A separate held-press guard survives menu rollout and resize
until release, so dragging an arrow press into the world cannot fire shots.
An existing menu above the arrow retains input priority. Arrow and minimap
menus replace one another. Menu teleport resolves the first currently visible
member from its frozen ID order; missing/invisible groups produce the existing
source error. Session's map flag and ten-second cooldown are preserved. No server
or wire-protocol changes were made.

Two deliberate native corrections preserve usable source behavior:

- PartyOverlay compares an arrow's previous position before drawing the new
  projection and can merge into a hidden/stale slot during camera changes.
  Native groups use current projections and visible groups in the same slot order.
- Flash scales the attached tooltip at larger window sizes but positions it using
  unscaled content dimensions. The real 4K bottom-edge reference clips the lower
  rows/footer. Native applies the source inward-placement formula to scaled bounds,
  keeping the full tooltip readable. Base-size placement remains source-equivalent.

## Authoritative sources

Paths under `realm-client/src`:

- `com/company/assembleegameclient/map/partyoverlay/{PartyOverlay,GameObjectArrow,PlayerArrow}.as`
- `com/company/assembleegameclient/map/{Map,Camera}.as`
- `com/company/assembleegameclient/objects/{Party,BasicObject,Player}.as`
- `com/company/util/{RectangleUtil,Trig}.as`
- `com/company/assembleegameclient/ui/tooltip/{PlayerGroupToolTip,ToolTip}.as`
- `com/company/assembleegameclient/ui/menu/{PlayerGroupMenu,Menu,TeleportMenuOption}.as`

Server: `Game/Entities/Player.Update.cs` (15-unit sight),
`Networking/GameServer.Social.cs`, `Game/Entities/Player.Guild.cs`, and
`Resources/Worlds/Worlds.xml`. The server enforces neither the client cooldown
nor the map teleport flag. Flash revision `17bdf079452a1c470b8895eae9cf108599e7addd`;
server revision `bb75bc1e438b465986a8ada354b6332f69e0e3a6`.

## Verification commands and evidence

From `/home/kevin/Desktop/RotMG/realm-cpp`:

```sh
cmake --preset release
cmake --build --preset release -j 4
ctest --preset release --output-on-failure
cmake --preset sanitize
cmake --build --preset sanitize -j 4
ctest --preset sanitize --output-on-failure
python3 tests/party_arrow_reference_tests.py
python3 tests/party_arrow_gpu_tests.py
python3 tests/party_arrow_live_tests.py ../realm-server/bin/Release/net8.0/RotMG.dll
python3 tests/party_arrow_live_tests.py ../realm-server/bin/Release/net8.0/RotMG.dll --client build-sanitize/realm_party_arrow_session_inputs
python3 tests/player_group_reference_tests.py
python3 tests/player_group_gpu_tests.py
python3 tests/player_group_live_tests.py ../realm-server/bin/Release/net8.0/RotMG.dll
python3 tests/ui_resize_gpu_tests.py
python3 tests/dialog_gpu_tests.py
python3 tests/guild_board_gpu_tests.py
python3 tests/guild_members_gpu_tests.py
python3 tests/live_server_tests.py ../realm-server/bin/Release/net8.0/RotMG.dll
git diff --check
```

- Final Release **28/28** (17.06s) and ASan/UBSan **28/28** (39.86s) pass. Logs are
  recorded in `build/party-arrow-reference/ctest-{release,sanitize}.log`.
  `party_arrows` has **35 checks**, including independent edge coordinates,
  rotation/offset/zoom/resize, source draw-radius expansion, strict merge radius,
  ordered groups, hover movement/reorder/removal, invisible/NoMiniMap members,
  inward tooltip anchors, retained redraws and teardown.
- **18 real Flash/native fixtures** have matching projection and rotation metrics
  and were reviewed side by side: four edges, corners, normal/hover, grouped
  tooltip, rotated/offset camera, zoom, 720p, 1200p and 4K. Exporter compiles the
  unmodified PlayerArrow/GameObjectArrow/PlayerGroupToolTip classes, supplies
  original class assets and captures at playing-window dimensions. Shared
  PlayerGroup's **21** source/native frame layouts were also freshly checked.
- Actual SDL/TCP/OpenGL tests pass: grouped hover, mouse-down menu, passive rows,
  first visible selection, live visibility changes, no-eligible error, cooldown
  refusal/expiry, map flag, offset camera, rollout and 800x600 / 1280x720 /
  1600x1200 / 3840x2160 resize. A held arrow press dragged into the world and
  another held through resize send zero PLAYERSHOOT. A positive control using
  the same held-input path outside UI sends PLAYERSHOOT. Exactly two permitted
  TELEPORT packets target the expected second then first member; no ability or
  other gameplay/social leakage occurs. Scripted holds explicitly mirror button
  state because SDL_PushEvent does not alter SDL_GetMouseState.
- Fresh isolated real-server Release and sanitizer clients each walk a peer past
  the source draw radius, expose a party arrow, open the native group menu and
  resolve its snapshot. Exactly two TELEPORTs pass with two local authoritative
  GOTO events, immediate cooldown refusal and expiry. Wire GOTOACK also includes
  other-entity GOTO messages; logs record that total honestly. Disposable accounts,
  temporary settings and SQLite preserve the user's data. Separate fresh native
  group/server checks pass ordered visibility and the actual GuildHall map flag
  with zero TELEPORTs in that restricted map.
- Existing minimap, resize/pulse, Dialog/Purchase Error, guild board and guild
  members GPU checks pass. The full isolated gameplay/server regression passes:
  308 MOVE, 16 PLAYERSHOOT, 5 USEITEM, 3 SHOOTACK, 3 INVSWAP, 1 INVDROP,
  container pickup, magic potion, vault reconnect, purchase feedback, PlayerMenu,
  account lists/chat and the existing completed empty trade. This is not evidence
  of a completed nonempty trade or a whole-client speedup.
- `git diff --check` passes; sibling Flash/server working trees are clean.
  No material task-05 acceptance gap remains.

Initial live-harness attempts exposed missing remote interpolation and a route
that could get stuck at a floating-point tile-center boundary near Nexus objects.
The final harness uses normal native movement on collision-checked waypoint
segments, snaps the final sub-0.06-unit step to a valid tile center, and applies
remote interpolation each frame. An early assertion counted only local GOTOACKs;
the final observer also accounts for other-entity GOTO packets. Initial GPU
assertions used the wrong PLAYERSHOOT ID and a portrait-shadow pixel; those
assertions were corrected before successful runs. These are recorded harness
corrections, not undisclosed successful checks or server/runtime changes.

## Captures and limits

`build/party-arrow-reference/comparison-*.png`, `contact.png`, and
`{flash,native}/metrics.json` hold the 18 reviewed source/native pairs.
`gpu/*.png` holds playing-window native captures and `tooltip-3840-detail.png`.
`gpu/*-wire.json` records exact teleport targets and shot times. `live.log` and
`live-sanitize.log` record fresh real-server movement/arrow/menu checks.

Flash references are offline AIR CPU BitmapData captures of real classes, not a
live Flash GameSprite. Native GPU captures use Mesa 26.2.4 / AMD RX 6700 XT and
an actual SDL/TCP playing session. The real-server arrow tests exercise native
session/render-model/menu logic headlessly; they are distinct from the GPU
fixture. Small antialiasing, glyph edges and halo falloff differ, especially when
enlarged. Layout, original art, colors, content and normal/hover states match at
a glance. The resized tooltip placement repair above is intentional and visible
in the pairs. No exact-pixel or whole-client performance claim is made.

## Next task facts

Implement **06-skins**: owned/class-filtered skin chooser and startup selection
where Flash exposes it, default reset, RESKIN, rejection handling and live
portraits. Party arrow tooltips reuse PlayerGroup; minimap groups and PlayerMenu
reuse the common `player_row_label` and class portrait pack. Update shared player
presentation consistently across these surfaces, HUD/party and world rendering.
Menus continue to own IDs/data snapshots rather than World pointers.

Preserve the same checkout, packs/backups and the guild-members supplement.
Read this handoff, chain and source specs. Carry the user's new-chat-after-each-
completion authorization forward, check the next dispatch receipt, call
list_projects, select RotMG's local project at `/home/kevin/Desktop/RotMG`, retain
the configured default model and create exactly one successor after completion.
Task 17 creates none. No automation is wanted. Subagents are not requested;
if later authorized, every subagent must be GPT-6 Luna (`gpt-6-luna`).
