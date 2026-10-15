# Realm C++

A native C++20 client for the RotMG game served by `../realm-server`. It renders with OpenGL 3.3 and SDL2, builds its menus and HUD with RmlUi, and talks to the existing app and game servers over the same protocol. The original art, object XML, fonts, and sounds come from the Flash client source in `../realm-client`: the build scans that tree with `tools/import_flash.py`, and at run time fonts and UI art are read from it through `--flash-root`. The goal is a client that looks and plays basically like the original and performs better. Pixel-level parity with Flash is not a goal; gameplay rules and server compatibility are.

## Coverage

- **Startup:** loading, title, sign-in and registration, character selection and creation, saved accounts, and clickable news with fame details or browser links.
- **Play:** movement, shooting and abilities, inventory and potions, portals, parties and quest targets with edge arrows and tooltips, player menus, trading, guild invitations, chat and speech balloons. Davy Jones's Locker has its key HUD; idle players receive warnings and disconnect after 20 minutes.
- **Guilds:** registrar, guild hall entry, guild board, chronicle, and account name changes.
- **World:** JM and WMAP maps, terrain transitions, 3D models, connected walls, cave walls, stalagmites, multiline signs, death particles, status effects, Drunk and Blind filters, dyes, and skins. Tutorial maps show proximity-driven HUD highlights.
- **UI:** the RmlUi HUD and menus, minimap, item and player tooltips, dialogs, and an Options screen with rebindable keys. Item tooltips retain the Astrum styling.
- **Audio:** one-shot sound effects through the vendored miniaudio output.
- **Map editor:** ground, object and region palettes with original art; draw, erase, sample, named objects, rectangles, pan/zoom, undo/redo, JM load/save, and existing-character map testing.

## Build

### Prerequisites

- CMake 3.22 or newer, Ninja (the presets use it), Python 3, and a C++20 compiler.
- Found with `find_package`: libxml2, zlib, libpng, FreeType, and nlohmann_json. The client (`REALM_BUILD_VIEWER=ON`, the default) also needs SDL2, OpenGL, and GLEW. Pillow is needed by some GPU test scripts (see [tests/README.md](tests/README.md)).
- Vendored under `third_party/`, so configuring needs no network: RmlUi 6.3 ([VENDORED.md](third_party/rmlui/VENDORED.md)) and miniaudio 0.11.25 ([README](third_party/miniaudio/README.md)). Only the optional Tracy profiler (`REALM_TRACY=ON`) is downloaded.
- Sibling checkouts: `../realm-client` is required, since the build scans it into generated headers. `../realm-server` supplies world files for `--map`, the default sound-effect root, and the .NET project used by live tests.
- Linux is the verified platform. The code has Windows branches (`%APPDATA%`, `ws2_32`) that remain unverified.

### Build and test

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release
```

The `release` preset builds into `build/`, including `build/realm_client`. Generated headers from the Flash source go to `build/generated/`. The `sanitize` preset builds into `build-sanitize/` with AddressSanitizer and UBSan and `REALM_BUILD_VIEWER=OFF`, so it has no `realm_client` and no GPU tests:

```sh
cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize
```

### Configure options

| Option | Default | Effect |
|---|---|---|
| `REALM_FLASH_ROOT` | `../realm-client` | Flash source to import (also the default `--flash-root`) |
| `REALM_SFX_ROOT` | `../realm-server/Resources/Web/sfx` | Default sound-effect root; the environment variable overrides it at run time |
| `REALM_BUILD_VIEWER` | `ON` | Build `realm_client` and the OpenGL, SDL, and RmlUi libraries |
| `REALM_AUDIO` | `ON` | Build native sound-effect output (`realm_audio`) |
| `REALM_SANITIZERS` | `OFF` | Enable AddressSanitizer and UBSan |
| `REALM_TRACY` | `OFF` | Fetch and enable the Tracy profiler |
| `REALM_ALLOCATION_TRACKING` | `OFF` | Count main-thread allocations in telemetry |
| `REALM_USE_LLD` | `ON` | Use lld where supported; Release keeps GNU ld |

## C++ readability

Install the pinned formatter, then format or check the hand-written C++ files:

```sh
python3 -m pip install clang-format==20.1.8
python3 tools/format_cpp.py
python3 tools/format_cpp.py --check
```

The checked-in rules use a 100-column limit, braced control flow, and space between
definitions. The command covers `src`, `include`, `tests`, and `tools`, and honors
the generated-code exclusions in `.clang-format-ignore`. Vendored dependencies
and build outputs are outside its scope.

Within functions, separate logical steps with blank lines and keep declarations
separate. Prefer short, clear, one-word local names when their meaning is
unambiguous; conventional coordinates and loop indices can stay short. Readability
changes must preserve evaluation order, numerical behavior, and public APIs.

## Run

### Startup flow

```sh
./build/realm_client
./build/realm_client --server HOST --app-port 7777 --port 2050
```

Defaults: server `127.0.0.1`, app port `7777` (account and character HTTP API), and game port `2050`. `--server` sets the host for both.

The client starts like the Flash client: loading, title, character selection, then gameplay. The server does not accept guest logins, so the first time you play it asks you to sign in or register. The account is saved to `~/.config/realm-cpp/account.json` (under `$XDG_CONFIG_HOME` if set; `%APPDATA%\realm-cpp\account.json` on Windows), or to `--account-file`. Later launches verify it and go straight to the title screen.

Controls, from `realm_client --help`. They use the Flash defaults and can be rebound on the Options screen:

- WASD moves. Q and E rotate the view when Allow Camera Rotation is on.
- Drag items to swap or drop them. Double-click equips or uses, Shift-click uses, Ctrl-click moves between inventory and backpack, and keys 1 to 8 use inventory slots.
- F or V, or Shift-click or double-click the health or magic stacks, uses a potion.
- Click Enter, or press 0, beside a portal to use it.
- O opens the Options screen. Escape in game returns to character selection.

Options are saved to `settings.json` beside the account file. `--settings-file` overrides the path.

### Map editor

Choose **EDITOR** on the title screen. D draws, E erases, A samples, I edits an object's name, U/R undo and redo,
and C clears the map. Shift-drag draws a rectangle, Ctrl-drag or the middle mouse button pans, and the wheel zooms.
The palette has Ground, Object and Region tabs, a filter, tooltips, and its own wheel scrolling.

L and S open path dialogs for loading and saving Flash `.jm` maps. T tests the current map using the first saved
character; testing requires a registered account and a character. Ending the test, dying or following a portal
returns to the editor with the document preserved. Escape or Back returns to the title screen.

### Product flags

| Flag | Default | Meaning |
|---|---|---|
| `--server HOST` | `127.0.0.1` | Server host (app and game) |
| `--app-port N` | `7777` | App (account and character API) port |
| `--port N` | `2050` | Game server port |
| `--account-file PATH` | per-user config directory | Saved account |
| `--settings-file PATH` | beside the account file | Saved options |
| `--flash-root PATH` | configured `REALM_FLASH_ROOT` | Flash source for fonts and UI art |
| `--width N`, `--height N` | `800`, `600` | Window size, 64 to 8192 |
| `--audio auto\|on\|off\|null` | `auto` | Sound output; `auto` plays only in the interactive client |
| `--hidden` | off | Hide the window; needs `--frames N` or `--script` |

### Test-only modes

These replace the startup flow, cannot be combined, and are meant for fixtures and tests:

- **`--connect HOST`** joins a live server directly. It takes `--username NAME` and `--password-stdin` (the password is read from stdin), then `--character N` or `--create CLASS_TYPE`, with optional `--game N`.

  ```sh
  ./build/realm_client --connect 127.0.0.1 --port 2050 --username NAME --password-stdin --character 0
  ```

- **`--map PATH.jm|PATH.wmap`** runs an offline world with no server. Fixture flags set status and scene state: `--hp`, `--condition`, `--breath`, `--ghost`, `--ally`, `--ally-name`, `--notify TEXT[:RRGGBB]`, `--damage`, `--damage-effects`, `--status-age`, `--effect-time`, and `--scene-time`.

  ```sh
  ./build/realm_client --map ../realm-server/Resources/Worlds/Nexus/0.jm
  ```

- **`--stress MANIFEST.json`** runs a rendered offline stress scene. It needs `--frames N` and `--stress-summary RESULT.json`; `--stress-delay MS` takes 0 to 100. Manifests live in `tests/fixtures/`.

Run control and diagnostics:

- `--frames N`, `--benchmark` (600 frames when none are given; vsync and Max FPS off), `--fixture-step-ms 20` (fixed clock, needs finite frames), and `--capture PATH.png`.
- `--telemetry PATH.jsonl [--trace PATH.json] [--telemetry-warmup N]` writes telemetry; the warm-up count defaults to 30.
- `--script FILE` replays timed input lines. The grammar is in `--help`.

`realm_client --help` prints the complete list.

### Environment variables

| Variable | Effect |
|---|---|
| `REALM_AUDIO=0` | Disable sound output |
| `REALM_AUDIO_DEVICE=null` | Select the null audio output |
| `REALM_AUDIO_CACHE_MB=N` | Decoded sound cache size, 1 to 4096 MiB |
| `REALM_SFX_ROOT=PATH` | Sound-effect root (`NAME.mp3` files) |
| `REALM_QUAD_CHECKSUM=1` | Enable the painter-stream digest, written as `quad_stream_checksum` in stress summaries |
| `REALM_UI_ACTION_TRACE` | Print startup actions to stderr when set |

## Testing

Tests run through CTest. `ctest --preset release` runs everything; `ctest --preset sanitize` runs the headless tests under sanitizers.

- **`unit`**: headless, no GL context or display (`ctest --test-dir build -L unit -j 6`).
- **`gpu`**: opens an SDL window or GL context, so it needs a display and OpenGL 3.3. GPU tests share a lock and never overlap.
- **`live`**: no CTest test uses a live server. The `*_live_tests.py` scripts run by hand against a server build:

  ```sh
  dotnet build ../realm-server/RotMG.csproj --configuration Release --output build/test-server/bin
  python3 tests/live_server_tests.py build/test-server/bin/RotMG.dll
  ```

Labels, fixtures, and the scripts that CTest does not run are described in [tests/README.md](tests/README.md).

To check that a change to rendering or ordering does not alter the painter stream, compare the stress summary before and after it. The same manifest must produce the same `quad_stream_checksum`:

```sh
REALM_QUAD_CHECKSUM=1 ./build/realm_client --hidden --stress tests/fixtures/S1-mixed-dense.json --stress-summary OUT.json --frames 120
```

This run needs a display.

## Architecture

### CMake targets

Headless libraries, always built. Each row lists what the library owns and what it links:

| Target | Owns | Links |
|---|---|---|
| `realm_build_options` | Include paths (`include/`, `build/generated/`), warnings, sanitizer and profiler flags | (interface) |
| `realm_support` | Camera, projectile math, telemetry, dynamic-atlas residency, shared draw types | build options |
| `realm_platform` | Headless platform queries (such as resident memory) | build options |
| `realm_network` | Packet codecs (`Protocol`, `Messages`), nonblocking TCP (`SocketServer`), blocking HTTP POST (`AppEngineClient`) | `realm_support` |
| `realm_audio` | One-shot sound output and decoding (`REALM_AUDIO`) | `realm_network`, `realm_miniaudio` |
| `realm_game` | World, entities, session, movement, combat, inventory, party, account, settings, catalog | `realm_network`, libxml2, zlib, nlohmann_json |
| `realm_assets` | Decoded Flash assets: images, models, animations, fonts, terrain, HUD art, appearance caches | `realm_game`, libpng, FreeType |
| `realm_ui` | Headless UI logic: HUD, minimap, tooltips, chat, dialogs, guild screens, trade, options, `ui::Stack` | `realm_assets` |
| `realm_fixtures` | Stress scene generator (`StressTimeline.cpp`) | `realm_assets` |

Viewer libraries, built when `REALM_BUILD_VIEWER=ON`:

| Target | Owns | Links |
|---|---|---|
| `realm_render` | OpenGL renderer: streamed uploads, post-processing, GPU timers, atlases, scene culling and draw decisions, backdrops | `realm_assets`, OpenGL, GLEW |
| `realm_ui_gpu` | GPU minimap composition | `realm_ui`, `realm_render` |
| `realm_platform_sdl` | SDL2 window, input, and GL context | `realm_platform`, SDL2 |
| `realm_startup` | Startup screens: controller, HTTP task service, retained view model | `realm_ui`, `realm_render` |
| `realm_ui_rmlui` | The one RmlUi context, the embedded `ui/*.rml` and `ui/*.rcss` files, and the vendored RmlUi GL3 and SDL backends | `realm_ui`, RmlUi, SDL2, OpenGL |
| `realm_ui_views` | RmlUi presentation of retained surfaces and the startup screens | `realm_ui_rmlui`, `realm_startup`, `realm_render` |
| `realm_application` | The `realm_client` coordinator: frame loop, layers, options, script replay, fixtures, audio bridge | the viewer libraries, `realm_audio` |

Executables:

- **`realm_client`** (`src/Main.cpp`): the client. It links `realm_application`.
- **`realm_session`** (`src/SessionMain.cpp`): a headless live protocol client, used by loopback tests.
- **`realm_stress`** (`tools/StressInputs.cpp`): the stress-test driver. It is not a product binary.
- **`realm_*_session_inputs`**, **`realm_delete_stale_inputs`**, **`realm_skin_*_inputs`**: scripted drivers that the Python loopback tests run.
- **`realm_*_tests`**: test executables registered with `realm_add_test`. See [tests/README.md](tests/README.md).

### Boundaries

Dependencies point downward. Game and asset code never include SDL, OpenGL, or RmlUi headers, and GL headers are allowed only in `realm_render`, `realm_platform_sdl`, and `realm_ui_rmlui`.

- `cmake/ClientBoundaries.cmake` fails configuration when a boundary library links anything outside its allow list.
- `tests/client_boundaries_tests.py` (CTest `client_boundaries`) rejects upward includes and GL includes outside the owners.
- Every header under `include/realm/` and every internal header under `src/` (such as `src/core/ApplicationState.hpp`) must have an owner in `cmake/header-owners.json`. The test fails on a missing entry.

To add a source file, list it in the target's `realm_library(...)` call in `cmake/ClientTargets.cmake`. To add a header, add it to `cmake/header-owners.json`.

### Source map

Domain folders keep recognizable RotMG names (`objects/`, `map/`, `messaging/`, `hud/`, `ui/`, `screens/`, ...), while asset, HUD, and AppEngine subsystems are grouped by their C++ ownership rather than Flash's utility grab-bags or Robotlegs subpackages. A file is named after the C++ type it defines, so searching for a type finds its file. When the type ports an AS3 class with a different name or package, the header's first comment line points to it, for example `// Flash: com/company/assembleegameclient/map/Map.as` in `map/World.hpp`. The table under [Flash names](#flash-names) lists every such pair. A public header lives at `include/realm/<package>/<File>.hpp` and its implementation at `src/<package>/<File>.cpp`.

A few domain folders span paired data and GPU/view layers (`map/`, `background/`, `minimap/`, `stage3D/`, `lib/ui/`). The library layering in [Boundaries](#boundaries) is defined by `cmake/ClientTargets.cmake` and `cmake/header-owners.json`.

| Package | Contents | Flash counterpart |
|---|---|---|
| `objects/`, `objects/particles/` | `Entity`, `Catalog` and `ObjectProperties`, `Movement`, `Combat`, `ProjectileProperties`, `Party`, `StatMap`, `ConditionEffect`, `ItemData`, `StatusArt`, `Particles`, `ParticleProperties` | `assembleegameclient/objects`, `util/ConditionEffect` |
| `map/`, `map/mapoverlay/`, `map/partyoverlay/` | `World` (map and entity store), `Camera`, `Terrain`, `GroundProperties`, `Scene` (entity visibility and draw decisions), `PostEffects`, `EntityIndex`, `EntitySets`, world feedback, particle and sound events, `SpeechBalloons`, `PartyArrows` | `assembleegameclient/map` |
| `messaging/`, `net/`, `appengine/` | `Protocol` and `Messages` (generated by `tools/generate_messages.py`; do not edit by hand), `Session` (connection lifecycle and packet handling), `Transport` (nonblocking TCP), `Http` (HTTP form POST), `SavedCharactersList`, `CharacterFame`, `Legend`, `GuildMemberList`, and the other account replies | `kabam/rotmg/messaging`, `kabam/lib/net`, `assembleegameclient/appengine`, `fame`, `legends` |
| `parameters/` | `Settings` (persistent settings and the options table) | `assembleegameclient/parameters` |
| `sound/` | `SoundEffectLibrary` (mixer, loader, and output in `realm_audio`) and `SoundCues` (headless sound requests). miniaudio is vendored in `third_party/miniaudio` | `assembleegameclient/sound` |
| `game/` | `Inventory`, `IdleWatcher` (pure game rules and idle disconnect timer) | `kabam/rotmg/game`, `assembleegameclient/game` |
| `hud/` | `Hud` and `HudState`, `HudInput`, `CreditDisplay`, `SafeAreaDisplay`, `KeysView` | `kabam/rotmg/ui/view` (`HUDView`, `KeysView`), `kabam/rotmg/game/view` |
| `ui/` and subpackages | `Chat`, `Trade`, `Scrollbar`, `SkinChooser`, `GraphicsUtil`, `dialogs/`, `tooltip/`, `menu/`, `options/OptionsScreen`, `guild/GuildMembers`, `board/GuildBoard`, `panels/GuildInvite` | `assembleegameclient/ui`, `kabam/rotmg/classes/view` |
| `minimap/` | `Minimap` and its GPU composition | `kabam/rotmg/minimap` |
| `screens/` | Startup screens. `Startup.cpp` is the public controller. `StartupUpdate`, `StartupInput`, `StartupDialogs` and `StartupFame` hold the rest of its implementation. `StartupState.hpp` holds screen and account state, `StartupHttpTasks` HTTP task ownership, `StartupViewModel` the retained view model, and `StartupRml` the RmlUi presentation | `assembleegameclient/screens`, `kabam/rotmg/account/web/view` |
| `mapeditor/`, `tutorial/` | `MapDocument` (JM files and edit history), `EditorScreen` (headless editor UI), proximity requirements and HUD highlights | `assembleegameclient/mapeditor`, `tutorial` |
| `stage3D/` | `Renderer` (GL resources and frame submission) with `RendererStream`, `RendererPostProcess`, `RendererGpuTimers` and `RendererShader`, plus `Atlas`, `TextureResidency` (dynamic-atlas allocator) and `DrawTypes` | `kabam/rotmg/stage3D` |
| `assets/`, `engine3d/`, `background/` | `Image`, `AnimationLibrary`, `Appearances` (dye and skin composition), `ProcessedTextures`, `FlashDraw`, `FontBook` (FreeType with the original Myriad fonts), `HudArt` (native drawing of Flash UI art), `Model3D`, `ConnectedObjects`, `Backdrop` and `BackdropData` | `kabam/rotmg/assets`, `assembleegameclient/util`, `com/company/ui/fonts`, `engine3d`, `background` |
| `util/` | `Random` (`FlashRandom`), `Xml`, `Telemetry`, `Utf8`, `TextFormat`, `AllocationTracking` | `com/company/util`, `assembleegameclient/util` |
| `lib/ui/` | `Stack` (`ui::Stack`, which owns coordinates and input; each screen is a layer), `RmlHost` (the single RmlUi context), and `RetainedUi` (retained CPU snapshot surfaces) | `kabam/lib/ui` |
| `core/` | The `realm_client` coordinator. `Application.cpp` (`run`), `ApplicationLoop*.cpp` (per-frame phases: input, update, panels, render), `ApplicationState` (`ClientState`), `ApplicationLayers*.cpp` (screen layers in `ui::Stack` order), `ApplicationOptions` (command line), `ApplicationScript` (`--script`), `ApplicationFixtures` (offline `--map` loading), and `ApplicationAudio` | `kabam/rotmg/core`, `GameSprite` |
| `platform/` | `Platform`, `PlatformSdl` (window, input, GL context), `SdlKeyCodes` | — |
| `fixtures/` | `StressTimeline` (stress scene generator) | — |

Headers generated from the Flash source (`build/generated/flash_*.hpp`) come from `tools/import_flash.py` and `tools/compile_*.py`.

### Flash names

Types whose AS3 class has a different name:

| C++ type (file) | AS3 class |
|---|---|
| `World` (`map/World`) | `Map` |
| `Entity` (`objects/Entity`) | `GameObject` |
| movement rules (`objects/Movement`) | `Player` |
| `Catalog` (`objects/Catalog`) | `ObjectLibrary` |
| `Session` (`messaging/Session`) | `GameServerConnection` |
| `net::Transport` (`net/Transport`) | `SocketServer` |
| `Settings` (`parameters/Settings`) | `Parameters` |
| `Atlas` (`stage3D/Atlas`) | `SpriteAtlas` |
| `Appearances` (`assets/Appearances`) | `TextureRedrawer` |
| `AnimationLibrary` (`assets/AnimationLibrary`) | `AnimatedChars` |
| `GuildBoard` (`ui/board/GuildBoard`) | `GuildBoardWindow` |
| `GuildMembers` (`ui/guild/GuildMembers`) | `GuildPlayerList` |
| `PartyArrows` (`map/partyoverlay/PartyArrows`) | `PartyOverlay` |
| `ItemTooltip` (`ui/tooltip/ItemTooltip`) | `EquipmentToolTip` |
| `PlayerTooltip` (`ui/tooltip/PlayerTooltip`) | `PlayerToolTip` |
| `SavedCharacterTooltipImage` (`ui/tooltip/SavedCharacterTooltip`) | `MyPlayerToolTip` |
| `SkinChooser` (`ui/SkinChooser`) | `CharacterSkinListView` |
| `Trade`, `TradeView` (`ui/Trade`) | `TradePanel` |
| `OptionsScreen` (`ui/options/OptionsScreen`) | `Options` |
| `Minimap` (`minimap/Minimap`) | `MiniMap` |


## Code style

- `.clang-format` is the Google base style with 120 columns, `SortIncludes: Never`, and pointers attached to the type (`int* p`). Format changed files with `clang-format`. Generated files are listed in `.clang-format-ignore`.
- Functions and variables are `snake_case`. Data members end with an underscore (`hud_revision_`). Types are `PascalCase`.
- Headers use `#pragma once`. File names are PascalCase. Most `src/<package>/<File>.cpp` files have a same-named header (`src/map/World.cpp` pairs with `include/realm/map/World.hpp`). The layer files, the entry points, and a few others, such as `ItemTooltipContent.cpp`, do not.
- Include public headers as `"realm/map/World.hpp"`, with the `realm/` prefix.
- Keep comments to behavior and reasons. Do not add task IDs or process labels.

## Glossary

Comments and names refer to classes from the original ActionScript 3 client when the native code mirrors their rules. The common ones:

| Term | Meaning |
|---|---|
| Flash source, `--flash-root` | The AS3 client tree in `../realm-client`; art, XML, fonts, and sounds are read from it |
| `flash::` namespace, `build/generated/flash_*.hpp` | Tables generated from the Flash source by `tools/import_flash.py` and `tools/compile_*.py` |
| `realm::flash_draw` | Native drawing helpers that reproduce Flash `Graphics`/`BitmapData` vector and bitmap drawing |
| SWF | Flash's binary movie container; `flash_draw` parses vector shapes from it |
| `BitmapData` | AS3 pixel buffer; Flash colors are stored premultiplied (`premultiply_flash_bitmap`) |
| `TextField`, FTE, `SimpleText` | AS3 text classes; `FontBook.cpp` mirrors their metrics (twip advances, 2 px gutter) |
| Myriad | The embedded font faces from the Flash source, rasterized with FreeType |
| `DisplayObject`, `Sprite`, display list | AS3 scene-graph classes; UI layout and child order follow them |
| `ColorTransform`, `ColorMatrixFilter`, `BlurFilter` | AS3 color and filter classes behind tints, Stasis greyscale, glow, and blur |
| `GameSprite`, `Map.as`, `Camera.as` | AS3 world classes: the game view, map layers and post effects, and the 50 px-per-unit camera |
| `GameObject`, `Character.as`, `Projectile.as` | AS3 entity classes whose rules `Entity.hpp`, `Combat.cpp`, and `ProjectileProperties.cpp` follow |
| `ObjectLibrary`, `objectType` | The object-definition registry and the XML attribute naming an object's type |
| `ConditionEffect.as` | Status-condition table: ID, bit `1 << (ID-1)`, icon frames |
| `Parameters.data_` | AS3 persistent settings (for example `playSFX`, `playPewPew`) |
| `SoundEffectLibrary` | AS3 sound manager; `SoundEffectLibrary.cpp` keeps its play gate and per-tick dedupe |
| `KeyCodes.as`, Flash key code | Flash `keyCode` numbers; `SdlKeyCodes.hpp` converts SDL keys to and from them |
| `TextureRedrawer`, dye | AS3 sprite recoloring; see `Appearances.cpp` |
| `FlashRandom` | PRNG that reproduces the original integer arithmetic, so server-seeded patterns match |
| `IDataInput`/`IDataOutput` | AS3 binary I/O; the protocol is big-endian |

Not Flash-derived: "flash" in `Particles.hpp` and `Hud.cpp` means a visual flash effect.

## Documentation

- [docs/fps-optimizations.md](docs/fps-optimizations.md): the GPU minimap, terrain chunk, and streaming design. Measurements are in [docs/fps-optimization-results.json](docs/fps-optimization-results.json).
- [tests/README.md](tests/README.md): test labels, fixtures, and the checksum oracle.
- [Completed Flash gap audit](docs/history/flash-gap-audit.md): implemented parity gaps, source findings and validation.
- [docs/history/](docs/history/README.md): historical records, not current process. These include the [port spec](docs/history/port-spec.md), [progress log](docs/history/progress.md), [Flash UI specs](docs/history/flash-specs.md), [UI parity audit](docs/history/ui-parity-audit.md), and the [task](docs/history/client-task-chain.md) and [quality](docs/history/client-quality-chain.md) chains.
