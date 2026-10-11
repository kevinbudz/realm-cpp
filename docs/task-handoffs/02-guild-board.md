# 02-guild-board: complete

Completed 2026-10-10 in the existing local checkout. Next task: **03-guild-members**.
The sequential-chat authorization and dispatch procedure remain in
`docs/client-task-chain.md`. No subagents, scheduled automation, commits or
resets were used. Existing uncommitted/untracked work was preserved. Flash and
server sibling repositories remain clean; font/HUD art packs and backups were
not modified.

## Implementation

`include/realm/guild_board.hpp` / `src/guild_board.cpp` implement the actual
GuildBoardPanel View action and GuildBoardWindow flow. The view uses 400x400
masked 16pt regular Myriad text, literal HTML-like text, source-colored URL
links (including wrapped destinations), optional scrollbar, officer Edit and
Close. Editing always uses the scrollbar with Cancel/Save, multiline UTF8
input, mouse selection, keyboard navigation, selection/replacement, clipboard,
and caret scrolling. Cancel discards the draft; Save uses the server-returned
text. The real EditBoard explicitly sets `border=false`, overriding the
SimpleText input constructor's default border; native follows the real class.
Source board dimensions are 400 or 426 by **432**, with a 2px white cut frame
at (-6,-6), 0x333333 fill, and 14pt/120-wide buttons at y404.

`include/realm/scrollbar.hpp` / `src/scrollbar.cpp` provide the reusable Flash
scrollbar: gray track, white/gold arrows and cut thumb, normalized bounded
position, thumb sizing, dragging, page clicks, fifth-page wheel movement,
held arrows, and release/focus cleanup. A fitting document remains finite and
stationary. View text scrolls by pixels; edit text scrolls by complete lines
and hides a partially visible final input line, as Flash does.

HTTP uses the existing native `net::http_post` on an asynchronous worker.
`Session::credentials()` supplies the current session credentials without
reading or altering the user's saved account. `/guild/getBoard` sends
username/password; `/guild/setBoard` adds board. XML Error replies and transport
failures open reusable standard dialogs. Loading/Saving hide the backdrop,
matching the source; the board itself supplies black/.8. Flash only traces
errors and leaves its pending screen stuck: native adds Load Error/Save Error
with Retry and Close/Back. Back preserves an unsaved draft.

The current server first parses the form, then calls UrlDecode(board) again.
The native board protects its board field through both decoding layers so
literal '+' and '%xx' are stored unchanged. Server permissions remain
unchanged. Rank >=20 enables editing; an authoritative demotion exits edit.
One request is active at a time. Map teardown closes immediately without
waiting for HTTP. Generations discard stale replies; reopening queues a fresh
load after the old worker completes. The worker owns its request values and
never references the UI. Process destruction can wait for the bounded HTTP
deadline; map teardown does not.

`src/main.cpp` routes SDL pointer/text/keyboard/wheel/focus/resize events before
game controls, handles Ctrl+A/C/X/V, Return and UTF8 deletion, opens URL links,
and suppresses held movement/shooting/ability polling and HUD hover while the
board is visible. MAPINFO/RECONNECT/DEATH/disconnect clear the board. Retained
surfaces and text redraw at drawable density and reuse unchanged content.
URL detection uses a cached linear scan of the source grammar; wrapped link
rows reference the board text instead of duplicating long destination strings.
Runtime remains native; AIR is only an offline reference exporter.

## Authoritative sources

- `realm-client/src/com/company/assembleegameclient/ui/board/{GuildBoardWindow,ViewBoard,EditBoard}.as`
- `realm-client/src/com/company/assembleegameclient/ui/{Scrollbar,TextButton}.as`
- `realm-client/src/com/company/ui/SimpleText.as`
- `realm-client/src/com/company/assembleegameclient/ui/panels/GuildBoardPanel.as`
- `realm-client/src/com/company/assembleegameclient/ui/layout/ScaledScreen.as`
- `realm-server/Networking/{AppServer,AppServer.Social}.cs`
- `realm-server/Common/{Database.Guild,Models}.cs`

Flash revision `17bdf079452a1c470b8895eae9cf108599e7addd`; server revision
`bb75bc1e438b465986a8ada354b6332f69e0e3a6`.

## Fresh verification

Commands from `/home/kevin/Desktop/RotMG/realm-cpp`:

```sh
cmake --preset release
cmake --build --preset release -j 4
ctest --preset release --output-on-failure
cmake --preset sanitize
cmake --build --preset sanitize -j 4
ctest --preset sanitize --output-on-failure
python3 tests/guild_board_reference_tests.py
python3 tests/guild_board_gpu_tests.py
python3 tests/guild_board_live_tests.py ../realm-server/bin/Release/net8.0/RotMG.dll
python3 tests/guild_board_live_tests.py ../realm-server/bin/Release/net8.0/RotMG.dll --client build-sanitize/realm_guild_board_tests
python3 tests/dialog_gpu_tests.py
python3 tests/ui_resize_gpu_tests.py
python3 tests/live_server_tests.py ../realm-server/bin/Release/net8.0/RotMG.dll
git diff --check
```

- **23/23 Release** suites pass (4.03 s); **23/23 ASan/UBSan** suites pass
  (22.82 s). The board suite has **33 checks**, including permissions,
  edit/cancel, UTF8 deletion, selection/cut, multiline input, links, caret
  scrolling, resize press cancellation, retained cache and scrollbar controls.
- The independent HTTP peer suite passes native raw load/save/readback,
  fragmented UTF8 responses, protected form encoding, read-only rank,
  XML errors, save-error Back with draft preservation, retry, and stale reply
  rejection with an immediate nonblocking close and queued fresh reopen.
- **13 real Flash/native geometry fixtures pass**. Source classes compiled
  under AIR without modifying their source. View/member/officer, long/top and
  bottom scroll, empty, short/long edit, hover and resized states were captured
  and reviewed side by side. Board widths, heights and button geometry match.
- The actual native SDL/OpenGL client receives the GuildBoard object over TCP,
  clicks the real panel, loads over HTTP, edits and inserts a newline, saves
  literal '+ %20 & café', restores the view, resizes through 1280x720,
  1600x1200 and 3840x2160, returns to base and closes. Modal R/F/1/Escape/G
  input sends no gameplay packets. Base pixels restore after resizing.
  This is a loopback TCP/HTTP fixture, not a real-server GPU capture.
- The isolated real server passes native HTTP officer save and persisted
  readback; exact Unicode/plus/percent/ampersand/HTML-like content; member
  read-only access; a server-side lower-rank save rejection preserving the
  stored text; no-guild error and invalid-account error. Release and sanitizer
  native board clients both pass. The disposable SQLite fixture seeds only
  its own registered account/guild rows; server code/settings and the user's
  saved account are untouched. These use the shared native component
  headlessly; the SDL/GPU path is independently covered above.
- Purchase Error SDL/TCP/modal resize/Close regression and existing HUD/menu
  resize suite pass, including 720 Play pulse frames at drawable texel size.
- Full isolated gameplay/server regression passes; see `live-gameplay.log`
  for its packet counts and the existing empty-trade limitation.
- `git diff --check` passes. Both sibling repositories are clean. No new
  compile warning remains.

The first live HTTP attempt used localhost while the isolated HttpListener
prefix was configured as 127.0.0.1, producing HTTP 404 before account handling.
The runner now uses the configured app hostname. The first GPU edit fixture
clicked between two letters; its Home key now makes the intended insertion
position explicit. A visual check caught and fixed partial final edit-line
rendering before the full verification runs.

## Captures and accepted differences

`build/guild-board-reference/{flash,native}/*.png` and `metrics.json` contain
real source and native surfaces. `comparison-*.png` covers all 13 pairs;
`comparison-gallery.png` is an overview. `gpu/{panel,loading,view,blocked,edit,
typed,saving,saved,view-1280,view-1600,view-3840,restored,closed}.png` contains
actual native window captures. `gpu/run.log` identifies AMD RX 6700 XT / Mesa
26.2.4 / OpenGL 4.6. `live.log` records the isolated board run;
`{release,sanitize}-ctest.log` preserve fresh full-suite output.

Layout, content, colors, art and states match at a glance. Native glyph
coverage/rounding and frame cut-edge antialiasing differ slightly. Text at
larger sizes is rerasterized from the original Myriad outlines, per the resize
contract, rather than magnified base text. Small row/glyph-edge rounding
variations remain accepted. No exact pixel parity or whole-client speedup is
claimed. No material acceptance gap remains for task 02.

## Successor facts

Implement **03-guild-members** next. Reuse Scrollbar and Dialog. Scrollbar input
coordinates are local design pixels, while GuildBoard/Dialog pointers use
drawable pixels. Owners must set FontBook density to stageHeight/600 before
update and upload only changed images. Scaled content centers on the physical
window. Standard Dialog is independently centered and has its own canvas
padding. Each modal owner supplies its own backdrop.

Guild member HTTP uses the same current-session credentials and app host;
HttpListener requires the configured hostname in its Host header. Guild board
is ButtonPanel kind 0; chronicle is kind 1, character changer kind 2, reskin
kind 3. Read authoritative Flash/server sources again for member/rank actions
and chat feedback. Preserve the inherited uncommitted work and art packs.
After task 03 passes acceptance, record evidence and start exactly one task 04
successor per the user's sequential-chat authorization; do not schedule an
automation or override the configured model. Subagents are not requested.
