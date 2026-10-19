# Vulkan renderer

The plan, design and status of the OpenGL/Vulkan renderer choice. Last updated 2026-10-19, at commit
`831d1b2` (phase 1, S0 and S1-S4 committed; S5-S8 unfinished, see [Status](#status)).

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
| 2 | Vulkan world renderer (S0-S8) | S0-S4 committed; S5-S7 code in `s5-s8-wip.patch`, unverified; S8 not done |
| 3 | RmlUi Vulkan backend, then remove the WIP gate | M1 committed (geometry, scissor, textures); M2-M4 open; gate kept |
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
| S5 | Walls (SSBO), prepared meshes, model instances | WIP patch |
| S6 | Post effects, draw_overlay, back and presented capture | WIP patch |
| S7 | GPU timers, telemetry, perf | WIP patch; first perf run below |
| S8 | Validation sweep (RADV + lavapipe), ctest, final patch | Not done |

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

**Memory and lifetime.**
- Per-frame data (instances, mesh vertices, overlay) is bump-allocated from the slot's host-visible
  ring. WIP: 8 MB chunks, 16 MB retained per slot, idle chunks freed after 3 frames, oversize chunks
  after 1. `REALM_VK_RING_MEMORY=host` forces plain host memory; the default prefers
  DEVICE_LOCAL|HOST_VISIBLE (ReBAR) when available.
- Device buffers (layers, terrain, model geometry) come from `VkArena` (WIP): 16 MB blocks, first-fit
  with coalescing, dedicated allocations for oversize requests. No per-change vkAllocateMemory.
- Walls: per-slot host-visible SSBO copies (GL's generation/tail logic per slot), so there is no
  write-after-read hazard.
- Releases go to the current slot's retire list and are destroyed when its fence signals. A surface
  reallocation keeps its handle and retires the old image.

**Descriptors.** Never update a set in flight. set0 = atlas sampler + wall SSBO (cached per atlas
handle and wall version); set1 = surface sampler per (handle, filter), handle 0 uses a 1×1 dummy;
set2 = post source per target. Pools use FREE_DESCRIPTOR_SET; released surfaces free their sets.

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
- Current WIP direction: `Renderer::set_presented_capture(bool)`. While a game session is playing
  (the only time death can happen), each frame's final image is copied on the GPU into one persistent
  device-local image before present, and `capture(presented)` reads that image back. Menus pay
  nothing. GL ignores the toggle.
- Measured cost (800×600 nexus-pan, see perf): about +13 µs per frame at ~11k FPS, about 12% at that
  extreme. On S3 it is within noise.
- Alternative if that cost is too high: defer the death snapshot one frame. The caller requests it,
  the next finish_frame fills it from the world image before any death UI draws. This needs the death
  flow in `ApplicationLoopUpdate.cpp` restructured, and the snapshot is one frame later than GL's.

**Timers.** Per-slot query pool, 69 timestamps (5 + 2×32), reset at command-buffer begin, polled
WITH_AVAILABILITY. Ticks × timestampPeriod, masked to timestampValidBits (0 disables timers). Vulkan
uses TOP/BOTTOM_OF_PIPE stamps, so its numbers approximate GL's.

### Test-only switches

| Variable | Effect |
|---|---|
| `REALM_VULKAN_WIP=1` | Allow Vulkan to start. Without it Vulkan fails with "Vulkan UI not implemented yet" and the client falls back to OpenGL. Removed in phase 3. |
| `REALM_VK_VALIDATION=1` | Khronos validation layer, debug messenger, synchronization validation when available; counts at shutdown. Off costs nothing. |
| `REALM_VK_DEVICE=index\|substring` | Pick a device, e.g. `llvmpipe` for lavapipe. No match throws. |
| `REALM_VK_PRESENT=immediate\|mailbox\|fifo` | Override the present mode. |
| `REALM_VK_STATS=1` | (WIP) ring, arena and descriptor-set summary at shutdown. |
| `REALM_VK_RING_MEMORY=host` | (WIP) force the ring into plain host memory. |
| `REALM_VK_PRESENTED_COPY=1` | (WIP) force the per-frame presented copy. |
| `REALM_TEST_RML=file.rml` | (Phase 3) Shows this RML document over the UI host, for RmlUi features the client's screens do not use (clip masks). |
| `REALM_TEST_WORLD_ONLY=1` | Draw the world only (no HUD, RmlUi, overlay text or death fade), above the renderer, so both APIs draw the same quads. |

### Parity

The bar on RADV, per world-only fixture: at least 99.9% identical pixels, with any outliers isolated
to triangle edges and no region-wide differences. The stricter "max channel delta ≤ 1" bar was
dropped: a few edge pixels flip coverage between the two shader compilers. Three bounded experiments,
including `precise` on gl_Position and matching GL's expression order, left them unchanged; the root
cause was not found. Lavapipe is report-only; its bar is "no structural differences".

World-only, 800×600, 120 frames, GL vs Vulkan, RADV (S4):

| Fixture | Differing px | Max delta |
|---|---|---|
| S1-enemies-dense, S1-players-dense, S2-enemies-dense, S3-enemies-dense, S3-players-dense, S3-enemies-spread | 0 | 0 |
| S1-projectiles-dense | 82 (0.017%) | 4 |
| S3-projectiles-dense | 73 (0.015%) | 20 |
| S3-combat-cluster | 181 (0.038%) | 255 (edge pixels, plus a short column at x=267) |
| S3-models-dense (WIP, S5) | 28 | 213 (edges) |
| nexus-pan (WIP, S5) | 0 | 0 |

Lavapipe vs GL on llvmpipe differs by 0.10%-3.04% per fixture. An earlier, unlabelled reading said
this is llvmpipe rasterization and not our Vulkan code. That is **not verified**: redo it as a
labelled four-way comparison (GL-llvmpipe vs GL-RADV, VK-lavapipe vs VK-RADV, GL-llvmpipe vs
VK-lavapipe, GL-RADV vs VK-RADV) with `tools/parity4.py`.

Walls and prepared meshes: no stress fixture draws them (mesh_upload_bytes = 0). The only Vulkan
evidence so far is the synthetic harness `harness/s5.cpp` (within 1 LSB of GL). The GL GPU test
`model_instances` covers them for GL.

### First perf run (WIP build, unverified)

`tools/perf_run.sh`: 3 interleaved reps, 300 frames, `--benchmark` (vsync off), hidden, world only,
**800×600** (the runner did not pass `--width/--height`; rerun at 1920×1024). Medians of the per-rep
means:

| Row | S3-combat-cluster FPS | S3 GPU median ms | nexus-pan FPS |
|---|---|---|---|
| GL | ~101 | ~5.5 | ~8,400 |
| VK, 2 frames in flight | ~101 | ~5.4 | ~11,200 |
| VK, ring in plain host memory | ~102 | ~5.6 | ~11,600 |
| VK, presented copy on | ~101 | ~5.4 | ~9,900 |
| VK, 3 frames in flight | ~101 | ~5.4 | ~11,100 |

Readings: S3 is CPU-bound and both APIs tie, so the earlier "Vulkan GPU is slower on S3" (9.3 vs
6.2 ms) was a vsync/downclock artifact. On a light scene Vulkan is ~30% faster. Frames in flight 2
vs 3 and the ring memory type make no measurable difference, so keep 2 frames and the default ring.
The telemetry metadata still prints GL's `quad_stream` strategy under Vulkan (fix).

## Phase 3: the RmlUi Vulkan backend

### Status

| Milestone | Content | State |
|---|---|---|
| M1 | Premultiplied triangles, scissor, RmlUi transform, textures (TGA files, `item:`/`art:` nearest, host-written, generated) | Done, committed |
| M2 | Clip masks (`EnableClipMask`, `RenderToClipMask` Set/SetInverse/Intersect, stencil) | Done (test sample, below); unused by the client's screens |
| M3 | Layers, blur filter (Settings), other filters, gradient shaders | Open: logs once, draws without the effect |
| M4 | Frame cache (`frame_reused`) | Open: always false |
| Gate | `REALM_VULKAN_WIP` | Kept: M2-M3 missing, so full-UI parity is not established |

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

Clip masks (M2): no client screen clips with border-radius (`overflow` is only `overflow-y:auto`
without rounded corners), so the Options capture never reaches them. `REALM_TEST_RML` loads a sample
(rounded `overflow:hidden` panels, a child overhanging the corner) over the HUD. GL and VK on NVIDIA
both clip to the rounded shape; 270 px differ, all on the corner edges (the stencil is binary on both).

Options screen (scripted, `key o`, GL vs VK, NVIDIA): the Settings screen draws, but the HUD behind it
is blurred on GL (`RetainedUi::set_blur` sets `filter: blur()`) and sharp on VK, so the differences are
the blur (M3).

Not verified: the Settings screen (blur, the only RmlUi filter the client uses, from
`RetainedUi::set_blur`), gradient decorators, and border-radius clipping. No headless flag opens
Options, so they need M2-M3 and a capture harness first.

## Remaining work

### Phase 2 (apply `s5-s8-wip.patch` first; it applies to `831d1b2`)

1. Build it and rerun GL identity: PNG captures of S3-combat-cluster, S1-mixed-dense and churn
   byte-identical to the base build, and the 300-frame checksums below.
2. Decide the presented-capture approach (see Capture) from the measured cost.
3. Rerun the perf matrix at 1920×1024 (`tools/perf_run.sh` with `--width 1920 --height 1024`).
4. Memory bounds: run S3-combat-cluster and churn with `REALM_VK_STATS=1`. Steady-state ring ≤ 16 MB
   per slot, bounded arena blocks, bounded surface descriptor sets under churn.
5. Validation sweep with `REALM_VK_VALIDATION=1` (confirm sync validation is active) on RADV and
   lavapipe: S3-combat-cluster, S1-enemies-dense, S3-models-dense, nexus-pan, churn, plus the s5 and
   s23 harnesses. Bar: 0 errors, no SYNC- messages.
6. Labelled four-way lavapipe comparison.
7. Wall/mesh parity on a real scene: a fixture with walls, or `model_instances` under Vulkan.
8. Full ctest. Known failures: appearance_compose, options_gpu.

### Phase 3: RmlUi Vulkan backend (M2-M4 and gate; M1 is done, see above)

- Original plan notes follow. Our own backend in `src/lib/ui/RmlGpuVk.cpp`. The vendored RmlUi has only the GL3 backend, and
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

- Full-UI parity against GL, perf matrix (Nexus and S3, GL vs Vulkan) on RADV, and correctness on
  lavapipe.
- Check the fallback on a machine without Vulkan.
- Ideally test a Windows AMD/Intel stack, the case that motivated the work.

## How to test

Checksum oracle (GL identity), from `realm-cpp`, **300 frames** (tests/README.md says 120, which gives
different values):

```
REALM_QUAD_CHECKSUM=1 build/realm_client --hidden --stress tests/fixtures/S1-mixed-dense.json --stress-summary s1.json --frames 300
```

| Fixture | quad | state | packet |
|---|---|---|---|
| S1-mixed-dense | 5956456988386327452 | 12694967582451890774 | 4156475164162667594 |
| S3-combat-cluster | 1151069304813412503 | 10048529582793468071 | 17530971269395398748 |

World-only parity capture (Vulkan: add `REALM_VULKAN_WIP=1 REALM_VK_VALIDATION=1` and
`--renderer vulkan`; lavapipe: `REALM_VK_DEVICE=llvmpipe`; GL on llvmpipe: `LIBGL_ALWAYS_SOFTWARE=1`):

```
REALM_TEST_WORLD_ONLY=1 build/realm_client --hidden --renderer gl --stress tests/fixtures/S3-combat-cluster.json --stress-summary o.json --frames 120 --fixture-step-ms 20 --capture gl.png
```

Compare: `python3 docs/vulkan/tools/compare.py gl.png vk.png diff.png` (differing %, max delta,
histogram, bbox). `tools/clusters.py` groups differing pixels; `tools/perf_stats.py` summarises
telemetry JSONL files.

Harnesses (`harness/`): `s23.cpp` checks frames, present, resize, capture, and atlas/surface/layer
readback; `s5.cpp` draws walls and meshes in either API. Build with
`harness/build.sh REALM_CPP_DIR BUILD_DIR` (or `build5.sh`) against a Release build with Vulkan ON.
Their link lines are hard-coded for this machine.
