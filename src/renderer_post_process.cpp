#include <GL/glew.h>
#include "renderer_post_process.hpp"
#include "renderer_shader.hpp"
#include "realm/telemetry.hpp"
#include <stdexcept>

namespace realm {
namespace {
constexpr auto post_vertex_shader = R"glsl(#version 330 core
uniform vec2 viewport;
uniform vec4 rect;
void main() {
    vec2 local = vec2(gl_VertexID & 1, (gl_VertexID >> 1) & 1);
    vec2 pixel = rect.xy + rect.zw * local;
    gl_Position = vec4(pixel.x / viewport.x * 2.0 - 1.0, 1.0 - pixel.y / viewport.y * 2.0, 0.0, 1.0);
}
)glsl";
constexpr auto post_fragment_shader = R"glsl(#version 330 core
uniform sampler2D scene;
uniform int mode;
uniform vec2 axis;
uniform vec2 region;
uniform float blur;
uniform bool blind;
uniform float alpha;
uniform float sidebar;
out vec4 color;
vec4 fetch(ivec2 c) {
    return (c.x < 0 || c.y < 0 || c.x >= int(region.x) || c.y >= int(region.y)) ? vec4(0) : texelFetch(scene, c, 0);
}
void main() {
    if (mode == 1) {
        // HurtOverlay: radial gradient ratios 0/155/255, alphas 0/0/.92, ellipse through the corners.
        vec2 q = vec2(gl_FragCoord.x, region.y - gl_FragCoord.y);
        float r = length((q - region * 0.5) / (region * 0.70710678));
        float edge = clamp((r - 155.0 / 255.0) / (1.0 - 155.0 / 255.0), 0.0, 1.0) * 0.92;
        color = vec4(1.0, 55.0 / 255.0, 0.0, edge * alpha); return;
    }
    if (mode == 2) {
        float t = clamp((gl_FragCoord.x - (region.x - sidebar)) / sidebar, 0.0, 1.0);
        color = vec4(0.0, 0.0, 0.0, t); return;
    }
    ivec2 c = ivec2(gl_FragCoord.xy);
    vec4 sum;
    if (blur <= 0.0) sum = fetch(c);
    else {
        float half_width = blur * 0.5;
        int n = min(int(ceil(half_width)), 160);
        sum = vec4(0);
        for (int i = -n; i <= n; ++i)
            sum += clamp(half_width - abs(float(i)) + 0.5, 0.0, 1.0) * fetch(c + ivec2(axis) * i);
        sum /= blur;
    }
    vec3 rgb = blind ? vec3(0.05 * (sum.r + sum.g + sum.b)) : sum.rgb;
    color = vec4(rgb, 1.0);
}
)glsl";

}

RendererPostProcess::RendererPostProcess() {
  GLuint vertex{}, fragment{};
  try {
    vertex = renderer_detail::compile_shader(GL_VERTEX_SHADER, post_vertex_shader);
    fragment = renderer_detail::compile_shader(GL_FRAGMENT_SHADER, post_fragment_shader);
    program_ = glCreateProgram();
    glAttachShader(program_, vertex);
    glAttachShader(program_, fragment);
    glLinkProgram(program_);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    vertex = fragment = 0;
    GLint linked{};
    glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    if (!linked) throw std::runtime_error("Post shader link failed");

    glUseProgram(program_);
    glUniform1i(glGetUniformLocation(program_, "scene"), 2);
    viewport_ = glGetUniformLocation(program_, "viewport");
    rect_ = glGetUniformLocation(program_, "rect");
    mode_ = glGetUniformLocation(program_, "mode");
    axis_ = glGetUniformLocation(program_, "axis");
    region_ = glGetUniformLocation(program_, "region");
    blur_ = glGetUniformLocation(program_, "blur");
    blind_ = glGetUniformLocation(program_, "blind");
    alpha_ = glGetUniformLocation(program_, "alpha");
    sidebar_ = glGetUniformLocation(program_, "sidebar");
    glGenVertexArrays(1, &vao_);
  } catch (...) {
    if (vertex) glDeleteShader(vertex);
    if (fragment) glDeleteShader(fragment);
    release();
    throw;
  }
}

RendererPostProcess::~RendererPostProcess() { release(); }

void RendererPostProcess::release() noexcept {
  glDeleteFramebuffers(static_cast<GLsizei>(framebuffers_.size()), framebuffers_.data());
  glDeleteTextures(static_cast<GLsizei>(textures_.size()), textures_.data());
  if (vao_) glDeleteVertexArrays(1, &vao_);
  if (program_) glDeleteProgram(program_);
  framebuffers_.fill(0);
  textures_.fill(0);
  vao_ = program_ = 0;
  width_ = height_ = 0;
}

// The offscreen world and the blur's intermediate share one size; reallocated only on resize.
void RendererPostProcess::ensure_targets(int width, int height) {
  if (textures_[0] && width_ == width && height_ == height) return;
  if (!textures_[0]) {
    glGenTextures(static_cast<GLsizei>(textures_.size()), textures_.data());
    glGenFramebuffers(static_cast<GLsizei>(framebuffers_.size()), framebuffers_.data());
  }
  for (std::size_t i = 0; i < textures_.size(); ++i) {
    glBindTexture(GL_TEXTURE_2D, textures_[i]);
    telemetry::add(telemetry::Counter::texture_storage_bytes,
                   static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffers_[i]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures_[i], 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      throw std::runtime_error("Post-effect framebuffer incomplete");
    }
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  width_ = width;
  height_ = height;
}

std::size_t RendererPostProcess::run_post(int width, int height, int clip_width, const ScreenEffects& effects) {
  std::size_t draw_count{};
  glDisable(GL_BLEND);
  glUseProgram(program_);
  glBindVertexArray(vao_);
  glUniform2f(viewport_, static_cast<float>(width), static_cast<float>(height));
  glUniform2f(region_, static_cast<float>(clip_width), static_cast<float>(height));
  glUniform1i(mode_, 0);
  const auto pass = [&](GLuint framebuffer, GLuint source, float dx, float dy, float blur, bool blind) {
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, source);
    glUniform4f(rect_, 0, 0, static_cast<float>(clip_width), static_cast<float>(height));
    glUniform2f(axis_, dx, dy);
    glUniform1f(blur_, blur);
    glUniform1i(blind_, blind);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    ++draw_count;
  };
  if (effects.blur > 0) {
    pass(framebuffers_[1], textures_[0], 1, 0, effects.blur, false);
    pass(0, textures_[1], 0, 1, effects.blur, effects.blind);
  } else
    pass(0, textures_[0], 0, 0, 0, effects.blind);
  glActiveTexture(GL_TEXTURE0);
  glEnable(GL_BLEND);
  return draw_count;
}

// Breath vignette first, then the sidebar strip over it (Map.as child order).
std::size_t RendererPostProcess::run_screen_overlays(int width, int height, int clip_width,
                                                     const ScreenEffects& effects) {
  std::size_t draw_count{};
  glUseProgram(program_);
  glBindVertexArray(vao_);
  glUniform2f(viewport_, static_cast<float>(width), static_cast<float>(height));
  glUniform2f(region_, static_cast<float>(clip_width), static_cast<float>(height));
  if (effects.vignette > 0) {
    glUniform1i(mode_, 1);
    glUniform1f(alpha_, effects.vignette);
    glUniform4f(rect_, 0, 0, static_cast<float>(clip_width), static_cast<float>(height));
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    ++draw_count;
  }
  if (effects.sidebar > 0 && effects.sidebar <= static_cast<float>(clip_width)) {
    glUniform1i(mode_, 2);
    glUniform1f(sidebar_, effects.sidebar);
    glUniform4f(rect_, static_cast<float>(clip_width) - effects.sidebar, 0, effects.sidebar,
                static_cast<float>(height));
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    ++draw_count;
  }
  return draw_count;
}
}
