#include "realm/renderer.hpp"
#include <algorithm>
#include <cstddef>
#include <cmath>
#include <numbers>
#include <limits>
#include <stdexcept>
#include <string>

namespace realm {
namespace {
constexpr auto vertex_shader = R"glsl(#version 330 core
layout(location=0) in vec2 origin;
layout(location=1) in vec2 axisX;
layout(location=2) in vec2 axisY;
layout(location=3) in vec4 spriteUV;
layout(location=4) in float page;
layout(location=5) in vec4 multiply;
layout(location=6) in vec4 offset;
layout(location=7) in float kind;
layout(location=8) in vec2 uvOffset;
uniform vec2 viewport;
out vec2 local;
flat out vec4 bounds;
flat out float layer;
flat out vec4 tint;
flat out vec4 bias;
flat out float mode;
flat out vec2 scroll;
void main() {
    local = vec2(gl_VertexID & 1, (gl_VertexID >> 1) & 1);
    vec2 pixel = origin + axisX * local.x + axisY * local.y;
    gl_Position = vec4(pixel.x / viewport.x * 2.0 - 1.0, 1.0 - pixel.y / viewport.y * 2.0, 0.0, 1.0);
    bounds = spriteUV; layer = page; tint = multiply; bias = offset; mode = kind; scroll = uvOffset;
}
)glsl";
constexpr auto fragment_shader = R"glsl(#version 330 core
in vec2 local;
flat in vec4 bounds;
flat in float layer;
flat in vec4 tint;
flat in vec4 bias;
flat in float mode;
flat in vec2 scroll;
uniform sampler2DArray sprites;
out vec4 color;
void main() {
    if (mode > 1.5 && mode < 2.5) { color = tint; return; }
    if (mode > 0.5 && mode < 1.5) {
        float r = min(1.0, length((local - 0.5) * 2.0));
        color = vec4(tint.rgb, mix(tint.a, bias.a, r)); return;
    }
    vec2 coords = mode > 2.5 && mode < 3.5 ? fract(local + scroll) : local;
    vec2 uv = bounds.xy + coords * bounds.zw;
    vec2 halfTexel = vec2(0.5 / 2048.0);
    if (mode < 3.5) uv = clamp(uv, bounds.xy + halfTexel, bounds.xy + bounds.zw - halfTexel);
    vec4 texel = texture(sprites, vec3(uv, layer));
    // Flash UI is composited by the regular display list, which uses the
    // premultiplied bitmap color once. World Stage3D uses SOURCE_ALPHA again.
    if (mode > 4.5 && texel.a > 0.0) texel.rgb /= texel.a;
    color = clamp(texel * tint + bias, 0.0, 1.0);
}
)glsl";

GLuint shader(GLenum type, const char* source) {
    const auto id = glCreateShader(type); glShaderSource(id, 1, &source, nullptr); glCompileShader(id);
    GLint ok{}; glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        std::string log(4096, '\0'); glGetShaderInfoLog(id, static_cast<GLsizei>(log.size()), nullptr, log.data());
        glDeleteShader(id); throw std::runtime_error("Shader compilation: " + log);
    }
    return id;
}
std::array<float, 2> floats(Vec2 p) { return {static_cast<float>(p.x), static_cast<float>(p.y)}; }
}
Quad sprite_quad(SpriteRegion s, Vec2 origin, Vec2 axis_x, Vec2 axis_y) {
    constexpr float inv = 1.0F / Atlas::page_size;
    Quad q; q.origin = floats(origin); q.axis_x = floats(axis_x); q.axis_y = floats(axis_y);
    q.uv = {static_cast<float>(s.x) * inv, static_cast<float>(s.y) * inv,
            static_cast<float>(s.width) * inv, static_cast<float>(s.height) * inv};
    q.page = static_cast<float>(s.page); return q;
}
Quad projectile_quad(SpriteRegion sprite, Vec2 center, double angle, int size, double zoom, bool outline) {
    // Point3D.as: the path is a rotated square; outlined textures retain their
    // pixel scale and are clipped by the square instead of fitting inside it.
    const double radius = 8.0 * size / 100 / std::sin(24.0 * std::numbers::pi / 180);
    const double c = std::cos(angle), s = std::sin(angle), extent = radius * zoom;
    auto q = sprite_quad(sprite, {center.x + (s-c)*extent, center.y - (s+c)*extent},
                         {2*c*extent,2*s*extent}, {-2*s*extent,2*c*extent});
    if (outline) {
        constexpr double inv = 1.0 / Atlas::page_size;
        q.uv = {static_cast<float>((sprite.x + sprite.width*.5-radius)*inv),
                static_cast<float>((sprite.y + sprite.height*.5-radius)*inv),
                static_cast<float>(2*radius*inv),static_cast<float>(2*radius*inv)};
        q.kind = 4;
    }
    return q;
}
Quad radial_shadow(Vec2 origin, Vec2 x, Vec2 y, std::array<float, 3> color, float center, float edge) {
    Quad q; q.origin = floats(origin); q.axis_x = floats(x); q.axis_y = floats(y);
    q.kind = 1; q.multiply = {color[0], color[1], color[2], center}; q.offset[3] = edge; return q;
}
Renderer::Renderer(std::size_t capacity) : capacity_(capacity) {
    if (!capacity || capacity > 1000000) throw std::invalid_argument("Invalid quad capacity");
    const auto vs = shader(GL_VERTEX_SHADER, vertex_shader);
    GLuint fs{};
    try { fs = shader(GL_FRAGMENT_SHADER, fragment_shader); }
    catch (...) { glDeleteShader(vs); throw; }
    program_ = glCreateProgram(); glAttachShader(program_, vs); glAttachShader(program_, fs); glLinkProgram(program_);
    glDeleteShader(vs); glDeleteShader(fs);
    GLint ok{}; glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) { glDeleteProgram(program_); program_ = 0; throw std::runtime_error("Shader link failed"); }
    glUseProgram(program_);
    glUniform1i(glGetUniformLocation(program_, "sprites"), 0);
    viewport_location_ = glGetUniformLocation(program_, "viewport");
    glGenVertexArrays(1, &vao_); glBindVertexArray(vao_);
    glGenBuffers(static_cast<GLsizei>(buffers_.size()), buffers_.data());
    for (auto id : buffers_) {
        glBindBuffer(GL_ARRAY_BUFFER, id);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(capacity_ * sizeof(Quad)), nullptr, GL_STREAM_DRAW);
    }
    glGenQueries(static_cast<GLsizei>(timers_.size()), timers_.data());
    glEnable(GL_BLEND); glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_FRAMEBUFFER_SRGB);
}
Renderer::~Renderer() {
    glDeleteQueries(static_cast<GLsizei>(timers_.size()), timers_.data());
    glDeleteBuffers(static_cast<GLsizei>(buffers_.size()), buffers_.data());
    glDeleteVertexArrays(1, &vao_); glDeleteProgram(program_);
}
void Renderer::draw(const Atlas& atlas, std::span<const Quad> quads, int width, int height, int clip_width,std::span<const Quad> overlay) {
    const auto total=quads.size()+overlay.size();
    if (width <= 0 || height <= 0 || clip_width < 0 || clip_width > width || total > 1000000)
        throw std::invalid_argument("Invalid draw dimensions or quad count");
    if (total > capacity_) capacity_ = std::min(std::size_t{1000000}, std::max(total, capacity_ * 2));
    const auto query = frame_ % timers_.size();
    bool measure = !timer_pending_[query];
    if (timer_pending_[query]) {
        GLint ready{}; glGetQueryObjectiv(timers_[query], GL_QUERY_RESULT_AVAILABLE, &ready);
        if (ready) {
            GLuint64 ns{}; glGetQueryObjectui64v(timers_[query], GL_QUERY_RESULT, &ns);
            gpu_ms_ = static_cast<double>(ns) / 1000000.0; measure = true;
        }
    }
    // Queries are polled several frames later. Never wait for a result mid-frame.
    if (measure) glBeginQuery(GL_TIME_ELAPSED, timers_[query]);
    glViewport(0, 0, width, height); glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST); glScissor(0, 0, clip_width, height);
    glUseProgram(program_); glUniform2f(viewport_location_, static_cast<float>(width), static_cast<float>(height));
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D_ARRAY, atlas.texture());
    glBindVertexArray(vao_); glBindBuffer(GL_ARRAY_BUFFER, buffers_[frame_ % buffers_.size()]);
    // Orphaning prevents overwrite of in-flight data; three buffer IDs reduce reuse pressure.
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(capacity_ * sizeof(Quad)), nullptr, GL_STREAM_DRAW);
    if (!quads.empty()) glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(quads.size_bytes()), quads.data());
    if(!overlay.empty())glBufferSubData(GL_ARRAY_BUFFER,static_cast<GLintptr>(quads.size_bytes()),static_cast<GLsizeiptr>(overlay.size_bytes()),overlay.data());
    const std::array<GLint, 9> sizes{2, 2, 2, 4, 1, 4, 4, 1, 2};
    const std::array<std::size_t, 9> offsets{offsetof(Quad, origin), offsetof(Quad, axis_x), offsetof(Quad, axis_y),
        offsetof(Quad, uv), offsetof(Quad, page), offsetof(Quad, multiply), offsetof(Quad, offset), offsetof(Quad, kind), offsetof(Quad, uv_offset)};
    for (GLuint i = 0; i < sizes.size(); ++i) {
        glEnableVertexAttribArray(i); glVertexAttribPointer(i, sizes[i], GL_FLOAT, GL_FALSE, sizeof(Quad),
                                                          reinterpret_cast<void*>(offsets[i]));
        glVertexAttribDivisor(i, 1);
    }
    draw_calls_ = quads.empty() ? 0 : 1;
    if (!quads.empty()) glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(quads.size()));
    if(!overlay.empty()) {
        glScissor(0,0,width,height);
        for(GLuint i=0;i<sizes.size();++i)glVertexAttribPointer(i,sizes[i],GL_FLOAT,GL_FALSE,sizeof(Quad),reinterpret_cast<void*>(quads.size_bytes()+offsets[i]));
        glDrawArraysInstanced(GL_TRIANGLE_STRIP,0,4,static_cast<GLsizei>(overlay.size()));++draw_calls_;
    }
    if (measure) { glEndQuery(GL_TIME_ELAPSED); timer_pending_[query] = true; }
    glDisable(GL_SCISSOR_TEST); ++frame_;
}
Image Renderer::capture(int width, int height) const {
    Image image{width, height, std::vector<unsigned char>(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4)};
    glPixelStorei(GL_PACK_ALIGNMENT, 1); glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, image.pixels.data());
    const auto stride = static_cast<std::size_t>(width) * 4;
    for (int y = 0; y < height / 2; ++y) {
        auto top = image.pixels.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) * stride);
        auto bottom = image.pixels.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(height - 1 - y) * stride);
        std::swap_ranges(top, top + static_cast<std::ptrdiff_t>(stride), bottom);
    }
    return image;
}
}
