# Vulkan renderer

The plan, design and status of the OpenGL/Vulkan renderer choice. Last updated 2026-10-19, after the S5-S8
work was committed on `vulkan-s5-s8` (phase 1 and S0-S7 committed, S8 measured, see
[Remaining work](#remaining-work)).

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
| 2 | Vulkan world renderer (S0-S8) | S0-S7 committed; S8 partly done (see below); still behind `REALM_VULKAN_WIP` |
| 3 | RmlUi Vulkan backend, then remove the WIP gate | Not started |
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
| S8 | Validation sweep, ctest, GL identity, parity, memory, perf | Partly done: ctest, GL identity, parity, memory and perf measured. **Validation sweep not run**: the Khronos layer is not installed on the laptop. Presented-capture decision open |

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
- Measured cost (1920×1024, world only, see [Perf](#perf-measured)): Renoir nexus-pan GPU 0.84 → 0.85
  ms median, FPS 1035 → 711 (about 31%); NVIDIA S3 about 4%.
- **Recommendation: implement the deferred snapshot.** The caller requests it, the next finish_frame
  fills it from the world image before any death UI draws. This needs the death flow in
  `ApplicationLoopUpdate.cpp` restructured, and the snapshot is one frame later than GL's. Not
  implemented.

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
| `REALM_VK_PRESENTED_COPY=1` | Force the per-frame presented copy. |
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

Readings:
- VK wins S3 on Renoir by about 28% (GPU-bound: 16.2 vs 22.8 ms) and nexus on NVIDIA by about 13%.
- VK loses nexus on Renoir by about 22% (the CPU waits on a higher VK GPU time) and S3 on NVIDIA by about
  7% (CPU-bound: VK CPU 12.9 vs 11.8 ms).
- The presented copy costs about 31% on Renoir nexus and about 4% on NVIDIA S3. Recommend the
  deferred-snapshot alternative (see Capture).

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

## Remaining work

### Phase 2 (S8)

1. **Validation sweep: not run.** The Khronos validation layer (`vulkan-validation-layers`) is not
   installed and installing it needs sudo. Bar when run: 0 errors, no SYNC- messages, on RADV and
   lavapipe: S3-combat-cluster, S1-enemies-dense, S3-models-dense, nexus-pan, churn, plus the s5 and s23
   harnesses.
2. **Presented-capture decision:** implement the deferred snapshot (see Capture). Open.
3. **Renoir nexus GPU gap:** VK GPU 0.84 ms vs GL 0.70 ms, and with the presented copy 1035 → 711 FPS.
   Open, part of item 2. Measured, not yet explained.
4. **NVIDIA S3 CPU gap:** VK CPU 12.9 ms vs GL 11.8 ms (CPU-bound). Open.
5. Done this session (measured above): GL identity, world-only parity on 3 drivers with a labelled
   four-way comparison, memory bounds, perf matrix at 1920×1024, wall/mesh parity under Vulkan. Full
   ctest (`ctest --test-dir build-vk -j4`, 2026-10-19): 89 of 91 pass. The 2 failures are
   `appearance_compose` and `options_gpu`, both failing before this work; not investigated here.

### Phase 3: RmlUi Vulkan backend

- Our own backend in `src/lib/ui/RmlGpuVk.cpp`. The vendored RmlUi has only the GL3 backend, and
  RmlUi's upstream Vulkan backend lacks filters and layers anyway.
- It must support the blur filter (Settings screen), border-radius clipping (clip masks), and the
  existing frame cache.
- RmlHost uploads textures between draw() and draw_overlay(), so uploads can arrive while the
  swapchain render pass is open. The "keep the pass open" logic must close the pass before any
  transfer and reopen with LOAD.
- Replace the stopgap in `RmlHost.cpp` that finds the window by scanning SDL window ids for
  SDL_WINDOW_VULKAN: pass the window from WindowSession instead.
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
