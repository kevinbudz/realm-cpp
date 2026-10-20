#include "VulkanBackend.hpp"
#include <memory>
#include <stdexcept>

#if REALM_VULKAN_ENABLED
#include "VkArena.hpp"
#include "VkCommon.hpp"
#include "VkFrame.hpp"
#include "VkPipelines.hpp"
#include "VkRenderer.hpp"
#include "VkResources.hpp"
#endif

namespace realm::gfx {
namespace {
#if REALM_VULKAN_ENABLED
class VulkanBackend final : public Backend {
 public:
  [[nodiscard]] Api api() const override { return Api::vulkan; }

  void initialise(const WindowSurface& window) override {
    vk::context().initialise(window);
    vk::frames().create();
    vk::resources().create();
    vk::pipelines().create();
    vk::frames().set_before_submit([](VkCommandBuffer command) {
      vk::resources().close_transfers(command);
      record_presented_copy(command);
    });
  }

  void shutdown() noexcept override {
    report_stats();  // REALM_VK_STATS=1: the peaks and the final holdings, for the memory checks
    // Resources retire into the frames, so they go first; then the frames finish and release.
    vk::resources().destroy();
    vk::frames().destroy();
    release_presented_copy();
    vk::pipelines().destroy();
    vk::arena().destroy();
    vk::context().shutdown();
  }

  void set_swap_interval(int interval) override { vk::frames().set_swap_interval(interval); }

  [[nodiscard]] GraphicsInfo info() override { return vk::context().info(); }

  void check_frame() override {}

  [[nodiscard]] Limits limits() override {
    const auto& device = vk::context();
    if (!device.ready()) {
      throw std::logic_error("Vulkan limits queried before initialisation");
    }
    return {static_cast<int>(device.limits.maxImageDimension2D),
            static_cast<int>(device.limits.maxImageArrayLayers)};
  }

  [[nodiscard]] bool copy_images_supported() override { return true; }

  [[nodiscard]] std::uint32_t create_surface(bool linear) override {
    return vk::resources().create_surface(linear);
  }

  void allocate_surface(std::uint32_t texture, int width, int height) override {
    vk::resources().allocate_surface(texture, width, height);
  }

  void upload_surface(std::uint32_t texture, int x, int y, int width, int height,
                      const std::byte* rgba) override {
    vk::resources().upload_surface(texture, x, y, width, height, rgba);
  }

  void set_surface_filter(std::uint32_t texture, bool linear) override {
    vk::resources().set_surface_filter(texture, linear);
  }

  [[nodiscard]] std::uint32_t create_layers(int size, int layers) override {
    return vk::resources().create_layers(size, layers);
  }

  void upload_layer(std::uint32_t texture, int layer, int x, int y, int width, int height,
                    const std::byte* rgba) override {
    vk::resources().upload_layer(texture, layer, x, y, width, height, rgba);
  }

  // Vulkan reports a failed upload at the call that made it, so there is nothing to check later.
  void verify_layers(std::uint32_t& /*texture*/) override {}

  [[nodiscard]] std::uint32_t resize_layers(std::uint32_t from, int size, int layers, int kept,
                                            bool /*copy_images*/) override {
    return vk::resources().resize_layers(from, size, layers, kept);
  }

  void release_texture(std::uint32_t texture) noexcept override { vk::resources().release(texture); }

  [[nodiscard]] std::uint32_t create_buffer(std::span<const std::byte> bytes) override {
    return vk::resources().create_buffer(bytes);
  }

  void release_buffer(std::uint32_t buffer) noexcept override { vk::resources().release(buffer); }

  [[nodiscard]] std::unique_ptr<RendererBackend> create_renderer(
      std::size_t capacity, bool persistent_streaming,
      std::size_t persistent_minimum_bytes) override {
    return std::make_unique<VkRenderer>(capacity, persistent_streaming, persistent_minimum_bytes);
  }
};
#else
// Builds without the Vulkan headers: every entry throws, so creation fails and the client falls
// back to OpenGL.
[[noreturn]] void unavailable() {
  throw std::runtime_error("Vulkan renderer not available in this build");
}

class VulkanBackend final : public Backend {
 public:
  [[nodiscard]] Api api() const override { return Api::vulkan; }
  void initialise(const WindowSurface& /*window*/) override { unavailable(); }
  void shutdown() noexcept override {}
  void set_swap_interval(int /*interval*/) override {}
  [[nodiscard]] GraphicsInfo info() override { unavailable(); }
  void check_frame() override { unavailable(); }
  [[nodiscard]] Limits limits() override { unavailable(); }
  [[nodiscard]] bool copy_images_supported() override { unavailable(); }
  [[nodiscard]] std::uint32_t create_surface(bool /*linear*/) override { unavailable(); }
  void allocate_surface(std::uint32_t /*texture*/, int /*width*/, int /*height*/) override {
    unavailable();
  }
  void upload_surface(std::uint32_t /*texture*/, int /*x*/, int /*y*/, int /*width*/,
                      int /*height*/, const std::byte* /*rgba*/) override {
    unavailable();
  }
  void set_surface_filter(std::uint32_t /*texture*/, bool /*linear*/) override { unavailable(); }
  [[nodiscard]] std::uint32_t create_layers(int /*size*/, int /*layers*/) override {
    unavailable();
  }
  void upload_layer(std::uint32_t /*texture*/, int /*layer*/, int /*x*/, int /*y*/, int /*width*/,
                    int /*height*/, const std::byte* /*rgba*/) override {
    unavailable();
  }
  void verify_layers(std::uint32_t& /*texture*/) override { unavailable(); }
  [[nodiscard]] std::uint32_t resize_layers(std::uint32_t /*from*/, int /*size*/, int /*layers*/,
                                            int /*kept*/, bool /*copy_images*/) override {
    unavailable();
  }
  void release_texture(std::uint32_t /*texture*/) noexcept override {}
  [[nodiscard]] std::uint32_t create_buffer(std::span<const std::byte> /*bytes*/) override {
    unavailable();
  }
  void release_buffer(std::uint32_t /*buffer*/) noexcept override {}
  [[nodiscard]] std::unique_ptr<RendererBackend> create_renderer(
      std::size_t /*capacity*/, bool /*persistent_streaming*/,
      std::size_t /*persistent_minimum_bytes*/) override {
    unavailable();
  }
};
#endif
}  // namespace

Backend& vulkan_backend() {
  static VulkanBackend backend;
  return backend;
}
}  // namespace realm::gfx
