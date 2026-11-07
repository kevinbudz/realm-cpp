#include "GraphicsBackend.hpp"
#include "gl/GlBackend.hpp"
#include "vk/VulkanBackend.hpp"

namespace realm::gfx {
Backend& backend() {
  return active_api() == Api::vulkan ? vulkan_backend() : opengl_backend();
}
}  // namespace realm::gfx
