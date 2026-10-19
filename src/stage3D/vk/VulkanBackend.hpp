#pragma once
#include "../GraphicsBackend.hpp"

namespace realm::gfx {
// The Vulkan backend. Phase 1 is a stub: every entry throws "Vulkan renderer not available in this
// build", so creation fails and the client falls back to OpenGL.
[[nodiscard]] Backend& vulkan_backend();
}  // namespace realm::gfx
