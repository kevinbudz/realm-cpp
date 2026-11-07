#pragma once
// Internal to realm_ui_rmlui. The API-specific half of RmlHost: RmlUi's render interface, the
// textures RmlHost writes itself, GPU timing, and the bindings handed back after a pass. RmlHost
// keeps the context, documents, elements, layers and listeners, and includes no GL header.
// The OpenGL implementation (RmlGpuGl.cpp) holds HostRender (RenderInterface_GL3), the timer ring
// and the state resets. A Vulkan implementation would sit beside it later.
#include "realm/assets/HudArt.hpp"
#include <RmlUi/Core/Types.h>
#include <cstddef>
#include <memory>
#include <optional>

namespace Rml {
class RenderInterface;
}

namespace realm::ui {
class RmlGpu {
 public:
  virtual ~RmlGpu() = default;

  // Registered with Rml::SetRenderInterface; RmlUi calls it per geometry, as before.
  [[nodiscard]] virtual Rml::RenderInterface& interface() = 0;
  // Runs once the window's context is current. Throws "RmlUi GL3 initialization failed" on failure.
  virtual void initialise() = 0;
  // Runs after Rml::Shutdown; releases the timer ring.
  virtual void shutdown() noexcept = 0;
  [[nodiscard]] virtual int max_texture_size() const = 0;

  // Textures RmlHost creates and writes itself (native images, pulse text). Nearest sampling,
  // clamp to edge. Returns 0 when the texture cannot be created.
  [[nodiscard]] virtual Rml::TextureHandle create_texture(int width, int height) = 0;
  // Writes a rectangle of tightly packed premultiplied RGBA rows.
  virtual void write_texture(Rml::TextureHandle texture, int x, int y, int width, int height,
                             const Rml::byte* rgba) = 0;
  // Premultiplied RGBA at the image's own size, sampled nearest (HUD art, items).
  [[nodiscard]] virtual Rml::TextureHandle nearest_texture(const Rml::byte* rgba, std::size_t size,
                                                           Rml::Vector2i dimensions) = 0;
  // The frame cache must not reuse a host-written texture's old pixels.
  virtual void content_changed() = 0;

  // One render() pass: begin_frame resets the draw count and sets up the viewport; the GPU time
  // comes from a timer ring (begin_timer returns false when no slot is free; end_timer only after a
  // true begin).
  virtual void begin_frame(int drawable_width, int drawable_height) = 0;
  virtual void end_frame() = 0;
  [[nodiscard]] virtual std::size_t draws() const = 0;
  [[nodiscard]] virtual bool frame_reused() const = 0;
  [[nodiscard]] virtual bool begin_timer() = 0;
  virtual void end_timer() = 0;
  // Completed pass times, oldest first; nullopt while none is ready.
  [[nodiscard]] virtual std::optional<double> poll_timers_ms() = 0;

  // Leaves no program, VAO or buffer bound, as RmlUi does not.
  virtual void reset_bindings() noexcept = 0;
};

// The implementation for gfx::active_api(). Throws if the API's renderer cannot be created.
[[nodiscard]] std::unique_ptr<RmlGpu> make_rml_gpu(const HudArt* art);
// The Vulkan implementation (RmlGpuVk.cpp): RmlUi's geometry and textures on the renderer's UI pass.
[[nodiscard]] std::unique_ptr<RmlGpu> make_vulkan_rml_gpu(const HudArt* art);

// Releases a texture that outlives the host (its renderer is gone). Only for teardown order.
void release_texture_after_teardown(Rml::TextureHandle texture) noexcept;
}  // namespace realm::ui
