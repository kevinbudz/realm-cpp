// RmlUi's Vulkan seam (include/realm/stage3D/vk/VkUi.hpp). The work happens in the frame's renderer;
// without Vulkan the calls do nothing and no texture can be made.
#include "realm/stage3D/vk/VkUi.hpp"

#if REALM_VULKAN_ENABLED
#include "../GraphicsBackend.hpp"
#include "VkRenderer.hpp"
#include "VkResources.hpp"

namespace realm::gfx::vk::ui {
namespace {
// The UI pass closes before any transfer: a copy inside a render pass is invalid.
void close_pass() {
  if (auto* renderer = active_renderer()) {
    renderer->ui_close();
  }
}
}  // namespace

void begin_frame(int width, int height) {
  if (auto* renderer = active_renderer()) {
    renderer->ui_frame(width, height);
  }
}

void draw(const Draw& draw) {
  if (auto* renderer = active_renderer()) {
    renderer->ui_draw(draw);
  }
}

void end_frame() { close_pass(); }

bool replay_cached(std::uint64_t serial) {
  if (auto* renderer = active_renderer()) {
    return renderer->ui_replay(serial);
  }
  return false;
}

void record_bundle(const Draw* draws, std::size_t count, std::uint64_t serial) {
  if (auto* renderer = active_renderer()) {
    renderer->ui_record_bundle(draws, count, serial);
  }
}

std::uint32_t push_layer() {
  if (auto* renderer = active_renderer()) {
    return renderer->ui_push_layer();
  }
  return 0;
}

void pop_layer() {
  if (auto* renderer = active_renderer()) {
    renderer->ui_pop_layer();
  }
}

void composite(const Composite& composite) {
  if (auto* renderer = active_renderer()) {
    renderer->ui_composite(composite);
  }
}

std::uint32_t create_texture(int width, int height, bool linear) {
  // The texture needs the device and its resources, not the renderer: RmlUi loads documents (and
  // their art) before the first world frame.
  if (width <= 0 || height <= 0) {
    return 0;
  }
  close_pass();
  auto& resources = realm::gfx::vk::resources();
  const auto texture = resources.create_surface(linear);
  resources.allocate_surface(texture, width, height);
  return texture;
}

void upload_texture(std::uint32_t texture, int x, int y, int width, int height,
                    const std::byte* rgba) {
  if (!texture) {
    return;
  }
  close_pass();
  realm::gfx::vk::resources().upload_surface(texture, x, y, width, height, rgba);
}

void release_texture(std::uint32_t texture) noexcept {
  if (texture) {
    realm::gfx::vk::resources().release(texture);
  }
}

int max_texture_size() { return static_cast<int>(realm::gfx::backend().limits().max_texture_size); }
}  // namespace realm::gfx::vk::ui

#else

namespace realm::gfx::vk::ui {
void begin_frame(int /*width*/, int /*height*/) {}
void draw(const Draw& /*draw*/) {}
void end_frame() {}
bool replay_cached(std::uint64_t /*serial*/) { return false; }
void record_bundle(const Draw* /*draws*/, std::size_t /*count*/, std::uint64_t /*serial*/) {}
std::uint32_t push_layer() { return 0; }
void pop_layer() {}
void composite(const Composite& /*composite*/) {}
std::uint32_t create_texture(int /*width*/, int /*height*/, bool /*linear*/) { return 0; }
void upload_texture(std::uint32_t /*texture*/, int /*x*/, int /*y*/, int /*width*/, int /*height*/,
                    const std::byte* /*rgba*/) {}
void release_texture(std::uint32_t /*texture*/) noexcept {}
int max_texture_size() { return 2048; }
}  // namespace realm::gfx::vk::ui
#endif
