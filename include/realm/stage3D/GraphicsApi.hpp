#pragma once
// The graphics API the client renders with. Chosen once at startup, before any GPU object exists.
// The values are the Renderer option's choice values (Settings key "renderer").
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace realm::gfx {
// webgpu is the browser build's WebGPU backend (emdawnwebgpu); the web build's "OpenGL" is WebGL2.
enum class Api : int { opengl = 0, vulkan = 1, webgpu = 2 };

[[nodiscard]] std::string_view api_name(Api api);  // "OpenGL", "Vulkan" or "WebGPU"
// The --renderer spelling, "gl", "vulkan" or "webgpu"; nullopt for anything else.
[[nodiscard]] std::optional<Api> parse_api_flag(std::string_view flag);

// Process-wide selection read by every GPU-facing class (Atlas, SurfaceTexture, TerrainBuffer,
// Renderer, the UI host). Written once by the startup flow before the window opens. OpenGL until
// then, so tests and tools that never open the client keep working.
void select_api(Api api);
[[nodiscard]] Api active_api();

// The --renderer choice for this run, when one was given. It overrides the saved setting for this
// run only, and is never saved.
void record_command_line_api(Api api);
[[nodiscard]] std::optional<Api> command_line_api();

// The window's part of the Vulkan setup, supplied by the platform session (the render library has no
// SDL): the loader's vkGetInstanceProcAddr as an opaque pointer, the instance extensions the window
// needs, and the surface for an instance (0 on failure). OpenGL does not use it.
struct WindowSurface {
  std::function<void*()> instance_proc;
  std::function<std::vector<std::string>()> instance_extensions;
  std::function<std::uint64_t(void* instance)> create_surface;
};

// Set when the requested API could not start and the client fell back to OpenGL.
struct Fallback {
  Api requested{};
  std::string reason;  // e.g. "Vulkan renderer not available in this build"
};

void record_fallback(Fallback fallback);
[[nodiscard]] const std::optional<Fallback>& fallback();
}
