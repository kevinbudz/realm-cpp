# 07-dyes-masks — complete

Completed 2026-10-11. Next: **08-particles**.

## Implementation and source behavior

Native `Appearances` composes solid dyes and 4x4/5x5/9x9/10x10 textiles through
red/green masks. The embedded source shader was inspected directly: mask alpha
zero preserves the source; red chooses tex1, otherwise green chooses tex2;
channel brightness shades the chosen RGB. Operation zero preserves the source.
Texturing happens at five times source density, with patterns anchored to the
whole frame, followed by Flash's integer size/5 rule. Mirrored masks and extended
attack frames come from AnimationLibrary. Explicit XML Mask is imported too.
Unmasked art retains its colors. Source pixel alpha is normalized before
composing animated appearances; the CPU/GPU paths preserve their respective
bitmap blending conventions.

Scene uses the optional existing processed pack for available neutral keys and
native compositions for dyes or any unbaked size. Missing processed keys no
longer abort a frame. Existing packs and their validation are retained. Runtime
stays native C++/SDL/OpenGL; AIR is only used by offline reference exporters.
Merchant world sprites show their sold item; nearby dye merchants preview only
the local world sprite through source ordered-cell/strict-distance rules. No
preview mutates authoritative dyes or portraits.

Stats 29/30 and size/skin changes update world appearances and the shared live
HUD/party/player tooltip/menu/minimap and arrow-group portraits. Menus refresh
only appearance fields, preserving labels, ordered IDs and data snapshots.
No menu holds World pointers. Skin ownership/class checks, startup metadata,
teleport map flags and the ten-second cooldown are preserved. Actual dye use
still goes through Session/inventory and waits for server appearance stats.

Missing finite baked item/dim icons now use the same native XML-mask compositor
and the bounded HUD decoded cache. Native tier text handles an absent baked tier
record. This fixes cloth inventory/tooltip rendering found by the real server
fixture without replacing the existing HUD/art/font packs.

Relevant native files: include/realm/{appearance,catalog,hud,hud_art,processed,
player_menu,player_tooltip,renderer,scene,world}.hpp; src/{appearance,catalog,hud,
hud_art,player_group,player_menu,player_tooltip,main,renderer,scene,world}.cpp.
Source: com/company/assembleegameclient/{objects/TextureData.as,objects/Player.as,
objects/GameObject.as,objects/Merchant.as,objects/ObjectLibrary.as,
util/TextureRedrawer.as,util/TextureRedrawer_textureShaderEmbed_.dat,
util/redrawers/GlowRedrawer.as}; server Game/Entities/Player.UseItem.cs and
Common/Models.cs. Protocol unchanged.

## Bounds and visual differences

- CPU compositions: LRU **256 entries / 32 MiB**. Raw image sets and animation
  sources are finite. GPU appearances: separate LRU **256 entries / 32 MiB**;
  weak CPU owners prevent the GPU cache pinning evicted CPU bitmap copies.
- Already-built quads retain their GPU surfaces until frame submission. Cache
  eviction cannot substitute/delete their textures. Temporary frame owners are
  released on the next quad clear; this is distinct from retained cache bounds.
- World identity plus generation clears CPU/GPU caches on map replacement,
  including transitions between background and session worlds.
- Raster content is capped at 1024 per axis for hostile very large size stats,
  while density preserves authoritative world placement. Source-sized art and
  tested normal sizes remain at native density. Padding adds at most 24 pixels.
- HUD packs still use 128 entries / 4 MiB each, including generated icons;
  with the skins supplement the combined bound is 256 / 8 MiB.
- Source/native sprites and portrait rows match at a glance. Small halo,
  outline-alpha, gradient rounding and fractional nearest-boundary differences
  remain. Enlarged text stays sharp via existing drawable-density rendering.
  No exact-pixel or whole-client performance claim is made.

## Verification

Final logs/captures are collected in `build/dye-reference`.

- Final Release full CTest: **31/31, 20.04 seconds**.
- Final ASan/UBSan full CTest: **31/31, 67.45 seconds**.
- Dye suite: **2600 checks**, including generated cloth icons and cache bounds.
- Actual SDL/OpenGL/TCP fixture passes 322 live changes, 300 simultaneous
  appearances, map teardown, resized portraits, USEITEM and shot control.
  After teardown CPU/GPU each hold **6 entries / 211952 bytes**. Earlier churn
  reached both 256-entry limits, with CPU 8846880 / GPU 11084808 bytes.
- Both final native Release and sanitizer real-server dye fixtures pass:
  **USEITEM=4, RESKIN=1**, remote observation and persisted dyes/consumption.
- Fresh shared skin/group/arrow/resize/dialog/board/members GPU checks pass;
  shared real Flash group reference **21/21** passes too.
- Full isolated gameplay/server regression passes: 310 MOVE, 16 PLAYERSHOOT,
  five USEITEM, three SHOOTACK, three INVSWAP, one INVDROP, teleport/account
  lists/request-trade/chat, BUYRESULT1 dialog and the completed **empty** trade.
  No completed nonempty-trade claim is made.
- `git diff --check` passes; sibling Flash/server checkouts remain clean.

Reproduce with `ctest --preset release`, `ctest --preset sanitize`,
`python3 tests/dye_reference_tests.py`, `python3 tests/dye_ui_reference_tests.py`,
`python3 tests/dye_gpu_tests.py` and
`python3 tests/dye_live_tests.py ../realm-server/bin/Release/net8.0/RotMG.dll`
(add `--client build-sanitize/realm_dye_session_inputs` for sanitizer).
The sanitizer preset excludes the SDL viewer, so GPU evidence is Release;
CPU/cache/network evidence covers both native variants. The fixed processed
pack in the GPU fixture is `build/outline-art` and supplements neutral skins.

Source comparisons:
130 real embedded-shader/TextureRedrawer fixtures across five class/skin
animations, directions/actions, solid/textile dyes, padded portraits and
sizes 50/100/137/200/350; plus static masked/unmasked objects. Dimensions and
opaque interior colors pass. Boundary/halo differences stay visible in pairs.
24 real PlayerGroupToolTip/PlayerGroupMenu fixtures exercise source dyed
Player.getPortrait at base, 720p, 1200p and 4K densities. Frame metrics match;
normal and resized pairs were visually inspected. Existing group reference
regression also passes all 21 cases.

The CTest dye suite exercises every one of 2555 textured catalog definitions,
13 live size samples through 65535, all textiles, object masks, lazy hits,
count/byte eviction, retained bitmap lifetime, live snapshots, merchant preview
and missing cloth icon generation. Actual SDL/TCP/OpenGL fixture checks one
USEITEM, live world/HUD/menu dyes, skin/size changes, unbaked lookups, resized
portraits, 322 stat changes, 300 simultaneous appearances across one frame,
map replacement/cache teardown and one positive-control shot. Real-server
fixtures use disposable settings/SQLite/accounts, check four actual cloth
USEITEMs, remote observation, stored size137, HUD/group/menu/tooltip refresh,
no optimistic dye changes, skin preservation and HTTP/SQLite persistence.
Exactly USEITEM=4 and RESKIN=1; all consumed slots are empty afterward.

## Task 08 handoff

Implement **08-particles** next: inventory actual Flash effect/particle events,
blood/trails, ability/projectile effects, lifetimes and map teardown. Inspect
source before scoping the implementation; preserve authoritative combat and
ordered rendering. The source/native renderer now supports bounded lazy masked
appearances; do not return to exhaustive baking or throw for unbaked sizes.
Use native runtime, preserve fonts/HUD/backups and all current snapshots/input
restrictions. Existing source and GPU fixtures can be extended for effects.

Continue the authorized sequential chain in the same local checkout. Preserve
all uncommitted/untracked work and sibling repos. After completing task 08,
record its handoff/ledger and create exactly one 09-models chat via list_projects
and the RotMG local project; check its dispatch receipt first, save the new
receipt and take one zero-time wait snapshot. Default model. No automations or
subagents requested; any later authorized subagent must be GPT-6 Luna. Task 17
has no successor. Do not certify incomplete acceptance checks.

Successor dispatched exactly once: `01a12b91-453f-70e3-a1b7-6529840a4d2d`
on local RotMG project. Receipt: `08-particles-dispatch.json`. The single
zero-time wait snapshot confirms the chat is active and has begun source reads;
saved in `08-particles-dispatch-snapshot.json`. No automations or subagents.
