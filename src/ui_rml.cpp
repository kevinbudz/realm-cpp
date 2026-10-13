#include "realm/ui_rml.hpp"
#include "realm/telemetry.hpp"
#include "realm/ui_draw.hpp"
#include "realm/utf8.hpp"
#include <GL/glew.h>
#include <RmlUi/Core.h>
#include <RmlUi/Core/Decorator.h>
#include <RmlUi/Core/FontEffect.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/MeshUtilities.h>
#include <RmlUi/Core/RenderManager.h>
#include "RmlUi_Platform_SDL.h"
#include "RmlUi_Renderer_GL3.h"
#include <SDL.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <list>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_map>

// The RmlUi host. RmlUi is a global library, so everything its interfaces share lives in one Runtime that the single
// RmlHost owns; the engine classes below are general-purpose.
namespace realm::ui {
namespace {
constexpr std::uint32_t white = 0xffffff;
constexpr std::size_t cache_entry_limit = 512, cache_byte_limit = 16 * 1024 * 1024;
constexpr double text_padding = 8, text_gutter = 2;  // FontBook shadow padding and TextField gutter, logical px
constexpr int smallest_size = 8,
              largest_size = 48;  // FontBook has no list of baked sizes; every size in range is probed

std::string num(double value) {
  char buffer[40];
  std::snprintf(buffer, sizeof buffer, "%.3f", value);
  std::string text = buffer;
  while (text.back() == '0') text.pop_back();
  if (text.back() == '.') text.pop_back();
  return text;
}
// FontBook returns straight alpha; RmlUi textures are premultiplied.
std::vector<Rml::byte> premultiplied(const Image& image) {
  std::vector<Rml::byte> out(image.pixels);
  for (std::size_t i = 0; i < out.size(); i += 4) {
    const unsigned alpha = out[i + 3];
    if (alpha == 255) continue;
    for (unsigned c = 0; c < 3; ++c) out[i + c] = static_cast<Rml::byte>((out[i + c] * alpha + 127) / 255);
  }
  return out;
}

// State shared by the engine interfaces, the custom element and the decorator.
struct NativeTexture;
struct Runtime {
  const FontBook* fonts{};
  const HudArt* art{};
  class HostRender* render{};
  class PainterCache* painters{};
  RmlStats stats;
  double clock_ms{}, dp{1};
  std::size_t pulse_bytes{};
  std::size_t native_image_bytes{};
  // Keep source textures through a composition transaction: reordering children must not evict an image before
  // its new child acquires it. Entries held only by this map are released after Context::Update.
  std::unordered_map<const Image*, std::shared_ptr<NativeTexture>> native_images;
};
Runtime* runtime = nullptr;
void uploaded(std::size_t bytes) {
  ++runtime->stats.texture_uploads;
  runtime->stats.texture_upload_bytes += bytes;
  telemetry::add(telemetry::Counter::texture_upload_bytes, bytes);
}

class HostSystem final : public SystemInterface_SDL {
 public:
  using SystemInterface_SDL::SystemInterface_SDL;
  double GetElapsedTime() override { return runtime->clock_ms / 1000.0; }
  bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
    if (type == Rml::Log::LT_ERROR || type == Rml::Log::LT_ASSERT) {
      ++runtime->stats.log_errors;
      std::cerr << "[rmlui error] " << message << '\n';
    } else if (type == Rml::Log::LT_WARNING) {
      ++runtime->stats.log_warnings;
      std::cerr << "[rmlui warning] " << message << '\n';
    }
    return true;
  }
};

// RmlUi's GL3 renderer with Flash pixel art: item:TYPE and art:KEY sources come from HudArt (premultiplied RGBA) and
// are sampled nearest so icons stay crisp.
class HostRender final : public RenderInterface_GL3 {
 public:
  explicit HostRender(const HudArt* art) : art_(art) {}
  void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                      Rml::TextureHandle texture) override {
    ++draws;
    RenderInterface_GL3::RenderGeometry(geometry, translation, texture);
  }
  std::size_t draws{};
  Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override {
    std::string key;
    if (source.rfind("item:", 0) == 0)
      key = "item" + source.substr(5);
    else if (source.rfind("art:", 0) == 0)
      key = source.substr(4);
    else
      return RenderInterface_GL3::LoadTexture(dimensions, source);
    if (!art_ || !art_->contains(key)) {
      Rml::Log::Message(Rml::Log::LT_ERROR, "Missing HUD art '%s'.", key.c_str());
      return {};
    }
    const auto& image = art_->image(key);
    dimensions = {image.width, image.height};
    return nearest_texture(image.pixels.data(), image.pixels.size(), dimensions);
  }
  // Premultiplied RGBA at the texture's own resolution: nothing resamples it.
  Rml::TextureHandle nearest_texture(const Rml::byte* pixels, std::size_t size, Rml::Vector2i dimensions) {
    const auto handle = GenerateTexture(Rml::Span<const Rml::byte>(pixels, size), dimensions);
    if (handle) filter(handle, GL_NEAREST);
    return handle;
  }
  static void filter(Rml::TextureHandle handle, GLint mode) {
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(handle));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
  }
  // A texture of fixed capacity that is refilled in place (the pulsing title's one texture, as native's
  // SurfaceTexture).
  static Rml::TextureHandle empty_texture(int width, int height) {
    GLuint id{};
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    filter(id, GL_NEAREST);
    return id;
  }

 private:
  const HudArt* art_;
};

// The font engine over the shared FontBook. A face is one {pixel size, bold, density} triple; logical sizes are the
// baked FontBook sizes (px size / dp ratio) and every metric is scaled by the density, so RCSS in dp lays text out with
// the same logical advances as the native backend.
class HostFonts final : public Rml::FontEngineInterface {
 public:
  void set_density(double value) {
    if (value != density_) {
      density_ = value;
      ++version_;
    }
  }
  void set_sizes(std::vector<int> sizes) { sizes_ = std::move(sizes); }
  Rml::FontFaceHandle GetFontFaceHandle(const Rml::String&, Rml::Style::FontStyle, Rml::Style::FontWeight weight,
                                        int size) override {
    const bool bold = static_cast<int>(weight) >= 700;
    // RmlUi hands over the dp-scaled size truncated to whole px, which is ambiguous below a 1:1 ratio. Among the baked
    // sizes whose product (RmlUi's own float arithmetic) truncates to it, the least truncated one is the exact one.
    int logical = 0;
    double best = 2;
    for (const int candidate : sizes_) {
      const float product = static_cast<float>(candidate) * static_cast<float>(density_);
      if (static_cast<int>(product) != size) continue;
      if (const double fraction = product - std::floor(product); fraction < best) {
        best = fraction;
        logical = candidate;
      }
    }
    if (!logical) logical = std::max(1, static_cast<int>(std::ceil(size / density_ - 1e-4)));
    const auto key = std::make_tuple(size, bold, density_);
    if (const auto found = index_.find(key); found != index_.end()) return found->second;
    const auto& fonts = *runtime->fonts;
    Face face;
    try {
      const double baseline = fonts.baseline(logical, bold), height = fonts.layout("x", logical, bold).height;
      face.size = logical;
      face.bold = bold;
      face.density = density_;
      face.baseline = baseline;
      face.metrics.size = size;
      face.metrics.ascent = static_cast<float>(baseline * density_);
      face.metrics.descent = static_cast<float>((height - baseline) * density_);
      face.metrics.line_spacing = static_cast<float>(fonts.line_advance(logical, bold) * density_);
      face.metrics.x_height = face.metrics.ascent * .55F;
      face.metrics.underline_position = static_cast<float>(density_);
      face.metrics.underline_thickness = static_cast<float>(std::max(1.0, density_));
      face.metrics.has_ellipsis = false;
    } catch (const std::exception& error) {
      Rml::Log::Message(Rml::Log::LT_ERROR, "No baked font for %d px (logical %d): %s", size, logical, error.what());
      return 0;
    }
    faces_.push_back(std::make_unique<Face>(face));
    return index_[key] = faces_.size();
  }
  Rml::FontEffectsHandle PrepareFontEffects(Rml::FontFaceHandle, const Rml::FontEffectList& effects) override {
    // Only the TextField drop shadow is supported: a font-effect behind the text that has no texture of its own.
    for (const auto& effect : effects)
      if (effect->GetLayer() == Rml::FontEffect::Layer::Back && !effect->HasUniqueTexture()) return 1;
    return 0;
  }
  const Rml::FontMetrics& GetFontMetrics(Rml::FontFaceHandle handle) override { return face(handle).metrics; }
  int GetStringWidth(Rml::FontFaceHandle handle, Rml::StringView string, const Rml::TextShapingContext&,
                     Rml::Character = Rml::Character::Null) override {
    const auto& f = face(handle);
    return static_cast<int>(std::ceil(
        runtime->fonts->measure(std::string_view(string.begin(), string.size()), f.size, f.bold) * f.density));
  }
  int GenerateString(Rml::RenderManager& render_manager, Rml::FontFaceHandle handle, Rml::FontEffectsHandle effects,
                     Rml::StringView string, Rml::Vector2f position, Rml::ColourbPremultiplied colour, float,
                     const Rml::TextShapingContext& shaping, Rml::TexturedMeshList& meshes) override {
    const int width = GetStringWidth(handle, string, shaping);
    if (string.empty()) return width;
    ++runtime->stats.text_meshes;
    const auto& f = face(handle);
    const bool shadow = effects == 1;
    const double padding = shadow ? text_padding : 0, s = f.density;
    // The bitmap's glyph origin sits (padding+gutter, baseline+padding) logical px in; its whole-pixel origin is
    // floored and the remainder, quantized to a quarter pixel, becomes the glyph phase.
    const double bx = position.x - (padding + text_gutter) * s, by = position.y - (f.baseline + padding) * s;
    double ox = std::floor(bx), oy = std::floor(by), px = std::round((bx - ox) * 4) / 4,
           py = std::round((by - oy) * 4) / 4;
    if (px >= 1) {
      ox += 1;
      px = 0;
    }
    if (py >= 1) {
      oy += 1;
      py = 0;
    }
    const std::string text(string.begin(), string.size());
    const std::string key = text + '\0' + std::to_string(f.size) + (f.bold ? 'b' : 'r') + (shadow ? 's' : 'n') +
                            std::to_string(s) + '|' + num(px) + '|' + num(py);
    auto found = cache_.find(key);
    if (found == cache_.end()) {
      try {
        // At exactly 1x the baked Flash glyphs are used, as native does; otherwise outlines at the face's density.
        const bool baked = s == 1 && px == 0 && py == 0 && runtime->fonts->pixel_scale() == 1;
        const auto bitmap = baked
                                ? runtime->fonts->rasterize(text, f.size, f.bold, shadow, white)
                                : runtime->fonts->rasterize_at_scale(text, f.size, s, f.bold, shadow, white, {px, py});
        ++runtime->stats.text_rasterizations;
        auto data = std::make_shared<std::vector<Rml::byte>>(premultiplied(bitmap.image));
        const Rml::Vector2i dimensions{bitmap.image.width, bitmap.image.height};
        Entry entry;
        entry.width = dimensions.x;
        entry.height = dimensions.y;
        entry.bytes = data->size();
        entry.texture =
            render_manager.MakeCallbackTexture([data, dimensions](const Rml::CallbackTextureInterface& texture) {
              uploaded(data->size());
              return texture.GenerateTexture(Rml::Span<const Rml::byte>(data->data(), data->size()), dimensions);
            });
        lru_.push_front(key);
        entry.lru = lru_.begin();
        bytes_ += entry.bytes;
        found = cache_.emplace(key, std::move(entry)).first;
      } catch (const std::exception& error) {
        Rml::Log::Message(Rml::Log::LT_ERROR, "Cannot rasterize text: %s", error.what());
        return width;
      }
    } else
      lru_.splice(lru_.begin(), lru_, found->second.lru);
    Rml::TexturedMesh quad;
    Rml::MeshUtilities::GenerateQuad(
        quad.mesh, {static_cast<float>(ox), static_cast<float>(oy)},
        {static_cast<float>(found->second.width), static_cast<float>(found->second.height)}, colour);
    quad.texture = found->second.texture;
    meshes.push_back(std::move(quad));
    return width;
  }
  // Rml::Shutdown calls this while the render managers are alive: the callback textures must be released now.
  void Shutdown() override;
  int GetVersion(Rml::FontFaceHandle) override { return version_; }
  // Called between frames, never while RmlUi holds a mesh: an evicted texture bumps the version, so every text
  // element regenerates geometry (and so re-fetches or re-rasterizes) before it renders again.
  void trim() {
    bool evicted = false;
    while (!lru_.empty() && (cache_.size() > cache_entry_limit || bytes_ > cache_byte_limit)) {
      const auto found = cache_.find(lru_.back());
      bytes_ -= found->second.bytes;
      cache_.erase(found);
      lru_.pop_back();
      evicted = true;
    }
    if (evicted) ++version_;
  }
  // Everything is dropped; the version bump makes every text element regenerate before its next render.
  void release() {
    cache_.clear();
    lru_.clear();
    bytes_ = 0;
    ++version_;
  }
  void clear() {
    cache_.clear();
    lru_.clear();
    bytes_ = 0;
  }
  [[nodiscard]] std::size_t bytes() const { return bytes_; }
  [[nodiscard]] std::size_t entries() const { return cache_.size(); }

 private:
  struct Face {
    int size{};
    bool bold{};
    double density{1}, baseline{};
    Rml::FontMetrics metrics{};
  };
  struct Entry {
    Rml::CallbackTexture texture;
    int width{}, height{};
    std::size_t bytes{};
    std::list<std::string>::iterator lru;
  };
  [[nodiscard]] const Face& face(Rml::FontFaceHandle handle) const { return *faces_.at(handle - 1); }
  double density_{1};
  int version_{1};
  std::vector<int> sizes_;
  std::vector<std::unique_ptr<Face>> faces_;
  std::map<std::tuple<int, bool, double>, std::size_t> index_;
  std::unordered_map<std::string, Entry> cache_;
  std::list<std::string> lru_;
  std::size_t bytes_{};
};

// Painter output as retained textures, shared by every element that paints the same request. An element holds its
// texture while it uses it, so trimming only drops entries nobody references and a texture is never freed mid-frame.
struct PaintTexture {
  Rml::CallbackTexture texture;
  int width{}, height{};
  std::size_t bytes{};
  std::list<std::string>::iterator lru;
};
struct PaintData;
class PainterCache {
 public:
  void add(std::string name, Painter painter, bool premultiplied) {
    sources_[std::move(name)] = {std::move(painter), premultiplied};
  }
  [[nodiscard]] std::shared_ptr<PaintTexture> get(Rml::RenderManager& manager, const PaintRequest& request) {
    std::string key = request.name;
    for (const auto& argument : request.args) key += '\x1f' + argument;
    char sizes[96];
    std::snprintf(sizes, sizeof sizes, "|%.4f|%.4f|%.4f", request.width, request.height, request.scale);
    key += sizes;
    if (const auto found = cache_.find(key); found != cache_.end()) {
      lru_.splice(lru_.begin(), lru_, found->second->lru);
      return found->second;
    }
    const auto source = sources_.find(request.name);
    if (source == sources_.end()) {
      if (unknown_.insert(request.name).second)
        Rml::Log::Message(Rml::Log::LT_ERROR, "No painter named '%s' is registered.", request.name.c_str());
      return nullptr;
    }
    Image image;
    try {
      image = source->second.paint(request);
    } catch (const std::exception& error) {
      Rml::Log::Message(Rml::Log::LT_ERROR, "Painter '%s' failed: %s", request.name.c_str(), error.what());
      return nullptr;
    }
    if (image.width < 1 || image.height < 1 ||
        image.pixels.size() != static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4) {
      Rml::Log::Message(Rml::Log::LT_ERROR, "Painter '%s' returned an invalid image.", request.name.c_str());
      return nullptr;
    }
    ++runtime->stats.painter_rasterizations;
    auto data = std::make_shared<std::vector<Rml::byte>>(source->second.premultiplied ? std::move(image.pixels)
                                                                                      : premultiplied(image));
    const Rml::Vector2i dimensions{image.width, image.height};
    auto entry = std::make_shared<PaintTexture>();
    entry->width = dimensions.x;
    entry->height = dimensions.y;
    entry->bytes = data->size();
    entry->texture = manager.MakeCallbackTexture([data, dimensions](const Rml::CallbackTextureInterface& texture) {
      // The painter's pixels are already at drawable resolution and are placed on whole pixels: nearest.
      const auto handle = runtime->render->nearest_texture(data->data(), data->size(), dimensions);
      if (!handle) return false;
      uploaded(data->size());
      texture.SetTextureHandle(handle, dimensions);
      return true;
    });
    lru_.push_front(key);
    entry->lru = lru_.begin();
    bytes_ += entry->bytes;
    cache_.emplace(key, entry);
    return entry;
  }
  // Between frames: evict least recently used entries beyond the limits, skipping those an element still uses.
  void trim() {
    while (cache_.size() > cache_entry_limit || bytes_ > cache_byte_limit) {
      bool evicted = false;
      for (auto at = lru_.end(); at != lru_.begin() && !evicted;) {
        --at;
        const auto found = cache_.find(*at);
        if (found->second.use_count() > 1) continue;
        bytes_ -= found->second->bytes;
        cache_.erase(found);
        lru_.erase(at);
        evicted = true;
      }
      if (!evicted) break;
    }
  }
  void release();
  void clear() {
    cache_.clear();
    lru_.clear();
    bytes_ = 0;
  }
  [[nodiscard]] std::size_t bytes() const { return bytes_; }
  [[nodiscard]] std::size_t entries() const { return cache_.size(); }
  std::set<PaintData*> live;
  std::uint64_t generation{1};

 private:
  struct Source {
    Painter paint;
    bool premultiplied{};
  };
  std::map<std::string, Source> sources_;
  std::set<std::string> unknown_;
  std::unordered_map<std::string, std::shared_ptr<PaintTexture>> cache_;
  std::list<std::string> lru_;
  std::size_t bytes_{};
};

// One element's painted decoration. It is rebuilt whenever the cache was released or the density changed.
struct PaintData {
  PaintData(std::string name, std::vector<std::string> args, Rml::Vector2f box) {
    request.name = std::move(name);
    request.args = std::move(args);
    // Painters size their image as ceil(logical*scale); keep float noise from adding a pixel.
    request.width = std::max(box.x - 1e-6F, 1e-6F);
    request.height = std::max(box.y - 1e-6F, 1e-6F);
    runtime->painters->live.insert(this);
  }
  ~PaintData() {
    if (runtime && runtime->painters) runtime->painters->live.erase(this);
  }
  PaintData(const PaintData&) = delete;
  PaintData& operator=(const PaintData&) = delete;
  // Returns false when there is nothing to draw (unknown painter, painter failure).
  bool refresh(Rml::Element* element) {
    auto* manager = element->GetRenderManager();
    if (!manager) return false;
    PaintRequest scaled = request;
    scaled.scale = runtime->dp;
    scaled.width /= scaled.scale;
    scaled.height /= scaled.scale;
    texture = runtime->painters->get(*manager, scaled);
    generation = runtime->painters->generation;
    scale = runtime->dp;
    geometry = {};
    if (!texture) return false;
    const auto alpha = static_cast<Rml::byte>(element->GetComputedValues().opacity() * 255.F);
    Rml::Mesh mesh;
    Rml::MeshUtilities::GenerateQuad(mesh, {0, 0},
                                     {static_cast<float>(texture->width), static_cast<float>(texture->height)},
                                     Rml::ColourbPremultiplied(alpha, alpha));
    geometry = manager->MakeGeometry(std::move(mesh));
    return true;
  }
  [[nodiscard]] bool stale() const { return generation != runtime->painters->generation || scale != runtime->dp; }
  PaintRequest request;  // width/height in drawable px until refresh() scales them
  std::shared_ptr<PaintTexture> texture;
  Rml::Geometry geometry;
  std::uint64_t generation{};
  double scale{};
};
void PainterCache::release() {
  ++generation;
  clear();
  for (auto* data : live) {
    data->texture.reset();
    data->geometry = {};
  }
}
void HostFonts::Shutdown() {
  clear();
  if (runtime && runtime->painters) runtime->painters->clear();
}

// RCSS `decorator: paint(NAME ARG ARG ...)`: NAME and ARGs are the whitespace-separated tokens inside the parentheses,
// parsed as one string property so the painter defines their meaning.
class PaintDecorator final : public Rml::Decorator {
 public:
  PaintDecorator(std::string name, std::vector<std::string> args) : name_(std::move(name)), args_(std::move(args)) {}
  Rml::DecoratorDataHandle GenerateElementData(Rml::Element* element, Rml::BoxArea) const override {
    // The border box, whatever paint area was asked for: painters draw whole controls.
    const auto box = element->GetBox().GetSize(Rml::BoxArea::Border);
    if (box.x <= 0 || box.y <= 0) return INVALID_DECORATORDATAHANDLE;
    // A failed painter still gets data (with no texture), so RmlUi does not warn and retry on every change.
    auto data = std::make_unique<PaintData>(name_, args_, box);
    data->refresh(element);
    return reinterpret_cast<Rml::DecoratorDataHandle>(data.release());
  }
  void ReleaseElementData(Rml::DecoratorDataHandle handle) const override {
    delete reinterpret_cast<PaintData*>(handle);
  }
  void RenderElement(Rml::Element* element, Rml::DecoratorDataHandle handle) const override {
    auto* data = reinterpret_cast<PaintData*>(handle);
    if (data->stale()) data->refresh(element);
    if (!data->texture) return;
    // The texture holds exactly one drawable pixel per texel: whole-pixel placement.
    const auto at = element->GetAbsoluteOffset(Rml::BoxArea::Border);
    data->geometry.Render({std::round(at.x), std::round(at.y)}, data->texture->texture);
  }

 private:
  std::string name_;
  std::vector<std::string> args_;
};
class PaintInstancer final : public Rml::DecoratorInstancer {
 public:
  PaintInstancer() {
    args_ = RegisterProperty("args", Rml::String()).AddParser("string").GetId();
    RegisterShorthand("decorator", "args", Rml::ShorthandType::RecursiveRepeat);
  }
  Rml::SharedPtr<Rml::Decorator> InstanceDecorator(const Rml::String&, const Rml::PropertyDictionary& declared,
                                                   const Rml::DecoratorInstancerInterface&) override {
    const auto* property = declared.GetProperty(args_);
    if (!property) return nullptr;
    std::istringstream words(property->Get<Rml::String>());
    std::vector<std::string> tokens;
    for (std::string token; words >> token;) tokens.push_back(std::move(token));
    if (tokens.empty()) {
      Rml::Log::Message(Rml::Log::LT_ERROR, "paint() needs a painter name.");
      return nullptr;
    }
    std::string name = std::move(tokens.front());
    tokens.erase(tokens.begin());
    return std::make_shared<PaintDecorator>(std::move(name), std::move(tokens));
  }

 private:
  Rml::PropertyId args_{};
};

// Built-in painters. Colors are 0xRRGGBB or #rrggbb; `none` (or a missing argument where optional) is no paint.
std::optional<std::uint32_t> color_argument(const PaintRequest& request, std::size_t index, bool required) {
  if (index >= request.args.size()) {
    if (required)
      throw std::invalid_argument("'" + request.name + "' is missing argument " + std::to_string(index + 1));
    return std::nullopt;
  }
  const auto& text = request.args[index];
  if (text == "none") return std::nullopt;
  const char* digits = text.c_str();
  if (text.rfind("0x", 0) == 0 || text.rfind("0X", 0) == 0)
    digits += 2;
  else if (text[0] == '#')
    ++digits;
  char* end = nullptr;
  const auto value = std::strtoul(digits, &end, 16);
  if (end == digits || *end || value > 0xffffffUL)
    throw std::invalid_argument("'" + text + "' is not a 0xRRGGBB color");
  return static_cast<std::uint32_t>(value);
}
double number_argument(const PaintRequest& request, std::size_t index, std::optional<double> fallback) {
  if (index >= request.args.size()) {
    if (fallback) return *fallback;
    throw std::invalid_argument("'" + request.name + "' is missing argument " + std::to_string(index + 1));
  }
  char* end = nullptr;
  const double value = std::strtod(request.args[index].c_str(), &end);
  if (end == request.args[index].c_str() || *end || !std::isfinite(value))
    throw std::invalid_argument("'" + request.args[index] + "' is not a number");
  return value;
}
// ui_path composites over a premultiplied surface, so on a transparent image its result is premultiplied.
Image paint_cut_rect(const PaintRequest& request) {
  const auto fill = color_argument(request, 0, true), stroke = color_argument(request, 1, true);
  const double cut = number_argument(request, 2, std::nullopt),
               stroke_width = stroke ? number_argument(request, 3, 1.0) : 0, half = stroke_width / 2;
  auto image = ui_image(request.width, request.height, request.scale);
  if (request.width > 2 * half && request.height > 2 * half)
    ui_path(image, cut_edge_rect(half, half, request.width - 2 * half, request.height - 2 * half, cut), fill, stroke,
            stroke_width);
  return image;
}
Image paint_rect(const PaintRequest& request) {
  auto image = ui_image(request.width, request.height, request.scale);
  if (const auto fill = color_argument(request, 0, true))
    ui_rectangle(image, 0, 0, request.width, request.height, *fill, true);
  return image;
}
Image paint_menu_backdrop(const PaintRequest& request) {
  auto image = ui_image(request.width, request.height, request.scale);
  ui_menu_backdrop(image, request.width);
  return image;
}

bool valid_native_image(const Image& image) {
  if (image.width <= 0 || image.height <= 0) return false;
  const auto width = static_cast<std::size_t>(image.width), height = static_cast<std::size_t>(image.height);
  if (width > std::numeric_limits<std::size_t>::max() / 4 ||
      height > std::numeric_limits<std::size_t>::max() / (width * 4))
    return false;
  return image.pixels.size() == width * height * 4;
}

struct NativeGlState {
  GLint active{}, texture{}, unpack_alignment{}, unpack_row_length{}, unpack_buffer{};
  NativeGlState() {
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack_alignment);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &unpack_row_length);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack_buffer);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  }
  ~NativeGlState() {
    glPixelStorei(GL_UNPACK_ALIGNMENT, unpack_alignment);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, unpack_row_length);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(unpack_buffer));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture));
    glActiveTexture(static_cast<GLenum>(active));
  }
};

struct NativeTexture {
  ~NativeTexture() {
    if (handle && !transferred) glDeleteTextures(1, &handle);
    if (runtime) runtime->native_image_bytes -= bytes;
  }
  static std::shared_ptr<NativeTexture> create(Rml::RenderManager& manager, const Image& image,
                                               std::uint64_t revision) {
    auto texture = std::make_shared<NativeTexture>();
    texture->width = image.width;
    texture->height = image.height;
    texture->revision = revision;
    {
      NativeGlState state;
      glGenTextures(1, &texture->handle);
      if (!texture->handle) return {};
      glBindTexture(GL_TEXTURE_2D, texture->handle);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    texture->bytes = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4;
    runtime->native_image_bytes += texture->bytes;
    std::weak_ptr<NativeTexture> weak = texture;
    texture->callback = manager.MakeCallbackTexture([weak](const Rml::CallbackTextureInterface& interface) {
      const auto owner = weak.lock();
      if (!owner || !owner->handle) return false;
      interface.SetTextureHandle(static_cast<Rml::TextureHandle>(owner->handle), {owner->width, owner->height});
      owner->transferred = true;
      return true;
    });
    const HudRegion full{0, 0, image.width, image.height};
    texture->upload(image, std::span<const HudRegion>(&full, 1));
    return texture;
  }
  void update(const Image& image, std::uint64_t next_revision, std::span<const HudRegion> dirty) {
    if (next_revision == revision && image.width == width && image.height == height) return;
    if (image.width != width || image.height != height) throw std::logic_error("native image storage size changed");
    if (dirty.empty()) {
      const HudRegion full{0, 0, width, height};
      upload(image, std::span<const HudRegion>(&full, 1));
    } else
      upload(image, dirty);
    revision = next_revision;
  }
  void upload(const Image& image, std::span<const HudRegion> regions) {
    NativeGlState state;
    glBindTexture(GL_TEXTURE_2D, handle);
    for (const auto& region : regions) {
      const std::int64_t x0 = std::max<std::int64_t>(0, region.x), y0 = std::max<std::int64_t>(0, region.y);
      const std::int64_t x1 = std::min<std::int64_t>(width, static_cast<std::int64_t>(region.x) + region.width);
      const std::int64_t y1 = std::min<std::int64_t>(height, static_cast<std::int64_t>(region.y) + region.height);
      if (x1 <= x0 || y1 <= y0) continue;
      const auto x = static_cast<int>(x0), y = static_cast<int>(y0), w = static_cast<int>(x1 - x0),
                 h = static_cast<int>(y1 - y0);
      const std::size_t row_bytes = static_cast<std::size_t>(w) * 4;
      std::vector<Rml::byte> pixels(row_bytes * static_cast<std::size_t>(h));
      for (int row = 0; row < h; ++row) {
        const auto source =
            (static_cast<std::size_t>(y + row) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4;
        const auto destination = static_cast<std::size_t>(row) * row_bytes;
        std::copy_n(image.pixels.data() + source, row_bytes, pixels.data() + destination);
        for (std::size_t column = 0; column < row_bytes; column += 4) {
          const unsigned alpha = pixels[destination + column + 3];
          if (alpha == 255) continue;
          for (std::size_t channel = 0; channel < 3; ++channel)
            pixels[destination + column + channel] = static_cast<Rml::byte>(
                (static_cast<unsigned>(pixels[destination + column + channel]) * alpha + 255U) >> 8U);
        }
      }
      glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
      const auto upload_bytes = pixels.size();
      ++runtime->stats.native_image_uploads;
      runtime->stats.native_image_upload_bytes += upload_bytes;
      uploaded(upload_bytes);
    }
  }
  Rml::CallbackTexture callback;
  GLuint handle{};
  int width{}, height{};
  std::uint64_t revision{};
  std::size_t bytes{};
  bool transferred{};
};

class NativeImageElement final : public Rml::Element {
 public:
  RMLUI_RTTI_DeclareWithParent(NativeImageElement, Rml::Element) explicit NativeImageElement(const Rml::String& name)
      : Rml::Element(name) {}
  void set_image(std::shared_ptr<NativeTexture> image) { image_ = std::move(image); }
  void set_source(HudRegion source) {
    if (!source_set_ || source.x != source_.x || source.y != source_.y || source.width != source_.width ||
        source.height != source_.height) {
      source_ = source;
      source_set_ = true;
      mesh_valid_ = false;
    }
  }
  void set_tint(std::array<float, 4> multiply) {
    if (multiply_ != multiply) {
      multiply_ = multiply;
      mesh_valid_ = false;
    }
  }

 protected:
  void OnRender() override {
    if (!image_ || !image_->handle) return;
    const auto box = GetBox().GetSize(Rml::BoxArea::Border);
    if (box.x <= 0 || box.y <= 0) return;
    const HudRegion source = source_set_ ? source_ : HudRegion{0, 0, image_->width, image_->height};
    const std::int64_t x0 = std::max<std::int64_t>(0, source.x), y0 = std::max<std::int64_t>(0, source.y);
    const std::int64_t x1 = std::min<std::int64_t>(image_->width, static_cast<std::int64_t>(source.x) + source.width);
    const std::int64_t y1 = std::min<std::int64_t>(image_->height, static_cast<std::int64_t>(source.y) + source.height);
    if (x1 <= x0 || y1 <= y0) return;
    const auto& computed = GetComputedValues();
    const auto image_color = computed.image_color();
    const float alpha = static_cast<float>(computed.opacity()) * multiply_[3];
    const float tint_alpha = static_cast<float>(image_color.alpha) / 255.F * alpha;
    const Rml::ColourbPremultiplied color(static_cast<Rml::byte>(image_color.red * multiply_[0] * tint_alpha),
                                          static_cast<Rml::byte>(image_color.green * multiply_[1] * tint_alpha),
                                          static_cast<Rml::byte>(image_color.blue * multiply_[2] * tint_alpha),
                                          static_cast<Rml::byte>(255.F * tint_alpha));
    const auto source_x = static_cast<int>(x0), source_y = static_cast<int>(y0);
    const auto source_width = static_cast<int>(x1 - x0), source_height = static_cast<int>(y1 - y0);
    const bool changed = !mesh_valid_ || mesh_width_ != box.x || mesh_height_ != box.y || mesh_source_.x != source_x ||
                         mesh_source_.y != source_y || mesh_source_.width != source_width ||
                         mesh_source_.height != source_height || mesh_texture_width_ != image_->width ||
                         mesh_texture_height_ != image_->height || mesh_color_ != color;
    if (changed) {
      Rml::Mesh mesh;
      Rml::MeshUtilities::GenerateQuad(
          mesh, {0, 0}, box, color,
          {static_cast<float>(source_x) / image_->width, static_cast<float>(source_y) / image_->height},
          {static_cast<float>(source_x + source_width) / image_->width,
           static_cast<float>(source_y + source_height) / image_->height});
      if (auto* manager = GetRenderManager())
        geometry_ = manager->MakeGeometry(std::move(mesh));
      else
        return;
      mesh_width_ = box.x;
      mesh_height_ = box.y;
      mesh_source_ = {source_x, source_y, source_width, source_height};
      mesh_texture_width_ = image_->width;
      mesh_texture_height_ = image_->height;
      mesh_color_ = color;
      mesh_valid_ = true;
      ++runtime->stats.native_image_meshes;
    }
    const auto offset = GetAbsoluteOffset(Rml::BoxArea::Border);
    geometry_.Render(offset, image_->callback);
  }

 private:
  std::shared_ptr<NativeTexture> image_;
  Rml::Geometry geometry_;
  HudRegion source_{}, mesh_source_{};
  std::array<float, 4> multiply_{1, 1, 1, 1};
  float mesh_width_{}, mesh_height_{};
  int mesh_texture_width_{}, mesh_texture_height_{};
  Rml::ColourbPremultiplied mesh_color_{};
  bool source_set_{}, mesh_valid_{};
};
RMLUI_RTTI_Define(NativeImageElement)

    class NativeImageInstancer final : public Rml::ElementInstancer {
 public:
  Rml::ElementPtr InstanceElement(Rml::Element*, const Rml::String& name, const Rml::XMLAttributes&) override {
    return Rml::ElementPtr(new NativeImageElement(name));
  }
  void ReleaseElement(Rml::Element* element) override { delete element; }
};

// The pulsing title as a custom element: native's method inside RmlUi. Every frame it rasterizes through FontBook at
// density*pulse with the exact sub-pixel phase, uploads into one fixed-capacity texture and draws one texel-exact quad.
// Attributes: text; size (baked logical size, default 36); bold (default true); pulse ("0" = static, default "1").
class PulseText final : public Rml::Element {
 public:
  explicit PulseText(const Rml::String& name) : Rml::Element(name) {}
  ~PulseText() override {
    if (runtime) runtime->pulse_bytes -= bytes_;
  }

 protected:
  void OnRender() override {
    auto& rt = *runtime;
    const auto text = GetAttribute<Rml::String>("text", "");
    const int size = GetAttribute<int>("size", 36);
    const auto bold_attribute = GetAttribute<Rml::String>("bold", "true");
    const bool bold = bold_attribute != "false" && bold_attribute != "0",
               pulsing = GetAttribute<Rml::String>("pulse", "1") != "0";
    const double rho = rt.dp, pulse = pulsing ? 1.05 + .05 * std::sin(rt.clock_ms / 200.0) : 1.0;
    const auto box = rt.fonts->layout(text, size, bold);
    const auto corner = GetAbsoluteOffset(Rml::BoxArea::Border);
    const Vec2 origin{corner.x, corner.y}, pivot{origin.x + box.width * rho / 2, origin.y + box.height * rho / 2};
    const double density = rho * pulse, padding = text_padding;
    const Vec2 bitmap_origin{pivot.x + (origin.x - pivot.x) * pulse - padding * density,
                             pivot.y + (origin.y - pivot.y) * pulse - padding * density};
    const Vec2 at{std::floor(bitmap_origin.x), std::floor(bitmap_origin.y)},
        phase{bitmap_origin.x - at.x, bitmap_origin.y - at.y};
    // A static title only re-uploads when what it draws changes.
    char fingerprint[160];
    std::snprintf(fingerprint, sizeof fingerprint, "%d|%d|%.6f|%.6f|%.6f", size, bold ? 1 : 0, density, phase.x,
                  phase.y);
    const std::string key = text + '\0' + fingerprint;
    if (!texture_ || key != key_ || pulsing) {
      const auto bitmap = rt.fonts->rasterize_at_scale(text, size, density, bold, true, white, phase);
      ++rt.stats.text_rasterizations;
      w_ = bitmap.image.width;
      h_ = bitmap.image.height;
      if (w_ > capacity_w_ || h_ > capacity_h_) {
        // Capacity for the whole pulse range, as native reserves.
        capacity_w_ = static_cast<int>(std::ceil((box.width + padding * 2) * rho * 1.1)) + 1;
        capacity_h_ = static_cast<int>(std::ceil((box.height + padding * 2) * rho * 1.1)) + 1;
        capacity_w_ = std::max(capacity_w_, w_);
        capacity_h_ = std::max(capacity_h_, h_);
        const auto handle = HostRender::empty_texture(capacity_w_, capacity_h_);
        const Rml::Vector2i capacity{capacity_w_, capacity_h_};
        texture_ =
            GetRenderManager()->MakeCallbackTexture([handle, capacity](const Rml::CallbackTextureInterface& texture) {
              texture.SetTextureHandle(handle, capacity);
              return true;
            });
        handle_ = static_cast<GLuint>(handle);
      }
      const auto pixels = premultiplied(bitmap.image);
      glBindTexture(GL_TEXTURE_2D, handle_);
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w_, h_, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
      glBindTexture(GL_TEXTURE_2D, 0);
      uploaded(pixels.size());
      rt.pulse_bytes += pixels.size() - bytes_;
      bytes_ = pixels.size();
      key_ = key;
    }
    Rml::Mesh mesh;
    Rml::MeshUtilities::GenerateQuad(mesh, {static_cast<float>(at.x), static_cast<float>(at.y)},
                                     {static_cast<float>(w_), static_cast<float>(h_)},
                                     GetComputedValues().color().ToPremultiplied(GetComputedValues().opacity()), {0, 0},
                                     {static_cast<float>(w_) / static_cast<float>(capacity_w_),
                                      static_cast<float>(h_) / static_cast<float>(capacity_h_)});
    GetRenderManager()->MakeGeometry(std::move(mesh)).Render({0, 0}, texture_);
  }

 private:
  Rml::CallbackTexture texture_;
  GLuint handle_{};
  int capacity_w_{}, capacity_h_{}, w_{}, h_{};
  std::size_t bytes_{};
  std::string key_;
};
class PulseInstancer final : public Rml::ElementInstancer {
 public:
  Rml::ElementPtr InstanceElement(Rml::Element*, const Rml::String& name, const Rml::XMLAttributes&) override {
    return Rml::ElementPtr(new PulseText(name));
  }
  void ReleaseElement(Rml::Element* element) override { delete element; }
};

// Listeners are owned by the host; one whose element is gone (or that was removed) is dropped between frames.
class Listener final : public Rml::EventListener {
 public:
  explicit Listener(std::function<void(Rml::Event&)> function) : function_(std::move(function)) {}
  void ProcessEvent(Rml::Event& event) override { function_(event); }
  void OnAttach(Rml::Element*) override { ++attached_; }
  void OnDetach(Rml::Element*) override { --attached_; }
  [[nodiscard]] bool orphaned() const { return attached_ <= 0; }

 private:
  std::function<void(Rml::Event&)> function_;
  int attached_{};
};

// ui/ files by name (directories stripped), plus the generated text.rcss.
class HostFiles final : public Rml::FileInterface {
 public:
  explicit HostFiles(const std::string& text_rcss) : text_(text_rcss) {}
  Rml::FileHandle Open(const Rml::String& path) override {
    const auto slash = path.find_last_of("/\\");
    const std::string_view name =
        slash == std::string::npos ? std::string_view(path) : std::string_view(path).substr(slash + 1);
    const std::string* data = name == "text.rcss" ? &text_ : embedded_ui_file(name);
    return data ? reinterpret_cast<Rml::FileHandle>(new Open_{data, 0}) : 0;
  }
  void Close(Rml::FileHandle file) override { delete reinterpret_cast<Open_*>(file); }
  size_t Read(void* buffer, size_t size, Rml::FileHandle file) override {
    auto& open = *reinterpret_cast<Open_*>(file);
    const auto count = std::min(size, open.data->size() - open.position);
    std::copy_n(open.data->data() + open.position, count, static_cast<char*>(buffer));
    open.position += count;
    return count;
  }
  bool Seek(Rml::FileHandle file, long offset, int origin) override {
    auto& open = *reinterpret_cast<Open_*>(file);
    const long base = origin == SEEK_SET   ? 0
                      : origin == SEEK_CUR ? static_cast<long>(open.position)
                                           : static_cast<long>(open.data->size());
    const long target = base + offset;
    if (target < 0 || target > static_cast<long>(open.data->size())) return false;
    open.position = static_cast<std::size_t>(target);
    return true;
  }
  size_t Tell(Rml::FileHandle file) override { return reinterpret_cast<Open_*>(file)->position; }
  size_t Length(Rml::FileHandle file) override { return reinterpret_cast<Open_*>(file)->data->size(); }

 private:
  struct Open_ {
    const std::string* data;
    std::size_t position;
  };
  const std::string& text_;
};

int rml_modifiers(std::uint16_t sdl) {
  int result = 0;
  if (sdl & KMOD_CTRL) result |= Rml::Input::KM_CTRL;
  if (sdl & KMOD_SHIFT) result |= Rml::Input::KM_SHIFT;
  if (sdl & KMOD_ALT) result |= Rml::Input::KM_ALT;
  if (sdl & KMOD_NUM) result |= Rml::Input::KM_NUMLOCK;
  if (sdl & KMOD_CAPS) result |= Rml::Input::KM_CAPSLOCK;
  return result;
}

class HostLayer final : public Layer {
 public:
  explicit HostLayer(RmlHost::Impl& impl) : impl_(impl) {}
  [[nodiscard]] std::string_view name() const override { return "rmlui"; }
  [[nodiscard]] bool active() const override;
  [[nodiscard]] bool modal() const override;
  [[nodiscard]] bool covers(Vec2 point) const override;
  [[nodiscard]] bool wants_text() const override;
  Result handle(const Event& event) override;
  void cancel() override;

 private:
  void release_pointer();
  RmlHost::Impl& impl_;
  unsigned pressed_{};  // SDL button bits this layer has seen go down
};
}

struct RmlHost::Impl {
  Impl(const FontBook& font_book, const HudArt* hud_art)
      : fonts(font_book), art(hud_art), render(hud_art), files(text_rcss), layer(*this) {
    SDL_Window* window = SDL_GL_GetCurrentWindow();
    if (!window) throw std::runtime_error("The RmlUi host needs the SDL window's GL context to be current");
    if (!render) throw std::runtime_error("RmlUi's GL3 renderer could not be created");
    rt.fonts = &fonts;
    rt.art = art;
    rt.render = &render;
    rt.painters = &painters;
    runtime = &rt;
    try {
      probe_fonts();
      system = std::make_unique<HostSystem>(window);
      if (!RmlGL3::Initialize()) throw std::runtime_error("RmlUi GL3 initialization failed");
      std::array<GLuint, 4> ids{};
      glGenQueries(static_cast<GLsizei>(ids.size()), ids.data());
      for (std::size_t i = 0; i < ids.size(); ++i) queries[i].id = ids[i];
      Rml::SetSystemInterface(system.get());
      Rml::SetRenderInterface(&render);
      Rml::SetFontEngineInterface(&font_engine);
      Rml::SetFileInterface(&files);
      Rml::SetTextInputHandler(&ime);
      if (!Rml::Initialise()) throw std::runtime_error("Rml::Initialise failed");
      initialised = true;
      Rml::Factory::RegisterElementInstancer("pulse-text", &pulse_instancer);
      Rml::Factory::RegisterElementInstancer("native-image", &native_image_instancer);
      paint_instancer = std::make_unique<PaintInstancer>();
      Rml::Factory::RegisterDecoratorInstancer("paint", paint_instancer.get());
      context = Rml::CreateContext("realm", {800, 600});
      if (!context) throw std::runtime_error("Cannot create the RmlUi context");
      painters.add("cut-rect", paint_cut_rect, true);
      painters.add("rect", paint_rect, true);
      painters.add("menu-backdrop", paint_menu_backdrop, true);
    } catch (...) {
      teardown();
      throw;
    }
  }
  ~Impl() { teardown(); }

  struct Query {
    GLuint id{};
    bool pending{};
    std::uint64_t order{};
  };
  // Every baked size becomes a font-engine candidate and a pair of text classes with the FontBook's own line heights.
  void probe_fonts() {
    std::vector<int> sizes;
    for (int size = smallest_size; size <= largest_size; ++size) {
      bool baked = false;
      for (const bool bold : {false, true}) {
        try {
          const double line = fonts.layout("x", size, bold).height;
          text_classes += ".t" + std::to_string(size) + (bold ? "b" : "") + "{font-size:" + num(size) +
                          "dp;line-height:" + num(line) + "dp" + (bold ? ";font-weight:bold" : "") + "}\n";
          baked = true;
        } catch (const std::exception&) {
        }
      }
      if (baked) sizes.push_back(size);
    }
    font_engine.set_sizes(std::move(sizes));
    // text.rcss: the classes, and body's line height for the default 14 regular text.
    text_rcss = text_classes;
    try {
      text_rcss += "body{line-height:" + num(fonts.layout("x", 14, false).height) + "dp}\n";
    } catch (const std::exception&) {
    }
  }
  void teardown() {
    if (runtime != &rt) return;
    rt.native_images.clear();
    if (initialised) {
      if (context) {
        context->UnloadAllDocuments();
        Rml::RemoveContext("realm");
        context = nullptr;
      }
      Rml::Shutdown();
      initialised = false;
    }
    if (queries[0].id)
      for (auto& query : queries) glDeleteQueries(1, &query.id);
    // RmlUi leaves its program, VAO and bindings current; hand the GL context back with nothing bound.
    glUseProgram(0);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    runtime = nullptr;
  }
  // Context dimensions, dp ratio and FontBook density follow the drawable; a change of density regenerates all text.
  void resize(const Viewport& viewport) {
    if (!viewport.valid() || (viewport.drawable_width == width && viewport.drawable_height == height)) return;
    width = viewport.drawable_width;
    height = viewport.drawable_height;
    rt.dp = viewport.scale();
    font_engine.set_density(rt.dp);
    context->SetDimensions({width, height});
    context->SetDensityIndependentPixelRatio(static_cast<float>(rt.dp));
  }
  void poll_queries() {
    for (;;) {
      Query* oldest = nullptr;
      for (auto& query : queries)
        if (query.pending && (!oldest || query.order < oldest->order)) oldest = &query;
      if (!oldest) return;
      GLint ready{};
      glGetQueryObjectiv(oldest->id, GL_QUERY_RESULT_AVAILABLE, &ready);
      if (!ready) return;
      GLuint64 nanoseconds{};
      glGetQueryObjectui64v(oldest->id, GL_QUERY_RESULT, &nanoseconds);
      rt.stats.gpu_ms = static_cast<double>(nanoseconds) / 1e6;
      oldest->pending = false;
    }
  }
  [[nodiscard]] bool any_document(bool modal_only) const {
    for (int i = 0; i < context->GetNumDocuments(); ++i) {
      const auto* document = context->GetDocument(i);
      if (document && document->IsVisible() && (!modal_only || document->IsModal())) return true;
    }
    return false;
  }

  const FontBook& fonts;
  const HudArt* art;
  // RmlUi's global interfaces and the objects it points at must outlive Rml::Shutdown, so they are members.
  Runtime rt;
  std::string text_classes, text_rcss;
  HostRender render;
  std::unique_ptr<HostSystem> system;
  HostFonts font_engine;
  HostFiles files;
  TextInputMethodEditor_SDL ime;
  PulseInstancer pulse_instancer;
  NativeImageInstancer native_image_instancer;
  std::unique_ptr<PaintInstancer> paint_instancer;  // registers properties, so only once RmlUi is initialised
  PainterCache painters;
  std::vector<std::unique_ptr<Listener>> listeners;
  HostLayer layer;
  bool initialised{};
  Rml::Context* context{};
  int width{}, height{};
  std::array<Query, 4> queries{};
  std::uint64_t query_order{};
};

namespace {
bool HostLayer::active() const { return impl_.any_document(false); }
bool HostLayer::modal() const { return impl_.any_document(true); }
bool HostLayer::covers(Vec2 point) const {
  auto& context = *impl_.context;
  auto* element = context.GetElementAtPoint({static_cast<float>(point.x), static_cast<float>(point.y)});
  // The context root and a document's own body fill the screen without being something to click.
  return element && element != context.GetRootElement() && element != element->GetOwnerDocument();
}
bool HostLayer::wants_text() const {
  auto* focus = impl_.context->GetFocusElement();
  if (!focus) return false;
  auto* document = focus->GetOwnerDocument();
  if (!document || !document->IsVisible()) return false;
  if (focus->GetTagName() == "textarea") return true;
  if (focus->GetTagName() != "input") return false;
  const auto type = focus->GetAttribute<Rml::String>("type", "text");
  return type == "text" || type == "password";
}
void HostLayer::release_pointer() {
  auto& context = *impl_.context;
  context.ProcessMouseLeave();
  // Releasing with the pointer gone ends an unfinished drag without dropping.
  for (int button = 1; button <= 3; ++button)
    if (pressed_ & (1U << button)) context.ProcessMouseButtonUp(RmlSDL::ConvertMouseButton(button), 0);
  pressed_ = 0;
}
void HostLayer::cancel() { release_pointer(); }
Result HostLayer::handle(const Event& event) {
  auto& context = *impl_.context;
  const int modifiers = rml_modifiers(event.modifiers);
  switch (event.kind) {
    case EventKind::pointer_move:
    case EventKind::pointer_down:
    case EventKind::pointer_up:
    case EventKind::wheel: {
      // Occlusion is judged before the event: a click that closes a document still belongs to it.
      const bool covered = covers(event.point);
      context.ProcessMouseMove(static_cast<int>(std::lround(event.point.x)),
                               static_cast<int>(std::lround(event.point.y)), modifiers);
      const unsigned bit = 1U << (event.button & 31);
      bool own_release = false;
      if (event.kind == EventKind::pointer_down) {
        pressed_ |= bit;
        context.ProcessMouseButtonDown(RmlSDL::ConvertMouseButton(event.button), modifiers);
      } else if (event.kind == EventKind::pointer_up) {
        own_release = pressed_ & bit;
        pressed_ &= ~bit;
        context.ProcessMouseButtonUp(RmlSDL::ConvertMouseButton(event.button), modifiers);
      } else if (event.kind == EventKind::wheel)
        context.ProcessMouseWheel(Rml::Vector2f{0, static_cast<float>(-event.wheel)}, modifiers);  // SDL up is positive
      if (event.kind == EventKind::pointer_down && covered) return Result::capture;
      if (event.kind == EventKind::pointer_down) pressed_ &= ~bit;
      // Not Context::IsMouseInteracting: it counts hovering a document's own body, so a full-window document would
      // keep every click from the world. Only what is drawn, or a press this layer still holds, is its own.
      if (covered || own_release || pressed_) return Result::consumed;
      return Result::pass;
    }
    case EventKind::key_down:
    case EventKind::key_up: {
      // Without a text control or a modal document the game's own key bindings keep working.
      if (!wants_text() && !modal()) return Result::pass;
      const auto key = RmlSDL::ConvertKey(event.key);
      if (event.kind == EventKind::key_down)
        context.ProcessKeyDown(key, modifiers);
      else
        context.ProcessKeyUp(key, modifiers);
      return Result::consumed;
    }
    case EventKind::text:
      if (!wants_text() && !modal()) return Result::pass;
      context.ProcessTextInput(Rml::String(event.text));
      return Result::consumed;
    case EventKind::composition: {
      if (!wants_text()) return modal() ? Result::consumed : Result::pass;
      SDL_TextEditingEvent edit{};
      edit.type = SDL_TEXTEDITING;
      // SDL's edit text holds 32 bytes: keep whole code points.
      std::size_t length = std::min<std::size_t>(event.text.size(), sizeof edit.text - 1);
      while (length > 0 && length < event.text.size() &&
             (static_cast<unsigned char>(event.text[length]) & 0xC0U) == 0x80U)
        --length;
      std::copy_n(event.text.data(), length, edit.text);
      edit.start =
          event.cursor < 0 ? static_cast<Sint32>(code_points(std::string_view(edit.text, length))) : event.cursor;
      edit.length = 0;
      impl_.ime.HandleEdit(edit);
      return Result::consumed;
    }
    case EventKind::pointer_left:
    case EventKind::focus_lost:
      release_pointer();
      return Result::pass;
    case EventKind::resized:
      impl_.resize(event.viewport);
      return Result::pass;
  }
  return Result::pass;
}
}

RmlHost::RmlHost(const FontBook& fonts, const HudArt* art) {
  if (runtime) throw std::logic_error("Only one RmlHost can exist at a time");
  impl_ = std::make_unique<Impl>(fonts, art);
}
RmlHost::~RmlHost() = default;
Rml::Context& RmlHost::context() { return *impl_->context; }
Rml::ElementDocument* RmlHost::load(std::string_view name) {
  REALM_ZONE("ui.document.load");
  const auto start = telemetry::Clock::now();
  const std::string path = std::string(name) + ".rml";
  const auto slash = path.find_last_of("/\\");
  const auto* source = embedded_ui_file(slash == std::string::npos ? path : path.substr(slash + 1));
  auto* document = impl_->context->LoadDocument(path);
  auto& stats = impl_->rt.stats;
  ++stats.document_loads;
  if (source) stats.document_load_bytes += source->size();
  stats.document_load_us += static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(telemetry::Clock::now() - start).count());
  return document;
}
Rml::ElementDocument* RmlHost::load_from_memory(const std::string& rml, std::string_view source_name) {
  REALM_ZONE("ui.document.load");
  const auto start = telemetry::Clock::now();
  auto* document = impl_->context->LoadDocumentFromMemory(rml, std::string(source_name));
  auto& stats = impl_->rt.stats;
  ++stats.document_loads;
  stats.document_load_bytes += rml.size();
  stats.document_load_us += static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(telemetry::Clock::now() - start).count());
  return document;
}
void RmlHost::close(Rml::ElementDocument*& document) {
  if (document) document->Close();
  document = nullptr;
}
void RmlHost::show(Rml::ElementDocument& document, bool modal, bool focus) {
  document.Show(modal ? Rml::ModalFlag::Modal : Rml::ModalFlag::None,
                focus ? Rml::FocusFlag::Auto : Rml::FocusFlag::None);
}
void RmlHost::hide(Rml::ElementDocument& document) { document.Hide(); }
void RmlHost::register_painter(std::string name, Painter painter) {
  impl_->painters.add(std::move(name), std::move(painter), false);
}
void RmlHost::listen(Rml::Element& element, const char* event, std::function<void(Rml::Event&)> function,
                     bool capture) {
  impl_->listeners.push_back(std::make_unique<Listener>(std::move(function)));
  element.AddEventListener(event, impl_->listeners.back().get(), capture);
}
void RmlHost::resize(const Viewport& viewport) { impl_->resize(viewport); }
void RmlHost::update(std::int32_t time_ms) {
  auto& i = *impl_;
  i.rt.clock_ms = time_ms;
  i.font_engine.trim();
  i.painters.trim();
  std::erase_if(i.listeners, [](const auto& listener) { return listener->orphaned(); });
  i.context->Update();
  std::erase_if(i.rt.native_images, [](const auto& entry) { return entry.second.use_count() == 1; });
}
void RmlHost::render(int drawable_width, int drawable_height) {
  if (drawable_width <= 0 || drawable_height <= 0) return;
  auto& i = *impl_;
  i.poll_queries();
  Impl::Query* query = nullptr;
  for (auto& candidate : i.queries)
    if (!candidate.pending) {
      query = &candidate;
      break;
    }
  if (query) glBeginQuery(GL_TIME_ELAPSED, query->id);
  i.render.draws = 0;
  i.render.SetViewport(drawable_width, drawable_height);
  i.render.BeginFrame();
  i.context->Render();
  i.render.EndFrame();
  if (query) {
    glEndQuery(GL_TIME_ELAPSED);
    query->pending = true;
    query->order = ++i.query_order;
  }
  i.rt.stats.draws = i.render.draws;
}
void RmlHost::release_caches() {
  impl_->font_engine.release();
  impl_->painters.release();
}
Layer& RmlHost::layer() { return impl_->layer; }
RmlStats RmlHost::stats() const {
  const auto& i = *impl_;
  auto result = i.rt.stats;
  result.text_cache_bytes = i.font_engine.bytes() + i.rt.pulse_bytes;
  result.text_cache_entries = i.font_engine.entries();
  result.painter_cache_bytes = i.painters.bytes();
  result.painter_cache_entries = i.painters.entries();
  result.native_image_bytes = i.rt.native_image_bytes;
  result.documents = static_cast<std::size_t>(i.context->GetNumDocuments());
  return result;
}
void RmlHost::reset_stats() {
  auto& s = impl_->rt.stats;
  s.text_rasterizations = s.painter_rasterizations = s.texture_uploads = s.texture_upload_bytes = s.text_meshes = 0;
  s.native_image_uploads = s.native_image_upload_bytes = s.native_image_meshes = s.log_warnings = s.log_errors = 0;
  s.document_loads = s.document_load_us = s.document_load_bytes = 0;
}
std::string RmlHost::text_classes() const { return impl_->text_classes; }
const FontBook& RmlHost::fonts() const { return impl_->fonts; }
const HudArt* RmlHost::art() const { return impl_->art; }
double RmlHost::scale() const { return impl_->rt.dp; }

void set_native_image(Rml::Element& element, const Image& image, std::uint64_t revision,
                      std::span<const HudRegion> dirty) {
  if (!runtime) return;
  auto* native = rmlui_dynamic_cast<NativeImageElement*>(&element);
  auto* manager = element.GetRenderManager();
  if (!native || !manager || !valid_native_image(image)) return;
  GLint max_texture_size{};
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
  if (image.width > max_texture_size || image.height > max_texture_size) return;
  auto found = runtime->native_images.find(&image);
  auto texture = found == runtime->native_images.end() ? std::shared_ptr<NativeTexture>{} : found->second;
  if (!texture || texture->width != image.width || texture->height != image.height) {
    texture = NativeTexture::create(*manager, image, revision);
    if (!texture) return;
    runtime->native_images[&image] = texture;
  } else if (revision != texture->revision)
    texture->update(image, revision, dirty);
  native->set_image(std::move(texture));
}
void set_native_source(Rml::Element& element, HudRegion source) {
  if (auto* native = rmlui_dynamic_cast<NativeImageElement*>(&element)) native->set_source(source);
}
void set_native_tint(Rml::Element& element, std::array<float, 4> multiply) {
  if (!std::all_of(multiply.begin(), multiply.end(), [](float value) { return std::isfinite(value); })) return;
  for (auto& value : multiply) value = std::clamp(value, 0.F, 1.F);
  if (auto* native = rmlui_dynamic_cast<NativeImageElement*>(&element)) native->set_tint(multiply);
}
}
