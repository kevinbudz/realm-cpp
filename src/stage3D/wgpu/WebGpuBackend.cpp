#include "WebGpuBackend.hpp"
#include "realm/stage3D/WebGpuStart.hpp"
#include <memory>
#include <stdexcept>

#if REALM_WEBGPU_ENABLED
#include "WgpuContext.hpp"
#include "WgpuPipelines.hpp"
#include "WgpuRenderer.hpp"
#include "WgpuResources.hpp"
#endif

namespace realm::gfx {
namespace {
#if REALM_WEBGPU_ENABLED
class WebGpuBackend final : public Backend {
 public:
  [[nodiscard]] Api api() const override { return Api::webgpu; }

  void initialise(const WindowSurface& /*window*/) override {
    // The canvas is the window: the surface comes from "#canvas", the device from the start-up gate.
    wgpu::context().initialise();
    wgpu::resources().create();
    wgpu::pipelines().create(wgpu::context().format());
    wgpu::resources().set_release_hook([](std::uint32_t handle) { wgpu::pipelines().forget_surface(handle); });
  }

  void shutdown() noexcept override {
    wgpu::resources().set_release_hook({});
    wgpu::resources().destroy();
    wgpu::pipelines().destroy();
    wgpu::context().shutdown();
  }

  // The browser paces the frame (requestAnimationFrame); there is no swap interval to set.
  void set_swap_interval(int /*interval*/) override {}

  [[nodiscard]] GraphicsInfo info() override { return wgpu::context().info(); }

  void check_frame() override {}

  [[nodiscard]] Limits limits() override {
    if (!wgpu::context().ready()) {
      throw std::logic_error("WebGPU limits queried before initialisation");
    }
    return {static_cast<int>(wgpu::context().max_texture_size()),
            static_cast<int>(wgpu::context().max_array_layers())};
  }

  // Copies between textures are recorded in WebGPU, so resizes always take the copy path.
  [[nodiscard]] bool copy_images_supported() override { return true; }

  [[nodiscard]] std::uint32_t create_surface(bool linear) override { return wgpu::resources().create_surface(linear); }

  void allocate_surface(std::uint32_t texture, int width, int height) override {
    wgpu::resources().allocate_surface(texture, width, height);
  }

  void upload_surface(std::uint32_t texture, int x, int y, int width, int height, const std::byte* rgba) override {
    wgpu::resources().upload_surface(texture, x, y, width, height, rgba);
  }

  void set_surface_filter(std::uint32_t texture, bool linear) override {
    wgpu::resources().set_surface_filter(texture, linear);
  }

  [[nodiscard]] std::uint32_t create_layers(int size, int layers) override {
    return wgpu::resources().create_layers(size, layers);
  }

  void upload_layer(std::uint32_t texture, int layer, int x, int y, int width, int height, const std::byte* rgba) override {
    wgpu::resources().upload_layer(texture, layer, x, y, width, height, rgba);
  }

  // The writes are queued: a failure shows as an uncaptured error in the log, not at the call.
  void verify_layers(std::uint32_t& /*texture*/) override {}

  [[nodiscard]] std::uint32_t resize_layers(std::uint32_t from, int size, int layers, int kept,
                                            bool /*copy_images*/) override {
    return wgpu::resources().resize_layers(from, size, layers, kept);
  }

  void release_texture(std::uint32_t texture) noexcept override { wgpu::resources().release(texture); }

  [[nodiscard]] std::uint32_t create_buffer(std::span<const std::byte> bytes) override {
    return wgpu::resources().create_buffer(bytes);
  }

  void release_buffer(std::uint32_t buffer) noexcept override { wgpu::resources().release(buffer); }

  [[nodiscard]] std::unique_ptr<RendererBackend> create_renderer(std::size_t capacity, bool persistent_streaming,
                                                                 std::size_t persistent_minimum_bytes) override {
    return std::make_unique<wgpu::WgpuRenderer>(capacity, persistent_streaming, persistent_minimum_bytes);
  }
};

void begin_start() {
  wgpu::begin_device_request();
}

WebGpuStart start_state() {
  switch (wgpu::device_state()) {
    case wgpu::DeviceState::idle:
      return WebGpuStart::idle;
    case wgpu::DeviceState::requesting:
      return WebGpuStart::requesting;
    case wgpu::DeviceState::ready:
      return WebGpuStart::ready;
    case wgpu::DeviceState::failed:
      break;
  }
  return WebGpuStart::failed;
}

std::string start_reason() {
  return wgpu::device_error();
}
#else
// Builds without the WebGPU option: every entry throws, so creation fails and the client falls back.
[[noreturn]] void unavailable() {
  throw std::runtime_error("WebGPU renderer not available in this build");
}

class WebGpuBackend final : public Backend {
 public:
  [[nodiscard]] Api api() const override { return Api::webgpu; }
  void initialise(const WindowSurface& /*window*/) override { unavailable(); }
  void shutdown() noexcept override {}
  void set_swap_interval(int /*interval*/) override {}
  [[nodiscard]] GraphicsInfo info() override { unavailable(); }
  void check_frame() override { unavailable(); }
  [[nodiscard]] Limits limits() override { unavailable(); }
  [[nodiscard]] bool copy_images_supported() override { unavailable(); }
  [[nodiscard]] std::uint32_t create_surface(bool /*linear*/) override { unavailable(); }
  void allocate_surface(std::uint32_t /*texture*/, int /*width*/, int /*height*/) override { unavailable(); }
  void upload_surface(std::uint32_t /*texture*/, int /*x*/, int /*y*/, int /*width*/, int /*height*/,
                      const std::byte* /*rgba*/) override {
    unavailable();
  }
  void set_surface_filter(std::uint32_t /*texture*/, bool /*linear*/) override { unavailable(); }
  [[nodiscard]] std::uint32_t create_layers(int /*size*/, int /*layers*/) override { unavailable(); }
  void upload_layer(std::uint32_t /*texture*/, int /*layer*/, int /*x*/, int /*y*/, int /*width*/, int /*height*/,
                    const std::byte* /*rgba*/) override {
    unavailable();
  }
  void verify_layers(std::uint32_t& /*texture*/) override { unavailable(); }
  [[nodiscard]] std::uint32_t resize_layers(std::uint32_t /*from*/, int /*size*/, int /*layers*/, int /*kept*/,
                                            bool /*copy_images*/) override {
    unavailable();
  }
  void release_texture(std::uint32_t /*texture*/) noexcept override {}
  [[nodiscard]] std::uint32_t create_buffer(std::span<const std::byte> /*bytes*/) override { unavailable(); }
  void release_buffer(std::uint32_t /*buffer*/) noexcept override {}
  [[nodiscard]] std::unique_ptr<RendererBackend> create_renderer(std::size_t /*capacity*/,
                                                                 bool /*persistent_streaming*/,
                                                                 std::size_t /*persistent_minimum_bytes*/) override {
    unavailable();
  }
};

void begin_start() {}

WebGpuStart start_state() {
  return WebGpuStart::failed;
}

std::string start_reason() {
  return "WebGPU renderer not available in this build";
}
#endif
}  // namespace

Backend& webgpu_backend() {
  static WebGpuBackend backend;
  return backend;
}

void begin_webgpu_start() {
  begin_start();
}

WebGpuStart webgpu_start_state() {
  return start_state();
}

std::string webgpu_start_reason() {
  return start_reason();
}
}  // namespace realm::gfx
