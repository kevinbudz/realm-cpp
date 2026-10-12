# Realm C++

The current improvement workflow is the [native client quality chain](docs/client-quality-chain.md): one verified task per chat, covering capacity under dense workloads, code organization, UI/vector graphics, and remaining client features. Its [benchmark contract](docs/client-quality/benchmarks.md) distinguishes rendered sprites from active gameplay capacity.

The [ownership and dependency guide](docs/client-quality/architecture.md) describes
the headless game/assets/UI libraries, graphics adapters, shared application
coordinator, and the CMake/CTest checks that enforce their boundaries.

A native C++20 client using the current `realm-client` as its visual and
functional shell. Keep the UI and playfield looking basically the same,
preserve gameplay rules and server compatibility, and prioritize performance,
responsiveness, and improvements over Flash. Exact pixel matching is optional;
subpixel text and antialiasing differences are acceptable.

**The port is in progress. The startup sequence (loading, title, account
sign-in/registration, character selection and creation) and live movement and
shooting work. Several screens and rendering/gameplay systems are still
missing. This is not a finished replacement client.**

The authoritative Flash baseline is `17bdf079452a1c470b8895eae9cf108599e7addd`,
including the twelve recently pulled commits. Generated metadata fingerprints
the source and assets. The Flash, server, and C# repositories remain unchanged.

## Implemented

- C++20/CMake with SDL2/OpenGL 3.3, libpng, libxml2, zlib, and nlohmann JSON.
- All 63 packet IDs and typed incoming/outgoing codecs for this fork;
  nonblocking TCP, bounded framing/queues, login, character creation/loading,
  map updates, ticks, MOVE/PONG/GOTOACK/shoot/hit acknowledgments and reconnects.
- Original XML definitions: 2,562 objects and 214 grounds. JM and WMAP loading,
  dense entity storage, remote interpolation, local movement/collision,
  terrain speed/sinking/push, weapon timing, RNG and projectile trajectories.
- Original image/animation registrations, direction and frame fallbacks,
  mirrored frames and extended attack frames; walk/attack animation selection.
- Ordered affine quad instancing for terrain, sprites, walls, health bars,
  projectile geometry and radial shadows. No texture-based reordering.
- Orthographic camera with inverse mouse mapping, zoom, rotation, resizing,
  HUD allowance, and centered/offset positioning.
- Terrain transitions, edges/corners/composites, UV animation and overlays.
  Live terrain uses 8,192 image slots, evicts old variants, pins current-frame
  textures, and uploads only new compositions. Storage stays bounded.
- Original Flash outlines/glow can be baked once into PNGs. AIR is used only
  by development/baking tools; native runtime does not need Flash or AIR.
- Flash BitmapData color quantization and premultiplied texture upload.
  Colored translucent assets preserve the existing Stage3D appearance.
- Retained character name and XP/fame/HP/MP panel with incremental atlas
  uploads, plus minimap fog memory, zoom, guild/enemy/portal markers and arrow.
- Six-stat HUD tab with authoritative values, class caps, boost colors, click
  selection and B-key cycling through inventory/stats/backpack.
- Guild registrar with creation/renunciation dialogs, guild hall entry, and
  account name changes with rejection feedback and saved/reconnect credentials.
- Guild board read/edit/save and guild chronicle member fame/rank/slots, scrolling,
  permission-aware promote/demote/remove confirmations and server chat feedback.
- Standard Flash dialogs with wrapped body text, hover/disabled buttons, sharp
  resized text and modal Purchase Error feedback for rejected purchases.
- Cached native item tooltips with effects, ItemData modifiers, equipped-item
  comparisons, class restrictions, and scrolling for long descriptions.
- Exact framebuffer capture/comparison tools and isolated real-server tests.

## Build

Requires CMake >=3.22, a C++20 compiler, Python 3, SDL2, GLEW, OpenGL 3.3,
libpng, libxml2, FreeType, zlib, and nlohmann JSON. The presets use Ninja. Pillow is needed
for optional image comparisons. AIR SDK and Java are needed only for original
Flash reference exporters and texture baking.

```sh
cd /home/kevin/Desktop/RotMG/realm-cpp
cmake --preset release
cmake --build --preset release
ctest --preset release
```

The default source location is `../realm-client`; configure
`-DREALM_FLASH_ROOT=/path/to/realm-client` for another checkout. Source changes
regenerate the fingerprint and invalidate incompatible processed textures.
Linux/NVIDIA has been verified locally. Other platforms remain unverified.

## Run

### Client startup sequence

`realm_client` starts like the Flash client: loading screen, title screen,
character selection, then gameplay. It needs `build/fonts-client` and
`build/hud-art` (see Fonts and HUD art below). It looks for them beside the
executable or in `./build`; you can also pass `--fonts` and `--hud-art`.

```sh
./build/realm_client                                   # app server 127.0.0.1:7777, game port 2050
./build/realm_client --server HOST --app-port 7777 --port 2050
./build/realm_viewer --client                          # same thing from the harness binary
```

Click **play** on the title screen to open character selection. Click
**play** there (or a character row) to enter the game. With no characters it
opens the class grid; pick a class to create one. The server does not accept
guest logins, so the first time you play the client asks you to sign in or
register. The account is then saved to `~/.config/realm-cpp/account.json`
(`$XDG_CONFIG_HOME`, or `%APPDATA%` on Windows; override it with
`--account-file`). Later launches verify the saved account and go straight to
the logged-in title screen. In game, Escape disconnects and returns to
character selection. Death, server failures and disconnects also return
there, with a message. Escape on the title screen quits.

`--script FILE` replays timed input for headless checks. Each line is
`MS click X Y`, `MS move X Y`, `MS resize WIDTH HEIGHT`, `MS key NAME`, `MS text STRING`, `MS capture PATH.png` or
`MS quit`, where MS is milliseconds since start and X/Y are window points.
Combine it with `--hidden`.

Window resizing preserves the UI's proportions and redraws its retained surfaces
at drawable resolution, including HiDPI pixels. Enlarged text uses the original
Myriad font outlines through FreeType; menus, HUD values/labels, item/player
tooltips and guild/name dialogs no longer enlarge their base-resolution text.
The Play pulse rasterizes each animation frame at its current size and fractional
position, with reusable GPU storage. Original raster artwork retains its pixel art.
At 800x600, static text and HUD artwork keep the existing reference rendering.

### Development harness

```sh
# Bake original Flash texture filters for the Nexus and projectile appearances
python3 tools/bake_textures.py --output build/baked-nexus ../realm-server/Resources/Worlds/Nexus/0.jm

# Offline map viewer with original processed sprite appearances
./build/realm_viewer --map ../realm-server/Resources/Worlds/Nexus/0.jm --processed build/baked-nexus --fonts build/fonts-client

# Connect to an existing character; password is read from stdin
./build/realm_viewer --connect 127.0.0.1 --port 2050 --username NAME --password-stdin --character 0 --fonts build/fonts-client --hud-art build/hud-art

# Headless live protocol client
./build/realm_session --server 127.0.0.1 --port 2050 --username NAME --password-stdin --character 0 --seconds 10

# Development asset gallery and rendering stress fixture
./build/realm_viewer
./build/realm_viewer --hidden --benchmark --frames 600 --sprites 10000
```

Controls are the Flash defaults and are rebindable on the Options screen (below):
WASD moves, Q/E rotates the camera when Allow Camera Rotation is on, =/- changes
minimap zoom when the HUD is enabled (or world zoom in the development gallery),
left mouse shoots, Space uses the ability, I toggles autofire, R resets the camera
angle and Insert/F5 escape to Nexus during a live session. C toggles camera
centering in the development gallery only. Tab switches the development gallery;
Left/Right selects image sets and Up/Down changes gallery pages. Escape exits
this harness. These controls and CLI are development tooling, not the final
Flash-equivalent UI. Use `--create CLASS_TYPE` instead of `--character N` to
create a character. The headless client accepts hexadecimal class IDs.

With `--hud-art`, drag items between equipment/inventory slots or onto the
world to drop them. Double-click equips or uses; Shift-click uses consumables;
Ctrl-click transfers between main inventory and backpack. Keys 1–8 use items
in the visible inventory, and clicking the main/backpack icons switches grids.
Click the stats icon to view ATT/DEF/SPD/DEX/VIT/WIS; B cycles the available
tabs. Equipment remains interactive in the stats tab. F and V, Shift-click or
double-click on the health/magic stacks use potions. Standing within one
tile of a loot bag or chest shows its eight slots below the inventory. Drag
items between it and your slots, double-click to pick up, or Shift-click to
use. Items dropped on the world go into a nearby bag's first empty slot.
Beside a portal, its panel shows the name with an Enter button (or Full /
Locked). Click Enter or press 0 to use it. Hover equipment, inventory, backpack,
loot slots, potion stacks, or merchant item icons to see item descriptions,
effects, modifiers, class restrictions, and comparisons with equipped gear.
Long tooltips scroll with the mouse wheel. Guild registrars offer guild creation or confirmed renunciation; guild hall
portals admit guild members. Name changers open the account-name dialog and
show server rejections. This server charges 1000 gold for a changed name,
and successful changes update your saved login and reconnect credentials.
Hovering a party member shows the player tooltip; clicking one opens the
player menu (Teleport, Invite, Lock/Unlock, Trade, Ignore/Unignore).
Hover minimap player markers to see a group tooltip; click to open its menu.
Teleport selects the first visible player in the displayed order when the map
allows it, with the original ten-second cooldown. Edge markers also group.
Chat works like Flash: Enter opens the chat field (Enter again sends),
`/` starts a command, Tab starts a `/tell` to recent whisperers (Shift+Tab
for older ones), G starts guild chat, PageUp/PageDown scroll history and
Escape closes the field. Messages also appear as speech balloons above the
speaker. Trade requests appear in the interaction panel with Accept/Reject;
during a trade, click your items to offer them and press Trade once its
three-second countdown ends.

World appearances now compose missing sizes and dyes lazily in native C++.
Optional `--processed` packs supply original neutral Flash filters; an absent
appearance falls back to the native compositor. Red/green masks support solid
dyes and all four textile sets, including live skin and portrait updates.
CPU and GPU appearance caches each retain at most 256 entries / 32 MiB;
unbaked inventory icons use the existing bounded HUD cache.

## Verify

```sh
cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize
python3 tests/render_tests.py build/realm_viewer
python3 tests/flash_gpu_tests.py build/baked-nexus
python3 tests/projectile_gpu_tests.py build/baked-nexus

# Real server with temporary settings and database; original resources read only
# First build its DLL into a separate output directory:
dotnet build ../realm-server/RotMG.csproj --configuration Release --output build/test-server/bin
python3 tests/live_server_tests.py build/test-server/bin/RotMG.dll

# Optional exact-pixel diagnostic; differences alone are not release blockers
python3 tools/compare_frames.py captures/flash.png captures/native.png --output captures/comparison
```

Tests cover core math/wire formats, imported definitions/worlds, movement,
combat, animation, bounded terrain/minimap cache behavior, and independent TCP framing
and session acknowledgments. Inventory tests cover paired ItemData prediction,
FIFO rejection rollback and input, with an independent TCP peer exercising
Session's complete inventory packet flow. Real-server verification observed
CREATE, LOAD, 100 MOVE, 16 PLAYERSHOOT, 4 USEITEM, 3 SHOOTACK, 1 INVSWAP and
1 INVDROP; all three fixture sessions stayed connected. Its swap, consumable
use and drop each received successful inventory results.

### Font development

Original regular and bold Myriad fonts are already in the Flash source. The
copies found in Downloads are identical. AIR is used for offline reference
generation; native text layout, filtering and rendering run in C++.

```sh
python3 tools/bake_fonts.py --output build/fonts-client --reference-shadows
./build/realm_viewer --fonts build/fonts-client --text '100/100' --font-size 14 --font-bold --font-shadow
python3 tests/font_cpu_tests.py build/fonts-client --stress
python3 tests/font_gpu_tests.py build/fonts-client
```

The default pack includes size 13 and its half-pixel vertical coverage for the
stats tab. Rebuild older font and HUD artwork packs before using that tab.

The optional exact font comparisons expose multiline and coverage differences.
Small subpixel differences are accepted; readable text, correct wrapping, and
usable clipping/layout are the requirements. The regular/bold size-14
HP, value, level, name and basic text reference fixtures match exactly. Runtime
uses a compact binary index for logical layout and base-size coverage, loads font
styles on demand and keeps decoded glyphs in a bounded cache. Resized and animated
text additionally loads the original regular/bold OTF files from the Flash asset
root and rasterizes at physical size.

### HUD and minimap references

```sh
# Bake finite original HUD artwork; runtime is native and loads icons on demand
python3 tools/bake_hud_art.py --output build/hud-art
# Separate guild fame/rank/remove supplement; existing HUD bundle stays intact
python3 tools/bake_guild_members_art.py --output build/hud-art/guild-members
# Original skin portraits, walking previews, stars and currency art (separate supplement)
python3 tools/bake_skin_art.py
# 140 assembled portrait/equipment/inventory fixtures, including 533 items
python3 tests/inventory_hud_tests.py --rebuild-reference
# 140 stats/tab transitions across all 14 classes, with exact source comparisons
python3 tests/stats_hud_tests.py --rebuild-reference
# 126 potion widget states across all 14 classes, with exact source comparisons
python3 tests/potion_hud_tests.py --rebuild-reference
# 112 nearby container panel states across all 14 classes, with exact source comparisons
python3 tests/container_hud_tests.py --rebuild-reference
# 84 portal panel states across all 37 Portal definitions, with exact source comparisons
python3 tests/portal_hud_tests.py --rebuild-reference
# 41 default PartyPanel states; 40 exact, 1 pinned font coverage difference
python3 tests/party_hud_tests.py --rebuild-reference
# 28 ButtonPanel/ReskinPanel states (guild board, chronicle, character changer, reskin)
python3 tests/button_hud_tests.py --rebuild-reference
# Re-measure Flash ColorTransform rendering into assets/party-transforms.bin
python3 tools/bake_party_transforms.py
# Regenerate and compare 429 fixtures across all 26 animated ground definitions
python3 tests/animated_terrain_tests.py
# Four fully matching source panels; all seven CPU/GPU uploads are checked
python3 tests/hud_tests.py build/fonts-client --cases 1 2 5 6
# All original map colors and 24 unrotated views match exactly
python3 tests/minimap_tests.py --angles 0 --rebuild-reference
# Full comparisons currently fail on known glyph/rotated-arrow differences
python3 tests/hud_tests.py build/fonts-client
python3 tests/minimap_tests.py
```

The optional Flash comparisons keep strict diagnostic exit codes; those codes
alone are not product completion gates. Review visible defects at playing size
and prioritize correctness, performance, and usability. Base class portraits,
equipment, tier/dose labels and main/backpack inventory regions match the source
fixtures exactly. Each HUD artwork pack uses a 128-entry / 4 MiB decoded cache; the separate skin supplement brings the combined bound to 256 entries / 8 MiB. Party hover, player menus and edge party arrows with grouped tooltips/menus are implemented. MoneyChanger
has no panel in this Flash fork. Guild-register, guild-hall
and name-changer panels and their native dialogs are implemented. Merchant, closed vault chest, and guild
merchant purchase panels are implemented; BUYRESULT 1 opens the standard Purchase
Error dialog, while other results appear in chat. The isolated real-server
insufficient-funds purchase flow is verified. Owned/class skin selection, startup purchases, RESKIN and live skin portraits are verified. Dyes and dragged sprite visuals remain for subsequent tasks.
Minimap group tooltips/menus, edge grouping, teleport restrictions and cooldown
are verified through real Flash class captures, SDL/TCP/GPU and isolated server
checks. Minimap resizing is covered by the native CPU and GPU resize checks.

Individual reference tests verify sprite processing, terrain composition and
projectile geometry. **They do not establish complete gameplay visual quality
or a speedup over Flash.** Measure representative implemented workloads now
to guide optimization. Whole-client speedup claims require matched full-client
workloads with comparable feature coverage and visual quality.

See [the port specification](docs/port-spec.md) and
[implementation progress](docs/progress.md) for scope, evidence, and remaining work.

### Native item tooltip checks

```sh
# Content, hover routing, class restrictions, comparison values and cache reuse
./build/realm_tooltip_tests
# Render every imported item and save representative native images for review
./build/realm_tooltip_tests build/fonts-client build/hud-art build/tooltip-reference
```

Tooltips use one retained surface with a logical 256x580 viewport. Its texture
resizes with drawable density. Pointer movement repositions
the overlay; item/comparison changes rebuild it, and scrolling copies a slice
of the existing content. These checks use native layout and gameplay data.

### Native resize and pulse checks

```sh
./build/realm_ui_resize_tests build/fonts-client build/hud-art
python3 tests/ui_resize_gpu_tests.py
```

These cover fractional/enlarged/shrinking densities, restored base rendering,
retained-cache invalidation, resized menu input and tooltips at 4K. The GPU runner
also exercises 720 Play pulse frames, checking that native-size text quads reuse
texture storage. It supplies an isolated local HTTP fixture and uses no live account.

### Native guild/name interaction checks

```sh
./build/realm_interaction_tests
./build/realm_interaction_tests build/fonts-client build/hud-art build/interaction-reference
# Scripted native app, isolated wire/web peers, screenshots at two playing sizes
python3 tests/social_hud_tests.py
# One scenario, with CPU timing and sampled Linux process memory
python3 tests/social_hud_tests.py --case create --width 800
```

CTest also runs an independent social TCP peer that checks request/reply
handling and renamed credentials on reconnect. The scripted name scenarios
check saved-account startup, name persistence and returning to character
selection with the new login. These fixtures do not establish live-server
purchase verification or representative whole-client performance.

### Guild board

The nearby Guild Board panel's View button opens the native board. Officers and
higher ranks can Edit, Cancel or Save; other members can read and Close. The
multiline editor supports selection, clipboard and scrolling. Loading/Saving
run native HTTP asynchronously; request errors offer Retry and Close/Back.
The game stays behind the source-style modal backdrop.

```sh
python3 tests/guild_board_reference_tests.py
python3 tests/guild_board_gpu_tests.py
python3 tests/guild_board_live_tests.py ../realm-server/bin/Release/net8.0/RotMG.dll
```

Evidence and source/native captures are documented in
[the guild board handoff](docs/task-handoffs/02-guild-board.md).

### Options

O, or the wrench button at the top right of the character details (Nexus only; every
other map shows the Nexus button), opens the Flash Options screen: Controls, Gameplay,
Hot Keys, Chat, Graphics and Sound tabs of key mappers and choice boxes. Click a key
box and press the new key (Escape cancels); binding a key unsets the action that had
it. "reset to defaults" resets the visible tab, "back to home" leaves the game, and
O or Escape closes the screen. Every change is applied immediately and saved to
`settings.json` beside the account file (`--settings-file PATH` overrides it, and the
development harness keeps the defaults unless it is given one). Game controls and
chat keys are suppressed while the screen is open.

### Guild chronicle

The nearby Guild Chronicle panel's View button opens the native member screen.
It shows current guild/member fame, stored member order, ranks and open slots.
Officers and higher ranks see the actions their Flash rank permits. Each action
requires confirmation; server chat replies drive refresh or an Error dialog.
HTTP failures offer Retry/Close. Continue closes the screen; game controls are
suppressed while it is open.

The icon supplement is required beside the HUD pack; bake it with the command
above. Runtime stays native and does not need AIR.

```sh
python3 tests/guild_members_reference_tests.py
python3 tests/guild_members_gpu_tests.py
python3 tests/guild_members_live_tests.py ../realm-server/bin/Release/net8.0/RotMG.dll
```

Evidence and comparison limits are recorded in
[the guild members handoff](docs/task-handoffs/03-guild-members.md).

### Combat particles

The native scene renders SHOWEFFECT abilities, AOE, sprite flashes and camera
jitter, blood/debris, projectile trails, level-up/healing particles and registered
object emitters. Particles expire and map replacement/disconnect clears their
state. Solid particles use one shader path without per-color texture allocation;
textured particles share the bounded appearance caches.

```sh
python3 tests/particle_reference_tests.py
python3 tests/particle_gpu_tests.py
python3 tests/particle_live_tests.py ../realm-server/bin/Release/net8.0/RotMG.dll
```

Runtime stays native; the first command uses AIR only to capture actual Flash
classes offline. Reference snapshots share randomized source geometry for visual
comparison; independent CTest and network fixtures validate native simulation.
See [the particle handoff](docs/task-handoffs/08-particles.md) for inventory,
resource bounds, evidence and accepted visual differences.

### Lighting, backgrounds and post effects

Flash has no per-map lighting (model shading is the fixed task 09 rule), so this covers
what it does draw around the world: the Drunk blur and Blind filters on the map view
(HUD and overlays untouched), the low-breath red vignette, the 10 px sidebar gradient,
and the MAPINFO star/Nexus-water backgrounds. The world is rendered offscreen only
while a filter is active; sizes scale with the window like the HUD.

```sh
python3 tests/postfx_reference_tests.py
python3 tests/postfx_live_tests.py ../realm-server/bin/Release/net8.0/RotMG.dll
ctest --test-dir build -R postfx
```

The first command needs the AIR SDK and compares the real `BlurFilter`, `ColorMatrixFilter`,
`HurtOverlay`, `GradientOverlay` and background classes against the native renderer.
The viewer can force the fixtures with `--condition 64|256|320 --breath N --background 1|2
--effect-time MS`. See [the lighting handoff](docs/task-handoffs/10-lighting.md).
