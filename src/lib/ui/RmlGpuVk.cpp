// The Vulkan side of the UI seam for phase S1: renders nothing. Every handle is 0, every draw is a
// no-op and no timers run, so RmlHost keeps working while the world renders on Vulkan. The real
// Vulkan RmlUi backend replaces this in phase 3. It has no GL or Vulkan dependency.
#include "RmlGpu.hpp"
#include <RmlUi/Core/RenderInterface.h>
#include <optional>

namespace realm::ui {
namespace {
class NullRenderInterface final : public Rml::RenderInterface {
 public:
  Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> /*vertices*/,
                                              Rml::Span<const int> /*indices*/) override {
    return 0;
  }
  void RenderGeometry(Rml::CompiledGeometryHandle /*geometry*/, Rml::Vector2f /*translation*/,
                      Rml::TextureHandle /*texture*/) override {}
  void ReleaseGeometry(Rml::CompiledGeometryHandle /*geometry*/) override {}
  Rml::TextureHandle LoadTexture(Rml::Vector2i& /*texture_dimensions*/,
                                 const Rml::String& /*source*/) override {
    return 0;
  }
  Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> /*source*/,
                                     Rml::Vector2i /*source_dimensions*/) override {
    return 0;
  }
  void ReleaseTexture(Rml::TextureHandle /*texture*/) override {}
  void EnableScissorRegion(bool /*enable*/) override {}
  void SetScissorRegion(Rml::Rectanglei /*region*/) override {}
};

class NullRmlGpu final : public RmlGpu {
 public:
  [[nodiscard]] Rml::RenderInterface& interface() override { return render_; }
  void initialise() override {}
  void shutdown() noexcept override {}
  [[nodiscard]] int max_texture_size() const override { return 2048; }
  [[nodiscard]] Rml::TextureHandle create_texture(int /*width*/, int /*height*/) override {
    return 0;
  }
  void write_texture(Rml::TextureHandle /*texture*/, int /*x*/, int /*y*/, int /*width*/,
                     int /*height*/, const Rml::byte* /*rgba*/) override {}
  [[nodiscard]] Rml::TextureHandle nearest_texture(const Rml::byte* /*rgba*/, std::size_t /*size*/,
                                                   Rml::Vector2i /*dimensions*/) override {
    return 0;
  }
  void content_changed() override {}
  void begin_frame(int /*drawable_width*/, int /*drawable_height*/) override {}
  void end_frame() override {}
  [[nodiscard]] std::size_t draws() const override { return 0; }
  [[nodiscard]] bool frame_reused() const override { return false; }
  [[nodiscard]] bool begin_timer() override { return false; }
  void end_timer() override {}
  [[nodiscard]] std::optional<double> poll_timers_ms() override { return std::nullopt; }
  void reset_bindings() noexcept override {}

 private:
  NullRenderInterface render_;
};
}  // namespace

std::unique_ptr<RmlGpu> make_vulkan_rml_gpu(const HudArt* /*art*/) {
  return std::make_unique<NullRmlGpu>();
}
}  // namespace realm::ui
