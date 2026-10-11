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
uniform sampler2D surface;
uniform bool standalone;
out vec4 color;
void main() {
    if(mode > 6.5 && mode < 7.5) {
        vec2 pixel=floor(local*(scroll+8.0));
        vec2 d=max(vec2(4)-pixel,pixel-(scroll+3.0));
        float edge=max(d.x,d.y);
        color=edge<=0.0 ? tint : vec4(0,0,0,edge<=1.0 ? 0.8 : 0.0);return;
    }
    if (mode > 1.5 && mode < 2.5) { color = tint; return; }
    if (mode > 0.5 && mode < 1.5) {
        float r = min(1.0, length((local - 0.5) * 2.0));
        color = vec4(tint.rgb, mix(tint.a, bias.a, r)); return;
    }
    vec2 coords = mode > 2.5 && mode < 3.5 ? fract(local + scroll) : local;
    if(mode > 7.5 && mode < 8.5) {
        vec2 p=local-.5;coords=vec2(p.x*scroll.x+p.y*scroll.y,-p.x*scroll.y+p.y*scroll.x)+.5;
        if(any(lessThan(coords,vec2(0)))||any(greaterThan(coords,vec2(1)))) { color=vec4(0);return; }
    }
    vec2 uv = bounds.xy + coords * bounds.zw;
    vec2 halfTexel = 0.5 / vec2(standalone ? textureSize(surface, 0) : textureSize(sprites, 0).xy);
    if (mode < 3.5) uv = clamp(uv, bounds.xy + halfTexel, bounds.xy + bounds.zw - halfTexel);
    if(mode > 5.5 && (any(lessThan(uv,vec2(0)))||any(greaterThan(uv,vec2(1))))) { color=vec4(0);return; }
    vec4 texel = standalone ? texture(surface, uv) : texture(sprites, vec3(uv, layer));
    // Flash UI is composited by the regular display list, which uses the
    // premultiplied bitmap color once. World Stage3D uses SOURCE_ALPHA again.
    if (mode > 4.5 && mode < 5.5 && texel.a > 0.0) texel.rgb /= texel.a;
    // Stasis: ColorMatrixFilter(greyscaleFilterMatrix) on ordinary sprites.
    if (scroll.x > 0.5 && (mode < 0.5 || (mode > 5.5 && mode < 6.5))) texel.rgb = vec3(dot(texel.rgb, vec3(0.3, 0.59, 0.11)));
    color = clamp(texel * tint + bias, 0.0, 1.0);
}
)glsl";

constexpr auto mesh_vertex_shader = R"glsl(#version 330 core
layout(location=0) in vec2 position;
layout(location=1) in vec2 texcoord;
layout(location=2) in float shade;
uniform vec2 viewport;
out vec2 uv;
out float lit;
void main() {
    gl_Position = vec4(position.x / viewport.x * 2.0 - 1.0, 1.0 - position.y / viewport.y * 2.0, 0.0, 1.0);
    uv = texcoord; lit = shade;
}
)glsl";
// Model programs: textured groups sample the object sprite (nearest, repeated)
// times the face shade; Solid* groups are the flat object color times it.
constexpr auto mesh_fragment_shader = R"glsl(#version 330 core
in vec2 uv;
in float lit;
uniform sampler2DArray sprites;
uniform vec4 bounds;
uniform float layer;
uniform vec3 flat_color;
uniform bool textured;
out vec4 color;
void main() {
    if (!textured) { color = vec4(flat_color * lit, 1.0); return; }
    // Nearest sampling with the sprite repeated over the face, as Flash's bitmap fills tile it.
    vec2 page = vec2(textureSize(sprites, 0).xy);
    vec2 size = bounds.zw * page;
    vec2 coords = (bounds.xy * page + mod(floor(uv * size), size) + 0.5) / page;
    vec4 texel = texture(sprites, vec3(coords, layer));
    color = vec4(texel.rgb * lit, texel.a);
}
)glsl";

// Map-sprite post effects (Map.as). Mode 0 is one separable pass of the Flash
// box BlurFilter over the offscreen world, optionally folded with the blind
// ColorMatrixFilter (linear, so it commutes with the blur). Taps outside the
// world viewport are transparent, so edges blend toward the black behind the map.
// Mode 1 is the HurtOverlay breath vignette and mode 2 the GradientOverlay strip.
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
SurfaceTexture::~SurfaceTexture() { if(texture_)glDeleteTextures(1,&texture_); }
void SurfaceTexture::update(const Image& image) { update_region(image,0,0,image.width,image.height); }
void SurfaceTexture::update_region(const Image& image,int x,int y,int w,int h) {
    if(image.width<1||image.height<1||image.pixels.size()!=static_cast<std::size_t>(image.width)*static_cast<std::size_t>(image.height)*4||x<0||y<0||w<1||h<1||x+w>image.width||y+h>image.height)
        throw std::invalid_argument("Invalid UI texture upload");
    GLint limit{};glGetIntegerv(GL_MAX_TEXTURE_SIZE,&limit);
    if(image.width>limit||image.height>limit||reserved_width_>limit||reserved_height_>limit)throw std::runtime_error("UI surface exceeds GPU texture limit");
    if(!texture_)glGenTextures(1,&texture_);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,texture_);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    const bool resized=width_!=image.width||height_!=image.height;
    if(resized) { width_=image.width;height_=image.height;x=y=0;w=width_;h=height_; }
    if((!retain_capacity_&&resized)||std::max(width_,reserved_width_)>storage_width_||std::max(height_,reserved_height_)>storage_height_) {
        const auto capacity=[&](int size) { int result=1;while(result<size)result*=2;return std::min(result,limit); };
        storage_width_=retain_capacity_ ? capacity(std::max(width_,reserved_width_)) : std::max(width_,reserved_width_);
        storage_height_=retain_capacity_ ? capacity(std::max(height_,reserved_height_)) : std::max(height_,reserved_height_);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,storage_width_,storage_height_,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    }
    Image pixels{w,h,std::vector<unsigned char>(static_cast<std::size_t>(w)*static_cast<std::size_t>(h)*4)};
    for(int row=0;row<h;++row)std::copy_n(image.pixels.data()+(static_cast<std::size_t>(row+y)*static_cast<std::size_t>(image.width)+static_cast<std::size_t>(x))*4,static_cast<std::size_t>(w)*4,pixels.pixels.data()+static_cast<std::size_t>(row)*static_cast<std::size_t>(w)*4);
    premultiply_flash_bitmap(pixels);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glTexSubImage2D(GL_TEXTURE_2D,0,x,y,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels.pixels.data());
    glActiveTexture(GL_TEXTURE0);
}
Quad SurfaceTexture::quad(Vec2 origin,Vec2 ax,Vec2 ay,int x,int y,int w,int h) const {
    if(!texture_)throw std::logic_error("UI surface was not uploaded");
    if(w==0)w=width_;
    if(h==0)h=height_;
    Quad q;q.origin=floats(origin);q.axis_x=floats(ax);q.axis_y=floats(ay);q.texture=texture_;q.kind=5;
    const auto fw=static_cast<float>(storage_width_),fh=static_cast<float>(storage_height_);
    q.uv={static_cast<float>(x)/fw,static_cast<float>(y)/fh,static_cast<float>(w)/fw,static_cast<float>(h)/fh};return q;
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
Quad mesh_quad(std::shared_ptr<const MeshDraw> mesh) {
    Quad q; q.kind = 9; q.owner = std::move(mesh); return q;
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
    {
        const auto mesh_vs = shader(GL_VERTEX_SHADER, mesh_vertex_shader);
        GLuint mesh_fs{};
        try { mesh_fs = shader(GL_FRAGMENT_SHADER, mesh_fragment_shader); }
        catch (...) { glDeleteShader(mesh_vs); glDeleteProgram(program_); throw; }
        mesh_program_ = glCreateProgram(); glAttachShader(mesh_program_, mesh_vs); glAttachShader(mesh_program_, mesh_fs); glLinkProgram(mesh_program_);
        glDeleteShader(mesh_vs); glDeleteShader(mesh_fs);
        GLint mesh_ok{}; glGetProgramiv(mesh_program_, GL_LINK_STATUS, &mesh_ok);
        if (!mesh_ok) { glDeleteProgram(mesh_program_); glDeleteProgram(program_); program_ = mesh_program_ = 0; throw std::runtime_error("Mesh shader link failed"); }
        glUseProgram(mesh_program_);
        glUniform1i(glGetUniformLocation(mesh_program_, "sprites"), 0);
        mesh_viewport_ = glGetUniformLocation(mesh_program_, "viewport");
        mesh_bounds_ = glGetUniformLocation(mesh_program_, "bounds");
        mesh_color_ = glGetUniformLocation(mesh_program_, "flat_color");
        mesh_page_ = glGetUniformLocation(mesh_program_, "layer");
        mesh_textured_ = glGetUniformLocation(mesh_program_, "textured");
        glGenVertexArrays(1, &mesh_vao_); glBindVertexArray(mesh_vao_);
        glGenBuffers(1, &mesh_buffer_); glBindBuffer(GL_ARRAY_BUFFER, mesh_buffer_);
        for (GLuint i = 0; i < 3; ++i) glEnableVertexAttribArray(i);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void*>(offsetof(MeshVertex, x)));
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void*>(offsetof(MeshVertex, u)));
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void*>(offsetof(MeshVertex, shade)));
        glBindVertexArray(0);
    }
    {
        const auto post_vs = shader(GL_VERTEX_SHADER, post_vertex_shader);
        GLuint post_fs{};
        try { post_fs = shader(GL_FRAGMENT_SHADER, post_fragment_shader); }
        catch (...) { glDeleteShader(post_vs); glDeleteProgram(mesh_program_); glDeleteProgram(program_); throw; }
        post_program_ = glCreateProgram(); glAttachShader(post_program_, post_vs); glAttachShader(post_program_, post_fs); glLinkProgram(post_program_);
        glDeleteShader(post_vs); glDeleteShader(post_fs);
        GLint post_ok{}; glGetProgramiv(post_program_, GL_LINK_STATUS, &post_ok);
        if (!post_ok) { glDeleteProgram(post_program_); glDeleteProgram(mesh_program_); glDeleteProgram(program_); post_program_ = mesh_program_ = program_ = 0; throw std::runtime_error("Post shader link failed"); }
        glUseProgram(post_program_);
        glUniform1i(glGetUniformLocation(post_program_, "scene"), 2);
        post_viewport_ = glGetUniformLocation(post_program_, "viewport"); post_rect_ = glGetUniformLocation(post_program_, "rect");
        post_mode_ = glGetUniformLocation(post_program_, "mode"); post_axis_ = glGetUniformLocation(post_program_, "axis");
        post_region_ = glGetUniformLocation(post_program_, "region"); post_blur_ = glGetUniformLocation(post_program_, "blur");
        post_blind_ = glGetUniformLocation(post_program_, "blind"); post_alpha_ = glGetUniformLocation(post_program_, "alpha");
        post_sidebar_ = glGetUniformLocation(post_program_, "sidebar");
        glGenVertexArrays(1, &post_vao_);
    }
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
    glDeleteBuffers(1, &mesh_buffer_); glDeleteVertexArrays(1, &mesh_vao_); glDeleteProgram(mesh_program_);
    glDeleteFramebuffers(2, post_buffers_.data()); glDeleteTextures(2, post_textures_.data());
    glDeleteVertexArrays(1, &post_vao_); glDeleteProgram(post_program_);
}
// The offscreen world and the blur's intermediate share one size; reallocated only on resize.
void Renderer::ensure_post_targets(int width, int height) {
    if (post_textures_[0] && post_width_ == width && post_height_ == height) return;
    if (!post_textures_[0]) { glGenTextures(2, post_textures_.data()); glGenFramebuffers(2, post_buffers_.data()); }
    for (int i = 0; i < 2; ++i) {
        glBindTexture(GL_TEXTURE_2D, post_textures_[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, post_buffers_[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, post_textures_[i], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0); throw std::runtime_error("Post-effect framebuffer incomplete");
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    post_width_ = width; post_height_ = height;
}
void Renderer::run_post(int width, int height, int clip_width, const ScreenEffects& fx) {
    glDisable(GL_BLEND); glUseProgram(post_program_); glBindVertexArray(post_vao_);
    glUniform2f(post_viewport_, static_cast<float>(width), static_cast<float>(height));
    glUniform2f(post_region_, static_cast<float>(clip_width), static_cast<float>(height));
    glUniform1i(post_mode_, 0);
    const auto pass = [&](GLuint framebuffer, GLuint source, float dx, float dy, float blur, bool blind) {
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, source);
        glUniform4f(post_rect_, 0, 0, static_cast<float>(clip_width), static_cast<float>(height));
        glUniform2f(post_axis_, dx, dy); glUniform1f(post_blur_, blur); glUniform1i(post_blind_, blind);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4); ++draw_calls_;
    };
    if (fx.blur > 0) {
        pass(post_buffers_[1], post_textures_[0], 1, 0, fx.blur, false);
        pass(0, post_textures_[1], 0, 1, fx.blur, fx.blind);
    } else pass(0, post_textures_[0], 0, 0, 0, fx.blind);
    glActiveTexture(GL_TEXTURE0); glEnable(GL_BLEND);
}
// Breath vignette first, then the sidebar strip over it, as in Map.as's child order.
void Renderer::run_screen_overlays(int width, int height, int clip_width, const ScreenEffects& fx) {
    glUseProgram(post_program_); glBindVertexArray(post_vao_);
    glUniform2f(post_viewport_, static_cast<float>(width), static_cast<float>(height));
    glUniform2f(post_region_, static_cast<float>(clip_width), static_cast<float>(height));
    if (fx.vignette > 0) {
        glUniform1i(post_mode_, 1); glUniform1f(post_alpha_, fx.vignette);
        glUniform4f(post_rect_, 0, 0, static_cast<float>(clip_width), static_cast<float>(height));
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4); ++draw_calls_;
    }
    if (fx.sidebar > 0 && fx.sidebar <= static_cast<float>(clip_width)) {
        glUniform1i(post_mode_, 2); glUniform1f(post_sidebar_, fx.sidebar);
        glUniform4f(post_rect_, static_cast<float>(clip_width) - fx.sidebar, 0, fx.sidebar, static_cast<float>(height));
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4); ++draw_calls_;
    }
}
void Renderer::draw(const Atlas& atlas, std::span<const Quad> quads, int width, int height, int clip_width,std::span<const Quad> overlay,const ScreenEffects& effects) {
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
    glClearColor(clear_color_[0],clear_color_[1],clear_color_[2],clear_color_[3]); glClear(GL_COLOR_BUFFER_BIT);
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
    glUniform1i(glGetUniformLocation(program_,"surface"),1);
    const auto standalone=glGetUniformLocation(program_,"standalone");
    draw_calls_=0;
    if(effects.filtered()) {
        // Flash filters only the map sprite: render the world offscreen, then resolve it filtered.
        ensure_post_targets(width,height);
        glBindFramebuffer(GL_FRAMEBUFFER,post_buffers_[0]);
        glDisable(GL_SCISSOR_TEST);glClear(GL_COLOR_BUFFER_BIT);glEnable(GL_SCISSOR_TEST);
    }
    const auto draw=[&](std::span<const Quad> list,std::size_t offset) {
        for(std::size_t first=0;first<list.size();) {
            if(list[first].kind>8.5F) {
                // Projected models share the painter-ordered stream but not the instanced program.
                if(const auto* mesh=static_cast<const MeshDraw*>(list[first].owner.get()); mesh&&!mesh->vertices.empty()) {
                    glUseProgram(mesh_program_);glBindVertexArray(mesh_vao_);glBindBuffer(GL_ARRAY_BUFFER,mesh_buffer_);
                    glUniform2f(mesh_viewport_,static_cast<float>(width),static_cast<float>(height));
                    glUniform4f(mesh_bounds_,mesh->bounds[0],mesh->bounds[1],mesh->bounds[2],mesh->bounds[3]);
                    glUniform1f(mesh_page_,mesh->page);glUniform3f(mesh_color_,mesh->color[0],mesh->color[1],mesh->color[2]);
                    glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(mesh->vertices.size()*sizeof(MeshVertex)),mesh->vertices.data(),GL_STREAM_DRAW);
                    for(const auto& group:mesh->groups) {
                        glUniform1i(mesh_textured_,group.solid?0:1);
                        glDrawArrays(GL_TRIANGLES,static_cast<GLint>(group.first),static_cast<GLsizei>(group.count));++draw_calls_;
                    }
                    glUseProgram(program_);glBindVertexArray(vao_);glBindBuffer(GL_ARRAY_BUFFER,buffers_[frame_%buffers_.size()]);
                }
                ++first;continue;
            }
            const auto texture=list[first].texture;std::size_t end=first+1;
            while(end<list.size()&&list[end].texture==texture&&list[end].kind<=8.5F)++end;
            glUniform1i(standalone,texture!=0);
            if(texture) { glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,texture);glActiveTexture(GL_TEXTURE0); }
            for(GLuint i=0;i<sizes.size();++i)glVertexAttribPointer(i,sizes[i],GL_FLOAT,GL_FALSE,sizeof(Quad),reinterpret_cast<void*>(offset+first*sizeof(Quad)+offsets[i]));
            glDrawArraysInstanced(GL_TRIANGLE_STRIP,0,4,static_cast<GLsizei>(end-first));++draw_calls_;first=end;
        }
    };
    draw(quads,0);
    const bool screen_overlays=effects.vignette>0||effects.sidebar>0;
    if(effects.filtered()) {
        run_post(width,height,clip_width,effects);
        glUseProgram(program_);glBindVertexArray(vao_);glBindBuffer(GL_ARRAY_BUFFER,buffers_[frame_%buffers_.size()]);
    }
    if(screen_overlays) {
        run_screen_overlays(width,height,clip_width,effects);
        glUseProgram(program_);glBindVertexArray(vao_);glBindBuffer(GL_ARRAY_BUFFER,buffers_[frame_%buffers_.size()]);
    }
    if(!overlay.empty()) {
        glScissor(0,0,width,height);
        draw(overlay,quads.size_bytes());
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
