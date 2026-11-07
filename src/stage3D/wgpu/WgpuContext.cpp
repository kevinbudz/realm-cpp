#include "WgpuContext.hpp"
#include <iostream>
#include <stdexcept>

namespace realm::gfx::wgpu {
namespace {
DeviceState state = DeviceState::idle;
std::string error;

Context& device_owner() {
  static Context context_instance;
  return context_instance;
}

std::string text(WGPUStringView view) {
  if (!view.data) {
    return {};
  }
  return view.length == WGPU_STRLEN ? std::string(view.data) : std::string(view.data, view.length);
}

void fail(std::string reason) {
  error = std::move(reason);
  state = DeviceState::failed;
}

void on_device_lost(WGPUDevice const* /*device*/, WGPUDeviceLostReason reason, WGPUStringView message,
                    void* /*userdata1*/, void* /*userdata2*/) {
  // Only the page reloading or the device being destroyed is expected; anything else is logged.
  if (reason != WGPUDeviceLostReason_Destroyed) {
    std::cerr << "WebGPU device lost: " << text(message) << '\n';
  }
}

void on_uncaptured_error(WGPUDevice const* /*device*/, WGPUErrorType type, WGPUStringView message,
                         void* /*userdata1*/, void* /*userdata2*/) {
  std::cerr << "WebGPU error (" << static_cast<int>(type) << "): " << text(message) << '\n';
}
}  // namespace

void on_device_ready(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message,
                     void* userdata1, void* /*userdata2*/) {
  auto& context = *static_cast<Context*>(userdata1);
  if (status != WGPURequestDeviceStatus_Success || !device) {
    fail("WebGPU device: " + text(message));
    return;
  }
  context.device_ = device;
  context.queue_ = wgpuDeviceGetQueue(device);
  state = DeviceState::ready;
}

void on_adapter_ready(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message,
                      void* userdata1, void* /*userdata2*/) {
  auto& context = *static_cast<Context*>(userdata1);
  if (status != WGPURequestAdapterStatus_Success || !adapter) {
    fail("WebGPU adapter: " + text(message));
    return;
  }
  context.adapter_ = adapter;
  WGPUDeviceDescriptor descriptor{};
  descriptor.uncapturedErrorCallbackInfo.callback = on_uncaptured_error;
  descriptor.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
  descriptor.deviceLostCallbackInfo.callback = on_device_lost;
  WGPURequestDeviceCallbackInfo info{};
  info.mode = WGPUCallbackMode_AllowSpontaneous;
  info.callback = on_device_ready;
  info.userdata1 = &context;
  wgpuAdapterRequestDevice(adapter, &descriptor, info);
}

void begin_device_request() {
  if (state != DeviceState::idle) {
    return;
  }
  state = DeviceState::requesting;
  auto& context = device_owner();
  context.instance_ = wgpuCreateInstance(nullptr);
  if (!context.instance_) {
    fail("WebGPU instance unavailable");
    return;
  }
  WGPURequestAdapterCallbackInfo info{};
  info.mode = WGPUCallbackMode_AllowSpontaneous;
  info.callback = on_adapter_ready;
  info.userdata1 = &context;
  wgpuInstanceRequestAdapter(context.instance_, nullptr, info);
}

DeviceState device_state() {
  return state;
}

const std::string& device_error() {
  return error;
}

Context::~Context() {
  shutdown();
}

void Context::read_limits() {
  WGPULimits limits{};
  if (wgpuDeviceGetLimits(device_, &limits) == WGPUStatus_Success) {
    max_texture_size_ = limits.maxTextureDimension2D;
    max_array_layers_ = limits.maxTextureArrayLayers;
  }
  WGPUAdapterInfo adapter_info{};
  if (wgpuAdapterGetInfo(adapter_, &adapter_info) == WGPUStatus_Success) {
    info_.vendor = text(adapter_info.vendor);
    info_.renderer = text(adapter_info.description);
    wgpuAdapterInfoFreeMembers(adapter_info);
  }
  info_.version = "WebGPU";
}

void Context::initialise() {
  if (state != DeviceState::ready) {
    throw std::runtime_error(error.empty() ? "WebGPU device not ready" : error);
  }
  if (surface_) {
    return;
  }
  read_limits();
  WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvas{};
  canvas.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
  canvas.selector.data = "#canvas";
  canvas.selector.length = 7;
  WGPUSurfaceDescriptor descriptor{};
  descriptor.nextInChain = &canvas.chain;
  surface_ = wgpuInstanceCreateSurface(instance_, &descriptor);
  if (!surface_) {
    throw std::runtime_error("WebGPU canvas surface unavailable");
  }
  WGPUSurfaceCapabilities capabilities{};
  if (wgpuSurfaceGetCapabilities(surface_, adapter_, &capabilities) != WGPUStatus_Success ||
      capabilities.formatCount == 0) {
    throw std::runtime_error("WebGPU canvas has no formats");
  }
  // The preferred format is BGRA8 (what browsers present); RGBA8 is taken when it is the only
  // choice of the two, and the first listed format otherwise.
  format_ = capabilities.formats[0];
  for (std::size_t i = 0; i < capabilities.formatCount; ++i) {
    if (capabilities.formats[i] == WGPUTextureFormat_BGRA8Unorm) {
      format_ = WGPUTextureFormat_BGRA8Unorm;
      break;
    }
    if (capabilities.formats[i] == WGPUTextureFormat_RGBA8Unorm) {
      format_ = WGPUTextureFormat_RGBA8Unorm;
    }
  }
  wgpuSurfaceCapabilitiesFreeMembers(capabilities);
}

void Context::shutdown() noexcept {
  if (encoder_) {
    wgpuCommandEncoderRelease(encoder_);
    encoder_ = nullptr;
  }
  if (frame_view_) {
    wgpuTextureViewRelease(frame_view_);
    frame_view_ = nullptr;
  }
  if (frame_texture_) {
    wgpuTextureRelease(frame_texture_);
    frame_texture_ = nullptr;
  }
  if (surface_) {
    wgpuSurfaceUnconfigure(surface_);
    wgpuSurfaceRelease(surface_);
    surface_ = nullptr;
  }
  configured_ = false;
}

void Context::configure(std::uint32_t width, std::uint32_t height) {
  WGPUSurfaceConfiguration config{};
  config.device = device_;
  config.format = format_;
  config.usage = WGPUTextureUsage_RenderAttachment;
  config.width = width;
  config.height = height;
  config.alphaMode = WGPUCompositeAlphaMode_Opaque;
  config.presentMode = WGPUPresentMode_Fifo;
  wgpuSurfaceConfigure(surface_, &config);
  width_ = width;
  height_ = height;
  configured_ = true;
}

WGPUCommandEncoder Context::encoder() {
  if (!encoder_) {
    encoder_ = wgpuDeviceCreateCommandEncoder(device_, nullptr);
  }
  return encoder_;
}

WGPUTextureView Context::target(std::uint32_t width, std::uint32_t height) {
  if (frame_view_) {
    return frame_view_;
  }
  if (!configured_ || width != width_ || height != height_) {
    configure(width, height);
  }
  WGPUSurfaceTexture acquired{};
  wgpuSurfaceGetCurrentTexture(surface_, &acquired);
  if (acquired.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
      acquired.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
    // Outdated or lost (a resize, a hidden canvas): reconfigure once and take the next texture.
    if (acquired.texture) {
      wgpuTextureRelease(acquired.texture);
    }
    configure(width, height);
    wgpuSurfaceGetCurrentTexture(surface_, &acquired);
    if (acquired.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        acquired.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
      if (acquired.texture) {
        wgpuTextureRelease(acquired.texture);
      }
      return nullptr;
    }
  }
  frame_texture_ = acquired.texture;
  frame_view_ = wgpuTextureCreateView(frame_texture_, nullptr);
  return frame_view_;
}

void Context::finish_frame() {
  if (encoder_) {
    const WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder_, nullptr);
    wgpuQueueSubmit(queue_, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder_);
    encoder_ = nullptr;
  }
  if (frame_view_) {
    wgpuTextureViewRelease(frame_view_);
    frame_view_ = nullptr;
  }
  if (frame_texture_) {
    wgpuTextureRelease(frame_texture_);
    frame_texture_ = nullptr;
  }
  ++serial_;
}

Context& context() {
  return device_owner();
}
}  // namespace realm::gfx::wgpu
