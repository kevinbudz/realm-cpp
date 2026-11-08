# Web port (WebGL2 + WebGPU)

Branch `webgpu` in realm-cpp and realm-server. Goal: the client runs in a browser, built with Emscripten
(emsdk 7.0.0 in `~/emsdk`), with WebGL2 as the default renderer and WebGPU as the opt-in one, the same
shape as the desktop OpenGL default + Vulkan opt-in with fallback. The native build must keep working
unchanged; every web change is behind `__EMSCRIPTEN__` (C++) or `EMSCRIPTEN` (CMake).

## Phases

| Phase | Content | State |
|---|---|---|
| W1 | Build: `web` CMake preset, ports, libxml2 FetchContent, asset preload, shell page | Done |
| W2 | Platform: main loop callback, WebGL2 context, no threads, file dialog stub, web options | Done |
| W3 | Network: WebSocket `Transport`, fetch-based async HTTP, server WebSocket port + CORS | Done |
| W4 | WebGL2 renderer: GLES 3.0 variant of `src/stage3D/gl/` and `RmlGpuGl.cpp` | Done |
| W5 | WebGPU backend: `gfx::Api::webgpu`, `src/stage3D/wgpu/`, `RmlGpuWgpu.cpp` (emdawnwebgpu) | Done (W5a renderer, W5b RmlUi) |
| W6 | Verify in a browser against a local server | Done (see below) |

## Running it

```
source ~/emsdk/emsdk_env.sh && cmake --preset web && cmake --build build-web
python3 web/serve.py
```

Open `realm_client.html?server=HOST&port=2051&appport=7777` (add `&renderer=webgpu` for WebGPU). The server
must be the `webgpu` branch of realm-server (WebSocket listener on Ports[2], CORS on the app server).

## Browser performance comparisons

The web shell exposes finite, offline stress runs using the existing deterministic fixture clock.
These synthetic scenes need no account or running game server. With `web/serve.py` running, open each
URL in the same browser on the same machine, keeping the tab in the foreground:

```
http://localhost:8080/realm_client.html?bench=mixed&renderer=gl
http://localhost:8080/realm_client.html?bench=mixed&renderer=webgpu
```

`bench` also accepts `models` and `ui`. The default run is 600 frames, with 120 warm-up frames excluded
from telemetry analysis, at a fixed 1280x720 canvas and with audio disabled. `frames` accepts 240 to
3600. The shell offers telemetry JSONL, fixture-summary JSON and run-environment JSON downloads when
the run finishes. A renderer mismatch or fallback is shown explicitly and invalidates that run as a
sample of the requested backend. Check the environment downloads for matching browser and canvas
dimensions, and repeat runs to assess variation.

Keep each environment download beside its matching telemetry download, preserving their filenames.
Compare one or more runs from each renderer with:

```
python3 tools/web_benchmark_report.py \
  --webgl2 PATH_TO_GL_TELEMETRY.jsonl \
  --webgpu PATH_TO_WEBGPU_TELEMETRY.jsonl \
  --output build/web-bench/report.md
```

The tool writes Markdown and JSON reports, rejects renderer fallback, runtime errors recorded in the
environment download, and mismatched fixtures, drawable sizes or total/warm-up frame counts. It
reports per-run distributions. Each renderer's
summary uses the median of its run medians and run p95s, so one unusually long run does not receive
extra weight.

Browser runs still use requestAnimationFrame. Compare CPU work in milliseconds (`cpu_ms` plus
`present_ms`) and available GPU `total_ms`, rather than interpreting a refresh-rate FPS cap as equal
renderer cost. Stream upload batching moves CPU work into `finish_frame`, which is included in
`present_ms`; comparing `cpu_ms` alone would miss that work. GPU samples may be unavailable or have an
unresolved tail at shutdown. WebGPU exposes whole-frame GPU time only; its other phase fields are not
independent measurements. Its timestamps exclude preceding queue uploads, so the GPU totals alone
do not compare all transfer and rendering work across backends.

`draw_calls` counts draws, not total browser API calls. The `webgpu_stream_*` counters cover only
staged stream allocations and their actual buffer uploads; static buffers, textures and other WebGPU
calls are outside those counters. Fewer calls are a useful optimization target, but do not by
themselves establish lower frame time or power consumption.

## Verification (W6)

Against a local realm-server (`webgpu` branch):

- **WebGL2** (Chromium, the desktop app's browser pane): title screen, character list fetch, register and sign
  in, enter the Nexus over WebSocket, walls, models, HUD, movement; 60 fps, no console errors. The saved login
  survives a reload (IDBFS).
- **WebGPU fallback**: with no adapter (the same Chromium), `renderer=webgpu` logs the reason and opens WebGL2.
- **WebGPU** (headless Firefox 158, `dom.webgpu.enabled`, driven over WebDriver BiDi): title screen (world,
  walls, blur, RmlUi text), login dialog, character screen, Nexus with walls, models, items, minimap and HUD,
  movement; 60 fps, no console errors. The model culling (`frontFace = CCW`) is correct.
- **WGSL**: every module (quad, mesh, model, post, ui) passes naga (Deno's WebGPU).
- **Native**: the full ctest suite passes except `appearance_compose`, which fails on unmodified HEAD too.

Fixes the browser runs found: `RmlHost` takes the SDL window for any non-GL API; `WgpuUi::create` creates its
uniform stream before the bind groups; `release_texture_after_teardown` deletes GL textures only under OpenGL;
the audio device opens on the first user gesture (`Player::allow_output`, called from `ApplicationAudio`).

Follow-ups, verified the same way:

- **Death background (WebGPU)**: while a session plays, `finish_frame` copies each swapchain texture into a
  retained texture; the death's `capture(presented)` starts a buffer readback of it and `take_capture()`
  delivers the image a few frames later, which `ApplicationLoop` puts into `death_background`. Seen in
  Firefox after `/killplayer`: the death screen fades out of the last Nexus frame.
- **GPU timers (WebGL2)**: `RendererGpuTimers` uses `EXT_disjoint_timer_query_webgl2` when the browser
  offers it (Chromium does; Firefox only with `webgl.enable-privileged-extensions`, and not in headless),
  and drops samples across a disjoint event. Chromium reports the full phase breakdown.
- **GPU timers (WebGPU)**: `WgpuTimers` uses the `timestamp-query` feature when the adapter has it: empty
  compute passes at the start and end of the frame write the stamps (both indices defined: Firefox rejects
  the undefined index as 0xFFFFFFFF), resolved and mapped per slot, polled at the start of the next draw.
  The whole frame is reported as `total_ms` (no phases). Firefox reports about 1 ms per title frame.

Remaining gaps: no RmlUi frame cache under WebGPU (render bundles cannot set the scissor or stencil
reference, which the clip masks need; measure before building one); no gradient shaders (no UI file uses
a gradient); miniaudio's Web Audio backend uses the deprecated `ScriptProcessorNode` (a console warning in
Chromium; AudioWorklet needs cross-origin isolation headers on the host).

## Decisions (the contract between pieces)

- **Toolchain flags** (link, set once in CMake under `if(EMSCRIPTEN)`): `-sUSE_SDL=2 -sUSE_LIBPNG=1
  -sUSE_ZLIB=1 -sUSE_FREETYPE=1 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -fwasm-exceptions
  -sALLOW_MEMORY_GROWTH=1 -sFETCH=1 -lwebsocket.js -lidbfs.js -sEXPORTED_RUNTIME_METHODS=...` and
  `-fwasm-exceptions` on compile too. **No pthreads** (no COOP/COEP hosting needed). Vulkan OFF, GLEW
  not used. WebGPU later adds `--use-port=emdawnwebgpu`.
- **GL headers**: one header `src/stage3D/gl/GlApi.hpp` includes `<GLES3/gl3.h>` under
  `__EMSCRIPTEN__` and `<GL/glew.h>` otherwise. Every realm file that included `<GL/glew.h>` includes it
  instead. Define `REALM_GLES 1` there on web so code can test one macro.
- **Assets**: the Flash art, XML and SFX are compiled into the wasm (`realm_embedded_assets`, from `assets/flash`
  and `assets/sfx`), and the runtime Flash root is the virtual `embedded:/flash`. Only the bench fixtures are
  preloaded (`--preload-file`). Not built or run here: emcc is not installed.
- **Main loop**: `application::run` hands its per-frame body to `emscripten_set_main_loop_arg(..., 0, 1)`;
  long-lived state (`owner`, `layers`) moves to heap storage owned by the callback arg. No frame cap,
  no `SDL_Delay`, no spin; requestAnimationFrame paces.
- **Threads**: none on web. The SFX loader runs its work inline from `poll()`. `std::async` HTTP callers
  move to `net::HttpTask` (below). FileDialog reports `available() == false`.
- **HTTP**: `net::HttpTask` (start a GET/POST, then `poll()` → pending / done(HttpResponse) / failed(msg))
  replaces `std::async(http_post...)` in callers. Native: wraps the existing blocking functions in
  `std::async`. Web: `emscripten_fetch` to `http://host:app_port/path`, form-encoded body.
- **Game connection**: on web, `Transport::connect(host, port)` opens `ws://host:port/` (binary) through
  `emscripten/websocket.h`; callbacks queue bytes and state, `poll()` drains them into `Framer`. The
  server listens for WebSocket on Ports[2] (default **2051**). Web default options: host =
  `location.hostname`, game port 2051, app port 7777; URL query `?server=&port=&appport=&renderer=`
  overrides.
- **Server**: a WebSocket listener (HttpListener on `http://+:2051/` or Address:2051) accepts each socket,
  creates a connected loopback TCP pair, passes the server end to `GameServer.Admit(socket, realIp)` and
  pumps bytes both ways. The HTTP API adds `Access-Control-Allow-Origin: *` on every response.
- **Renderer selection on web**: `gfx::Api` gains `webgpu`. Web default WebGL2 (the opengl API value);
  `?renderer=webgpu` or the saved setting selects WebGPU; failure falls back to WebGL2 like Vulkan → GL.
- **Not supported on web**: GPU timers where the browser lacks the extension or feature, synchronous `capture()` of the presented frame
  (`presented` captures return the back buffer before present or an empty image), file dialogs.

## WebGPU backend (W5a)

Selected with `--renderer webgpu` or the Renderer setting (web: "WebGPU" beside "WebGL2"). Built when
`REALM_ENABLE_WEBGPU` is ON, which is the default for the web build and an error for native builds. The
option defines `REALM_WEBGPU_ENABLED=1` for every target; the native build compiles only the stub
(`WebGpuBackend.cpp`), which reports "WebGPU renderer not available in this build".

**Files** (`src/stage3D/wgpu/`):

- `WebGpuBackend.{hpp,cpp}`: `gfx::webgpu_backend()` (the `Backend` for `Api::webgpu`) and the start-state
  entry points (`include/realm/stage3D/WebGpuStart.hpp`).
- `WgpuContext.{hpp,cpp}`: the async start (adapter, then device), the canvas surface (`#canvas`), the
  frame's encoder and swapchain view, `finish_frame`. The header's comment block is the RmlUi hook.
- `WgpuResources.{hpp,cpp}`: surfaces (RGBA8 2D), atlas layers (RGBA8 2D array), static buffers, and
  `Stream`, the per-frame bump allocator over reused chunk buffers. Allocations are staged on the CPU
  and uploaded with `queue.writeBuffer` per used chunk before the frame is submitted.
- `WgpuPipelines.{hpp,cpp}`: bind-group layouts, the two pipeline layouts, the pipelines (quad strip and
  list, model, mesh, post plain and blend), dummies, and the cached surface groups.
- `WgpuShaders.{hpp,cpp}`: the WGSL ports of `src/stage3D/vk/shaders/{quad,mesh,model,post}`.
- `WgpuRenderer.{hpp,cpp}`: `RendererBackend`: draw, draw_overlay, finish_frame, the post passes through
  two offscreen targets, and `ui_pass` (the RmlUi hook).

**Start and fallback.** `web_frame` (Application.cpp) calls `begin_webgpu_start()` once when WebGPU is the
requested API, then returns each frame while the state is `requesting`; the client is built when the state
is `ready` or `failed`. `open_window` takes WebGPU only when it is `ready`; otherwise it records the
fallback (reason from the browser) and opens WebGL2, as a failed Vulkan start opens OpenGL.
The adapter request does not force a fallback adapter, and the callback checks the returned adapter
before requesting its device. emdawnwebgpu maps the browser's `adapter.info.isFallbackAdapter` to
`WGPUAdapterType_CPU`; these adapters are rejected through the same WebGL2 fallback path. Ordinary
browser adapters report `Unknown`, which is accepted. This avoids relying on vendor-name heuristics;
it does not guarantee that the browser's WebGL2 fallback is hardware accelerated.

**Design choices.**

- Y: no flip anywhere. The Vulkan negative-height viewport made framebuffer rows top-down; WebGPU's
  viewport maps NDC y = +1 to row 0 the same way, so the GLSL clip formulas are copied unchanged, and
  `gl_FragCoord` is `@builtin(position)`.
- Culling: the model pipeline uses `frontFace = CCW` with back-face culling, the same framebuffer
  orientation Vulkan measured. Confirmed in Firefox.
- Walls: an RGBA32F texture, 1024 texels wide, read with `textureLoad` in the vertex stage (two texels
  per corner, the `WallGpuVertex` layout). Synced through `plan_walls`, as the Vulkan buffer is. This is the
  texture path the brief preferred; no storage buffers are used.
- Uniforms: one 80-byte block per draw (`QuadUniform`, `PostUniform`), written through `Stream` and read
  with a dynamic offset (256-byte aligned). Instances and mesh vertices are written the same way; a draw's
  firstInstance is the run index inside its submission, as in Vulkan.
- Samplers: textures have one mip, so `textureSampleLevel(..., 0.0)` replaces the implicit-LOD samples.
- Layers: a resize copies on its own submission, so later uploads keep their order.
- Presentation: the browser presents when the animation frame returns; no present call exists in this
  webgpu.h. `finish_frame` submits the encoder and releases the swapchain view.

**Known gaps (W5a).**

- RmlUi: closed by W5b (see below).
- `capture()` returns a transparent image of the requested size (no synchronous readback); a presented
  capture arrives later through `take_capture()` (see W6 follow-ups).
- `Atlas` page count is limited by the device's `maxTextureArrayLayers` (reported through `limits()`).

## RmlUi WebGPU (W5b)

`make_rml_gpu` returns `make_webgpu_rml_gpu` (RmlGpuWgpu.cpp) when the API is WebGPU, so the HUD and the
menus draw through the WebGPU passes. The seam is `include/realm/stage3D/wgpu/WebGpuUi.hpp` (plain data, as
`VkUi.hpp`); the work is in `src/stage3D/wgpu/WgpuUi.{hpp,cpp}`, owned by `WgpuRenderer` (`rml_ui()`).
The Targa decode moved to `src/lib/ui/RmlTarga.hpp`, shared with the Vulkan backend.

**Features** (the Vulkan M1-M3 set): geometry (20-byte `Rml::Vertex`, premultiplied blend), scissor, file, generated
and HUD-art textures (linear for files, nearest for art), clip masks (Set, SetInverse, Intersect, Test),
layers (PushLayer, PopLayer, CompositeLayers with blend mode Replace), and the blur and opacity filters.
Shaders (gradients) log once and draw without their effect, as on Vulkan.

**Passes.** The UI records into the frame's encoder between `draw()` and `draw_overlay()`. A UI pass is open
on the window or on the top layer; draws to the same view share it. A layer push, a pop, a composite and a texture
copy close it. A layer's pass starts cleared (transparent). Every UI pass has a depth-stencil attachment
(`depth24plus-stencil8`, one per window size), so one pipeline set serves the window and layers. The stencil is
cleared by the frame's first pass. A composite with filters copies the layer into a filter target, then
runs the blur as Vulkan does (downscale levels, vertical, horizontal, upscale; each a pass) and finally draws into
the destination through the blend (or replace). Opacities multiply and apply once.

**Stencil clip masks.** WebGPU cannot clear the stencil mid-pass, so Set and SetInverse draw a full-target quad in
clip space with the replace pipeline and the clear value as its reference, limited by the scissor. The mask
geometry then writes 1 (Set) or 0 (SetInverse) by replace. Intersect increments (clamped). Test compares equal.
Mask draws write no colour.

**Uniforms and vertices.** One 176-byte `UiBlock` per draw and per post pass (WGSL `UiBlock` in
`WgpuShaders.cpp`, dynamic offset through a UI `Stream`). Vertices and indices go through the same stream
(chunks now carry the index usage). Entry points: `geometry_vs/fs`, `post_vs/fs`.

**Texture writes keep frame order.** A write (`upload_texture`) closes the open UI pass and records a copy from a
staging buffer into the frame's encoder. A `queue.writeTexture` would run before the whole frame's submission,
so draws recorded before the write would see the new pixels. Releases during a frame wait for `end_frame`.

**Frame cache: not implemented.** The Vulkan cache replays secondary command buffers; WebGPU has no direct
counterpart here, so every draw runs at once and `frame_reused()` is always false. This is the correctness-first
choice; a render-bundle cache can follow.

**Stream reset (W5a fix).** `Stream::reset()` was never called, so the world's and the UI's streams grew every
frame. `WgpuRenderer::finish_frame` now resets both after the submission, which is the point where reuse is safe.

**Stream upload batching.** World/overlay instances, mesh vertices and per-draw uniforms, along with
RmlUi vertex/index/uniform allocations, are copied into CPU staging owned by their stream chunks.
`finish_frame` flushes both streams before submitting the frame encoder, then resets them after
submission. Uploads scale with used chunks rather than allocations; the world and UI retain separate
streams, so this is not a promise of one upload for the entire frame. Static-buffer and texture uploads
keep their existing paths. Atlas-resize and capture-readback submissions do not consume stream data.

The batching trades retained CPU staging memory and uploads of alignment padding for fewer calls.
It preserves draw order, dynamic uniform offsets, clipping and the existing shader layouts. Measure
CPU time and uploaded bytes together before changing chunk sizes or moving per-draw state into a
storage buffer. The no-filter path already skips post-processing; render bundles, async pipeline
creation and compute work remain separate changes to evaluate with browser measurements.

**Known gaps (W5b).**
- No gradient shaders; no frame cache; no RmlUi-only GPU timer (`begin_timer` returns false; the frame
  timer covers the UI).


## Renderer call reductions (2026-11-07)

Measured with interleaved headless-Firefox A/B runs (4 per cell, 900 frames, active CPU = `cpu_ms + present_ms`):
WebGPU -4 to -5% in mixed/ui/models; WebGL2 -1.5% (mixed), -2.4% (ui), flat (models). GPU totals unchanged.
The renderer is a small share of web CPU: in `mixed`, `scene.build`/`scene.draw_stream` dominate.

- **WebGL2** (`RendererGl.cpp`, `GlBackend.cpp`): consecutive model blocks keep the model program, VAO and
  culling bound (the default `glCullFace`/`glFrontFace` calls are gone); int and viewport uniforms skip unchanged
  values; unchanged mesh vertices skip `glBufferData`; `GL_UNPACK_ALIGNMENT` is set once; wall texels upload in at
  most three calls. Web only (`__EMSCRIPTEN__`/`REALM_GLES`): new atlas pages and layers skip the zero-fill upload,
  since WebGL2 and WebGPU zero-initialize storage.
- **WebGPU** (`WgpuRenderer.cpp`, `WgpuResources.cpp`): the atlas group is cached per (atlas, walls) view and the
  post scene groups per target; pipeline and group binds skip unchanged state within a pass; the filtered path's
  empty clear pass is folded into the canvas pass, which also takes the screen and HUD overlays (two passes fewer);
  instances pack straight into stream staging (`Stream::reserve`); layer buffers are rewritten in place unless the
  frame already recorded them; wall texels upload in at most three writes.
- **WebGPU UI** (`WgpuUi.cpp`): pipeline, group 0, scissor and stencil reference skip unchanged state; scene groups
  are cached per source view; texture uploads use pooled staging buffers (recycled after submission); the blur reads
  the layer directly instead of copying it first.

Benchmarking note: with `layout.frame_rate=0` (uncapped), headless Firefox stalls WebGPU presentation into a
99 ms / 1 ms frame pattern, so the uncapped RAF interval is not a WebGPU metric; at the default 60 Hz both backends
hold 16.7 ms. Compare active CPU and GPU totals instead.

### Round two (2026-11-07)

A Chromium CPU profile (`--profiling-funcs` build, headless Chromium on ANGLE/Vulkan) put 17% of WebGL2 main-thread
samples in `getError`, called once per frame by `check_frame`. In Chromium that call is a synchronous round trip to
the GPU process. Changes:

- **WebGL2** (`GlBackend.cpp`, `REALM_GLES` only): `check_frame` asks `emscripten_is_webgl_context_lost` instead of
  calling `glGetError`. Native GL keeps the `glGetError` check.
- **WebGPU** (`StreamStager.hpp`): chunk storage is sized once, so `reserve` no longer zero-fills on every frame;
  alignment-gap bytes are unspecified.
- **Shared** (`Chat.cpp`): `ChatView::glow` reuses a scratch buffer instead of allocating a zeroed one per glyph run.

Interleaved Chromium A/B (3 runs each), median frame interval, round one to round two: WebGL2 mixed 6.6 to 5.0 ms,
ui 1.2 to 0.6 ms, models 1.4 to 1.2 ms; WebGPU unchanged. In-frame CPU for WebGL2 rises by about 0.2 ms because
the GPU process now runs alongside the next frame instead of being drained, so compare the interval here, not
active CPU. Firefox is unchanged within noise (its `getError` is cheap and its interval is compositor-bound).
`-msimd128` was measured and gives no gain in either backend, so the build does not use it.
