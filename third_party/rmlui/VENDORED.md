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
