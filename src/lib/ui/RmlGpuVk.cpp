// RmlUi on Vulkan (phase 3): geometry, scissor, textures, clip masks, layers and blur. Compiled geometry is kept on
// the host and copied into the frame's ring when drawn. Shaders (gradients) are not implemented yet: they log once
// and draw without their effect.
//
// Frame cache (milestone M4, as RmlUi_Renderer_GL3). Between BeginFrame and EndFrame the draws are queued, not
// recorded. A frame whose draws were all queued is replayed from the renderer's recording of the same draw list,
// or recorded once. A clip mask, a layer or a composite flushes the queue and makes the frame ineligible (its draws
// then run at once), and so does a write to a texture that queued draws may sample (they must see the old pixels).
// "generation" moves on every compile, release, upload, texture creation and viewport change: the queue is only
// replayed when the draw list and the generation match the recording's. Releases during a recorded frame wait for
// EndFrame, because queued draws may still name the object. REALM_VK_UI_CACHE=0 runs every draw at once (the
// pre-M4 path, for comparison).
#include "RmlGpu.hpp"
#include "realm/stage3D/vk/VkUi.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/FileInterface.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
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

// REALM_VK_UI_CACHE=0 runs every draw at once (the pre-M4 path).
bool ui_cache_enabled() {
  const char* value = std::getenv("REALM_VK_UI_CACHE");
  return !(value && std::string_view(value) == "0");
}

// RmlUi's render interface on the Vulkan UI pass. HUD art ("item:" and "art:" sources) is nearest
// sampled, as on GL.
class VkRenderInterface final : public Rml::RenderInterface {
  // A draw as queued: the geometry by handle (it stays alive until EndFrame), with the state the draw takes.
  struct Command {
    Rml::CompiledGeometryHandle geometry = 0;
    std::uint32_t texture = 0;
    bool textured = false;
    gfx::vk::ui::Clip clip = gfx::vk::ui::Clip::none;
    std::uint32_t stencil_reference = 0;
    float transform[16] = {};
    float translate[2] = {0, 0};
    bool scissor = false;
    int region[4] = {0, 0, 0, 0};

    bool operator==(const Command& other) const {
      return geometry == other.geometry && texture == other.texture && textured == other.textured &&
             clip == other.clip && stencil_reference == other.stencil_reference &&
             std::equal(transform, transform + 16, other.transform) &&
             std::equal(translate, translate + 2, other.translate) && scissor == other.scissor &&
             std::equal(region, region + 4, other.region);
    }
  };

 public:
  explicit VkRenderInterface(const HudArt* art) : art_(art), cache_enabled_(ui_cache_enabled()) {}

  // BeginFrame: the window size, and the queue opens (when the cache is on).
  void begin_frame(int width, int height) {
    if (width != width_ || height != height_) {
      ++generation_;
    }
    width_ = width;
    height_ = height;
    draws = 0;
    recording_ = cache_enabled_;
    ineligible_ = false;
    frame_reused_ = false;
    queue_.clear();
    gfx::vk::ui::begin_frame(width, height);
  }

  // EndFrame: replays or records the frame's queued draws, closes the UI pass, then performs the releases that
  // waited for the frame.
  void end_frame() {
    if (recording_) {
      if (!queue_.empty()) {
        submit_queue();
      }
      recording_ = false;
      queue_.clear();
    }
    gfx::vk::ui::end_frame();
    for (const auto handle : deferred_geometry_) {
      geometries_[handle - 1] = Geometry{};
    }
    deferred_geometry_.clear();
    for (const auto texture : deferred_textures_) {
      gfx::vk::ui::release_texture(texture);
    }
    deferred_textures_.clear();
  }

  [[nodiscard]] bool frame_reused() const { return frame_reused_; }

  // Resource state the recorded draws depend on changed: the next frame that draws them is recorded again.
  void content_changed() { ++generation_; }

  Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                              Rml::Span<const int> indices) override {
    ++generation_;
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
    // While a clip mask is enabled, the geometry draws only where the stencil equals its reference.
    draw_geometry(handle, translation, texture, clip_ ? gfx::vk::ui::Clip::test : gfx::vk::ui::Clip::none);
  }

  // As RenderInterface_GL3: Set and SetInverse clear the stencil and write 1 (or 0) inside the
  // geometry; Intersect increments it. The reference after each operation is the value the following
  // draws must match.
  void RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry,
                        Rml::Vector2f translation) override {
    end_recording();
    gfx::vk::ui::Clip clip = gfx::vk::ui::Clip::mask_set;
    switch (operation) {
      case Rml::ClipMaskOperation::Set:
        clip = gfx::vk::ui::Clip::mask_set;
        stencil_reference_ = 1;
        break;
      case Rml::ClipMaskOperation::SetInverse:
        clip = gfx::vk::ui::Clip::mask_set_inverse;
        stencil_reference_ = 1;
        break;
      case Rml::ClipMaskOperation::Intersect:
        clip = gfx::vk::ui::Clip::mask_intersect;
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
    ++generation_;
    if (recording_) {
      geometries_[handle - 1].released = true;  // queued draws may still name it
      deferred_geometry_.push_back(handle);
      return;
    }
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
    ++generation_;
    if (recording_) {
      deferred_textures_.push_back(static_cast<std::uint32_t>(texture));  // queued draws may still sample it
      return;
    }
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

  // RmlUi's layers: a new layer is cleared and becomes the target of the draws that follow.
  Rml::LayerHandle PushLayer() override {
    end_recording();
    const auto layer = gfx::vk::ui::push_layer();
    if (!layer) {
      log_once("no UI layer could be made on Vulkan");
    }
    return layer;
  }

  void PopLayer() override {
    end_recording();
    gfx::vk::ui::pop_layer();
  }

  // Composites a layer through its filters (blur, opacity) into the destination, as RenderInterface_GL3 does.
  void CompositeLayers(Rml::LayerHandle source, Rml::LayerHandle destination, Rml::BlendMode blend_mode,
                       Rml::Span<const Rml::CompiledFilterHandle> filter_handles) override {
    end_recording();
    std::vector<gfx::vk::ui::Filter> filters;
    for (const auto handle : filter_handles) {
      if (handle && handle <= filters_.size()) {
        filters.push_back(filters_[handle - 1]);
      }
    }
    gfx::vk::ui::Composite composite;
    composite.source = static_cast<std::uint32_t>(source);
    composite.destination = static_cast<std::uint32_t>(destination);
    composite.replace = blend_mode == Rml::BlendMode::Replace;
    composite.scissor = scissor_;
    for (std::size_t i = 0; i < 4; ++i) {
      composite.region[i] = region_[i];
    }
    composite.filters = filters.data();
    composite.filter_count = filters.size();
    gfx::vk::ui::composite(composite);
  }

  Rml::CompiledShaderHandle CompileShader(const Rml::String& /*name*/, const Rml::Dictionary& /*parameters*/) override {
    log_once("shaders (gradients) are not implemented on Vulkan yet");
    return {};
  }

  Rml::CompiledFilterHandle CompileFilter(const Rml::String& name, const Rml::Dictionary& parameters) override {
    if (name != "blur" && name != "opacity") {
      log_once("this filter is not implemented on Vulkan yet (only blur and opacity)");
      return {};
    }
    gfx::vk::ui::Filter filter;
    if (name == "blur") {
      filter.sigma = Rml::Get(parameters, "sigma", 1.0f);
    } else {
      filter.opacity = Rml::Get(parameters, "value", 1.0f);
    }
    filters_.push_back(filter);
    return filters_.size();
  }

  void ReleaseFilter(Rml::CompiledFilterHandle /*filter*/) override {}

  // Draws compiled geometry (a clip mask's geometry draws into the stencil with no texture). Queued while the
  // frame is recorded, otherwise drawn now.
  void draw_geometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle texture,
                     gfx::vk::ui::Clip clip) {
    if (!handle || handle > geometries_.size() || !geometries_[handle - 1].live ||
        geometries_[handle - 1].released) {
      return;
    }
    Command command;
    command.geometry = handle;
    command.texture = static_cast<std::uint32_t>(texture);
    command.textured = texture != 0;
    command.clip = clip;
    command.stencil_reference = stencil_reference_;
    command.translate[0] = translation.x;
    command.translate[1] = translation.y;
    projection(command.transform);
    command.scissor = scissor_;
    for (std::size_t i = 0; i < 4; ++i) {
      command.region[i] = region_[i];
    }
    ++draws;
    if (recording_ && !ineligible_) {
      queue_.push_back(command);
      return;
    }
    gfx::vk::ui::draw(to_draw(command));
  }

  // Nearest sampling at the image's own size: HUD art and items.
  Rml::TextureHandle nearest_texture(const Rml::byte* pixels, std::size_t size, Rml::Vector2i dimensions) {
    return upload(pixels, size, dimensions, false);
  }

  // A new texture (the host creates it, then writes it).
  [[nodiscard]] Rml::TextureHandle create_texture(int width, int height) {
    ++generation_;
    return gfx::vk::ui::create_texture(width, height, false);
  }

  // A write to an existing texture. Queued draws run first: they must see the old pixels, as they would have
  // when recorded at once.
  void write_texture(Rml::TextureHandle texture, int x, int y, int width, int height, const Rml::byte* rgba) {
    // Without queued draws there is nothing to run first: the write's copy is recorded before any later pass.
    if (!queue_.empty()) {
      end_recording();
    }
    ++generation_;
    gfx::vk::ui::upload_texture(static_cast<std::uint32_t>(texture), x, y, width, height,
                                reinterpret_cast<const std::byte*>(rgba));
  }

  std::size_t draws{};

 private:
  struct Geometry {
    std::vector<gfx::vk::ui::Vertex> vertices;
    std::vector<std::uint32_t> indices;
    bool live = false;
    bool released = false;  // released during the frame: freed at EndFrame
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
    ++generation_;
    gfx::vk::ui::upload_texture(texture, 0, 0, dimensions.x, dimensions.y,
                                reinterpret_cast<const std::byte*>(pixels));
    return texture;
  }

  // The queued draw as the renderer records it. Its geometry is alive until EndFrame.
  [[nodiscard]] gfx::vk::ui::Draw to_draw(const Command& command) const {
    const auto& geometry = geometries_[command.geometry - 1];
    gfx::vk::ui::Draw draw;
    draw.geometry.vertices = geometry.vertices.data();
    draw.geometry.vertex_count = geometry.vertices.size();
    draw.geometry.indices = geometry.indices.data();
    draw.geometry.index_count = geometry.indices.size();
    draw.texture = command.texture;
    draw.textured = command.textured;
    draw.clip = command.clip;
    draw.stencil_reference = command.stencil_reference;
    std::memcpy(draw.transform, command.transform, sizeof draw.transform);
    draw.translate[0] = command.translate[0];
    draw.translate[1] = command.translate[1];
    draw.scissor = command.scissor;
    std::copy_n(command.region, 4, draw.region);
    return draw;
  }

  // Runs the queued draws now, in order, and makes the frame ineligible for reuse (RenderInterface_GL3's
  // EndRecording). Clip masks, layers and composites come through here: they change state the queue cannot
  // express.
  void end_recording() {
    if (!recording_) {
      return;
    }
    ineligible_ = true;
    for (const auto& command : queue_) {
      gfx::vk::ui::draw(to_draw(command));
    }
    queue_.clear();
  }

  // EndFrame of a frame with queued draws. The serial names the draw list: it moves when the list or the
  // generation differs from the last one recorded. The renderer replays its recording for the serial when that
  // recording is current for this frame slot; otherwise the list is recorded for the slot.
  void submit_queue() {
    if (!cache_valid_ || generation_ != cached_generation_ || queue_ != cached_queue_) {
      cached_queue_ = queue_;
      cached_generation_ = generation_;
      cache_valid_ = true;
      ++serial_;
    }
    frame_reused_ = gfx::vk::ui::replay_cached(serial_);
    if (!frame_reused_) {
      std::vector<gfx::vk::ui::Draw> draws_to_record;
      draws_to_record.reserve(queue_.size());
      for (const auto& command : queue_) {
        draws_to_record.push_back(to_draw(command));
      }
      gfx::vk::ui::record_bundle(draws_to_record.data(), draws_to_record.size(), serial_);
    }
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
  bool cache_enabled_;
  int width_ = 0;
  int height_ = 0;
  std::vector<Geometry> geometries_;
  bool scissor_ = false;
  std::array<int, 4> region_{};
  bool clip_ = false;
  std::uint32_t stencil_reference_ = 0;
  std::vector<gfx::vk::ui::Filter> filters_;  // compiled filters: handle k is element k - 1
  bool has_transform_ = false;
  Rml::Matrix4f transform_;
  std::unordered_set<std::string> logged_;

  // The frame cache (see the file comment).
  bool recording_ = false;   // between BeginFrame and EndFrame, with the cache on
  bool ineligible_ = false;  // this frame has run draws at once: no reuse
  bool frame_reused_ = false;
  std::vector<Command> queue_;
  std::vector<Rml::CompiledGeometryHandle> deferred_geometry_;
  std::vector<std::uint32_t> deferred_textures_;
  std::uint64_t generation_ = 0;  // resource state: moves on compile, release, upload and viewport changes
  std::vector<Command> cached_queue_;  // the draw list the renderer's recordings were made from
  std::uint64_t cached_generation_ = 0;
  bool cache_valid_ = false;
  std::uint64_t serial_ = 0;  // names cached_queue_ and generation_ (bumped when either changes)
};

class VkRmlGpu final : public RmlGpu {
 public:
  explicit VkRmlGpu(const HudArt* art) : render_(art) {}

  [[nodiscard]] Rml::RenderInterface& interface() override { return render_; }
  void initialise() override {}
  void shutdown() noexcept override {}
  [[nodiscard]] int max_texture_size() const override { return gfx::vk::ui::max_texture_size(); }

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

  void content_changed() override { render_.content_changed(); }

  void begin_frame(int drawable_width, int drawable_height) override {
    render_.begin_frame(drawable_width, drawable_height);
  }

  void end_frame() override { render_.end_frame(); }

  [[nodiscard]] std::size_t draws() const override { return render_.draws; }
  [[nodiscard]] bool frame_reused() const override { return render_.frame_reused(); }
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
