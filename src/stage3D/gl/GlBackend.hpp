#pragma once
#include "../GraphicsBackend.hpp"

namespace realm::gfx {
// The OpenGL implementation of the resource seam (GlBackend.cpp); its renderer is RendererGl.
[[nodiscard]] Backend& opengl_backend();
}  // namespace realm::gfx
