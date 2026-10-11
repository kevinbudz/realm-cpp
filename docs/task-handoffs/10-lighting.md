# 10-lighting — complete

Implemented and verified 2026-10-11. Next: **11-status-effects** (not started; no successor
chat was created from this session, see "Chain hand-off").

## Finding: Flash has no per-map lighting

Searched `realm-client` for ambient/darkness/fog/bloom/light objects and any MAPINFO lighting
field: there are none. The only lighting is the fixed model shade already done in task 09. The
task therefore covers everything Flash does draw around the world (spec in
`docs/flash-specs.md`, "Lighting, backgrounds and post effects"):

- Drunk `BlurFilter(d,d)`, `d = 20 + 10 sin(t/1000)`, and Blind `ColorMatrixFilter` (0.05 matrix) on the
  map view only; the software path applies both, as native does.
- Low-breath `HurtOverlay` radial vignette and the 10 px `GradientOverlay` sidebar strip.
- MAPINFO `background` 1 (`StarBackground`) and 2 (`NexusBackground`), drawn under the tiles.

## Implementation

- `include/realm/post_effects.hpp`, `src/post_effects.cpp`: condition bits (Blind 1<<6, Drunk 1<<8),
  blur diameter, breath alpha, vignette curve, `ScreenEffects` from the player entity (breath from
  stat 58, default -1). Sizes scale with `height / 600`, like the HUD.
- `src/renderer.cpp`: `Renderer::draw(..., overlay, effects)`. With a filter active the world renders into an
  offscreen RGBA target, then one resolve pass writes the default framebuffer inside the world viewport
  (separable box blur with transparent outside, folded with the blind matrix). Vignette and sidebar strip are
  drawn next, then the HUD overlay, so the HUD is never filtered. No filter: no extra target and the
  previous path. Targets reallocate only on a drawable size change.
- `src/backdrop_data.cpp` / `src/backdrop.cpp`, `include/realm/backdrop.hpp`: seeded star field and 1024x1024
  stitched fractal water (blue channel, nearest, drift 0.01 px/ms, rotation with the camera) as ordinary
  painter-ordered quads placed before terrain. `main.cpp` resets it per map generation from
  `MapInfo.background/seed`.
- Viewer fixtures: `--condition N --breath N --background N --effect-time MS` (with `--map`).

## Verification

- Build/CTest: release `build/` **35/35** (new `postfx_rules`, `postfx`), sanitizer `build-sanitize/` **34/34**
  (`postfx_rules`; the GL render suite needs the viewer build, which the sanitize preset disables). Logs:
  `build/lighting-ctest-release.log`, `build/lighting-ctest-sanitize.log`, `build/lighting-build-*.log`.
  `postfx_rules` (1.2M checks): bits, blur range, breath gating/peak, vignette stops, scaling, star spacing/range,
  determinism, water statistics and seamless tiling. `postfx` renders through the real `Renderer` and reads pixels:
  blind value and HUD isolation, blur edge/transparent-outside coverage, blur+blind, vignette per pixel, sidebar
  ramp, resize reallocation, backdrop determinism and water drift.
- Real Flash reference: `python3 tests/postfx_reference_tests.py` runs `tools/flash-reference/PostFxExporter.as`
  in AIR (real `BlurFilter`/`ColorMatrixFilter` with Map.as constants, real `HurtOverlay`, `GradientOverlay`,
  `StarBackground`, `NexusBackground`) against `build/realm_postfx_inputs` (the real `Renderer`). Same source frame,
  800x600, 600 px world. Mean grayscale difference: blind 0.18, drunk 0.7-0.9, breath 0.3-0.4, drunk+breath 1.1,
  gradient-only frame 0.06 (max 11, at the strip), stars 0.02-0.05, water 0.000 (exact; native noise bitmap fed to
  Flash's fill matrix at angles 0 and 0.7, drift at 0 and 7.3 s). Stars use the exact star list read from the real
  class. Output: `build/postfx-reference/{flash,native,comparison-*.png}`.
- Real server: `python3 tests/postfx_live_tests.py ../realm-server/bin/Release/net8.0/RotMG.dll` (isolated server and
  disposable account; also run with the sanitizer client): a seeded Pirate Rum used via the native input path turned
  on condition bit 8 from the server's CONDITION stat, native blur 10-30 px, sidebar 10 px, MAPINFO background 0
  (`build/postfx-reference/live-{release,sanitize}.log`). USEITEM=1.
- Resized captures at 1280x720 and 1920x1080 (`build/postfx-ref/contact-resized.png`): blur, sidebar and vignette scale
  with the window.

## Differences and limitations (honest)

- Nexus noise is not Flash's: `perlinNoise` is seeded by `Math.random` and its algorithm is not reproduced. Native uses a
  seeded stitched gradient noise with a gain (1.18) calibrated so that ten native fields match ten Flash fields in
  mean (128.8 vs 126.3) and std (20.8 vs 20.8). Pixel patterns differ by design; geometry and drift are exact.
- Drunk blur is compared as a box blur at the sizes tested; Flash's filter edge handling differs by a level or two
  (mean 0.7-0.9, max 9). Only the software path is followed; the GPU-path lens warp and the pause grayscale (no
  caller) are documented, not implemented.
- Breath cannot be produced by this server (`Player.Oxygen` is never assigned), so the vignette has fixture and
  Flash-reference evidence but no live evidence. Blind has no source in the server items checked.
- Blur scales with `height/600`; Flash was only compared at 800x600 (no resized Flash reference).
- Cost: a filtered frame renders the world offscreen plus 1-2 full-viewport passes; no performance claim was measured.
  Frames without a filter are unchanged.
- Backgrounds were not exercised in a live map (every server world sends 0); they were verified in isolation.

## Chain hand-off

`create_thread` is not available in this session (as in task 09), so no successor chat exists and there is no
`11-status-effects-dispatch.json`. Start 11-status-effects from `docs/client-task-chain.md`. It owns status icons/
text/breath bar and boost bars; the breath *vignette* and filters are done here and must keep working. Preserve the
uncommitted work, sibling repositories, models (task 09), particles, dyes, skins, authoritative stats, the teleport
flag/cooldown, snapshots and input guards. `Renderer::draw` now takes `ScreenEffects`; HUD/overlay quads are drawn after
the filter and must stay unfiltered.
