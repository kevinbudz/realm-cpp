# 01-dialogs: complete

Completed 2026-10-10 in the existing local checkout. Next task: **02-guild-board**.
The user's sequential-chat authorization remains in `docs/client-task-chain.md`.
No scheduled automation and no subagents were used. Existing uncommitted work
was preserved; no commits or resets were made. Flash/server sibling repositories
remain clean, and the existing font/HUD art bundles and their backups are intact.

## Implementation

`include/realm/dialog.hpp` and `src/dialog.cpp` implement reusable standard
Dialog/TextButton and MessageCloseDialog behavior: nullable title; centered
260-wide wrapped body; explicit multiline titles; zero, one or two buttons;
white/gold-hover/gray-disabled button states; cut corners, source colors, text
sizes and shadows; frame base alpha and offsets; matching integer layout and
outline-inclusive centering at stageHeight/600. Ordinary clicks return 0/1 to
the owner, while MessageClose closes on button 0. Press/release must hit the
same enabled button. Focus loss and resize cancel stale presses.

The frame is retained and regenerated only for relevant changes, at drawable
density. Original Myriad outlines provide sharp resized text. Long 14pt regular
body fields now fall back to native outlines when a needed baked vertical phase
does not exist. No extra glyph-phase baking was introduced. Explicit title
newlines preserve source line advance; source titles do not have automatic
wordWrap. Body paragraphs and blank lines wrap and keep their full content.

`src/main.cpp` routes BUYRESULT 1 to the component with title `Purchase Error`,
the server's body and `Close`, and omits the former error chat fallback. Other
results retain chat feedback. DialogsView's enclosing 0x151515/.6 dim layer is
separate from the standard dialog surface. Modal input precedes gameplay,
chat, HUD, trade and other dialogs, including continuous movement/shooting/
ability polling. Escape/Enter do not dismiss this click-only Flash dialog.
Map changes, reconnect/death and disconnect clear it. Existing specialized
guild/name/account forms were preserved.

## Authoritative sources inspected

- `realm-client/src/com/company/assembleegameclient/ui/dialogs/Dialog.as`
- `realm-client/src/com/company/assembleegameclient/ui/TextButton.as`
- `realm-client/src/com/company/ui/SimpleText.as`
- `realm-client/src/kabam/rotmg/ui/view/MessageCloseDialog.as` and
  `MessageCloseMediator.as`
- `realm-client/src/kabam/rotmg/dialogs/view/DialogsView.as`
- `realm-client/src/kabam/rotmg/messaging/impl/GameServerConnection.as`,
  `onBuyResult` at line 1940
- `realm-server/Game/Entities/Vendors/Vendors.cs` and
  `realm-server/Networking/GameServer.Social.cs`

Flash revision: `17bdf079452a1c470b8895eae9cf108599e7addd`.
Server revision: `bb75bc1e438b465986a8ada354b6332f69e0e3a6`.

## Fresh verification

All commands ran from `/home/kevin/Desktop/RotMG/realm-cpp`:

```sh
cmake --preset release
cmake --build --preset release -j 4
ctest --preset release
cmake --preset sanitize
cmake --build --preset sanitize -j 4
ctest --preset sanitize
python3 tests/dialog_reference_tests.py --rebuild-reference
python3 tests/dialog_gpu_tests.py
python3 tests/ui_resize_gpu_tests.py
python3 tests/live_server_tests.py ../realm-server/bin/Release/net8.0/RotMG.dll
git diff --check
```

- Release: **21/21** full CTest suites pass (2.67 s); ASan/UBSan: **21/21**
  pass (18.34 s). The new `dialogs` suite has 20 assertions covering result
  routing, wrapping/layout, cache reuse, two buttons, disabled buttons,
  press/release/drag-out rules, resize/focus loss and close/lifecycle behavior.
  Logs copied to `build/dialog-reference/{release,sanitize}-ctest.log`.
- The real Flash exporter compiled against unchanged source and ran under AIR.
  Native fixtures use the same case input. All ten frame heights and button
  positions match Flash. The final
  `--rebuild-reference` run freshly exported both source and native captures
  and regenerated the visual pairs.
- Real native SDL/TCP/GPU fixture passes fragmented BUYRESULT decoding,
  actual event routing, hover, outside click, suppression of Escape/R/F/1/
  Enter/G gameplay/chat input, 1280x720/1600x1200/3840x2160 window resizing,
  restoration of the base popup, and Close. Its wire peer observed no leaked
  gameplay packets. This is a loopback fixture, not a real-server capture.
- Existing GPU resize checks pass, including 720 sharp title Play pulse frames,
  HUD/tooltips at 4K, restored base pixels, and resized account-form input.
- The full isolated real-server runner passes: 309 MOVE, 16 PLAYERSHOOT,
  5 USEITEM, 3 SHOOTACK, 3 INVSWAP and 1 INVDROP across the six base sessions.
  A new `realm_session --exercise-purchase FONT_DIR` sends BUY to a real Nexus
  merchant whose gold price exceeds fixture funds. An immediate repeated BUY
  is suppressed: the proxy observes exactly **one BUY**. The server returns
  result 1 / `Purchase Error: Insufficient Funds.`; pending purchase clears,
  the shared native dialog renders at 1280x720 and its Close click succeeds.
  This real-server exercise uses the shared view/input component headlessly;
  the full SDL/GPU path is independently covered by the loopback fixture.
- PlayerMenu/PLAYERTEXT and the existing **empty** automated trade also passed
  in that real-server run. No nonempty automated trade is claimed. Accounts
  and settings/database were temporary; the second account registers via
  127.0.0.2. The user's saved account was not accessed.
- An initial live-run attempt overlapped relinking `realm_session` and hit a
  transient executable PermissionError. The fresh run after builds finished
  passed; this was not a server/protocol failure. An earlier long-message
  fixture exposed the missing font phase and was fixed before full checks.
- `git diff --check` passes. The inherited player-menu test has an existing
  narrowing-conversion warning; no new compilation warning was introduced.

## Visual evidence and accepted differences

`build/dialog-reference/flash/*.png` are real Flash Dialog/MessageCloseDialog
captures drawn through AIR CPU BitmapData at target stage density.
`build/dialog-reference/native/*.png` are the native retained component.

Reviewed side by side:

- `build/dialog-reference/comparison-base.png`: Purchase Error, confirmation
  with two buttons, hovered/disabled action, no title, no-button Loading,
  long body with blank paragraph and two-line title.
- `build/dialog-reference/comparison-{1280,1600,3840}.png`: resized popup pairs.
- `build/dialog-reference/gpu/{base,blocked,hover-1280,resized-1600,resized-3840,restored,closed}.png`:
  actual native OpenGL window captures.
- `build/dialog-reference/{flash,native}/metrics.json`: matching layout evidence.
- `build/dialog-reference/gpu/run.log`: renderer/device details.

Layout, sizes, colors, content and states match at a glance. Native glyph
coverage/position rounding, cut-edge antialiasing and shadow falloff differ
slightly; larger native halos are a little softer. These small differences
are accepted under the current contract. This is not exact screenshot parity.
The native font density is physical window density, rather than magnified
800x600 text. Native GPU: AMD Radeon RX 6700 XT, Mesa 26.2.4, OpenGL 4.6;
Flash reference uses CPU rendering. No whole-client speedup is claimed.
There are no unresolved material acceptance gaps for task 01.

## Successor facts

Task 02 should reuse `Dialog` for the board's no-button Loading/Saving views
and standard errors as appropriate, while preserving the board window's own
black/.8 backdrop. `Dialog::show` takes plain displayed text, nullable title
and optional labels; `update` expects FontBook density set to stageHeight/600,
and pointer coordinates are drawable pixels. Its Image carries pixel_scale;
use canvas_position and physical image dimensions when submitting a quad.
Ordinary button callbacks leave close/transition decisions to the owner.

Add the board/HTTP/scrollbar state through the existing native input/render
abstractions. Preserve asynchronous HTTP and account credentials; do not copy
credentials into visible diagnostics. See `docs/flash-specs.md` and real source
for officer permissions, raw board response and XML errors. Only 01 is complete;
the next chat owns claiming/implementing 02. Dispatch receipt belongs at
`docs/task-handoffs/02-guild-board-dispatch.json`.
