# Vulkan renderer

The plan, design and status of the OpenGL/Vulkan renderer choice. Last updated 2026-10-19, on `flourish`:
phase 1 and S0-S8 done (validation sweep clean, presented capture rate-limited), phase 3 M1-M3 done, see
[Remaining work](#remaining-work).

## Goal

The user asked for a **Renderer** option in Options (OpenGL / Vulkan, applied on restart) so the client
"runs as well as possible on as many hardware stacks as possible". Max FPS is the priority; Flash 1:1
parity and implementation complexity are not constraints. The main payoff is on stacks with weak GL
drivers (AMD and Intel on Windows). On the dev box (RX 6700 XT, Mesa RADV/radeonsi, Linux) the swap
floor test measured GL ~27-28 µs/frame and Vulkan ~18.5-20 µs/frame (Wayland/X11), so the gain there
is small except at very high frame rates.

Defaults (agreed): OpenGL stays the default. If Vulkan fails to start, the client falls back to
OpenGL and Options shows why. `--renderer gl|vulkan` overrides the setting. Vulkan 1.1 minimum.

## Phases

| Phase | Content | State |
|---|---|---|
| 1 | Backend seam, settings row, fallback, CMake option | Done, committed |
| 2 | Vulkan world renderer (S0-S8) | Done; still behind `REALM_VULKAN_WIP` |
| 3 | RmlUi Vulkan backend, then remove the WIP gate | M1-M3 committed (geometry, scissor, textures, clip masks, layers, blur); offline screens match GL; M4 frame cache open; gate kept |
| 4 | Final verification and perf matrix across stacks | Not started |

## Phase 1: the backend seam (done)

The seam sits at the existing high-level boundaries, not at a low-level RHI.

- `include/realm/stage3D/GraphicsApi.hpp`, `src/stage3D/GraphicsApi.cpp`: `gfx::Api {opengl, vulkan}`,
  `select_api`, `active_api`, `record_fallback`/`fallback`, `record_command_line_api`.
- `src/stage3D/GraphicsBackend.hpp`: `gfx::Backend` (resource leaves: texture layers, surfaces, limits,
  info, swap interval, init/shutdown) and `gfx::RendererBackend` (the frame: draw, draw_overlay,
  capture, timers, finish_frame).
- Handle contract: GPU resources are `uint32` handles, a GL name or a Vulkan table index + 1; 0 means
  none. Atlas, SurfaceTexture and TerrainBuffer go through `gfx::backend()`.
- `Renderer` (`src/stage3D/Renderer.cpp`) is a facade. GL lives in `src/stage3D/gl/` (RendererGl,
  GlBackend, RendererStream, RendererGpuTimers, RendererPostProcess, RendererShader).
- `WindowSession::open(api, ...)`, `close()`, `drawable_size`, `present`, `set_swap_interval`, and
  `vulkan_instance_proc/extensions/surface` (SDL stays in the platform layer).
- RmlUi draws through `src/lib/ui/RmlGpu.hpp` (`RmlGpuGl.cpp`; `RmlGpuVk.cpp` is a stub).
- Settings row `renderer` (OpenGL = 0, Vulkan = 1, restart required) after vsync. Telemetry metadata
  records `renderer` and `renderer_fallback`; the banner prints `Renderer <api>`.
- CMake: `REALM_ENABLE_VULKAN` defaults ON only when the Vulkan headers and `glslc` are found; an
  explicit ON without glslc fails configure. Shaders are compiled at build time and embedded by
  `cmake/EmbedSpirv.cmake`. No libvulkan link: the loader comes from SDL.

## Phase 2: the Vulkan world renderer

### Sub-steps

| Step | Content | State |
|---|---|---|
| S0 | Shared CPU planning extracted into `src/stage3D/DrawPlan.{hpp,cpp}` (GL only) | Committed |
| S1 | Loader, instance, device selection, validation opt-in, WIP gate | Committed |
| S2 | Swapchain, frame slots, present modes, resize | Committed |
| S3 | Handles, memory, staging, atlas, surfaces, buffers, readback | Committed |
| S4 | Quad pipeline: sprites, surfaces, clip, terrain, layers, overlay | Committed |
| S5 | Walls (SSBO), prepared meshes, model instances | Committed (e760fbe); `model_instances_vulkan` passes on 3 drivers |
| S6 | Post effects, draw_overlay, back and presented capture | Committed (e760fbe) |
| S7 | GPU timers, telemetry, perf | Committed (e760fbe); perf measured below |
| S8 | Validation sweep, ctest, GL identity, parity, memory, perf | Done: validation sweep clean on 3 drivers (see [Validation](#validation-sweep-measured)), presented capture rate-limited |

### Verified facts the design rests on

- GL quad FS reads `screen = (gl_FragCoord.x, viewport.y - gl_FragCoord.y)`; Vulkan's gl_FragCoord is
  already top-down, so the Vulkan shaders read `gl_FragCoord.xy`.
- `glReadPixels` is bottom-up and RendererGl::capture flips it; a Vulkan image-to-buffer copy is
  top-down already.
- `capture(presented=true)` (death snapshot, `ApplicationLoopUpdate.cpp` near line 188) reads GL_FRONT,
  the last displayed frame. Vulkan swapchain contents are undefined after present.
- Frame order in `present_frame`: draw → RmlUi render → draw_overlay (returning text, death fade) →
  capture → finish_frame → present.
- draw() clears the whole back buffer even when filtered; post writes only `clip_width` columns.
- Walls are 32-byte `WallGpuVertex` (two vec4).

### Design

**Loader and device.** SDL's `vkGetInstanceProcAddr` and our own function table (`VK_NO_PROTOTYPES`;
no volk). Instance API 1.1, SDL's extensions (+ debug utils when validating). Device filters: API ≥
1.1, one graphics+present queue family, `VK_KHR_swapchain`, RGBA8/BGRA8 UNORM with FIFO, RGBA8 optimal
SAMPLED|COLOR_ATTACHMENT|TRANSFER_SRC|TRANSFER_DST, maxImageDimension2D ≥ 2048, enough array layers,
maxStorageBufferRange ≥ 8 MiB. Score discrete > integrated > virtual > CPU. Single queue, main thread.

**Swapchain.** minImageCount + 1 (capped), OPAQUE. Vsync on: FIFO. Vsync off: MAILBOX, else
IMMEDIATE, else FIFO. Recreate on resize, OUT_OF_DATE, SUBOPTIMAL or mode change (oldSwapchain
passed). The vsync setting reaches the present mode through `realm::set_present_interval`.

**Frames.** `frames_in_flight` is a constant, 2 (`VkFrame.hpp`). Each slot: command buffer, fence,
image-available semaphore, timestamp query pool, host ring chunks, retire list. Render-finished
semaphore per swapchain image. Lifecycle: Idle → Recording (first GPU call acquires) → Submitted (at
capture() or finish_frame()) → Idle. `finish_frame()` submits and presents.
`Frames`, `Pipelines` and `Resources` are process singletons (`frames()`, `pipelines()`,
`resources()`), so a renderer's recorded commands can outlive it. Its destructor finishes the frame
first (see the crash fix below).

**Memory and lifetime.**
- Per-frame data (instances, mesh vertices, overlay) is bump-allocated from the slot's host-visible
  ring. 8 MB chunks, 16 MB retained per slot, idle chunks freed after 3 frames, oversize chunks
  after 1. `REALM_VK_RING_MEMORY=host` forces plain host memory; the default prefers
  DEVICE_LOCAL|HOST_VISIBLE (ReBAR) when available.
- Device buffers (layers, terrain, model geometry) come from `VkArena`: 16 MB blocks, first-fit
  with coalescing, dedicated allocations for oversize requests. No per-change vkAllocateMemory.
- Walls: per-slot host-visible SSBO copies (GL's generation/tail logic per slot), so there is no
  write-after-read hazard.
- Releases go to the current slot's retire list and are destroyed when its fence signals. A surface
  reallocation keeps its handle and retires the old image.

**Descriptors.** Never update a set in flight. set0 = atlas sampler + wall SSBO (cached per atlas
handle and wall buffer); set1 = surface sampler per (handle, filter), handle 0 uses a 1×1 dummy;
set2 = post source per target (scene sets). Pools use FREE_DESCRIPTOR_SET; released surfaces free their
sets. A cached set must not outlive what it names: a released atlas or surface handle, a destroyed wall
buffer or a destroyed post target. Handles are reused, so `Pipelines::forget_atlas_sets` and
`release_scene` drop them.

**Push constants (72 B).** viewport, ground origin/X/Y, ground time, clip rect, clip cut, int flags
(standalone, triangle list, ground space, clipped).

**Passes.**

| GL step | Vulkan |
|---|---|
| Clear back buffer (always) | swapchain pass CLEAR, first in frame |
| World batch (filtered: offscreen 0) | offscreen pass CLEAR, else swapchain pass LOAD |
| Screen/HUD overlays when filtered whole | offscreen 0, LOAD, quad blend |
| Blur H (offscreen 0 → 1) | post, no blend, clip rect |
| Blur V / single pass → swapchain | swapchain LOAD, post no blend, scissor clip_width |
| Overlays not whole-filtered | swapchain LOAD, post blend (vignette, sidebar) or quad |
| RmlUi | phase 3 (stub draws nothing) |
| draw_overlay | swapchain LOAD, full scissor |

Pipelines: quad strip + blend; quad list + blend (walls); mesh list + blend; model list + blend, cull
back; post no blend; post blend. Post targets use the swapchain format, so render passes and
pipelines are shared. The swapchain pass stays open across consecutive draws to the same target.

**Coordinates.** Viewport `{0, H, W, -H}` (negative height), so row 0 is the top in every target and
post texelFetch needs no flip. GL scissor is full height, so `{0, 0, clip_width, H}` matches.
Model front face is **COUNTER_CLOCKWISE** under the negative viewport. The design predicted CW; the
measurement on RADV was CW ≈ 31% differing pixels, CCW 0.006%. Wall culling stays in the shader.

**Shaders.** GLSL 450 compiled with `glslc --target-env=vulkan1.1` (no `-O`, to limit float
reordering). Same locations, instance stride 88 at instance rate, native firstInstance. Uniforms
become push constants; `samplerBuffer` walls become a readonly std430 `vec4[]` SSBO; `gl_VertexID` →
`gl_VertexIndex`. Files: `src/stage3D/vk/shaders/{quad,mesh,model,post}.{vert,frag}`.

**Capture.** `capture(back)`: copy the current target to a host buffer, submit, wait, read top-down,
swizzle BGRA. Synchronous, capture frames only.

`capture(presented)`, the death-screen background, is **an open decision**:
- The design review first rejected a per-frame full-frame copy (an 8 MB copy every ~89 µs frame).
- The first WIP defaulted to a black snapshot. Rejected: a visible regression.
- Current WIP (committed): `Renderer::set_presented_capture(bool)`. While a game session is playing
  (the only time death can happen), each frame's final image is copied on the GPU into one persistent
  device-local image before present, and `capture(presented)` reads that image back. Menus pay
  nothing. GL ignores the toggle. `REALM_VK_PRESENTED_COPY=1` forces it.
- Measured cost of a copy every frame (1920×1024, world only): Renoir nexus-pan FPS 1035 → 711 (about
  31%); NVIDIA S3 about 4%.
- **Decision (implemented): rate-limit the copy** to one per 16 ms (`presented_copy_interval` in
  `VkRenderer.cpp`). The snapshot is then at most one 60 Hz frame older than the last presented image,
  about what GL's front buffer gives on a 60 Hz display, and the death flow is unchanged. A resize copies
  at once. The deferred snapshot (request at death, fill at the next finish_frame) was the alternative:
  zero cost but a restructured death flow and a snapshot one frame late. Not needed: the rate-limited
  copy measured no cost (Renoir nexus 1377 FPS without copy, 1376 rate-limited, 929 every frame; see
  [Perf](#perf-measured)).
- `REALM_VK_PRESENTED_COPY=1` still copies every frame (the s23 harness reads the frame it just
  presented); `REALM_VK_PRESENTED_COPY=limited` forces the rate-limited copy, as a game session runs.

**Timers.** Per-slot query pool, 69 timestamps (5 + 2×32), reset at command-buffer begin, polled
WITH_AVAILABILITY. Ticks × timestampPeriod, masked to timestampValidBits (0 disables timers). Vulkan
uses TOP/BOTTOM_OF_PIPE stamps, so its numbers approximate GL's.

### Test-only switches

| Variable | Effect |
|---|---|
| `REALM_VULKAN_WIP=1` | Allow Vulkan to start. Without it Vulkan fails with "Vulkan UI not implemented yet" and the client falls back to OpenGL. Removed in phase 3. |
| `REALM_VK_VALIDATION=1` | Khronos validation layer, debug messenger, synchronization validation when available; counts at shutdown. Off costs nothing. |
| `REALM_VK_DEVICE=index\|substring` | Pick a device, e.g. `NVIDIA`, `RENOIR` or `llvmpipe` (lavapipe). No match throws. |
| `REALM_VK_PRESENT=immediate\|mailbox\|fifo` | Override the present mode. |
| `REALM_VK_STATS=1` | Ring, arena and descriptor-set summary at shutdown. |
| `REALM_VK_RING_MEMORY=host` | Force the ring into plain host memory. |
| `REALM_VK_PRESENTED_COPY=1\|limited` | Force the presented copy: every frame (`1`) or rate-limited as in a game session (`limited`). |
| `REALM_TEST_RML=file.rml` | Shows this RML document over the UI host, for RmlUi features the client's screens do not use (clip masks). |
| `REALM_TEST_WORLD_ONLY=1` | Draw the world only (no HUD, RmlUi, overlay text or death fade), above the renderer, so both APIs draw the same quads. |

### Parity

The bar per world-only fixture: at least 99.9% identical pixels, with any outliers isolated to
triangle edges and no region-wide differences. The stricter "max channel delta ≤ 1" bar was dropped: a
few edge pixels flip coverage between the two shader compilers. Three bounded experiments, including
`precise` on gl_Position and matching GL's expression order, left them unchanged; the root cause was not
found.

**Measured 2026-10-19, this laptop** (world only, 800×600, 120 frames, 12 fixtures: S1-enemies-dense,
S3-enemies-dense, S3-players-dense, S3-enemies-spread, S2-enemies-dense, S1-players-dense,
S3-combat-cluster, S1-projectiles-dense, S3-projectiles-dense, S3-models-dense, nexus-pan, churn):

| Comparison | Differing pixels, max over fixtures |
|---|---|
| Same driver, GL vs VK: NVIDIA | 0.023% (S1-enemies-dense) |
| Same driver, GL vs VK: RADV Renoir | 0.039% (churn) |
| Same driver, GL vs VK: lavapipe | 0.020% (S3-enemies-dense) |
| Same API across drivers (GL or VK) | 0.08%-4.5% (e.g. GL-lavapipe vs GL-RADV S3-models-dense 4.54%, nexus-pan 3.66%) |

Many fixtures are 0 on the same driver. The region check (16×16 tiles with >25% differing pixels)
finds only gl-radv/vk-radv churn: 2 tiles, one sprite's left edge flipping coverage (edge class).

Conclusion (measured): all three drivers meet the 99.9% bar. The four-way comparison (GL-lavapipe,
GL-RADV, VK-lavapipe, VK-RADV, `tools/parity4.py`) shows lavapipe-vs-RADV differences come from the two
rasterizers, not from our Vulkan code. This resolves the earlier unlabelled reading.

Walls and prepared meshes: no stress fixture draws them (mesh_upload_bytes = 0). They are covered by
`model_instances_vulkan` (below), which renders walls, wall overflow and model meshes through both
the instance path and a reference mesh path under Vulkan.

### model_instances_vulkan (measured 2026-10-19, 800×600)

`realm_model_instance_tests vulkan` runs the GPU section on Vulkan (ctest `model_instances_vulkan`);
the default run is OpenGL (`model_instances`, passes). Printed numbers, per device:

| Device | Walls: covered / differing / max Δ | Wall overflow: covered / differing / max Δ | Instance vs mesh, 688 frames: covered / differing (>16) |
|---|---|---|---|
| NVIDIA RTX 3050 Ti | 930,562 / 0 / 0 | 366,351 / 0 / 0 | 55,604,482 / 84 (56) |
| RADV Renoir | 930,542 / 0 / 0 | 366,217 / 4 / 7 | 55,604,464 / 92 (60) |
| lavapipe | 930,522 / 0 / 0 | 366,216 / 0 / 0 | 55,604,484 / 98 (50) |

All three pass (exit 0). The per-frame bar is at most 4 + covered/20000 pixels above Δ16, and at most
0.1% differing for walls. No tolerance was loosened. The GL run prints 55,604,466 / 72 (52). Both draw
paths (base-instance and per-run pointer) render identical pixels on every device.

### Bugs fixed in this work

**model_instances_vulkan crash (fixed).** The second pass of the GPU section (the per-run draw path)
crashed in the driver, reported as a segfault in `Resources::resize_layers`. Root cause: `Frames` is a
process singleton. The first pass's last `draw()` had recorded into the frame's command buffer, and the
renderer was destroyed without finishing that frame. The next renderer's first GPU call then recorded
into and submitted a command buffer that still bound the destroyed pipelines and render passes. RADV
crashed in the next `vkCmdClearColorImage`. NVIDIA and lavapipe lost the device (`vkWaitForFences`
returned VK_ERROR_DEVICE_LOST).

Fix (`VkRenderer::~VkRenderer`):
- `frames().finish()` before `vkDeviceWaitIdle`. This is the root cause. Ablation confirmed it alone
  fixes the test on all three drivers.
- `pipelines().release_scene()`: the cached scene sets name this renderer's post targets. Not exercised
  by the test; a latent handle-reuse hazard, fixed by reasoning from the cache keys.
- `Pipelines::forget_atlas_sets(atlas, walls)`: set-0 sets name a released atlas handle (the release hook)
  or the wall buffer being destroyed (renderer destructor and `upload_walls`). Handles are reused, so a
  stale set would be found by key. Also not exercised by the test; reasoned.

### Perf (measured)

1920×1024, world only, `--benchmark` (vsync off), 300 frames, 3 interleaved reps, medians of the
per-rep means. Columns: FPS / CPU median ms / GPU median ms. Measured 2026-10-19 on this laptop.

| Device | Scene | Row | FPS | CPU med ms | GPU med ms |
|---|---|---|---|---|---|
| NVIDIA RTX 3050 Ti | S3-combat-cluster | GL | 65.6 | 11.8 | 5.52 |
| NVIDIA RTX 3050 Ti | S3-combat-cluster | VK | 61.2 | 12.9 | 4.17 |
| NVIDIA RTX 3050 Ti | S3-combat-cluster | VK + presented copy | 58.8 | 13.2 | 4.18 |
| NVIDIA RTX 3050 Ti | nexus-pan | GL | 751 | 0.15 | 0.11 |
| NVIDIA RTX 3050 Ti | nexus-pan | VK | 846 | 0.14 | 0.09 |
| NVIDIA RTX 3050 Ti | nexus-pan | VK + presented copy | 846 | 0.14 | 0.09 |
| RADV Renoir | S3-combat-cluster | GL | 35.0 | 17.9 | 22.8 |
| RADV Renoir | S3-combat-cluster | VK | 44.7 | 16.3 | 16.2 |
| RADV Renoir | S3-combat-cluster | VK + presented copy | 47.3 | 16.4 | 15.6 |
| RADV Renoir | nexus-pan | GL | 1335 | 0.13 | 0.70 |
| RADV Renoir | nexus-pan | VK | 1035 | 0.84 | 0.84 |
| RADV Renoir | nexus-pan | VK + presented copy | 711 | 1.24 | 0.85 |

**Rerun 2026-10-19 (later, `flourish` with the rate-limited copy).** Same method; load average 3.7-6.5
during the runs, so absolute numbers drifted from the table above (the GL rows moved too):

| Device | Scene | GL | VK | VK + copy every frame | VK + copy rate-limited |
|---|---|---|---|---|---|
| RADV Renoir | nexus-pan FPS (GPU ms) | 1523 (0.59) | 1377 (0.62) | 929 (0.63) | 1376 (0.60) |
| RADV Renoir | S3-combat-cluster FPS (GPU ms) | 30.5 (28.95) | 32.9 (26.23) | 33.2 (25.87) | 33.3 (25.64) |
| NVIDIA RTX 3050 Ti | nexus-pan FPS (GPU ms) | 757 (0.13) | 846 (0.09) | 847 (0.09) | 846 (0.09) |
| NVIDIA RTX 3050 Ti | S3-combat-cluster FPS (GPU ms) | 53.3 (4.97) | 51.1 (5.20) | 51.2 (5.08) | 51.5 (5.08) |

The Renoir nexus VK row is noisy (per-rep 1191-1495 FPS). In this rerun VK beats GL on Renoir S3 by about
8%, not the 28% above; both runs agree on the direction.

Readings (first table):
- VK wins S3 on Renoir by about 28% (GPU-bound: 16.2 vs 22.8 ms) and nexus on NVIDIA by about 13%.
- VK loses nexus on Renoir by about 22% (the CPU waits on a higher VK GPU time) and S3 on NVIDIA by about
  7% (CPU-bound: VK CPU 12.9 vs 11.8 ms).
- The every-frame presented copy costs about 31% on Renoir nexus and about 4% on NVIDIA S3. Rate-limited,
  it costs nothing measurable (rerun table).

Earlier WIP numbers (800×600, unverified, superseded) are dropped: S3 looked CPU-bound with both APIs
tied, and the "Vulkan GPU is slower" reading was a vsync/downclock artifact. The current table is the
reference.

### Memory (measured)

`REALM_VK_STATS=1`, 300 frames, full UI, this laptop:

| Item | Result |
|---|---|
| Steady ring | 8 MB per slot (16 MB total) |
| Arena | 1 block, 16 MB |
| Surface descriptor sets | peak 6 → 1 |
| Descriptor pools | 1 |
| Ring peak at startup | 80-128 MB per slot, one-frame burst from atlas staging (`Resources::stage_image`), freed within 1-3 frames |

The steady-state bar is met. `create_ring_buffer` already falls back to host memory if the BAR heap is
full. The startup burst may matter on small-BAR systems; the fix would route large staging through plain
host memory. Not done.

### Validation sweep (measured)

2026-10-19, `REALM_VK_VALIDATION=1` (Khronos layer, synchronization validation on), on RADV Renoir, lavapipe
and NVIDIA: S3-combat-cluster, S1-enemies-dense, S3-models-dense, nexus-pan, churn (full UI; S3 and churn
also world only), the scripted Options capture, `realm_model_instance_tests vulkan`, and the s23 and s5
harnesses.

- No SYNC- messages anywhere. lavapipe and NVIDIA: 0 errors, 0 warnings.
- RADV: 8-12 errors per run, all from the UI stencil format. It was hard-coded `D24_UNORM_S8_UINT`, which
  RADV Renoir does not support as an attachment (VUID-VkSubpassDescription-pDepthStencilAttachment-02650,
  VUID-VkImageViewCreateInfo-usage-02277, VUID-VkImageCreateInfo-imageCreateMaxMipLevels-02251). Fixed:
  `stencil_format()` picks the first of D24S8, D32S8, D16S8 with optimal-tiling attachment support. After
  the fix RADV is 0 errors on S3 full UI, Options and s23. The clip-mask sample already clipped correctly
  on RADV before the fix (the driver tolerated the format) and is byte-identical after it.
- s23 failed "resized swapchain renders at 400x300" on NVIDIA and RADV: a harness bug. It cleared to 0.9
  and expected 230, but 0.9f × 255 = 229.49998, which these drivers round to 229 (lavapipe gives 230). The
  harness now clears to 0.8/0.2 (exact multiples of 1/255) and passes on all three, with
  `REALM_VK_PRESENTED_COPY=1` and with the `toggle` (rate-limited) path.

## Phase 3: the RmlUi Vulkan backend

### Status

| Milestone | Content | State |
|---|---|---|
| M1 | Premultiplied triangles, scissor, RmlUi transform, textures (TGA files, `item:`/`art:` nearest, host-written, generated) | Done, committed |
| M2 | Clip masks (`EnableClipMask`, `RenderToClipMask` Set/SetInverse/Intersect, stencil) | Done (test sample, below); the client's screens do not reach it |
| M3 | Layers (`PushLayer`/`PopLayer`/`CompositeLayers`), blur filter (Settings) | Done: Options screen matches GL to ±1 level |
| M3 (rest) | Other filters, gradient shaders | Not needed: the client's RCSS and C++ use neither (checked); unimplemented ones log once |
| M4 | Frame cache (`frame_reused`) | Open: always false |
| Gate | `REALM_VULKAN_WIP` | Kept: parity is verified for the HUD, Options and the test sample only; other screens are unchecked |

### Design

- `src/lib/ui/RmlGpuVk.cpp`: the Vulkan `RmlGpu`. Its `Rml::RenderInterface` keeps compiled geometry on
  the host (copied into the frame ring at draw time) and decodes TGA as RenderInterface_GL3 does.
  Projection and RmlUi's transform are composed on the CPU: `P * T`, with `P` mapping window pixels to
  clip space under the negative viewport.
- `include/realm/stage3D/vk/VkUi.hpp`: plain-data seam (no Vulkan headers), so `realm_ui_rmlui` needs
  no Vulkan include path. `src/stage3D/vk/VkUi.cpp` implements it over `VkRenderer::ui_*`.
- `VkRenderer`: the UI pass is a LOAD pass on the frame's swapchain image, sharing the framebuffer
  cache. The first UI draw opens it; `ui_close` ends it before any upload (a copy cannot run inside a
  pass) and `end_frame` ends it. Pipeline `ui.vert`/`ui.frag`: 20-byte `Rml::Vertex`, premultiplied
  blend (ONE, ONE_MINUS_SRC_ALPHA), no cull. Push constants: 4x4 matrix, translate, textured flag (76 B).
- Textures need the device, not the renderer: RmlUi loads the fame document before the first world
  frame, so `create_texture` must not require a `VkRenderer` (this was a bug, found by the log).
- Window: the Vulkan path needs the SDL window only for RmlUi's system interface (clipboard, cursor).
  `RmlHost` takes it from the app (`ui::set_host_window`, called after `WindowSession::open`) instead of
  scanning window ids.
- Client boundaries: `realm_ui_rmlui` may link `realm_render` (the Vulkan seam), recorded in
  `cmake/ClientBoundaries.cmake`.
- Clip masks (M2): the UI pass has a D24S8 stencil per frame slot. The first UI pass of a frame clears it
  (a `CLEAR` stencil load op), later passes keep it. Mask draws use pipelines with stencil ops (replace or
  increment, colour writes off), and tests use the dynamic reference, tracked as RenderInterface_GL3 does.
  Set and SetInverse clear with `vkCmdClearAttachments` inside the scissor, as GL's `glClear` does.
- Layers and blur (M3): each frame slot owns window-sized images: a layer per `PushLayer` depth, and two
  post targets. Pass kinds are (clear colour, offscreen, clear stencil). Offscreen passes start and end
  `SHADER_READ_ONLY_OPTIMAL`; subpass dependencies order the writes before the next pass's reads, so no
  explicit barriers. `PushLayer` clears the layer and keeps its pass open. `CompositeLayers` copies the
  source into post target 0 when filters apply, runs the blur, and draws into the destination inside the
  scissor (premultiplied blend, or plain for `BlendMode::Replace`). The blur follows GL3's `RenderBlur`:
  `SigmaToParameters` splits the sigma into halving downscales, one 7-tap vertical and horizontal pass
  clamped to the region (`ui_blur.frag` is `ui_post.frag`), then a linear upscale to the region. Sigma 3.5
  (the Settings blur) gives one downscale to sigma 1.75.
  Not reproduced: GL3's second, exact power-of-two upscale blit, which only differs when the region's size
  is not a multiple of two.

### Verification (M1)

GL is unchanged: the checksums and capture md5s match the values in [How to test](#how-to-test).

| Check | Result |
|---|---|
| S1-mixed-dense, 300 frames: quad / state / packet | match |
| S3-combat-cluster, 300 frames: quad / state / packet | match |
| GL capture md5: churn, S1-mixed-dense, S3-combat-cluster | match all three |

Full UI, 800×600, 120 frames, Vulkan vs GL capture (`compare.py`). Each row compares the same driver:
GL on NVIDIA against VK on NVIDIA, GL on RADV against VK on RADV (GL on RADV: `__GLX_VENDOR_LIBRARY_NAME=mesa
__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json`). Lavapipe is report-only; its GL reference is GL on llvmpipe (`LIBGL_ALWAYS_SOFTWARE=1`).

| Fixture | NVIDIA (GL vs VK): px / max Δ | RADV (GL vs VK): px / max Δ | lavapipe (GL vs VK, both llvmpipe): px / max Δ |
|---|---|---|---|
| S1-mixed-dense | 0 / 0 (byte-identical PNG) | 21 (0.0044%) / 48 | 0 / 0 |
| churn | 0 / 0 | 188 (0.0392%) / 196 | not run |
| S3-combat-cluster | 3 (0.0006%) / 1 | 178 (0.0371%) / 255 | not run |

RADV world-only churn (`REALM_TEST_WORLD_ONLY=1`, GL vs VK, both RADV) also gives 188 px (0.039%), so the
full-UI churn differences are the world's, and the HUD adds none. The RADV differences fall inside the
world area (x 143–545, y 234–578; the S3 box reaches the bottom-left, under the chat, checked by bounding box
only). They are the same-driver world-rasterization differences that phase 2 measured on RADV, not the UI.
Cross-driver numbers are not comparisons: GL-NVIDIA vs GL-RADV world-only churn is 0.81% on its own.

GL ctests: `ui_rml_host`, `ui_retained`, `ui_bundle_gpu`, `startup_news_gpu` pass. `options_gpu` and
`appearance_compose` fail, as before.

### Verification (M2, M3)

Clip masks: no client screen clips with border-radius (the only `overflow` values are `overflow-y:auto`
without rounded corners and one `overflow:hidden` field), so the screens never reach them.
`REALM_TEST_RML` loads a sample (rounded `overflow:hidden` panels, a child overhanging the corner) over the HUD.
GL and VK on NVIDIA both clip to the rounded shape; 270 px differ, all on the corner edges (the stencil
is binary on both).

Settings screen (scripted, `key o`): the HUD behind it is blurred by `RetainedUi::set_blur`
(`filter: blur(3.5dp)`). Before M3 the Vulkan capture showed the HUD sharp (max Δ 17, on the blurred
counters and backdrop). Now, 800×600, GL vs VK on the same driver:

| Check | NVIDIA (GL vs VK) | RADV (GL vs VK) |
|---|---|---|
| Options screen, px / max Δ | 14663 (3.05%) / 2 | 14800 (3.08%) / 2 |
| Pixels with Δ = 1 / Δ = 2 | 14651 / 12 | 14787 / 13 |
| S1-mixed-dense, full UI, after M2 and M3 (md5) | byte-identical to GL | not rerun |

The residual is ±1 level across the screen: rounding in the blur passes, not a region difference.
Warnings: the Options run logs none (before M3 it logged the filter and the layers).

The scripted capture (`--script`, the same format as `tests/options_gpu_tests.py`): a file with
`300 capture PATH`, `400 key o`, `900 capture PATH`, `1000 quit`, run with
`--hidden --map realm-server/Resources/Worlds/Tutorial/Nexus.jm --effect-time 0 --scene-time 0 --hp 50`.

Not verified: gradient decorators (none in the client), other filters (none), and the frame cache (M4).

### Verification (other screens)

2026-10-19, 800×600, GL vs VK on the same driver. Determinism needs two things: `--fixture-step-ms 20
--frames 400`, and a fixed startup backdrop. `Startup.cpp` `load_background` seeds from
`std::random_device`, so the runs used an LD_PRELOAD shim that replaces it with a fixed LCG. Two GL runs then
give 0 differing pixels on every screen. Common flags: `--hidden --width 800 --height 600 --effect-time 0
--scene-time 0 --fixture-step-ms 20 --frames 400 --script S --settings-file X --account-file Y`; screens are
reached with `click` script lines (e.g. play at 400,545).

| Screen | NVIDIA: px / max Δ | RADV: px / max Δ |
|---|---|---|
| Title | 7.46% / 1 | 7.45% / 5 (14 px ≥ 3) |
| Title fame popup | 8.60% / 2 | 8.53% / 2 |
| Character select | 3.79% / 1 | 3.72% / 3 |
| Classes | 2.41% / 1 | 2.37% / 8 (15 px ≥ 3, top rows) |
| Login dialog | 44.2% / 2 | 45.6% / 2 (±1-2 over the dimmed backdrop) |
| Legends | 2.76% / 1 | 2.69% / 3 |
| Map editor | 0.80% / 1 | 0.79% / 1 |
| Nexus tooltip | 0 / 0 | 0 / 0 |

Everything is ±1-2 levels (blend rounding) except a few dozen RADV pixels up to Δ 8; no missing or extra
elements, and no Vulkan UI warnings logged. Not reached offline (need a server): the death screen, guild,
shop and vault. Chat input did not open from a script.

## Remaining work

### Phase 2 (S8)

1. Validation sweep: done, clean after the stencil-format fix (see [Validation](#validation-sweep-measured)).
2. Presented capture: done, rate-limited (see Capture).
3. **Renoir nexus gap:** VK 1377 vs GL 1523 FPS in the rerun (CPU 0.59 vs 0.18 ms). The presented-copy part
   is gone; the rest is open, measured but not explained.
4. **NVIDIA S3 CPU gap:** VK CPU 12.9 ms vs GL 11.8 ms (CPU-bound). Open.
5. Done this session (measured above): GL identity, world-only parity on 3 drivers with a labelled
   four-way comparison, memory bounds, perf matrix at 1920×1024, wall/mesh parity under Vulkan. Full
   ctest (`ctest --test-dir build-vk -j4`, 2026-10-19): 89 of 91 pass. The 2 failures are
   `appearance_compose` and `options_gpu`, both failing before this work; not investigated here.
   Later the same day (`build-vk2`): the same two fail. `client_boundaries` failed because
   `include/realm/stage3D/vk/VkUi.hpp` had no owner in `cmake/header-owners.json` (fixed: `realm_render`).
   `renderer_gpu` can fail under `-j4` (GPU contention) and passes alone.

### Phase 3: RmlUi Vulkan backend (M1-M3 done, see above)

- M4: the frame cache. `frame_reused()` is always false under Vulkan, so a static UI is redrawn every
  frame. Reuse the previous frame's resolved UI image when the command queue and generation match, as
  RmlUi_Renderer_GL3 does.
- Parity: the offline screens match (see Verification (other screens)). The death screen, guild, shop and
  vault need a server and are unchecked.
- Then remove `REALM_VULKAN_WIP` so the Options choice takes effect.

### Phase 4: final verification

- Full-UI parity against GL, perf matrix (Nexus and S3, GL vs Vulkan), on RADV and correctness on
  lavapipe. The world-only parity and perf matrix above are a start.
- Check the fallback on a machine without Vulkan.
- Ideally test a Windows AMD/Intel stack, the case that motivated the work.

## How to test

Checksum oracle (GL identity), from `realm-cpp`, **300 frames** (tests/README.md says 120, which gives
different values). Measured 2026-10-19 after applying the S5-S7 work: PNG captures are byte-identical to
the base build, and the checksums below match.

```
REALM_QUAD_CHECKSUM=1 build/realm_client --hidden --stress tests/fixtures/S1-mixed-dense.json --stress-summary s1.json --frames 300
```

| Fixture | quad | state | packet |
|---|---|---|---|
| S1-mixed-dense | 5956456988386327452 | 12694967582451890774 | 4156475164162667594 |
| S3-combat-cluster | 1151069304813412503 | 10048529582793468071 | 17530971269395398748 |

Capture md5s (`glid.sh`, GL, 120 frames): churn `6c19e46498095a5add38c50366b55872`, S1-mixed-dense
`424955535ce96ab5e30d57725b417b6d`, S3-combat-cluster `7a76f9e61d6addbdde1e5eadd6153628`.

World-only parity capture (Vulkan: add `REALM_VULKAN_WIP=1 REALM_VK_VALIDATION=1` and
`--renderer vulkan`; lavapipe: `REALM_VK_DEVICE=llvmpipe`; GL on llvmpipe: `LIBGL_ALWAYS_SOFTWARE=1`):

```
REALM_TEST_WORLD_ONLY=1 build/realm_client --hidden --renderer gl --stress tests/fixtures/S3-combat-cluster.json --stress-summary o.json --frames 120 --fixture-step-ms 20 --capture gl.png
```

Compare: `python3 docs/vulkan/tools/compare.py gl.png vk.png diff.png` (differing %, max delta,
histogram, bbox). `tools/clusters.py` groups differing pixels; `tools/perf_stats.py` summarises
telemetry JSONL files.

GPU model tests: `REALM_VULKAN_WIP=1 REALM_VK_DEVICE=NVIDIA|RENOIR|llvmpipe build/tests/realm_model_instance_tests vulkan`
(ctest `model_instances_vulkan`); `build/tests/realm_model_instance_tests` runs the GL section.

Harnesses (`harness/`): `s23.cpp` checks frames, present, resize, capture, and atlas/surface/layer
readback; `s5.cpp` draws walls and meshes in either API. Build with
`harness/build.sh REALM_CPP_DIR BUILD_DIR` (or `build5.sh`) against a Release build with Vulkan ON.
Their link lines are hard-coded for this machine.
