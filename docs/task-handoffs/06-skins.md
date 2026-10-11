# 06-skins — complete

Completed 2026-10-11. The user accepted the current appearance ("it looks fine
for now") and explicitly requested the next chat. Next: **07-dyes-masks**.

## Implementation and source behavior

Catalog explicitly loads EmbeddedData_SkinsCXML.dat (15 skins, 14 classes)
and resolves class-safe skin textures/portraits. Account parses OwnedSkins
and class BestLevel; saved characters retain their Texture. Shared native
SkinChooser implements Classic-first source ordering, owned selection,
unowned Buy, walking/hover previews, class detail and exact purchase dialogs.
Startup class selection now opens the skin page; CREATE receives the selected
owned skin. Guest browsing resumes the selected class after sign-in.

Session::reskin enforces playing/ownership/class/range and sends existing
RESKIN without optimistic changes. Authoritative stat 70 drives animated world
sprites/shadows, HUD/party portraits, player tooltip/menu and minimap/arrow
groups. Open menus refresh appearance while preserving ordered ID snapshots;
no World pointers are retained. SDL modal input, keyboard/wheel and held-click
suppression cover resize, map/reconnect/death and focus transitions.

Native purchase HTTP uses current account credentials and configured host.
Successful delayed receipts remain attached to their account across Back or
new class selection. Normal in-game ownership uses startup cache: current
server refuses char/list and purchaseSkin while online. Purchase errors use
the original dialogs/text. Diagnostic direct-connect without startup metadata
can expose the server refusal with Retry/Cancel; this is not the normal path.

Relevant files: include/realm/{skins,catalog,account,startup,session}.hpp,
src/{skins,catalog,account,startup,session,main,scene,hud,party,player_tooltip,
player_menu,player_group,hud_art,processed}.cpp. Inspect sibling Flash
kabam/rotmg/characters/{reskin,view}, CharacterSkinListView/ListItem,
ClassDetailView, CharacterFactory, RadioButton, CreditDisplay and Dialog
subclasses; authoritative server RESKIN/skin purchase handling was exercised.

## Assets and bounds

Separate `build/hud-art/skins` contains 110 original source UI records via
`tools/bake_skin_art.py` and `tools/flash-reference/SkinsExporter.as`.
Existing main HUD/fonts/backups and guild-members supplement are preserved.
Per-pack decoded cache is 128 entries / 4 MiB; main+skins combined 256 / 8 MiB.
Walking preview changes upload only the bounded 104 logical-pixel region
(at most 375x375 physical at tested 4K), not the viewport every 200ms.

Neutral size-100 `build/baked-skins` has 300 original animation frames.
Generate inputs with `build/realm_bake_inputs build/baked-skins --skins`,
then use the original TextureExporter via offline AIR. ProcessedTextures
appends the sibling supplement and skips duplicates across packs while
retaining within-manifest validation. Runtime remains native C++/SDL/OpenGL.

## Final evidence

- Release full CTest: **30/30, 19.15 seconds** (`ctest-release.log`).
- ASan/UBSan full CTest: **30/30, 52.14 seconds** (`ctest-sanitize.log`).
- `tests/skin_tests.cpp`: **72 checks**, including ownership/class/Classic,
  invalid fallbacks, modal resizing, live portrait targets, art/cache bounds,
  unchanged redraw and bounded 4K animation upload.
- Native HTTP fixture tests cover successful purchase, failure preservation,
  late receipt across Back/class changes and account isolation.
- `tests/skin_reference_tests.py`: **44 actual Flash/native fixtures**, source
  frame/geometry checks and side-by-side review at 800x600, 1280x720,
  1600x1200 and 3840x2160. Wizard/Archer/Warrior, Classic/owned rows, startup,
  hover and both error states. Captures: flash/, native/, comparison-*.png,
  contact.png and detail-*.png under `build/skin-reference`.
- `tests/skin_gpu_tests.py`: actual SDL/OpenGL/native HTTP/TCP startup
  purchase/CREATE, Classic/owned RESKIN, real refusal, authoritative reopen,
  HUD/world/group menu portraits, guest/sign-in resume and four resize sizes.
  Wire: CREATE [782,836], RESKIN [0,836,0], exactly one positive-control shot
  and no UI input leakage (`gpu/wire.json`, `gpu-run.log`, `gpu/*.png`).
- `tests/skin_live_tests.py`: disposable real server with isolated settings,
  SQLite and accounts on separate loopbacks. Both native Release and sanitizer
  variants pass purchase persistence/credits=0, skin CREATE, owned/reset RESKIN,
  three actual server rejection probes and online purchase refusal without
  mutation. Exactly **RESKIN=6, actor CREATE=1, peer CREATE=1**. Logs:
  live-run.log/live.log and live-sanitize-run.log.
- Fresh shared group reference (**21**) and GPU, arrows GPU, UI resize,
  dialogs, guild board/members GPU and full isolated gameplay regression pass
  (`regressions/`). Full server evidence: 309 MOVE, 16 PLAYERSHOOT, five
  USEITEM, three SHOOTACK, three INVSWAP, one INVDROP, BUYRESULT1 popup,
  teleport/account-list/request-trade/chat and completed **empty** trade.
- `git diff --check` passed. Sibling realm-client and realm-server remain clean.

Evidence is in `/home/kevin/Desktop/RotMG/realm-cpp/build/skin-reference`.
Both builds use the existing release/sanitize presets; reproduce full suites
with `ctest --preset release` and `ctest --preset sanitize`. The sanitizer
preset excludes the SDL viewer, so visual GPU evidence is Release. Small
font/halo/antialiasing differences are accepted; no exact-pixel or whole-client
performance claim. Harness resize captures use spaced events so subsequent
resize events do not coalesce before a frame; RGB comparison avoids alpha-only
bounding-box false negatives. No completed nonempty-trade claim.

## Task 07 handoff

Player/object dyes and masks, all encountered appearance sizes, and bounded
lazy appearance caching are next. The neutral skin supplement does not solve
arbitrary sizes/dyes; current ProcessedTextures still throws for missing
appearances. Inspect AnimationFrame.mask, Entity.tex1/tex2 (stats 29/30),
Flash bitmap/textile compositor and real server updates. Preserve task 06's
class validation and authoritative skin propagation across every live portrait
and world surface. Keep source labels/order, ID snapshots, teleport map flag
and ten-second cooldown. No World pointers or optimistic skin state.

Continue the authorized chain in the same local checkout, preserving all
uncommitted/untracked work and original assets. After completing task 07,
write its handoff/update ledger and create exactly one 08-particles chat.
Check its dispatch receipt before creating; use list_projects, correct RotMG
local project, default model, save receipt and one zero-time wait snapshot.
No automation. No subagents unless explicitly authorized; if used, GPT-6 Luna.
Task 17 has no successor. Never mark unresolved acceptance gaps complete.
