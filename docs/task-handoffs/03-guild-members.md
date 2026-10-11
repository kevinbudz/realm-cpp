# 03-guild-members: complete

Completed 2026-10-10 in the existing local checkout. Next task:
**04-minimap-menu**. The user's sequential-chat authorization and dispatch
procedure remain in `docs/client-task-chain.md`. No subagents, scheduled
automations, commits or resets were used. Existing uncommitted/untracked work
was preserved. Flash/server sources and the existing font/HUD packs and backups
were not changed. Seven finite guild icons were added in a separate supplement.

## Implementation and source behavior

`include/realm/guild_members.hpp` / `src/guild_members.cpp` implement the native
GuildChroniclePanel View action and complete member screen. The source backdrop,
full-width MenuFrame, centered continue, guild name, current fame, member fame,
rank names/icons, own-member gold, stored member order and open slots are present.
The list uses the actual source mask at (10,110), 756x430, a 400px scroll page,
and Scrollbar(16,400) at (780,104). Its bounds include the first 40px icon's
negative 4px top and the open-slots field's fractional text height. These bounds
match all source fixtures, including empty and fifty-member lists.

Promote/demote/remove use the Flash GuildUtil permission matrix and exact
confirmation text through the shared standard Dialog. Officers can promote
initiates, but cannot promote a member to another officer even though this
server permits it. Leaders can promote officers to peer leaders. Founders
cannot create another founder. Equal/higher targets have no controls. Cancel
sends nothing. Confirmation sends one CHANGEGUILDRANK or GUILDREMOVE request;
there is no optimistic member mutation. Current player rank changes cancel an
open confirmation or an action not yet sent and update available controls.
Leaving/changing guild closes the screen.

`parse_guild_members` in `account.cpp` parses `<Members>`, CurrentFame, GuildLevel,
and Member Name/Rank/Fame while preserving XML order. The actual server omits
`Members.@name`; the title falls back to the current authoritative player guild.
HTTP sends current `Session::credentials()`, num=50 and offset=0 to the configured
app host/port. The host matches the server's HttpListener prefix. Requests run
asynchronously; generations discard stale replies and reopen queues a fresh
request. Close/map teardown does not wait on an HTTP worker. Process destruction may
wait for its bounded HTTP deadline. XML/transport
errors have Retry and Close; rank/removal errors have Ok followed by a refresh.

The server sends rank/removal results through TEXT, **not GUILDRESULT**.
Success requires the exact expected guild text and the local actor object ID.
Known rank/removal error strings open the source Error dialog, and the original
TEXT continues through normal native chat. Unrelated guild actors, guilds and
chat errors do not complete an action. One action is pending at a time; a
confirmation timeout explains uncertainty and refreshes after Ok without
resending the mutation. The protocol has no per-request IDs for these chat
errors, so identical concurrent server error strings cannot be distinguished;
modal input suppresses further local guild/game commands while waiting.

`src/main.cpp` routes all SDL pointer/keyboard/text/wheel/focus/resize events
before gameplay while the screen is open, suppresses held movement/firing/
abilities and HUD/player hover, and clears it on MAPINFO/RECONNECT/DEATH/
disconnect. Retained surfaces redraw text at drawable density and reuse stable
content. The screen overlays the still-running native game; AIR is only an
offline reference/art tool. The script harness now supports down/up/wheel so
actual SDL dragging and wheel input can be checked.

`tools/bake_guild_members_art.py` exports the actual GuildUtil fame/rank icons
and DeleteXGraphic into `build/hud-art/guild-members/{0..6}.png`. This finite
set is loaded only when the screen opens. The existing HUD bundle and its
backups stay intact. For another asset directory, bake into its guild-members
subdirectory. The exporter compiler needs a 2 GiB heap for this combined source
fixture; its configuration is local to the GuildMembersExporter invocation.

## Authoritative sources

- `realm-client/src/com/company/assembleegameclient/ui/guild/{GuildChronicleScreen,GuildPlayerList,MemberListLine}.as`
- `realm-client/src/com/company/assembleegameclient/util/GuildUtil.as`
- `realm-client/src/com/company/assembleegameclient/ui/{Scrollbar,dialogs/Dialog}.as`
- `realm-client/src/com/company/assembleegameclient/ui/layout/{ScaledScreen,MenuFrame}.as`
- `realm-client/src/com/company/assembleegameclient/screens/TitleMenuOption.as`
- `realm-client/src/com/company/rotmg/graphics/DeleteXGraphic.as`
- `realm-server/Networking/{AppServer.Social,GameServer.Social}.cs`
- `realm-server/Common/Database.Guild.cs`, `Game/Entities/Player.Guild.cs`

Flash revision `17bdf079452a1c470b8895eae9cf108599e7addd`; server revision
`bb75bc1e438b465986a8ada354b6332f69e0e3a6`.

## Verification commands

Run from `/home/kevin/Desktop/RotMG/realm-cpp`:

```sh
cmake --preset release
cmake --build --preset release -j 4
ctest --preset release --output-on-failure
cmake --preset sanitize
cmake --build --preset sanitize -j 4
ctest --preset sanitize --output-on-failure
python3 tests/guild_members_reference_tests.py
python3 tests/guild_members_gpu_tests.py
python3 tests/guild_members_live_tests.py ../realm-server/bin/Release/net8.0/RotMG.dll
python3 tests/guild_members_live_tests.py ../realm-server/bin/Release/net8.0/RotMG.dll --client build-sanitize/realm_guild_members_tests
python3 tests/dialog_gpu_tests.py
python3 tests/guild_board_gpu_tests.py
python3 tests/ui_resize_gpu_tests.py
python3 tests/live_server_tests.py ../realm-server/bin/Release/net8.0/RotMG.dll
git diff --check
```

- **25/25 Release** CTest suites pass (6.88 s); **25/25 ASan/UBSan** suites
  pass (26.99 s). The new controller suite passes **101 checks**, including
  the 75 permission-matrix cases, XML order/encoding/error handling,
  confirmations, cancellation, one-shot requests, unrelated chat rejection,
  timeout, authoritative permission changes, scrolling/drag and resize cleanup.
- Five independent HTTP scenarios pass: fragmented UTF8/XML load and exact
  form fields, server XML error/Close, Retry, stale response/nonblocking close/
  queued reopen, and malformed XML. Both presets include this peer suite.
- **16 real Flash/native geometry fixtures pass** and were reviewed side by
  side. The source members and full list bounds match, including fractional
  open-slots height and negative first-icon bounds. Original source classes
  compile without modifying the sibling repository.
- Actual SDL/TCP/HTTP/OpenGL checks pass: ChroniclePanel click, Loading, Cancel,
  promote, demote rejection/Error/Ok/refresh, remove, matching dedicated packet
  bodies/chat refresh, wheel and thumb drag, blocked gameplay keys, 720p/1200p/
  4K resize, restored base capture and Continue. The peers observe exactly two
  CHANGEGUILDRANK packets and one GUILDREMOVE, with four list requests and no
  leaked gameplay/chat commands. Fine wheel input uses preciseY when the SDL
  integer delta is zero; this also handles the observed synthetic-event
  conversion in [sdl2-compat](https://github.com/libsdl-org/sdl2-compat/blob/main/src/sdl2_compat.c).
- Isolated real-server checks pass with **Release and sanitizer native clients**.
  Each variant runs promote/demote/remove against offline and online targets,
  plus stale-client rank and removal permission rejections. They verify guild
  fame/level, member fame/order, actual Session packet counts, exact chat handling,
  refreshed HTTP list and persisted SQLite guild/member state. Online targets
  receive promotion/demotion chat and the kick notice. Each variant passes
  17 + 17 + 15 native checks. Captured logs: `live.log`, `live-sanitize.log`.
- Existing Dialog/Purchase Error, Guild Board and window/pulse resize GPU checks
  pass. The full isolated gameplay/server regression passes with 309 MOVE,
  16 PLAYERSHOOT, 5 USEITEM, 3 SHOOTACK, 3 INVSWAP and 1 INVDROP, purchase error,
  PlayerMenu/chat and the existing **empty** trade. This does not establish a
  completed nonempty trade.
- Standalone guild-art bake smoke check passes; all seven PNG hashes match the
  source fixture exports.
- `git diff --check` passes. Sibling Flash/server working trees remain clean.
  No required acceptance check remains unresolved.

## Captures and comparison limits

`build/guild-members-reference/comparison-*.png` contains the source/native
pairs; `flash/metrics.json` and `native/metrics.json` contain their matching
member counts and list bounds. Sixteen fixtures cover founder/leader/officer/
member permissions and own highlights, empty, fifty-member top/bottom, all
three confirmations, continue/action hover, loading, 1280x720, 1600x1200 and
3840x2160. These were reviewed individually side by side. The exporter uses
unmodified GuildPlayerList/MemberListLine with injected offline account/HTTP
fixtures, actual confirmation clicks, MenuFrame and TitleMenuOption; it does
not instantiate a full live Flash GameSprite.

`build/guild-members-reference/gpu/*.png` contains native SDL/OpenGL captures
for loading/list/cancel/promote/pending/demote/error/remove/scroll/resize/close.
Source BitmapData captures use AIR's CPU path; native GPU captures use Mesa
26.2.4 and AMD RX 6700 XT. Small glyph coverage/halo and cut-edge sampling
changes remain, especially with native text at enlarged sizes. Continue uses
the native shared text shadow, slightly tighter than the source's 12px,
half-alpha halo. Layout, artwork, content, colors and visual states match at
a glance. No exact pixel parity or whole-client speedup is claimed.

Native intentionally supplies recoverable HTTP errors and a pending indicator;
the source only traces load failures and waits for GUILDRESULT forever against
this server. The server itself remains unchanged. Both the actual SDL input path and isolated real-server action checks passed.

## Next task facts

Implement **04-minimap-menu**. Read the ledger, port contract, progress/specs
and this handoff. Reuse existing PlayerMenu/PlayerToolTip/group-row art where
appropriate. Check actual MiniMap/PlayerGroupToolTip/PlayerGroupMenu source
order, hover grouping, edge clamping and teleport eligibility; preserve the
client map flag and ten-second cooldown even though this server does not enforce
them. Use the same checkout. Keep the sequential-chat/default-model procedure
and user authorization in every successor; task 17 creates no successor.


## Successor dispatch

Created exactly one local successor for task 04 with the configured default
model, after resolving the RotMG project and checking that no dispatch receipt
existed. Thread ID: `01a12922-c43c-7543-9b3e-18f999734018`, host `local`.
Receipt: `docs/task-handoffs/04-minimap-menu-dispatch.json`. The successor owns
further implementation; this completed chat remains available.
