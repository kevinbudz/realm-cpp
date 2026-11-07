#include "GraphicsBackend.hpp"
#include "gl/GlBackend.hpp"
#include "vk/VulkanBackend.hpp"
#include "wgpu/WebGpuBackend.hpp"

namespace realm::gfx {
Backend& backend() {
  switch (active_api()) {
    case Api::vulkan:
      return vulkan_backend();
    case Api::webgpu:
      return webgpu_backend();
    case Api::opengl:
      break;
  }
  return opengl_backend();
}
}  // namespace realm::gfx
