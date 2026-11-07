#include "../../stage3D/gl/GlApi.hpp"
#include "RmlGpu.hpp"
#include <RmlUi/Core.h>
#include "RmlUi_Renderer_GL3.h"
#include "realm/stage3D/GraphicsApi.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>

namespace realm::ui {
namespace {
// RmlUi's GL3 renderer with Flash pixel art: item:TYPE and art:KEY sources come from HudArt
// (premultiplied RGBA) and are sampled nearest so icons stay crisp.
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
    if (source.rfind("item:", 0) == 0) {
      key = "item" + source.substr(5);
    } else if (source.rfind("art:", 0) == 0) {
      key = source.substr(4);
    } else {
      return RenderInterface_GL3::LoadTexture(dimensions, source);
    }
    if (!art_ || !art_->contains(key)) {
      Rml::Log::Message(Rml::Log::LT_ERROR, "Missing HUD art '%s'.", key.c_str());
      return {};
    }
    const auto& image = art_->image(key);
    dimensions = {image.width, image.height};
    return nearest_texture(image.pixels.data(), image.pixels.size(), dimensions);
  }

  // Premultiplied RGBA at the texture's own resolution: nothing resamples it.
  Rml::TextureHandle nearest_texture(const Rml::byte* pixels, std::size_t size,
                                     Rml::Vector2i dimensions) {
    const auto handle = GenerateTexture(Rml::Span<const Rml::byte>(pixels, size), dimensions);
    if (handle) {
      filter(handle, GL_NEAREST);
    }
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

 private:
  const HudArt* art_;
};

class GlRmlGpu final : public RmlGpu {
 public:
  explicit GlRmlGpu(const HudArt* art) : render_(art) {}

  [[nodiscard]] Rml::RenderInterface& interface() override { return render_; }

  void initialise() override {
    if (!RmlGL3::Initialize()) {
      throw std::runtime_error("RmlUi GL3 initialization failed");
    }
#if !REALM_GLES
    std::array<GLuint, 4> ids{};
    glGenQueries(static_cast<GLsizei>(ids.size()), ids.data());
    for (std::size_t i = 0; i < ids.size(); ++i) {
      queries_[i].id = ids[i];
    }
#endif
  }

  void shutdown() noexcept override {
#if !REALM_GLES
    if (queries_[0].id) {
      for (auto& query : queries_) {
        glDeleteQueries(1, &query.id);
      }
    }
#endif
  }

  [[nodiscard]] int max_texture_size() const override {
    GLint size{};
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &size);
    return size;
  }

  [[nodiscard]] Rml::TextureHandle create_texture(int width, int height) override {
    glActiveTexture(GL_TEXTURE0);
    GLuint id{};
    glGenTextures(1, &id);
    if (!id) {
      return 0;
    }
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return id;
  }

  void write_texture(Rml::TextureHandle texture, int x, int y, int width, int height,
                     const Rml::byte* rgba) override {
    glActiveTexture(GL_TEXTURE0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture));
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  [[nodiscard]] Rml::TextureHandle nearest_texture(const Rml::byte* rgba, std::size_t size,
                                                   Rml::Vector2i dimensions) override {
    return render_.nearest_texture(rgba, size, dimensions);
  }

  void content_changed() override { render_.TextureContentChanged(); }

  void begin_frame(int drawable_width, int drawable_height) override {
    render_.draws = 0;
    render_.SetViewport(drawable_width, drawable_height);
    render_.BeginFrame();
  }

  void end_frame() override { render_.EndFrame(); }

  [[nodiscard]] std::size_t draws() const override { return render_.draws; }

  [[nodiscard]] bool frame_reused() const override { return render_.frame_reused(); }

  // GL_TIME_ELAPSED is not in GLES 3.0: WebGL 2 reports no RmlUi GPU time, so the timers stay idle.
  [[nodiscard]] bool begin_timer() override {
#if REALM_GLES
    return false;
#else
    for (auto& query : queries_) {
      if (!query.pending) {
        active_ = &query;
        glBeginQuery(GL_TIME_ELAPSED, query.id);
        return true;
      }
    }
    return false;
#endif
  }

  void end_timer() override {
#if !REALM_GLES
    glEndQuery(GL_TIME_ELAPSED);
    active_->pending = true;
    active_->order = ++query_order_;
#endif
  }

  [[nodiscard]] std::optional<double> poll_timers_ms() override {
#if REALM_GLES
    return std::nullopt;
#else
    Query* oldest = nullptr;
    for (auto& query : queries_) {
      if (query.pending && (!oldest || query.order < oldest->order)) {
        oldest = &query;
      }
    }
    if (!oldest) {
      return std::nullopt;
    }
    GLint ready{};
    glGetQueryObjectiv(oldest->id, GL_QUERY_RESULT_AVAILABLE, &ready);
    if (!ready) {
      return std::nullopt;
    }
    GLuint64 nanoseconds{};
    glGetQueryObjectui64v(oldest->id, GL_QUERY_RESULT, &nanoseconds);
    oldest->pending = false;
    return static_cast<double>(nanoseconds) / 1e6;
#endif
  }

  void reset_bindings() noexcept override {
    glUseProgram(0);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
  }

 private:
  struct Query {
    GLuint id{};
    bool pending{};
    std::uint64_t order{};
  };

  HostRender render_;
  std::array<Query, 4> queries_{};
  [[maybe_unused]] Query* active_{};
  [[maybe_unused]] std::uint64_t query_order_{};
};
}  // namespace

std::unique_ptr<RmlGpu> make_rml_gpu(const HudArt* art) {
  if (gfx::active_api() == gfx::Api::vulkan) {
    return make_vulkan_rml_gpu(art);
  }
  if (gfx::active_api() == gfx::Api::webgpu) {
#if REALM_WEBGPU_ENABLED
    return make_webgpu_rml_gpu(art);
#else
    // GL has no context on the WebGPU canvas, so this fails the client's construction instead of drawing through GL.
    throw std::runtime_error("RmlUi WebGPU backend not available in this build");
#endif
  }
  return std::make_unique<GlRmlGpu>(art);
}

void release_texture_after_teardown(Rml::TextureHandle texture) noexcept {
  // Vulkan and WebGPU handles are table indices, freed with their device.
  auto id = static_cast<GLuint>(texture);
  if (id && gfx::active_api() == gfx::Api::opengl) {
    glDeleteTextures(1, &id);
  }
}
}  // namespace realm::ui
