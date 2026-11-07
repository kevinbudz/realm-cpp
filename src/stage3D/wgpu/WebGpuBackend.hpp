#pragma once
#include "../GraphicsBackend.hpp"

namespace realm::gfx {
// The WebGPU backend (web builds with REALM_WEBGPU_ENABLED). Without the option every entry throws
// "WebGPU renderer not available in this build", so creation fails and the client falls back.
[[nodiscard]] Backend& webgpu_backend();
}  // namespace realm::gfx
