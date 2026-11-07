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

Remaining gaps: no RmlUi frame cache and no gradient shaders under WebGPU; no GPU timers on either web
renderer; `capture()` is blank under WebGPU (the death background); miniaudio's Web Audio backend uses the
deprecated `ScriptProcessorNode` (a console warning in Chromium).

## Decisions (the contract between pieces)

- **Toolchain flags** (link, set once in CMake under `if(EMSCRIPTEN)`): `-sUSE_SDL=2 -sUSE_LIBPNG=1
  -sUSE_ZLIB=1 -sUSE_FREETYPE=1 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -fwasm-exceptions
  -sALLOW_MEMORY_GROWTH=1 -sFETCH=1 -lwebsocket.js -lidbfs.js -sEXPORTED_RUNTIME_METHODS=...` and
  `-fwasm-exceptions` on compile too. **No pthreads** (no COOP/COEP hosting needed). Vulkan OFF, GLEW
  not used. WebGPU later adds `--use-port=emdawnwebgpu`.
- **GL headers**: one header `src/stage3D/gl/GlApi.hpp` includes `<GLES3/gl3.h>` under
  `__EMSCRIPTEN__` and `<GL/glew.h>` otherwise. Every realm file that included `<GL/glew.h>` includes it
  instead. Define `REALM_GLES 1` there on web so code can test one macro.
- **Assets**: realm-client runtime files are preloaded to `/realm-client` and the sfx directory to
  `/sfx` (`--preload-file`). The Flash root used at runtime comes from a generated constant that the web
  build sets to `/realm-client` (the build-time import still reads the real sibling path).
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
- **Not supported on web**: GPU timer queries (stubbed), synchronous `capture()` of the presented frame
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
  `Stream`, the per-frame bump allocator over reused chunk buffers (`queue.writeBuffer`).
- `WgpuPipelines.{hpp,cpp}`: bind-group layouts, the two pipeline layouts, the pipelines (quad strip and
  list, model, mesh, post plain and blend), dummies, and the cached surface groups.
- `WgpuShaders.{hpp,cpp}`: the WGSL ports of `src/stage3D/vk/shaders/{quad,mesh,model,post}`.
- `WgpuRenderer.{hpp,cpp}`: `RendererBackend`: draw, draw_overlay, finish_frame, the post passes through
  two offscreen targets, and `ui_pass` (the RmlUi hook).

**Start and fallback.** `web_frame` (Application.cpp) calls `begin_webgpu_start()` once when WebGPU is the
requested API, then returns each frame while the state is `requesting`; the client is built when the state
is `ready` or `failed`. `open_window` takes WebGPU only when it is `ready`; otherwise it records the
fallback (reason from the browser) and opens WebGL2, as a failed Vulkan start opens OpenGL.

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
- `capture()` returns a transparent image of the requested size (no synchronous readback), and presented
  capture is not supported; the death background stays blank under WebGPU.
- No GPU timers (`gpu_ms` is -1, `timing` ignores its recorder).
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

**Known gaps (W5b).**
- No gradient shaders; no frame cache; no GPU timers (`begin_timer` returns false).
- The death background and any other `capture()` user stays blank under WebGPU (W5a gap).

