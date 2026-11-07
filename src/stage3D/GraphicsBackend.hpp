#pragma once
// Internal to realm_render. Two seams: Backend (resource operations, one per API, called per
// resource operation or per frame) and RendererBackend (the per-renderer frame; the Renderer facade
// forwards to it). No virtual call happens per quad.
//
// Texture and buffer handles are the uint32 values that Quad::texture, QuadMeta::texture,
// Atlas::texture(), SurfaceTexture, TerrainBuffer::buffer() and QuadLayer::buffer carry. A handle
// is only meaningful to the backend that created it, and one process uses one backend. 0 means
// none. OpenGL: the GL object name. Vulkan (later): an index into the backend's resource table.
#include "realm/stage3D/Atlas.hpp"
#include "realm/stage3D/GraphicsApi.hpp"
#include "realm/stage3D/Renderer.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace realm::gfx {
class RendererBackend {
 public:
  virtual ~RendererBackend() = default;
  virtual void set_clear_color(std::array<float, 4> color) = 0;
  virtual void draw(const Atlas& atlas, std::span<const Quad> quads, int width, int height,
                    int clip_width, std::span<const Quad> overlay,
                    const ScreenEffects& effects) = 0;
  virtual void draw_overlay(const Atlas& atlas, std::span<const Quad> overlay, int width,
                            int height) = 0;
  [[nodiscard]] virtual std::size_t draw_calls() const = 0;
  virtual void timing(telemetry::Recorder* recorder, std::uint64_t frame_id, bool normal) = 0;
  // While enabled, frames are timed without a recorder too (gpu_ms() only).
  virtual void set_gpu_timing(bool enabled) = 0;
  // Ends the frame: a no-op for OpenGL (the platform swaps); Vulkan submits and presents.
  virtual void finish_frame() = 0;
  virtual void poll_gpu() = 0;
  [[nodiscard]] virtual std::size_t pending_gpu() const = 0;
  [[nodiscard]] virtual std::size_t skipped_gpu() const = 0;
  [[nodiscard]] virtual double gpu_ms() const = 0;
  [[nodiscard]] virtual bool persistent_streaming() const = 0;
  [[nodiscard]] virtual Image capture(int width, int height, bool presented) const = 0;

  // While enabled, Vulkan and WebGPU keep a copy of each presented frame so capture(presented) can
  // read the last one back. OpenGL reads its back buffer and ignores this.
  virtual void set_presented_capture(bool /*enabled*/) {}
  // WebGPU: the presented capture arrives a few frames later; returns it once. Other backends: none.
  [[nodiscard]] virtual std::optional<Image> take_capture() { return std::nullopt; }
  // A capability: GL runs with base instances (GL 4.2); the per-run path renders the same pixels.
  virtual void set_base_instance_draws(bool enabled) = 0;
  [[nodiscard]] virtual bool base_instance_draws() const = 0;
};

class Backend {
 public:
  struct Limits {
    int max_texture_size{};
    int max_array_layers{};
  };

  virtual ~Backend() = default;
  [[nodiscard]] virtual Api api() const = 0;
  // Runs once the window (and its GL context, when OpenGL) exists, before any GPU object. Throws
  // std::runtime_error with the reason; the caller falls back to OpenGL.
  virtual void initialise(const WindowSurface& window) = 0;
  // Releases the API's own state. Runs after every GPU object is gone and before the window session
  // (which unloads the Vulkan loader) ends.
  virtual void shutdown() noexcept = 0;
  // The vsync interval of the window; applies to the presentation mode.
  virtual void set_swap_interval(int interval) = 0;
  [[nodiscard]] virtual GraphicsInfo info() = 0;
  // End-of-frame error check; GL: glGetError, throws "OpenGL error during frame".
  virtual void check_frame() = 0;
  [[nodiscard]] virtual Limits limits() = 0;
  // Whether a copy-image path exists (GL 4.3 / ARB_copy_image). Atlas::hardware_copy adds the
  // force_blit_copy test hook on top.
  [[nodiscard]] virtual bool copy_images_supported() = 0;

  // 2D RGBA8 surfaces (SurfaceTexture). Clamp to edge; nearest unless linear. upload_surface
  // takes tightly packed RGBA rows of the rectangle.
  [[nodiscard]] virtual std::uint32_t create_surface(bool linear) = 0;
  virtual void allocate_surface(std::uint32_t texture, int width, int height) = 0;
  virtual void upload_surface(std::uint32_t texture, int x, int y, int width, int height,
                              const std::byte* rgba) = 0;
  virtual void set_surface_filter(std::uint32_t texture, bool linear) = 0;

  // RGBA8 layer arrays (Atlas): size x size layers, nearest, clamp to edge. upload_layer takes
  // tightly packed RGBA rows of the rectangle in one layer.
  [[nodiscard]] virtual std::uint32_t create_layers(int size, int layers) = 0;
  virtual void upload_layer(std::uint32_t texture, int layer, int x, int y, int width, int height,
                            const std::byte* rgba) = 0;
  // Throws "GPU atlas upload failed" when an upload since create_layers failed; the array is then
  // released and the handle cleared.
  virtual void verify_layers(std::uint32_t& texture) = 0;
  // A new array of `layers` layers whose first `kept` layers are copied from `from` (the rest are
  // zeroed). copy_images picks the copy-image path over framebuffer blits. Throws
  // "GPU atlas resize failed" and leaves no handle behind; the caller releases `from` after
  // success.
  [[nodiscard]] virtual std::uint32_t resize_layers(std::uint32_t from, int size, int layers,
                                                    int kept, bool copy_images) = 0;
  virtual void release_texture(std::uint32_t texture) noexcept = 0;

  // Instance buffers for TerrainBuffer (uploaded once). QuadLayer buffers stay in the renderer:
  // they are created and re-uploaded in its draw path, and released here.
  [[nodiscard]] virtual std::uint32_t create_buffer(std::span<const std::byte> bytes) = 0;
  virtual void release_buffer(std::uint32_t buffer) noexcept = 0;

  [[nodiscard]] virtual std::unique_ptr<RendererBackend> create_renderer(
      std::size_t capacity, bool persistent_streaming, std::size_t persistent_minimum_bytes) = 0;
};

// The backend for the active API (gfx::active_api()). In a build without Vulkan, the Vulkan backend
// throws "Vulkan renderer not available in this build" from initialise() and create_renderer().
[[nodiscard]] Backend& backend();
}
