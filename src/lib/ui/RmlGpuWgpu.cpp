// RmlUi on WebGPU (the browser build, W5b): geometry, scissor, textures, clip masks (stencil), layers, and the blur
// and opacity filters, as RmlGpuVk.cpp, on the UI passes of WgpuUi. Compiled geometry is kept on the host and its
// vertices are written to the frame's stream when drawn. Shaders (gradients) are not implemented: they log once and
// draw without their effect.
//
// No frame cache: every draw runs at once, so frame_reused() is always false (correctness first; the Vulkan cache
// replays secondary command buffers, which WebGPU has no counterpart for in this backend). Textures released during
// a frame wait for EndFrame, as the draws recorded so far may sample them. There are no GPU timers.
#include "RmlGpu.hpp"
#include "RmlTarga.hpp"
#include "realm/stage3D/wgpu/WebGpuUi.hpp"
#include <RmlUi/Core.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace realm::ui {
namespace {
static_assert(sizeof(Rml::Vertex) == sizeof(gfx::wgpu::ui::Vertex));

// RmlUi's render interface on the WebGPU UI passes. HUD art ("item:" and "art:" sources) is nearest sampled, as on GL.
class WgpuRenderInterface final : public Rml::RenderInterface {
 public:
  explicit WgpuRenderInterface(const HudArt* art) : art_(art) {}

  // BeginFrame: the window size, and the UI frame opens.
  void begin_frame(int width, int height) {
    width_ = width;
    height_ = height;
    draws = 0;
    in_frame_ = true;
    gfx::wgpu::ui::begin_frame(width, height);
  }

  // EndFrame: closes the UI pass, then performs the releases that waited for the frame.
  void end_frame() {
    gfx::wgpu::ui::end_frame();
    in_frame_ = false;
    for (const auto texture : deferred_textures_) {
      gfx::wgpu::ui::release_texture(texture);
    }
    deferred_textures_.clear();
  }

  Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                              Rml::Span<const int> indices) override {
    Geometry geometry;
    geometry.vertices.resize(vertices.size());
    std::memcpy(geometry.vertices.data(), vertices.data(), vertices.size() * sizeof(Rml::Vertex));
    geometry.indices.reserve(indices.size());
    for (const int index : indices) {
      geometry.indices.push_back(static_cast<std::uint32_t>(index));
    }
    geometry.live = true;
    for (std::size_t i = 0; i < geometries_.size(); ++i) {
      if (!geometries_[i].live) {
        geometries_[i] = std::move(geometry);
        return i + 1;
      }
    }
    geometries_.push_back(std::move(geometry));
    return geometries_.size();
  }

  void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation,
                      Rml::TextureHandle texture) override {
    // While a clip mask is enabled, the geometry draws only where the stencil equals its reference.
    draw_geometry(handle, translation, texture, clip_ ? gfx::wgpu::ui::Clip::test : gfx::wgpu::ui::Clip::none);
  }

  // As RenderInterface_GL3: Set and SetInverse clear the stencil and write 1 (or 0) inside the geometry; Intersect
  // increments it. The reference after each operation is the value the following draws must match.
  void RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry,
                        Rml::Vector2f translation) override {
    gfx::wgpu::ui::Clip clip = gfx::wgpu::ui::Clip::mask_set;
    switch (operation) {
      case Rml::ClipMaskOperation::Set:
        clip = gfx::wgpu::ui::Clip::mask_set;
        stencil_reference_ = 1;
        break;
      case Rml::ClipMaskOperation::SetInverse:
        clip = gfx::wgpu::ui::Clip::mask_set_inverse;
        stencil_reference_ = 1;
        break;
      case Rml::ClipMaskOperation::Intersect:
        clip = gfx::wgpu::ui::Clip::mask_intersect;
        stencil_reference_ += 1;
        break;
    }
    draw_geometry(geometry, translation, 0, clip);
  }

  void EnableClipMask(bool enable) override { clip_ = enable; }

  void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override {
    if (!handle || handle > geometries_.size()) {
      return;
    }
    // The vertices were copied into the frame when drawn, so the geometry is free at once.
    geometries_[handle - 1] = Geometry{};
  }

  Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override {
    std::string key;
    if (source.rfind("item:", 0) == 0) {
      key = "item" + source.substr(5);
    } else if (source.rfind("art:", 0) == 0) {
      key = source.substr(4);
    } else {
      auto image = load_targa(source);
      if (!image) {
        return {};
      }
      dimensions = image->dimensions;
      return upload(image->pixels.data(), image->pixels.size(), dimensions, true);
    }
    if (!art_ || !art_->contains(key)) {
      Rml::Log::Message(Rml::Log::LT_ERROR, "Missing HUD art '%s'.", key.c_str());
      return {};
    }
    const auto& image = art_->image(key);
    dimensions = {image.width, image.height};
    return nearest_texture(image.pixels.data(), image.pixels.size(), dimensions);
  }

  Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions) override {
    return upload(source.data(), source.size(), dimensions, true);
  }

  void ReleaseTexture(Rml::TextureHandle texture) override {
    if (in_frame_) {
      deferred_textures_.push_back(static_cast<std::uint32_t>(texture));  // recorded draws may still sample it
      return;
    }
    gfx::wgpu::ui::release_texture(static_cast<std::uint32_t>(texture));
  }

  void EnableScissorRegion(bool enable) override { scissor_ = enable; }

  void SetScissorRegion(Rml::Rectanglei region) override {
    region_ = {region.Left(), region.Top(), region.Width(), region.Height()};
  }

  void SetTransform(const Rml::Matrix4f* transform) override {
    has_transform_ = transform != nullptr;
    if (transform) {
      transform_ = *transform;
    }
  }

  // RmlUi's layers: a new layer is cleared and becomes the target of the draws that follow.
  Rml::LayerHandle PushLayer() override {
    const auto layer = gfx::wgpu::ui::push_layer();
    if (!layer) {
      log_once("no UI layer could be made on WebGPU");
    }
    return layer;
  }

  void PopLayer() override { gfx::wgpu::ui::pop_layer(); }

  // Composites a layer through its filters (blur, opacity) into the destination, as RenderInterface_GL3 does.
  void CompositeLayers(Rml::LayerHandle source, Rml::LayerHandle destination, Rml::BlendMode blend_mode,
                       Rml::Span<const Rml::CompiledFilterHandle> filter_handles) override {
    std::vector<gfx::wgpu::ui::Filter> filters;
    for (const auto handle : filter_handles) {
      if (handle && handle <= filters_.size()) {
        filters.push_back(filters_[handle - 1]);
      }
    }
    gfx::wgpu::ui::Composite composite;
    composite.source = static_cast<std::uint32_t>(source);
    composite.destination = static_cast<std::uint32_t>(destination);
    composite.replace = blend_mode == Rml::BlendMode::Replace;
    composite.scissor = scissor_;
    for (std::size_t i = 0; i < 4; ++i) {
      composite.region[i] = region_[i];
    }
    composite.filters = filters.data();
    composite.filter_count = filters.size();
    gfx::wgpu::ui::composite(composite);
  }

  Rml::CompiledShaderHandle CompileShader(const Rml::String& /*name*/, const Rml::Dictionary& /*parameters*/) override {
    log_once("shaders (gradients) are not implemented on WebGPU yet");
    return {};
  }

  Rml::CompiledFilterHandle CompileFilter(const Rml::String& name, const Rml::Dictionary& parameters) override {
    if (name != "blur" && name != "opacity") {
      log_once("this filter is not implemented on WebGPU yet (only blur and opacity)");
      return {};
    }
    gfx::wgpu::ui::Filter filter;
    if (name == "blur") {
      filter.sigma = Rml::Get(parameters, "sigma", 1.0f);
    } else {
      filter.opacity = Rml::Get(parameters, "value", 1.0f);
    }
    filters_.push_back(filter);
    return filters_.size();
  }

  void ReleaseFilter(Rml::CompiledFilterHandle /*filter*/) override {}

  std::size_t draws{};

 private:
  struct Geometry {
    std::vector<gfx::wgpu::ui::Vertex> vertices;
    std::vector<std::uint32_t> indices;
    bool live = false;
  };

  // Draws compiled geometry (a clip mask's geometry draws into the stencil with no texture).
  void draw_geometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle texture,
                     gfx::wgpu::ui::Clip clip) {
    if (!handle || handle > geometries_.size() || !geometries_[handle - 1].live) {
      return;
    }
    const auto& geometry = geometries_[handle - 1];
    gfx::wgpu::ui::Draw draw;
    draw.geometry.vertices = geometry.vertices.data();
    draw.geometry.vertex_count = geometry.vertices.size();
    draw.geometry.indices = geometry.indices.data();
    draw.geometry.index_count = geometry.indices.size();
    draw.texture = static_cast<std::uint32_t>(texture);
    draw.textured = texture != 0;
    draw.clip = clip;
    draw.stencil_reference = stencil_reference_;
    projection(draw.transform);
    draw.translate[0] = translation.x;
    draw.translate[1] = translation.y;
    draw.scissor = scissor_;
    for (std::size_t i = 0; i < 4; ++i) {
      draw.region[i] = region_[i];
    }
    ++draws;
    gfx::wgpu::ui::draw(draw);
  }

  // Upload of premultiplied RGBA at the given size; 0 when it cannot be made.
  Rml::TextureHandle upload(const Rml::byte* pixels, std::size_t size, Rml::Vector2i dimensions, bool linear) {
    if (!pixels || dimensions.x <= 0 || dimensions.y <= 0 ||
        size != static_cast<std::size_t>(dimensions.x) * static_cast<std::size_t>(dimensions.y) * 4) {
      Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to generate texture.");
      return {};
    }
    const auto texture = gfx::wgpu::ui::create_texture(dimensions.x, dimensions.y, linear);
    if (!texture) {
      Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to generate texture.");
      return {};
    }
    gfx::wgpu::ui::upload_texture(texture, 0, 0, dimensions.x, dimensions.y,
                                  reinterpret_cast<const std::byte*>(pixels));
    return texture;
  }

 public:
  // Nearest sampling at the image's own size: HUD art and items.
  Rml::TextureHandle nearest_texture(const Rml::byte* pixels, std::size_t size, Rml::Vector2i dimensions) {
    return upload(pixels, size, dimensions, false);
  }

  // A new texture (the host creates it, then writes it).
  [[nodiscard]] Rml::TextureHandle create_texture(int width, int height) {
    return gfx::wgpu::ui::create_texture(width, height, false);
  }

  // A write to an existing texture: the copy is recorded in frame order.
  void write_texture(Rml::TextureHandle texture, int x, int y, int width, int height, const Rml::byte* rgba) {
    gfx::wgpu::ui::upload_texture(static_cast<std::uint32_t>(texture), x, y, width, height,
                                  reinterpret_cast<const std::byte*>(rgba));
  }

 private:
  // RmlUi's projection: window pixels (top-left origin) to clip space, times RmlUi's transform. Column-major. The
  // clip formulas are the Vulkan ones (y = 1 - 2 y / height): WebGPU's viewport puts row 0 at the top, as Vulkan's
  // negative viewport did.
  void projection(float* out) const {
    const float w = static_cast<float>(width_);
    const float h = static_cast<float>(height_);
    const float p[4][4] = {{2 / w, 0, 0, -1}, {0, -2 / h, 0, 1}, {0, 0, 0, 0.5f}, {0, 0, 0, 1}};
    float t[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    if (has_transform_) {
      const float* data = transform_.data();
      for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
          t[r][c] = data[c * 4 + r];
        }
      }
    }
    for (int c = 0; c < 4; ++c) {
      for (int r = 0; r < 4; ++r) {
        float sum = 0;
        for (int k = 0; k < 4; ++k) {
          sum += p[r][k] * t[k][c];
        }
        out[c * 4 + r] = sum;
      }
    }
  }

  void log_once(const char* message) {
    if (logged_.insert(message).second) {
      Rml::Log::Message(Rml::Log::LT_WARNING, "RmlUi WebGPU: %s.", message);
    }
  }

  const HudArt* art_;
  int width_ = 0;
  int height_ = 0;
  std::vector<Geometry> geometries_;
  bool scissor_ = false;
  std::array<int, 4> region_{};
  bool clip_ = false;
  std::uint32_t stencil_reference_ = 0;
  std::vector<gfx::wgpu::ui::Filter> filters_;  // compiled filters: handle k is element k - 1
  bool has_transform_ = false;
  Rml::Matrix4f transform_;
  std::unordered_set<std::string> logged_;
  bool in_frame_ = false;
  std::vector<std::uint32_t> deferred_textures_;
};

class WgpuRmlGpu final : public RmlGpu {
 public:
  explicit WgpuRmlGpu(const HudArt* art) : render_(art) {}

  [[nodiscard]] Rml::RenderInterface& interface() override { return render_; }
  void initialise() override {}
  void shutdown() noexcept override {}
  [[nodiscard]] int max_texture_size() const override { return gfx::wgpu::ui::max_texture_size(); }

  [[nodiscard]] Rml::TextureHandle create_texture(int width, int height) override {
    return render_.create_texture(width, height);
  }

  void write_texture(Rml::TextureHandle texture, int x, int y, int width, int height,
                     const Rml::byte* rgba) override {
    render_.write_texture(texture, x, y, width, height, rgba);
  }

  [[nodiscard]] Rml::TextureHandle nearest_texture(const Rml::byte* rgba, std::size_t size,
                                                   Rml::Vector2i dimensions) override {
    return render_.nearest_texture(rgba, size, dimensions);
  }

  void content_changed() override {}  // no frame cache to invalidate

  void begin_frame(int drawable_width, int drawable_height) override {
    render_.begin_frame(drawable_width, drawable_height);
  }

  void end_frame() override { render_.end_frame(); }

  [[nodiscard]] std::size_t draws() const override { return render_.draws; }
  [[nodiscard]] bool frame_reused() const override { return false; }  // no frame cache
  [[nodiscard]] bool begin_timer() override { return false; }         // no GPU timers
  void end_timer() override {}
  [[nodiscard]] std::optional<double> poll_timers_ms() override { return std::nullopt; }
  void reset_bindings() noexcept override {}

 private:
  WgpuRenderInterface render_;
};
}  // namespace

std::unique_ptr<RmlGpu> make_webgpu_rml_gpu(const HudArt* art) {
  return std::make_unique<WgpuRmlGpu>(art);
}
}  // namespace realm::ui
