# Vulkan renderer

The plan, design and status of the OpenGL/Vulkan renderer choice. Last updated 2026-10-19, on `flourish`:
phase 1 and S0-S8 done (validation sweep clean, presented capture rate-limited), phase 3 M1-M3 done and
the `REALM_VULKAN_WIP` gate removed: the Options Renderer choice now takes effect. Phase 4 final
verification done on the RX 6700 XT and lavapipe (see [Phase 4](#phase-4-final-verification)). See
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
| 2 | Vulkan world renderer (S0-S8) | Done |
| 3 | RmlUi Vulkan backend, then remove the WIP gate | M1-M4 done (geometry, scissor, textures, clip masks, layers, blur, frame cache); screens match GL; gate removed |
| 4 | Final verification and perf matrix across stacks | Done on this machine (RX 6700 XT, lavapipe, fallbacks, ctest); NVIDIA, Renoir and Windows not run. See [Phase 4](#phase-4-final-verification) |

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
| S1 | Loader, instance, device selection, validation opt-in, WIP gate (removed in phase 3) | Committed |
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
uses TOP/BOTTOM_OF_PIPE stamps, so its numbers approximate GL's. Per-model stamps are both BOTTOM_OF_PIPE:
a TOP_OF_PIPE start let pipelined model runs overlap, so their sum exceeded the frame's GPU time on RADV
(`telemetry_gpu`: `models_ms <= total_ms`).

### Test-only switches

| Variable | Effect |
|---|---|
| `REALM_RENDERER=gl\|vulkan` | Acts as `--renderer` (the flag wins), so test drivers that build their own command lines run on either API. (`REALM_VULKAN_WIP`, the phase 1-3 gate, is gone.) |
| `REALM_VK_VALIDATION=1` | Khronos validation layer, debug messenger, synchronization validation when available; counts at shutdown. Off costs nothing. |
| `REALM_VK_DEVICE=index\|substring` | Pick a device, e.g. `NVIDIA`, `RENOIR` or `llvmpipe` (lavapipe). No match throws. |
| `REALM_VK_PRESENT=immediate\|mailbox\|fifo` | Override the present mode. |
| `REALM_VK_STATS=1` | Ring, arena and descriptor-set summary at shutdown. |
| `REALM_VK_RING_MEMORY=host` | Force the ring into plain host memory. |
| `REALM_VK_PRESENTED_COPY=1\|limited` | Force the presented copy: every frame (`1`) or rate-limited as in a game session (`limited`). |
| `REALM_TEST_RML=file.rml` | Shows this RML document over the UI host, for RmlUi features the client's screens do not use (clip masks). |
| `REALM_TEST_WORLD_ONLY=1` | Draw the world only (no HUD, RmlUi, overlay text or death fade), above the renderer, so both APIs draw the same quads. |
| `REALM_VK_UI_CACHE=0` | Turns off the RmlUi frame cache (M4): every UI draw is recorded at once, as before M4. The comparison for the cache. |

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
| M4 | Frame cache: a static frame's queued draws replay from a per-slot secondary command buffer (`frame_reused`) | Done (see Verification (M4)); no measurable frame-time gain on the Nexus map |
| Gate | `REALM_VULKAN_WIP` | Removed 2026-10-19 after the server-screen checks (see Verification (server screens)) |

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
- Frame cache (M4, as RmlUi_Renderer_GL3). `RmlGpuVk.cpp` queues the draws between BeginFrame and EndFrame. A
  frame whose draws were all queued is replayed when its draw list equals the last recorded one and the
  generation has not moved (the generation changes on compile, release, texture creation or upload, content
  change and viewport change). Otherwise the draws are recorded once into the frame slot's secondary command
  buffer (`VkRenderer::ui_record_bundle`, `VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT | SIMULTANEOUS_USE_BIT`)
  and executed in the UI pass (`VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS`). The recording binds the atlas set
  and the surfaces it draws, sets its own viewport, and keeps its vertex and index bytes in its own host buffer
  (grows only). It is per frame slot: a slot's recording is re-recorded only after that slot's frame has
  finished, so no retire list is needed. A replay also checks the atlas set, the swapchain generation and the
  window size. A clip mask, a layer or a composite runs the queued draws at once and makes the frame ineligible,
  as does a write to a texture while draws are queued; releases during a recorded frame wait for EndFrame.
  The immediate and the recorded paths share `ui_record`. `REALM_VK_UI_CACHE=0` runs every draw at once.

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

Not verified: gradient decorators (none in the client), other filters (none). The frame cache (M4) is in Verification (M4).

### Verification (M4)

2026-10-19, the frame cache (Design). The cache-off runs use `REALM_VK_UI_CACHE=0` on the same binary. HEAD is
the binary before this work. The machine has one GPU (RX 6700 XT, RADV Navi22) and llvmpipe: no NVIDIA or Renoir
device (`REALM_VK_DEVICE=RENOIR` falls back to GL), so those perf legs are not measured.

| Check | Result |
|---|---|
| S1-mixed-dense full UI, 800×600, 120 frames, `--fixture-step-ms 20`: cache on, cache off, HEAD | 0 px; all three byte-identical (md5 `a0a944ea…`) |
| Nexus map (`--map ../realm-server/Resources/Worlds/Nexus/0.jm`), 800×600, 300 frames: on, off vs HEAD | 0 px, 0 px |
| Options script (`300 capture`, `400 key o`, `900 capture`, `1000 quit`): on and off vs HEAD, both captures | 0 px for all four |
| Clip-mask sample (`REALM_TEST_RML`, rounded `overflow:hidden`, 60 frames): on vs off | 0 px (the clip frames run at once) |
| Clip-mask sample, VK vs GL (same sample) | 227 px (0.047%), max Δ 83: the clip-edge difference recorded under M2 |
| Validation (`REALM_VK_VALIDATION=1`): Nexus 300 frames, Options script, S1 120 frames, clip sample | 0 errors, 0 warnings each |
| Replays (RmlGpuVk trace, not kept): Nexus 298 of 300 frames; S1 42 of 120; Options script 26 of 64 | the Options frames under PushLayer run at once |
| `REALM_RENDERER=vulkan ctest --test-dir build -L gpu -j1` (21 tests) | 20 pass; `options_gpu` fails at "a greyed-out Rotate key ignores clicks" (the baseline failure) |
| GL checksum oracle, 300 frames: S1 quad/state/packet, S3 quad/state/packet | match How to test |
| GL captures, HEAD binary vs new binary, 120 frames: churn, S1-mixed-dense, S3-combat-cluster | identical md5 for each pair (the md5s in How to test do not reproduce: `glid.sh` is not in the repo, and 800×600 does not match either) |
| Build (`cmake --build build -j6`) | no new warnings; the 10 `-Wconversion` warnings in `VkRenderer.cpp` (`ui_blur`, `ui_composite`) are on lines unchanged from HEAD |

Perf: Nexus map, 1920×1024, `--benchmark`, uncapped, telemetry off, RADV Navi22. Frame time is the wall-clock
slope between 20 000 and 100 000 frames (startup cancels), 3 interleaved reps per row. Load average (1 min)
was 1.7–2.2 during matrix A and 2.9–4.0 during matrix B (the box was busier; compare within a matrix only).

| Config | A: rep 1 / 2 / 3 (µs per frame) | A median | B: rep 1 / 2 / 3 (µs per frame) | B median |
|---|---|---|---|---|
| GL (RADV) | 96.8 / 92.3 / 91.0 | 92.3 (10 835 FPS) | 121.8 / 117.2 / 126.9 | 121.8 (8 213 FPS) |
| VK, cache off | 71.1 / 68.7 / 69.9 | 69.9 (14 315 FPS) | 92.6 / 94.0 / 85.8 | 92.6 (10 798 FPS) |
| VK, cache on | 70.3 / 68.8 / 70.8 | 70.3 (14 219 FPS) | 93.0 / 87.4 / 95.7 | 93.0 (10 752 FPS) |

Matrix A ran before the last write-path change, matrix B on the final binary. Cache on vs off: +0.5 µs (A) and
+0.4 µs (B) at the median, with per-rep differences of both signs (−6.6 to +9.9 µs in B). That is no measurable
gain and no measurable regression.

Why there is no gain here: the Nexus map's RmlUi documents issue one draw per frame (`ui_draw_calls` = 1 with and
without the cache; the `--hp`, `--breath` and `--notify` fixtures do not change it), and the title screen issues
three. The cache can only remove the RmlUi draw work, so on these scenes it has little to remove. Telemetry on (4000 frames, 2 reps each): `ui.documents.render` median 3.31 and
3.40 µs with the cache off, 2.82 and 2.81 µs with it on; frame CPU is unchanged within noise. A fuller RmlUi scene
is needed to see the cache (see Remaining work).

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
elements, and no Vulkan UI warnings logged. Chat input did not open from a script.

### Verification (server screens)

2026-10-19. The GPU tests start their own HTTP/TCP peers, so with `REALM_RENDERER` they run on Vulkan.

**Suite.** `ctest -L gpu -j1` (21 tests), GL and VK on NVIDIA and on RADV: VK fails nothing GL passes, after
one fix. `telemetry_gpu` failed on VK RADV only (per-model timer overlap, see Timers; fixed).
`options_gpu` fails on all four (baseline). `renderer_gpu` was rewritten for the gateless renderer (below).

**Pixels.** Captures of death_fame, flash_gap, delete_character, guild_board, guild_members, dialog and dye,
GL vs VK on the same driver, with the startup shim (see Verification (other screens)):

| Test | NVIDIA | RADV | Verdict |
|---|---|---|---|
| death_fame, deterministic screens (world, options, new-world) | 0-0.085% / ±1 | same | match |
| flash_gap (all) | 0.2-0.9% / ±1 | same | match |
| guild_board, guild_members (views, lists) | ≤ 0.001% / ±1; one 37 px Δ44 spot | same | match |
| dialog (all) | 0 | 0 | identical |
| dye | 1-2 px | 0 | match |
| delete_character (49 captures) | 2-49% / ±2 | ±2 (one px ±6) | blend rounding, region-wide like the login dialog |

Three apparent differences were timing, not rendering:
- guild_board `restored` (NV) and guild_members `restored` (RADV): the modal was already closed. A resize
  to 4K stalls 135-140 ms on both APIs (and 35-45 ms back), so the capture and the Close click 100 ms later
  can fall into one frame, and the click runs first. With the click moved 2 s later, VK passes. Resize cost,
  measured from telemetry around each resize: GL and VK within about 30% of each other (VK slightly slower on 4 of 6).
- death_fame `late-*` (NV): the menu background world scrolls with time, and VK vs VK runs differ as much
  (20-92%) as VK vs GL, also with `REALM_VK_PRESENTED_COPY=1`. Same scene and overlay; only the scroll
  position differs.

Not covered: shop and trade (no server fixture), death_fame screens that are not deterministic on GL.

**Fallback.** `renderer_gpu` now checks that a Vulkan build starts Vulkan (`--renderer vulkan`, the saved
setting, `REALM_RENDERER`), and forces the fallback with `REALM_VK_DEVICE=no-such-device`. A build with
`-DREALM_ENABLE_VULKAN=OFF` (which did not compile: the stub backend lacked `set_swap_interval`; fixed)
passes it through "Vulkan renderer not available in this build".

## Phase 4: final verification

2026-10-19, RX 6700 XT (desktop) (RADV Navi22, radeonsi), llvmpipe and lavapipe. This machine has no NVIDIA
or Renoir device, so the NVIDIA and Renoir rows above were not rerun. New rows are labelled `RX 6700 XT (desktop)`.
The build was current (`cmake --build build -j6`: no work to do) with the seven uncommitted M4 files.

### Loader path

The client links no Vulkan loader. `ldd build/realm_client` lists no libvulkan, and the link line has no
`-lvulkan` (`cmake/ClientTargets.cmake:113`). SDL dlopens the loader in `SDL_Vulkan_LoadLibrary`
(`src/platform/PlatformSdl.cpp:40`). `VkCommon.cpp:262` takes `vkGetInstanceProcAddr` from the session. A
failed open throws, and `open_window` falls back to OpenGL (`src/core/ApplicationState.cpp:47-51`).

### Perf matrix (RX 6700 XT (desktop), 1920×1024)

`--benchmark` (uncapped, no vsync), telemetry off, `--width 1920 --height 1024`. FPS is the wall-clock slope
between two frame counts, `(t(N2) − t(N1)) / (N2 − N1)`, so startup cancels. Nexus full UI: N1 20 000, N2
100 000 (as in M4). nexus-pan and S3-combat-cluster world only (`REALM_TEST_WORLD_ONLY=1`): nexus-pan 20 000 /
100 000, S3 1 000 / 4 000. S1-mixed-dense and churn full UI: 1 000 / 4 000. Three interleaved reps (the GL and VK
order alternates), median of the rep slopes. Load average (1 min) before each run: 1.4–3.3 on 20 threads.

| Scene | GL FPS (per rep) | VK FPS (per rep) | VK/GL median | VK/GL per rep | Load1 mean (range) |
|---|---|---|---|---|---|
| Nexus full UI | 11 075 (11 934 / 11 075 / 11 073) | 14 651 (14 748 / 14 651 / 14 584) | 1.32 | 1.24 / 1.32 / 1.32 | 1.55 (1.40–1.71) |
| nexus-pan, world only | 12 182 (12 480 / 12 182 / 12 147) | 13 643 (13 643 / 13 760 / 13 518) | 1.12 | 1.09 / 1.13 / 1.11 | 1.57 (1.44–1.77) |
| S3-combat-cluster, world only | 102 (95 / 103 / 102), 9.79 ms | 100 (98 / 103 / 100), 9.95 ms | 0.98 | 1.04 / 1.00 / 0.98 | 1.88 (1.47–2.35) |
| S1-mixed-dense, full UI | 378 (372 / 383 / 378) | 417 (417 / 420 / 403) | 1.10 | 1.12 / 1.10 / 1.07 | 2.27 (1.92–3.29) |
| churn, full UI | 372 (374 / 372 / 368) | 415 (415 / 418 / 411) | 1.12 | 1.11 / 1.12 / 1.12 | 2.04 (1.63–2.90) |

Telemetry pass for CPU and GPU medians (2 000 frames, 3 interleaved reps, `tools/perf_group.py`). Telemetry
adds its own cost, so use these medians for zones and the slope table for FPS:

| Scene | Row | Interval median ms | CPU median ms | GPU median ms |
|---|---|---|---|---|
| S3-combat-cluster, world only | GL | 9.03 | 8.26 | 6.02 |
| S3-combat-cluster, world only | VK | 8.90 | 8.37 | 5.83 |
| S1-mixed-dense, full UI | GL | 1.81 | 1.56 | 1.16 |
| S1-mixed-dense, full UI | VK | 1.43 | 1.36 | 1.22 |

Readings:
- VK wins Nexus full UI by 32%. The medians are close to M4's matrix A (GL 92, VK 70 µs per frame).
- VK wins nexus-pan by 12% and S1 and churn full UI by 10–12%.
- S3 world only is a tie (0.98). The rep ratios (1.04, 1.00, 0.98) sit inside the GL spread (95–103 FPS), and
  the telemetry CPU and GPU medians match. No scene loses by more than noise, so no zone breakdown is needed.

### Lavapipe correctness (full UI, 800×600, `--fixture-step-ms 20`)

VK: `REALM_VK_DEVICE=llvmpipe REALM_VK_VALIDATION=1 build/realm_client --renderer vulkan …`. GL:
`LIBGL_ALWAYS_SOFTWARE=1 build/realm_client --renderer gl …`. Stress scenes: `--stress tests/fixtures/<f>.json
--frames 120 --stress-summary …`. Nexus: `--map ../realm-server/Resources/Worlds/Nexus/0.jm --frames 300`.
Options: the script `300 capture`, `400 key o`, `900 capture`, `1000 quit` with `--frames 64`. A fixed clock
needs finite frames, so the M2/M3 script (no fixed clock) cannot be reused as is.

| Scene | Differing px (%) | Max Δ | Notes |
|---|---|---|---|
| S1-mixed-dense | 0 | 0 | |
| churn | 0 | 0 | |
| S3-combat-cluster | 4 (0.0008%) | 10 | Δ1 ×3, Δ10 ×1, inside the world (x 352–546, y 317–591) |
| Nexus map, 300 frames | 0 | 0 | |
| Options, closed | 0 | 0 | |
| Options, open | 15 617 (3.25%) | 2 | Δ1 15 605, Δ2 12: blur rounding, the M3 pattern |

All five VK runs log `Vulkan validation summary: 0 errors, 0 warnings`. The VK Options screen was viewed: all
rows and the footer are present. No missing or extra elements. The S3 residual is rounding-level.

### Full UI on RADV (GL vs VK, 800×600, validation on)

| Scene | Differing px (%) | Max Δ | M1 (before M4) |
|---|---|---|---|
| S1-mixed-dense, 120 frames | 21 (0.0044%) | 48 | 21 / 48 |
| churn, 120 frames | 184 (0.0383%) | 196 | 188 / 196 |
| S3-combat-cluster, 120 frames | 178 (0.0371%) | 255 | 178 / 255 |
| Nexus map, 300 frames | 15 (0.0031%) | 1 | not run |
| Options, open | 14 800 (3.08%) | 2 | 14 800 / 2 (M3) |

Validation: 0 errors and 0 warnings on all five VK runs. The residuals are the same world-area differences as
M1, within 4 px. The Options closed capture is identical.

### Fallback without a Vulkan loader

Two simulations. Neither touches system files:
- `SDL_VULKAN_LIBRARY=/nonexistent/libvulkan.so.1`. SDL 2.32.74 honours it.
- `LD_PRELOAD` of a scratch shim that makes `dlopen` fail for any `libvulkan` name.

| Case | Result |
|---|---|
| `SDL_VULKAN_LIBRARY` bad, `--renderer vulkan` | Exit 0. stderr: `Vulkan renderer unavailable: Vulkan loader: Failed loading …; starting with OpenGL`. Banner `Renderer OpenGL` |
| Shim, `--renderer vulkan` | Same. Telemetry metadata `renderer_fallback` is set |
| Shim, saved `renderer: 1`, no flag | Same fallback. The settings file is unchanged |
| Shim, `--renderer gl` | Starts on GL with no message |
| `VK_ICD_FILENAMES=<empty dir>`, `--renderer vulkan` | Falls back: `Installed Vulkan doesn't implement the VK_KHR_surface extension`. SDL's check fails before device selection, so the "No Vulkan device meets the baseline" text is not reached |
| No ICD, saved `renderer: 1` | Same fallback. The settings file is unchanged |
| Options Renderer hint, saved Vulkan, loader missing | Hover on the control: `Using OpenGL: Vulkan unavailable (Vulkan loader)`, two lines at 800×600. Before the fix the full SDL reason ran to four lines and was cut (the full reason stays in the log and in `renderer_fallback`) |
| Options Renderer hint, `--renderer vulkan`, no ICD | `Using OpenGL: Vulkan unavailable (Vulkan loader)`, two lines at 800×600 (the clause is the first of the reason; `OptionsScreen.cpp` `short_reason`, capped at 32 characters) |

### ctest

| Run | Command | Result |
|---|---|---|
| Full suite, GL | `ctest --test-dir build -j4` | 89 of 91 pass. Fail: `appearance_compose` (unit), `options_gpu` (gpu). Both baseline |
| GPU, GL | `ctest --test-dir build -L gpu -j1` | 20 of 21. Fail: `options_gpu` (baseline) |
| GPU, Vulkan | `REALM_RENDERER=vulkan ctest --test-dir build -L gpu -j1` | 20 of 21. Fail: `options_gpu`, same assertion ("a greyed-out Rotate key ignores clicks") |

No test regresses against GL.

### Open after Phase 4

- Windows AMD and Intel stacks: not available here. This is still the case that motivated the work.
- NVIDIA and Renoir rows: not rerun on this machine.
- Options hint at 800×600: the long loader reason needs four lines, and the last one is cut. A wording or
  layout choice, not changed here. Options: shorten the SDL reason, or give the footer more room.
- Hovering the Renderer label does not update the hint; hovering the control does. Other rows not checked.
- A real machine without `libvulkan.so.1` was not tested. The simulations cover the dlopen failure only.
- GL on llvmpipe prints `Swap interval unavailable` on stderr. It does not affect captures.

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
   `renderer_gpu` can fail under `-j4` (GPU contention) and passes alone. After the gate removal (no env):
   unit tests 69 of 70, GPU tests (`-j1`) 20 of 21; the failures are the two above.

### Phase 3: RmlUi Vulkan backend (M1-M4 done, see above)

- M4: done (see Design and Verification (M4)). The cache saves nothing measurable on the scenes measured:
  the Nexus map has one RmlUi draw per frame, the title screen three, and the Options screen flushes (a
  layer), so its frames are not reused. Open: measure a fuller RmlUi scene (live play with many documents,
  inventory, chat) before deciding that M4 is worth keeping. The NVIDIA and Renoir perf legs were not run
  (not on this machine; see Verification (M4)).
- Parity and the gate: done (see Verification (server screens)). Shop and trade are not covered by any
  fixture.

### Phase 4: final verification (done on this machine, see [Phase 4](#phase-4-final-verification))

- Done: full-UI parity on RADV and lavapipe, the perf matrix (RX 6700 XT), the fallback without a loader
  (simulated), and ctest under GL and Vulkan.
- Open: a Windows AMD/Intel stack (the case that motivated the work), a real machine without a Vulkan
  loader, and NVIDIA and Renoir reruns.

### CPU hotspots (2026-10-19)

Scene::build pass (GL, 1920×1024, uncapped). CPU is the client's `CPU event/build/submit mean` from runs without telemetry (3 interleaved reps, medians). FPS is the wall-clock slope (Nexus 20 000 to 100 000 frames, S3 world only 1 000 to 4 000, S1 and churn full UI 1 000 to 4 000).

| Item | Zone or metric | Before | After | Decision |
|---|---|---|---|---|
| 1 Terrain sink | S3 CPU mean (two sessions); callgrind Scene::build | 8.70 and 8.62 ms; 18.5 M Ir/frame | 7.98 and 8.10 ms; 17.4 M Ir/frame | accepted: only grounds with `sink` consult the composition (exact) |
| 2 Ground tops | `scene.ground_tops` | 0.009 ms | not changed | rejected: too small to matter |
| 3 Sign text key | `sign_text` calls | 0 on S3, S1, churn | not changed | no hot path in the fixtures (no Sign objects); the key keeps the scale |
| 4 Animated frame reuse | S3 CPU mean, same session | 8.10 ms | 7.91 ms | accepted: the baked path reuses the shadow pass's AnimationFrame (about 3 000 fewer animation_frame calls per frame) |
| 4 Residency lookups | `resident_appearance` 2.2 M Ir/frame | — | — | not changed: the find is an LRU touch, so skipping it changes eviction order |
| 5 Depth sort | `scene.sort_entities` | 0.39 ms (about 10% of scene.build) | not changed | incremental sort not attempted; see Open below |
| 6 Zoom text | per-frame zoom sweep, frame CPU | Low 0.037 ms, High 2.30 ms (p95 2.95) | see UI Quality below | UI Quality setting added (Low, High; High is the default) |

Final against base (medians of 3 reps):

| Scene | CPU mean base → final (ms) | FPS base → final |
|---|---|---|
| Nexus full UI | 0.0399 → 0.0395 | 11 015 → 11 328 |
| S3-combat-cluster, world only | 8.60 → 7.79 | 100.8 → 111.1 |
| S1-mixed-dense, full UI | 2.57 → 2.48 | 379 → 386 |
| churn, full UI | 2.49 → 2.43 | 372 → 386 |

Telemetry breakdown after the pass (S3, 3 reps, telemetry on, which adds about 2 ms; medians): frame.cpu 7.44 ms, scene.build 3.88 ms, of which `scene.draw_stream` 2.53, `scene.sort_entities` 0.39, `scene.shadows` 0.50, `scene.visibility` 0.29, `scene.ground_tops` 0.009.

Open (not on the list, measured): `projectile_quad` takes a sincos per projectile (about 3.6 M Ir/frame on S3, about 20 000 projectiles); `animation_frame` takes an atan2 for each walking entity (about 1.8 M Ir/frame); `Quad` copies in `emplace_back` (about 1.6 M Ir/frame). Per-entity zones distort the frame (the aggregate zone search is linear), so use callgrind diffs for per-entity costs.

UI Quality (Options › Video › Display, after Renderer; live, no restart). High (default) rasterizes names, signs, damage text and speech balloons at the camera zoom, so they stay crisp; Low rasterizes them once at density 1 and scales the bitmap. Verified 2026-10-19: High with frozen time (`--effect-time 0 --scene-time 0`) is byte-identical to the base build for damage text at zoom 1.3 and 2.0 and for a plain Nexus scene at both zooms. Names at High are rasterized at the zoom, which is the pre-density-1 behaviour, so they differ from the old density-1 names at zoom ≠ 1 by design. A zoom sweep with 20 signs, an ally name, a speaker name, a balloon and a damage number costs High 2.30 ms per frame (p95 2.95 ms, about 23 label re-rasters per frame) against Low 0.037 ms; with a fixed zoom both modes cost 0.033 ms.

Identity: the S1 and S3 quad, state and packet checksums match the oracle in How to test. 120-frame GL captures at zoom 1 are byte-identical base and final. New md5s (plain `--capture`, 120 frames): churn `e4b13f5d0fa6161f6906f8f5d26d289d`, S1-mixed-dense `7b427977854a1df0ab9b3769310a4026`, S3-combat-cluster `5cf3f88e7d3bbd6ce983e597e9edad91`.

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

World-only parity capture (Vulkan: add `REALM_VK_VALIDATION=1` and
`--renderer vulkan`; lavapipe: `REALM_VK_DEVICE=llvmpipe`; GL on llvmpipe: `LIBGL_ALWAYS_SOFTWARE=1`):

```
REALM_TEST_WORLD_ONLY=1 build/realm_client --hidden --renderer gl --stress tests/fixtures/S3-combat-cluster.json --stress-summary o.json --frames 120 --fixture-step-ms 20 --capture gl.png
```

Compare: `python3 docs/vulkan/tools/compare.py gl.png vk.png diff.png` (differing %, max delta,
histogram, bbox). `tools/clusters.py` groups differing pixels; `tools/perf_stats.py` summarises
telemetry JSONL files.

GPU model tests: `REALM_VK_DEVICE=NVIDIA|RENOIR|llvmpipe build/tests/realm_model_instance_tests vulkan`
(ctest `model_instances_vulkan`); `build/tests/realm_model_instance_tests` runs the GL section.

Harnesses (`harness/`): `s23.cpp` checks frames, present, resize, capture, and atlas/surface/layer
readback; `s5.cpp` draws walls and meshes in either API. Build with
`harness/build.sh REALM_CPP_DIR BUILD_DIR` (or `build5.sh`) against a Release build with Vulkan ON.
Their link lines are hard-coded for this machine.
