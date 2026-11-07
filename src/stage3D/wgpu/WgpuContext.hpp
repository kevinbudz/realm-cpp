#pragma once
// The WebGPU device: instance, adapter, device, queue, the canvas surface, and the current frame's
// command encoder and swapchain view. Internal to realm_render; compiled only with
// REALM_WEBGPU_ENABLED (the web build, through emdawnwebgpu).
//
// Start-up is asynchronous in the browser. begin_device_request() asks for the adapter, then the
// device, and returns at once; device_state() is polled from the frame callback (web_frame) until
// the device is ready or failed. Context::initialise() then creates the canvas surface.
//
// Shared API for the RmlUi WebGPU backend (RmlGpuWgpu.cpp), which plugs in as the Vulkan one does (the
// seam is realm/stage3D/wgpu/WebGpuUi.hpp, the passes are WgpuUi.hpp):
//   - context().device(), queue(), format(): create buffers, textures, pipelines and bind groups.
//   - context().encoder() and target(w, h): the UI passes record into the frame's encoder, on the
//     window's swapchain view or on their own layer and filter targets. WgpuRenderer::rml_ui() owns
//     them. Between draw() and draw_overlay() they land over the world and under the HUD overlay, as
//     RmlGpuVk does.
//   - Renderer::finish_frame() ends the frame (submits the encoder); the browser presents when the
//     animation frame returns, so nothing calls a present.
//   - Resources (WgpuResources.hpp) hands out texture views and samplers for handles, and the
//     pipelines' bind-group helpers are reused for atlas and surface sampling.
#include "realm/stage3D/GraphicsApi.hpp"
#include "../GraphicsBackend.hpp"
#include <webgpu/webgpu.h>
#include <cstdint>
#include <string>

namespace realm::gfx::wgpu {
enum class DeviceState : std::uint8_t { idle, requesting, ready, failed };

// Starts the adapter and device requests (idempotent; returns at once).
void begin_device_request();
[[nodiscard]] DeviceState device_state();
[[nodiscard]] const std::string& device_error();

// The process's device and its swapchain. The backend initialises it once the device is ready.
class Context {
 public:
  Context() = default;
  ~Context();
  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;

  // Creates the canvas surface ("#canvas") and picks its format. Throws with the reason.
  void initialise();
  // Unconfigures and releases the surface; the device stays (the page owns it for its lifetime).
  void shutdown() noexcept;
  [[nodiscard]] bool ready() const { return surface_ != nullptr; }

  [[nodiscard]] WGPUDevice device() const { return device_; }
  [[nodiscard]] WGPUQueue queue() const { return queue_; }
  [[nodiscard]] WGPUTextureFormat format() const { return format_; }
  [[nodiscard]] GraphicsInfo info() const { return info_; }
  // Device limits the atlas checks (maxTextureDimension2D and maxTextureArrayLayers).
  [[nodiscard]] std::uint32_t max_texture_size() const { return max_texture_size_; }
  [[nodiscard]] std::uint32_t max_array_layers() const { return max_array_layers_; }

  // The frame's command encoder; opens the frame on first use.
  [[nodiscard]] WGPUCommandEncoder encoder();
  // The frame's swapchain view for a width x height drawable. Acquires the swapchain texture on
  // first use and reconfigures the surface when the size changed. Null when no texture could be
  // acquired (the caller then draws nothing).
  [[nodiscard]] WGPUTextureView target(std::uint32_t width, std::uint32_t height);
  // Submits the frame's encoder and releases the swapchain view. The browser presents afterwards.
  void finish_frame();
  [[nodiscard]] std::uint64_t serial() const { return serial_; }

 private:
  void configure(std::uint32_t width, std::uint32_t height);
  void read_limits();

  WGPUInstance instance_ = nullptr;
  WGPUAdapter adapter_ = nullptr;
  WGPUDevice device_ = nullptr;
  WGPUQueue queue_ = nullptr;
  WGPUSurface surface_ = nullptr;
  WGPUTextureFormat format_ = WGPUTextureFormat_BGRA8Unorm;
  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  bool configured_ = false;
  GraphicsInfo info_;
  std::uint32_t max_texture_size_ = 0;
  std::uint32_t max_array_layers_ = 0;

  WGPUCommandEncoder encoder_ = nullptr;
  WGPUTexture frame_texture_ = nullptr;
  WGPUTextureView frame_view_ = nullptr;
  std::uint64_t serial_ = 0;

  friend void begin_device_request();
  friend void on_adapter_ready(WGPURequestAdapterStatus, WGPUAdapter, WGPUStringView, void*, void*);
  friend void on_device_ready(WGPURequestDeviceStatus, WGPUDevice, WGPUStringView, void*, void*);
};

// The process's context (the WebGPU backend initialises it).
[[nodiscard]] Context& context();
}  // namespace realm::gfx::wgpu
