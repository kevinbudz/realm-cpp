# Port specification

## Contract

Resize direction, 2026-10-10: preserve the current UI proportions, while redrawing
text and retained UI surfaces at actual drawable resolution when the window
changes size. Animated text must also redraw at its current size; the title's
Play pulse must remain sharp and move smoothly. Original raster artwork remains
pixel art. This explicit quality change supersedes reproduction of enlarged
base-resolution text; base-size static layout and appearance remain the reference.

Latest direction, 2026-10-10 (refines the next paragraph): HUD elements must
look almost 1:1 with Flash, the same at a glance; per-pixel exactness is not
required. Match layout, sizes, art, colors, text and visual states closely and
verify by side-by-side review against source captures.

Earlier direction, 2026-10-10: the player tooltip currently being implemented
and all future elements must appear 1:1 with the original Flash client.
Source layout, fonts, artwork, colors, alpha, shadows, layering and visual
states are the acceptance reference. Native implementation and optimization
remain appropriate, provided appearance matches. This supersedes the earlier
approximation allowances and optional-comparison guidance below for new and
revised elements. Historical evidence is retained; it does not certify newly
implemented elements or waive their comparison differences.

Scope clarified by the user on 2026-10-10: use the Flash client as the visual
and functional shell for a better native C++ client. The UI and playfield
should look basically the same, while performance, responsiveness, usability,
and native implementation quality take priority over exact visual reproduction.

Preserve server compatibility, authoritative game state, gameplay rules, and
the client's feature coverage. Keep the familiar layout, original art, camera
feel, and recognizable world presentation. Routine improvements to rendering,
text, interaction, scaling, and responsiveness are authorized within that
design. Broad redesigns or changes to gameplay rules require user direction.
Platform selection is still open; the foundation is verified on Linux.

Prioritize correctness and usable features, then measured performance and
quality improvements. Small rasterization differences are acceptable by
default: font coverage, kerning, subpixel placement, antialiasing, shadow
edges, color quantization, and a few minimap-arrow pixels. These are not
completion blockers. Avoid reverse-engineering Flash internals solely to
eliminate such differences. Text must remain readable and fit its layout;
combat cues and important world geometry must remain clear and accurate.

Use representative scenes to measure frame times, memory, loading, and input
responsiveness throughout implementation. Native shaders, batching, storage,
caches, standard font rasterizers, and rendering APIs can differ freely.
Effects can be rendered more efficiently or adjusted for quality/performance
while keeping their recognizable appearance and gameplay information. Preserve
correct overlap and layering. Runtime remains native; Flash/AIR can be used
offline for reference captures and asset baking.

## Updated source baseline

Flash commit: `17bdf079452a1c470b8895eae9cf108599e7addd`.
Previously inspected commit: `8a7039c`.
The twelve-commit range changes 35 files, with 3,382 insertions and 557 deletions.
Current source has 960 ActionScript files and about 69,000 lines.
Server HEAD during inspection: `bb75bc1`; it also has recent changes to abilities,
shoot acknowledgments, shuriken behavior, and protocol handling. Use the local
client/server sources rather than assuming a stock RotMG packet format.

| New baseline behavior | Consequence for the port |
| --- | --- |
| Static tile snapshots reused across translation/rotation | Profile native visibility and static terrain costs against this optimized baseline. Choose native world geometry/caching independently. |
| Batched radial shadows | Keep familiar soft shadows and correct layering; native analytic instancing is allowed. |
| Affine batching of wall faces | Keep recognizable wall shape, texture placement, and depth; use efficient native geometry. |
| GPU health/breath bars | Keep readable values, familiar colors, and correct layering; integrate native HUD/world overlays directly. |
| Texture/outline cache bounds and teardown fixes | Use explicit native resource lifetimes and cache budgets. Native eviction rules may differ, while visuals must remain stable across map changes. |
| Adaptive MSAA and added diagnostics | Record resolution, quality, backend, and AA when capturing references. FPS/profiler observations alone do not identify GPU work. |
| Equipment/projectile definition changes | Import current XML/PNG definitions and verify gameplay against the updated server. |

Camera.as, Projectile.as position math, Random.as, and SocketServer.as did not
change across this range. The initial native core remains applicable.

## Rendering requirements and verification

Use the player's selected Flash rendering mode and quality settings as a
consistent visual reference. Software and Stage3D paths can differ. Judge the
native client at normal playing size and in motion, including dense combat
and resized windows. The goal is familiar appearance and a better experience.

| Surface | Reference behavior | Native implementation freedom |
| --- | --- | --- |
| Pixel assets | Original art, correct frame selection, recognizable colors and transparency | Texture arrays/atlases, packed GPU storage, native filtering/blending |
| World camera | Familiar scale, zoom, rotation, HUD allowance, centered/offset placement | GPU camera uniforms, native culling, smoother presentation |
| Characters | Correct directions/actions, recognizable animation, size, masks/dyes | GPU UV transforms, cached compositions, native animation |
| Outlines/glow | Similar silhouette, contrast, and appearance at playing size | Native shaders or preprocessed textures; approximate Flash filters |
| Terrain | Familiar transitions, edges, top layers, water, sinking, animation | Chunk geometry, cached composites, shaders |
| Walls/models | Recognizable geometry, texture placement, lighting and sorting | Unified native meshes/shaders |
| Effects | Recognizable effects and accurate gameplay cues | GPU or CPU simulation, native post passes, quality/performance tuning |
| Text/UI | Familiar layout/style, readable text, correct content, usable resize and interaction | Native font rasterization, glyph atlases, retained UI; no subpixel matching requirement |

Capture title/login, character selection, idle world, moving and rotating camera,
dense combat, water/terrain transitions, walls/3D models, all character actions,
dyes, loot/tooltips, minimap, chat, inventory/trade, menus/dialogs, status effects,
and map reconnects. Include native and enlarged resolutions, multiple zoom
levels, and centered/offset camera states. Match the same deterministic time,
world state, camera, and UI state in each pair.

Review individual frames and animation sequences for missing content, wrong
layout or scale, distracting artifacts, incorrect ordering, and unclear
gameplay information. Fix materially clipped or unreadable text; tiny glyph
coverage differences and antialiasing do not warrant additional fidelity work.

Exact Flash pixel comparisons are optional diagnostics, not release gates.
Existing tools may still return failure for any differing pixels; classify
the visible impact before treating that result as a product defect. Their
pixel counts and amplified differences do not establish perceptual quality.
Keep functional, protocol, memory safety, bounded-cache, and meaningful native
rendering regression checks. Do not blanket-disable tests or inflate image
tolerances to hide actual defects. Record material visual tradeoffs and their
measured benefit; the small differences accepted above need no further approval.

## Milestones

1. **Foundation (implemented):** source inventory, portable build, original asset
   import, native affine-quad renderer, camera/projectile/RNG core, initial
   packet framing/codecs, source PNG capture validation.
2. **Familiar gameplay presentation:** world/object definitions and map ingestion,
   character animation, masks, outline/glow, terrain, walls/models, shadows,
   ordered world drawing, visually reviewed static and animated scenes.
3. **Playable session:** socket/login/account flows, complete packet codecs,
   world update/interpolation, collision and input, shooting/abilities,
   acknowledgments, reconnects and server compatibility.
4. **Complete visible client:** text, HUD, menus/dialogs, minimap, chat,
   inventory/trade, settings, account/character screens, particles, post effects,
   audio, all current features.
5. **Measured optimization:** matched recorded workloads, CPU/GPU timing,
   allocations, cache misses, draw/upload volume, peak/resident memory, and
   map-change hitches. Retain faster strategies that meet correctness and
   visual quality goals. Measure throughout development rather than waiting
   for every feature or an exact Flash image match.

The harness is development tooling and has no replacement game UI. Its raw
gallery/camera fixtures are not screenshots of a finished port.

## Measurement rules

Use the same machine, resolution, camera, entity/particle counts, quality,
and replayed input/network state. Run Flash and native separately, after
warmup, with frame caps/vsync controlled. Record median/p95/p99 frame times,
CPU simulation and submission time, GPU elapsed time, presentation wait,
upload volume, and memory. Include stationary, scrolling, rotation, initial
resource load, map transitions, and dense combat.

The native harness separately reports CPU event/build/submit and presentation
time. Its last GPU query is delayed, not a GPU average. A synthetic 10,000-sprite
fixture is useful to detect renderer regressions; it is not evidence for a
client FPS multiplier. Only matched full-client scenes can establish speedup.
