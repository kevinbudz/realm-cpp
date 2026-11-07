#include "realm/stage3D/GraphicsApi.hpp"
#include <utility>

namespace realm::gfx {
namespace {
Api active = Api::opengl;
std::optional<Fallback> fallback_state;
std::optional<Api> command_line_state;
}  // namespace

std::string_view api_name(Api api) {
  switch (api) {
    case Api::vulkan:
      return "Vulkan";
    case Api::webgpu:
      return "WebGPU";
    case Api::opengl:
      break;
  }
  return "OpenGL";
}

std::optional<Api> parse_api_flag(std::string_view flag) {
  if (flag == "gl") {
    return Api::opengl;
  }
  if (flag == "vulkan") {
    return Api::vulkan;
  }
#if REALM_WEBGPU_ENABLED
  if (flag == "webgpu") {
    return Api::webgpu;
  }
#endif
  return std::nullopt;
}

void select_api(Api api) {
  active = api;
}

Api active_api() {
  return active;
}

void record_command_line_api(Api api) {
  command_line_state = api;
}

std::optional<Api> command_line_api() {
  return command_line_state;
}

void record_fallback(Fallback fallback) {
  fallback_state = std::move(fallback);
}

const std::optional<Fallback>& fallback() {
  return fallback_state;
}
}  // namespace realm::gfx
