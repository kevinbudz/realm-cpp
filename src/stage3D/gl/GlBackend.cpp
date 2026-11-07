#include "GlApi.hpp"
#include "GlBackend.hpp"
#include "RendererGl.hpp"
#include <memory>
#include <stdexcept>
#include <vector>

namespace realm::gfx {
namespace {
// The OpenGL resource seam. Each method is the GL that Atlas, SurfaceTexture, TerrainBuffer and the
// renderer used to issue inline, in the same order.
class GlBackend final : public Backend {
 public:
  [[nodiscard]] Api api() const override { return Api::opengl; }

  // The context and GLEW were set up by the platform layer before any GPU object exists.
  void initialise(const WindowSurface& /*window*/) override {}
  void shutdown() noexcept override {}
  void set_swap_interval(int /*interval*/) override {}

  [[nodiscard]] GraphicsInfo info() override {
    return {reinterpret_cast<const char*>(glGetString(GL_VENDOR)),
            reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
            reinterpret_cast<const char*>(glGetString(GL_VERSION))};
  }

  void check_frame() override {
    if (glGetError() != GL_NO_ERROR) {
      throw std::runtime_error("OpenGL error during frame");
    }
  }

  [[nodiscard]] Limits limits() override {
    GLint max_size{};
    GLint max_layers{};
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_size);
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &max_layers);
    return {max_size, max_layers};
  }

  [[nodiscard]] bool copy_images_supported() override {
#if REALM_GLES
    return false;
#else
    return GLEW_VERSION_4_3 || GLEW_ARB_copy_image;
#endif
  }

  [[nodiscard]] std::uint32_t create_surface(bool linear) override {
    GLuint texture{};
    glGenTextures(1, &texture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, texture);
    const GLint filter = linear ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return texture;
  }

  void allocate_surface(std::uint32_t texture, int width, int height) override {
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  }

  void upload_surface(std::uint32_t texture, int x, int y, int width, int height,
                      const std::byte* rgba) override {
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glActiveTexture(GL_TEXTURE0);
  }

  void set_surface_filter(std::uint32_t texture, bool linear) override {
    const GLint filter = linear ? GL_LINEAR : GL_NEAREST;
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glActiveTexture(GL_TEXTURE0);
  }

  [[nodiscard]] std::uint32_t create_layers(int size, int layers) override {
    GLuint texture{};
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, size, size, layers, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    return texture;
  }

  void upload_layer(std::uint32_t texture, int layer, int x, int y, int width, int height,
                    const std::byte* rgba) override {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, x, y, layer, width, height, 1, GL_RGBA,
                    GL_UNSIGNED_BYTE, rgba);
  }

  void verify_layers(std::uint32_t& texture) override {
    if (glGetError() != GL_NO_ERROR) {
      glDeleteTextures(1, &texture);
      texture = 0;
      throw std::runtime_error("GPU atlas upload failed");
    }
  }

  [[nodiscard]] std::uint32_t resize_layers(std::uint32_t from, int size, int layers, int kept,
                                            bool copy_images) override {
    GLint read_binding{};
    GLint draw_binding{};
    GLint unit{};
    GLint bound{};
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_binding);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_binding);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &unit);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &bound);

    while (glGetError() != GL_NO_ERROR) {
    }

    GLuint fresh{};
    glGenTextures(1, &fresh);
    glBindTexture(GL_TEXTURE_2D_ARRAY, fresh);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, size, size, layers, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
#if !REALM_GLES
    if (copy_images) {
      glCopyImageSubData(from, GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0, fresh, GL_TEXTURE_2D_ARRAY, 0, 0, 0,
                         0, size, size, kept);
    } else
#else
    // WebGL 2 has no image copy: copy_images_supported() is false, so the blit below always runs.
    (void)copy_images;
#endif
    {
      // Blits are clipped by the scissor test, which the renderer enables while drawing.
      const bool scissor = glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE;
      glDisable(GL_SCISSOR_TEST);
      GLuint fbos[2]{};
      glGenFramebuffers(2, fbos);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, fbos[0]);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbos[1]);
      for (int layer = 0; layer < kept; ++layer) {
        glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, from, 0, layer);
        glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, fresh, 0, layer);
        glBlitFramebuffer(0, 0, size, size, 0, 0, size, size, GL_COLOR_BUFFER_BIT, GL_NEAREST);
      }
      glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
      glDeleteFramebuffers(2, fbos);
      if (scissor) {
        glEnable(GL_SCISSOR_TEST);
      }
    }
    if (kept < layers) {
      const std::vector<unsigned char> blank(static_cast<std::size_t>(size) *
                                             static_cast<std::size_t>(size) * 4);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      for (int layer = kept; layer < layers; ++layer) {
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, size, size, 1, GL_RGBA,
                        GL_UNSIGNED_BYTE, blank.data());
      }
    }
    const bool failed = glGetError() != GL_NO_ERROR;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(read_binding));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(draw_binding));
    if (failed) {
      glBindTexture(GL_TEXTURE_2D_ARRAY, static_cast<GLuint>(bound));
      glActiveTexture(static_cast<GLenum>(unit));
      glDeleteTextures(1, &fresh);
      throw std::runtime_error("GPU atlas resize failed");
    }
    glBindTexture(GL_TEXTURE_2D_ARRAY,
                  static_cast<GLuint>(bound) == from ? fresh : static_cast<GLuint>(bound));
    glActiveTexture(static_cast<GLenum>(unit));
    return fresh;
  }

  void release_texture(std::uint32_t texture) noexcept override {
    if (texture) {
      glDeleteTextures(1, &texture);
    }
  }

  [[nodiscard]] std::uint32_t create_buffer(std::span<const std::byte> bytes) override {
    GLuint buffer{};
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes.size()), bytes.data(),
                 GL_STATIC_DRAW);
    return buffer;
  }

  void release_buffer(std::uint32_t buffer) noexcept override {
    if (buffer) {
      glDeleteBuffers(1, &buffer);
    }
  }

  [[nodiscard]] std::unique_ptr<RendererBackend> create_renderer(
      std::size_t capacity, bool persistent_streaming,
      std::size_t persistent_minimum_bytes) override {
    return std::make_unique<realm::RendererGl>(capacity, persistent_streaming,
                                               persistent_minimum_bytes);
  }
};
}  // namespace

Backend& opengl_backend() {
  static GlBackend backend;
  return backend;
}
}  // namespace realm::gfx
