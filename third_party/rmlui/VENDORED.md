# RmlUi 6.3 (vendored)

- Upstream: https://github.com/mikke89/RmlUi, tag `6.3`
  (`https://codeload.github.com/mikke89/RmlUi/tar.gz/refs/tags/6.3`,
  SHA256 `d977298bb6147610e5984d5db85ddf284020d655a8713913f6982074f1dbdede`).
- License: MIT (`LICENSE.txt`).
- Vendored 2026-10-12 for Q10 (user decision: vendor rather than fetch at configure).
- Kept, unmodified: `Include/RmlUi/{Core,Core.h,Config}`, `Source/Core` (without
  `FontEngineDefault`, which is FreeType-only and unused with `RMLUI_FONT_ENGINE=none`),
  `Backends/RmlUi_Renderer_GL3.*`, `Backends/RmlUi_Platform_SDL.*`, `Backends/RmlUi_Include_GL3.h`.
- Dropped: samples, tests, Lua/SVG/Lottie/Debugger plugins, other backends, upstream CMake.
- `CMakeLists.txt` here is ours: a static `RmlUi::Core` matching the Q09 FetchContent
  configuration (static, no font engine, third-party containers on, column-major matrices).
- To update: replace these directories from the new tarball, re-delete `FontEngineDefault`,
  and record the new tag and hash here.

## Local modifications

- `Backends/RmlUi_Renderer_GL3.*` (marked `realm:`): the base layer is cleared, resolved and composited only over the region
  drawn that frame (`frame_bounds`, `previous_bounds`), and frames with transforms, layers, filters, shaders or other
  non-geometry writes fall back to full screen. Output is unchanged because the layer is zero outside the drawn region.

- `Backends/RmlUi_Renderer_GL3.*` frame cache (marked `realm:`, 2026-10-18): between BeginFrame and EndFrame, geometry, scissor
  and transform calls are queued and replayed in order. The layer clear of `previous_bounds` runs with the replay. A frame whose
  queue and resource generation equal the last replayed frame's composites the resolved layer without clearing, drawing or
  resolving. The generation is bumped by texture and geometry creation and release, by
  viewport changes, and by `TextureContentChanged()`, which the host must call before writing existing texture contents.
  Releases during a frame are deferred to EndFrame. Layer, clip-mask, filter and shader calls replay the queue and make that
  frame ineligible for reuse. Captures matched the uncached path on the Nexus map and a scripted
  hover, Options open and close.
- `Backends/RmlUi_Renderer_GL3.*` fixed GL state (marked `realm:`, 2026-10-18): the GL state backup and restore are gone, and
  the frame reads no GL state back, because each read waits for the driver. `SetFrameState` (entry) sets every state the pass
  depends on: viewport, clear values, texture unit 0, scissor off, cull off, premultiplied blend, sRGB off, stencil on with
  ALWAYS, depth off, colour mask all. `SetBaseState` (exit) leaves the renderer's baseline (Renderer.cpp draw and overlay
  paths): blend with the renderer's functions, depth, cull, stencil and scissor off, full drawable viewport, colour mask all,
  texture unit 0, default stencil function. The scissor test, scissor box and stencil reference are tracked in the backend.
  `Gfx::CheckGLError` runs only when `NDEBUG` is not defined. Dither, logic op, multisample and polygon mode are not touched
  anywhere in `src/`, so their GL defaults apply.
