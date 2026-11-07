#pragma once
// WebGPU's start-up as the run flow sees it. The browser grants the adapter and the device
// asynchronously: begin_webgpu_start() starts both requests (web builds with REALM_WEBGPU_ENABLED
// only), and the run flow polls webgpu_start_state() each frame until it is ready or failed. Every
// other build reports failed with a reason, so a --renderer webgpu run falls back to OpenGL.
#include <cstdint>
#include <string>

namespace realm::gfx {
enum class WebGpuStart : std::uint8_t { idle, requesting, ready, failed };

void begin_webgpu_start();
[[nodiscard]] WebGpuStart webgpu_start_state();
// The reason a failed start gives (the browser's message, or "not available in this build").
[[nodiscard]] std::string webgpu_start_reason();
}  // namespace realm::gfx
