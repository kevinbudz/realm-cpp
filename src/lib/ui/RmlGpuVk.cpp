// RmlUi on Vulkan (phase 3, milestone 1): geometry, scissor and textures. Compiled geometry is kept on
// the host and copied into the frame's ring when drawn. Clip masks, layers, filters and shaders
// (gradients) are not implemented yet: each logs once and draws without its effect.
#include "RmlGpu.hpp"
#include "realm/stage3D/vk/VkUi.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/FileInterface.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace realm::ui {
namespace {
static_assert(sizeof(Rml::Vertex) == sizeof(gfx::vk::ui::Vertex));

// The Targa header RmlUi's GL3 backend reads (18 bytes, packed).
#pragma pack(push, 1)
struct TgaHeader {
  char idLength;
  char colorMapType;
  char dataType;
  short colorMapOrigin;
  short colorMapLength;
  char colorMapDepth;
  short xOrigin;
  short yOrigin;
  short width;
  short height;
  char bitsPerPixel;
  char imageDescriptor;
};
#pragma pack(pop)

struct Image {
  std::vector<Rml::byte> pixels;  // premultiplied RGBA
  Rml::Vector2i dimensions{};
};

// Decodes an uncompressed 24/32-bit Targa to premultiplied RGBA, as RenderInterface_GL3::LoadTexture.
std::optional<Image> load_targa(const Rml::String& source) {
  auto* files = Rml::GetFileInterface();
  const auto file = files->Open(source);
  if (!file) {
    return std::nullopt;
  }
  files->Seek(file, 0, SEEK_END);
  const auto size = files->Tell(file);
  files->Seek(file, 0, SEEK_SET);
  if (size <= sizeof(TgaHeader)) {
    files->Close(file);
    Rml::Log::Message(Rml::Log::LT_ERROR, "Texture file '%s' is too small to be a TGA image.", source.c_str());
    return std::nullopt;
  }
  std::vector<Rml::byte> buffer(size);
  files->Read(buffer.data(), size, file);
  files->Close(file);

  TgaHeader header{};
  std::memcpy(&header, buffer.data(), sizeof header);
  const int color_mode = header.bitsPerPixel / 8;
  if (header.dataType != 2 || color_mode < 3 || header.width <= 0 || header.height <= 0) {
    Rml::Log::Message(Rml::Log::LT_ERROR, "Only 24/32bit uncompressed TGAs are supported ('%s').", source.c_str());
    return std::nullopt;
  }
  const auto width = static_cast<std::size_t>(header.width);
  const auto height = static_cast<std::size_t>(header.height);
  if (buffer.size() < sizeof header + width * height * static_cast<std::size_t>(color_mode)) {
    return std::nullopt;
  }
  const bool top_to_bottom = (header.imageDescriptor & 32) != 0;
  const Rml::byte* source_pixels = buffer.data() + sizeof header;
  Image image;
  image.dimensions = {header.width, header.height};
  image.pixels.assign(width * height * 4, 0);
  for (std::size_t y = 0; y < height; ++y) {
    const std::size_t target_row = top_to_bottom ? y : height - 1 - y;
    for (std::size_t x = 0; x < width; ++x) {
      const Rml::byte* in = source_pixels + (y * width + x) * static_cast<std::size_t>(color_mode);
      Rml::byte* out = image.pixels.data() + (target_row * width + x) * 4;
      const Rml::byte alpha = color_mode == 4 ? in[3] : 255;
      // Targa stores BGR(A): swap to RGB and premultiply.
      out[0] = static_cast<Rml::byte>((in[2] * alpha) / 255);
      out[1] = static_cast<Rml::byte>((in[1] * alpha) / 255);
      out[2] = static_cast<Rml::byte>((in[0] * alpha) / 255);
      out[3] = alpha;
    }
  }
  return image;
}

// RmlUi's render interface on the Vulkan UI pass. HUD art ("item:" and "art:" sources) is nearest
// sampled, as on GL.
class VkRenderInterface final : public Rml::RenderInterface {
 public:
  explicit VkRenderInterface(const HudArt* art) : art_(art) {}

  void set_viewport(int width, int height) {
    width_ = width;
    height_ = height;
    draws = 0;
  }

  std::size_t draws{};

  Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                              Rml::Span<const int> indices) override {
    Geometry geometry;
    geometry.vertices.resize(vertices.size());
    std::memcpy(geometry.vertices.data(), vertices.data(), vertices.size() * sizeof(Rml::Vertex));
    geometry.indices.reserve(indices.size());
    for (const int index : indices) {
      geometry.indices.push_back(static_cast<std::uint32_t>(index));
    }
    for (std::size_t i = 0; i < geometries_.size(); ++i) {
      if (!geometries_[i].live) {
        geometries_[i] = std::move(geometry);
        geometries_[i].live = true;
        return i + 1;
      }
    }
    geometry.live = true;
    geometries_.push_back(std::move(geometry));
    return geometries_.size();
  }

  void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation,
                      Rml::TextureHandle texture) override {
    if (!handle || handle > geometries_.size() || !geometries_[handle - 1].live) {
      return;
    }
    const auto& geometry = geometries_[handle - 1];
    gfx::vk::ui::Draw draw;
    draw.geometry.vertices = geometry.vertices.data();
    draw.geometry.vertex_count = geometry.vertices.size();
    draw.geometry.indices = geometry.indices.data();
    draw.geometry.index_count = geometry.indices.size();
    draw.texture = static_cast<std::uint32_t>(texture);
    draw.textured = texture != 0;
    draw.translate[0] = translation.x;
    draw.translate[1] = translation.y;
    projection(draw.transform);
    draw.scissor = scissor_;
    for (std::size_t i = 0; i < 4; ++i) {
      draw.region[i] = region_[i];
    }
    ++draws;
    gfx::vk::ui::draw(draw);
  }

  void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override {
    if (handle && handle <= geometries_.size()) {
      geometries_[handle - 1] = Geometry{};
    }
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
    gfx::vk::ui::release_texture(static_cast<std::uint32_t>(texture));
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

  void EnableClipMask(bool enable) override {
    if (enable) {
      log_once("clip masks (border-radius clipping) are not implemented on Vulkan yet");
    }
  }

  Rml::LayerHandle PushLayer() override {
    log_once("layers (filters, backdrop) are not implemented on Vulkan yet");
    return {};
  }

  Rml::CompiledShaderHandle CompileShader(const Rml::String& /*name*/, const Rml::Dictionary& /*parameters*/) override {
    log_once("shaders (gradients) are not implemented on Vulkan yet");
    return {};
  }

  Rml::CompiledFilterHandle CompileFilter(const Rml::String& /*name*/, const Rml::Dictionary& /*parameters*/) override {
    log_once("filters are not implemented on Vulkan yet");
    return {};
  }

  // Nearest sampling at the image's own size: HUD art and items.
  Rml::TextureHandle nearest_texture(const Rml::byte* pixels, std::size_t size, Rml::Vector2i dimensions) {
    return upload(pixels, size, dimensions, false);
  }

 private:
  struct Geometry {
    std::vector<gfx::vk::ui::Vertex> vertices;
    std::vector<std::uint32_t> indices;
    bool live = false;
  };

  // Upload of premultiplied RGBA at the given size; 0 when it cannot be made.
  Rml::TextureHandle upload(const Rml::byte* pixels, std::size_t size, Rml::Vector2i dimensions, bool linear) {
    if (!pixels || dimensions.x <= 0 || dimensions.y <= 0 ||
        size != static_cast<std::size_t>(dimensions.x) * static_cast<std::size_t>(dimensions.y) * 4) {
      Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to generate texture.");
      return {};
    }
    const auto texture = gfx::vk::ui::create_texture(dimensions.x, dimensions.y, linear);
    if (!texture) {
      Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to generate texture.");
      return {};
    }
    gfx::vk::ui::upload_texture(texture, 0, 0, dimensions.x, dimensions.y,
                                reinterpret_cast<const std::byte*>(pixels));
    return texture;
  }

  // RmlUi's projection: window pixels (top-left origin) to Vulkan clip space under the negative window
  // viewport, times RmlUi's transform. Column-major.
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
      Rml::Log::Message(Rml::Log::LT_WARNING, "RmlUi Vulkan: %s.", message);
    }
  }

  const HudArt* art_;
  int width_ = 0;
  int height_ = 0;
  std::vector<Geometry> geometries_;
  bool scissor_ = false;
  std::array<int, 4> region_{};
  bool has_transform_ = false;
  Rml::Matrix4f transform_;
  std::unordered_set<std::string> logged_;
};

class VkRmlGpu final : public RmlGpu {
 public:
  explicit VkRmlGpu(const HudArt* art) : render_(art) {}

  [[nodiscard]] Rml::RenderInterface& interface() override { return render_; }
  void initialise() override {}
  void shutdown() noexcept override {}
  [[nodiscard]] int max_texture_size() const override { return gfx::vk::ui::max_texture_size(); }

  [[nodiscard]] Rml::TextureHandle create_texture(int width, int height) override {
    return gfx::vk::ui::create_texture(width, height, false);
  }

  void write_texture(Rml::TextureHandle texture, int x, int y, int width, int height,
                     const Rml::byte* rgba) override {
    gfx::vk::ui::upload_texture(static_cast<std::uint32_t>(texture), x, y, width, height,
                                reinterpret_cast<const std::byte*>(rgba));
  }

  [[nodiscard]] Rml::TextureHandle nearest_texture(const Rml::byte* rgba, std::size_t size,
                                                   Rml::Vector2i dimensions) override {
    return render_.nearest_texture(rgba, size, dimensions);
  }

  void content_changed() override {}

  void begin_frame(int drawable_width, int drawable_height) override {
    render_.set_viewport(drawable_width, drawable_height);
    gfx::vk::ui::begin_frame(drawable_width, drawable_height);
  }

  void end_frame() override { gfx::vk::ui::end_frame(); }

  [[nodiscard]] std::size_t draws() const override { return render_.draws; }
  [[nodiscard]] bool frame_reused() const override { return false; }
  [[nodiscard]] bool begin_timer() override { return false; }  // no GPU timers yet
  void end_timer() override {}
  [[nodiscard]] std::optional<double> poll_timers_ms() override { return std::nullopt; }
  void reset_bindings() noexcept override {}

 private:
  VkRenderInterface render_;
};
}  // namespace

std::unique_ptr<RmlGpu> make_vulkan_rml_gpu(const HudArt* art) {
  return std::make_unique<VkRmlGpu>(art);
}
}  // namespace realm::ui
