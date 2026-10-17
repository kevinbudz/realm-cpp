#include <GL/glew.h>
#include "realm/stage3D/Renderer.hpp"
#include "realm/engine3d/Model3D.hpp"
#include "RendererGpuTimers.hpp"
#include "RendererPostProcess.hpp"
#include "RendererShader.hpp"
#include "RendererStream.hpp"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <stdexcept>

namespace realm {
GraphicsInfo graphics_info() {
  return {reinterpret_cast<const char*>(glGetString(GL_VENDOR)),
          reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
          reinterpret_cast<const char*>(glGetString(GL_VERSION))};
}

void check_graphics_error() {
  if (glGetError() != GL_NO_ERROR) {
    throw std::runtime_error("OpenGL error during frame");
  }
}

namespace {
// The instance attribute layout of a Quad, shared by every pass that draws quad lists.
const std::array<GLint, 9> quad_sizes{2, 2, 2, 4, 1, 4, 4, 1, 2};
const std::array<std::size_t, 9> quad_offsets{
    offsetof(Quad, origin), offsetof(Quad, axis_x), offsetof(Quad, axis_y),
    offsetof(Quad, uv),     offsetof(Quad, page),   offsetof(Quad, multiply),
    offsetof(Quad, offset), offsetof(Quad, kind),   offsetof(Quad, uv_offset)};
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
uniform bool groundSpace;
uniform vec2 groundOrigin, groundX, groundY;
uniform float groundTime;
// Runs holding wall instances draw triangle lists: six vertices per quad, in strip order.
uniform bool triangleList;
// Wall corners of a WallGeometryStore: two RGBA32F texels each (x y z u, then v shade flags pad).
uniform samplerBuffer geometry;
const int stripCorner[6] = int[6](0, 1, 2, 2, 1, 3);
out vec2 local;
flat out vec4 bounds;
flat out float layer;
flat out vec4 tint;
flat out vec4 bias;
flat out float mode;
flat out vec2 scroll;
void main() {
    bounds = spriteUV; layer = page; tint = multiply; bias = offset; mode = kind; scroll = uvOffset;
    if (kind > 16.5 && kind < 17.5) {
        // Wall instance: multiply = (first corner, corner count, neighbor black mask, unused). A triangle is kept when
        // its screen determinant (b - a) x (c - a) is positive, the winding the software path culls by. Its three
        // corners reach the same verdict, so the triangle is kept or clipped whole.
        int vertex = gl_VertexID;
        if (vertex >= int(multiply.y)) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); return; }
        int first = (int(multiply.x) + vertex / 3 * 3) * 2;
        vec4 g0 = texelFetch(geometry, first), h0 = texelFetch(geometry, first + 1);
        vec4 g1 = texelFetch(geometry, first + 2), h1 = texelFetch(geometry, first + 3);
        vec4 g2 = texelFetch(geometry, first + 4), h2 = texelFetch(geometry, first + 5);
        vec2 p0 = origin + axisX * g0.x + axisY * g0.y + uvOffset * g0.z;
        vec2 p1 = origin + axisX * g1.x + axisY * g1.y + uvOffset * g1.z;
        vec2 p2 = origin + axisX * g2.x + axisY * g2.y + uvOffset * g2.z;
        float det = (p1.x - p0.x) * (p2.y - p0.y) - (p1.y - p0.y) * (p2.x - p0.x);
        if (det <= 0.0) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); return; }
        int corner = vertex % 3;
        vec4 g = corner == 0 ? g0 : corner == 1 ? g1 : g2;
        vec2 pixel = corner == 0 ? p0 : corner == 1 ? p1 : p2;
        gl_Position = vec4(pixel.x / viewport.x * 2.0 - 1.0, 1.0 - pixel.y / viewport.y * 2.0, 0.0, 1.0);
        // flags = repeat + 2 * slot, slot 4 meaning no neighbor test; the mask says which neighbor slots are black.
        // Every corner of a triangle belongs to one polygon, so corner 0 holds the flags and shade of the triangle.
        int flags = int(h0.z);
        int slot = flags / 2;
        bool solid = slot != 4 && ((int(multiply.z) >> slot) & 1) != 0;
        local = vec2(g.w, corner == 0 ? h0.x : corner == 1 ? h1.x : h2.x);
        tint = vec4(h0.y, 0.0, 0.0, 1.0);
        scroll = vec2(solid ? 0.0 : 1.0, (flags & 1) != 0 ? 1.0 : 0.0);
        bias = vec4(0.0);
        mode = 17.0;
        return;
    }
    if (triangleList) {
        if (gl_VertexID >= 6) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); return; }
        int corner = stripCorner[gl_VertexID];
        local = vec2(corner & 1, (corner >> 1) & 1);
    } else {
        local = vec2(gl_VertexID & 1, (gl_VertexID >> 1) & 1);
    }
    vec2 pixel = origin + axisX * local.x + axisY * local.y;
    gl_Position = vec4(pixel.x / viewport.x * 2.0 - 1.0, 1.0 - pixel.y / viewport.y * 2.0, 0.0, 1.0);
    if (groundSpace) {
        pixel = groundOrigin + groundX * pixel.x + groundY * pixel.y;
        gl_Position = vec4(pixel.x / viewport.x * 2.0 - 1.0, 1.0 - pixel.y / viewport.y * 2.0, 0.0, 1.0);
        if (kind > 10.5 && kind < 12.5) {
            scroll = vec2(offset.w) + (kind < 11.5 ? uvOffset * groundTime : sin(uvOffset * groundTime));
            bias = vec4(0); mode = 3.0;
        }
    }
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
uniform bool clipped;
uniform vec4 clipRect;
uniform float clipCut;
uniform vec2 viewport;
out vec4 color;
void main() {
    vec2 screen = vec2(gl_FragCoord.x, viewport.y - gl_FragCoord.y);
    if (clipped && (any(lessThan(screen,clipRect.xy)) || any(greaterThanEqual(screen,clipRect.xy+clipRect.zw)))) discard;
    // Cut corners: each corner of the clip square loses a clipCut-sized triangle (device px).
    if (clipped) {
        vec2 inset = min(screen - clipRect.xy, clipRect.xy + clipRect.zw - screen);
        if (inset.x + inset.y < clipCut) discard;
    }
    if (mode > 16.5 && mode < 17.5) {
        // Wall corner (vertex_shader): solid faces are black; textured faces sample their sprite as the mesh program
        // does.
        if (scroll.x < 0.5) { color = vec4(0.0, 0.0, 0.0, 1.0); return; }
        vec2 page = vec2(textureSize(sprites, 0).xy);
        vec2 size = bounds.zw * page;
        vec2 pixels = floor(local * size);
        pixels = scroll.y > 0.5 ? mod(pixels, size) : clamp(pixels, vec2(0.0), size - 1.0);
        vec2 coords = (bounds.xy * page + pixels + 0.5) / page;
        vec4 texel = texture(sprites, vec3(coords, layer));
        color = vec4(texel.rgb * tint.x, texel.a);
        return;
    }
    if (mode > 12.5 && mode < 13.5) {
        vec2 uv = bounds.xy + local * bounds.zw;
        color = any(lessThan(uv,vec2(0))) || any(greaterThanEqual(uv,vec2(1)))
            ? vec4(27.0/255.0,27.0/255.0,27.0/255.0,1) : texture(surface,uv);
        return;
    }
    if (mode > 14.5 && mode < 15.5) {
        // Marker coverage at one logical pixel per drawable pixel uses four subpixel steps.
        vec2 p = screen;
        vec2 coverage = clamp(min(bias.xy+vec2(4)*scroll.x,floor(p)+vec2(1))-max(bias.xy,floor(p)),0.0,1.0);
        if (scroll.x == 1.0) coverage = floor(coverage*4.0+0.5)/4.0;
        color = vec4(tint.rgb,coverage.x*coverage.y); return;
    }
    if (mode > 15.5 && mode < 16.5) {
        vec2 d=(local-0.5)*32.0;
        vec2 uv=vec2((scroll.x*d.x+scroll.y*d.y)/8.0+0.5,(-scroll.y*d.x+scroll.x*d.y)/32.0+0.625);
        if(any(lessThan(uv,vec2(0)))||any(greaterThanEqual(uv,vec2(1)))) { color=vec4(0);return; }
        color=texture(surface,uv);if(color.a>0.0)color.rgb/=color.a;return;
    }
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
    // Bitmaps in the atlas' dynamic pages clamp to their own region like sprites (the neighbouring slot must never show);
    // mode 4 stays unclamped, and a standalone surface is bounded by the [0,1] test instead.
    if (mode < 3.5 || (mode > 4.5 && !standalone)) uv = clamp(uv, bounds.xy + halfTexel, bounds.xy + bounds.zw - halfTexel);
    if(standalone && mode > 5.5 && (any(lessThan(uv,vec2(0)))||any(greaterThan(uv,vec2(1))))) { color=vec4(0);return; }
    vec4 texel = standalone ? texture(surface, uv) : texture(sprites, vec3(uv, layer));
    // UI is composited by the regular display list, which uses the
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
layout(location=3) in vec4 spriteUV;
layout(location=4) in float page;
layout(location=5) in vec3 meshColor;
layout(location=6) in float texturedFlag;
layout(location=7) in float repeatFlag;
uniform vec2 viewport;
out vec2 uv;
out float lit;
flat out vec4 bounds;
flat out float layer;
flat out vec3 flatColor;
flat out float textured;
flat out float repeatTexture;
void main() {
    gl_Position = vec4(position.x / viewport.x * 2.0 - 1.0, 1.0 - position.y / viewport.y * 2.0, 0.0, 1.0);
    uv = texcoord; lit = shade;
    bounds = spriteUV; layer = page; flatColor = meshColor; textured = texturedFlag; repeatTexture = repeatFlag;
}
)glsl";
// Reference model program (prepared MeshDraw owners, see project_model): textured groups sample the
// object sprite (nearest, repeated) times the face shade; Solid* groups are the flat object color
// times it.
constexpr auto mesh_fragment_shader = R"glsl(#version 330 core
in vec2 uv;
in float lit;
flat in vec4 bounds;
flat in float layer;
flat in vec3 flatColor;
flat in float textured;
flat in float repeatTexture;
uniform sampler2DArray sprites;
out vec4 color;
void main() {
    if (!(textured > 0.5)) { color = vec4(flatColor * lit, 1.0); return; }
    // Nearest sampling with the sprite repeated over the face, as a tiled bitmap fill.
    vec2 page = vec2(textureSize(sprites, 0).xy);
    vec2 size = bounds.zw * page;
    vec2 pixels = floor(uv * size);
    pixels = repeatTexture > 0.5 ? mod(pixels, size) : clamp(pixels, vec2(0.0), size - 1.0);
    vec2 coords = (bounds.xy * page + pixels + 0.5) / page;
    vec4 texel = texture(sprites, vec3(coords, layer));
    color = vec4(texel.rgb * lit, texel.a);
}
)glsl";

// Instanced registered models: persistent model-space triangles (model_geometry) with
// one painter-stream Quad per placed model as instance data (model_quad). The affine
// camera/object transform, flat shade and material choice match project_model;
// GL back-face culling (front = counter-clockwise in window space) applies the same
// screen winding rule to the stored fan order.
constexpr auto model_vertex_shader = R"glsl(#version 330 core
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 texcoord;
layout(location=3) in float solid;
layout(location=4) in vec2 origin;
layout(location=5) in vec2 axisX;
layout(location=6) in vec2 axisY;
layout(location=7) in vec2 axisZ;
layout(location=8) in vec4 spriteUV;
layout(location=9) in float page;
layout(location=10) in vec4 multiply;
layout(location=11) in vec4 rotation;
uniform vec2 viewport;
out vec2 uv;
flat out float lit;
flat out vec4 bounds;
flat out float layer;
flat out vec3 flatColor;
flat out float textured;
void main() {
    vec2 pixel = origin + axisX * position.x + axisY * position.y + axisZ * position.z;
    gl_Position = vec4(pixel.x / viewport.x * 2.0 - 1.0, 1.0 - pixel.y / viewport.y * 2.0, 0.0, 1.0);
    // Lighting3D.LIGHT_VECTOR = normalize(1, 3, 2), in world space after the object rotation.
    vec3 n = vec3(normal.x * rotation.x - normal.y * rotation.y, normal.x * rotation.y + normal.y * rotation.x, normal.z);
    lit = 0.75 + 0.25 * max(0.0, dot(n, vec3(1.0, 3.0, 2.0) / 3.7416573867739413));
    uv = texcoord; bounds = spriteUV; layer = page; flatColor = multiply.rgb;
    textured = solid < 0.5 && multiply.a > 0.5 ? 1.0 : 0.0;
}
)glsl";
constexpr auto model_fragment_shader = R"glsl(#version 330 core
in vec2 uv;
flat in float lit;
flat in vec4 bounds;
flat in float layer;
flat in vec3 flatColor;
flat in float textured;
uniform sampler2DArray sprites;
out vec4 color;
void main() {
    if (textured < 0.5) { color = vec4(flatColor * lit, 1.0); return; }
    vec2 page = vec2(textureSize(sprites, 0).xy);
    vec2 size = bounds.zw * page;
    vec2 coords = (bounds.xy * page + mod(floor(uv * size), size) + 0.5) / page;
    vec4 texel = texture(sprites, vec3(coords, layer));
    color = vec4(texel.rgb * lit, texel.a);
}
)glsl";

std::array<float, 2> floats(Vec2 p) {
  return {static_cast<float>(p.x), static_cast<float>(p.y)};
}
}

SurfaceTexture::~SurfaceTexture() {
  if (texture_) {
    glDeleteTextures(1, &texture_);
  }
}

void SurfaceTexture::update(const Image& image, bool premultiplied) {
  update_region(image, 0, 0, image.width, image.height, premultiplied);
}

void SurfaceTexture::update_region(const Image& image, int x, int y, int w, int h,
                                   bool premultiplied) {
  REALM_ZONE("render.texture_upload");
  if (image.width < 1 || image.height < 1 ||
      image.pixels.size() !=
          static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4 ||
      x < 0 || y < 0 || w < 1 || h < 1 || x + w > image.width || y + h > image.height) {
    throw std::invalid_argument("Invalid UI texture upload");
  }

  if (!texture_limit_) {
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &texture_limit_);
  }
  const int limit = texture_limit_;
  if (image.width > limit || image.height > limit || reserved_width_ > limit ||
      reserved_height_ > limit) {
    throw std::runtime_error("UI surface exceeds GPU texture limit");
  }

  const bool created = !texture_;
  if (created) {
    glGenTextures(1, &texture_);
  }
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, texture_);
  if (created) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }

  const bool resized = width_ != image.width || height_ != image.height;
  if (resized) {
    width_ = image.width;
    height_ = image.height;
    x = y = 0;
    w = width_;
    h = height_;
  }

  if ((!retain_capacity_ && resized) || std::max(width_, reserved_width_) > storage_width_ ||
      std::max(height_, reserved_height_) > storage_height_) {
    const auto capacity = [&](int size) {
      int result = 1;
      while (result < size) {
        result *= 2;
      }
      return std::min(result, limit);
    };
    storage_width_ = retain_capacity_ ? capacity(std::max(width_, reserved_width_))
                                      : std::max(width_, reserved_width_);
    storage_height_ = retain_capacity_ ? capacity(std::max(height_, reserved_height_))
                                       : std::max(height_, reserved_height_);
    telemetry::add(telemetry::Counter::texture_storage_bytes,
                   static_cast<std::uint64_t>(storage_width_) *
                       static_cast<std::uint64_t>(storage_height_) * 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, storage_width_, storage_height_, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
  }

  telemetry::add(telemetry::Counter::texture_upload_bytes,
                 static_cast<std::uint64_t>(w) * static_cast<std::uint64_t>(h) * 4);
  telemetry::add(telemetry::Counter::ui_native_texture_uploads);
  telemetry::add(telemetry::Counter::ui_native_texture_upload_bytes,
                 static_cast<std::uint64_t>(w) * static_cast<std::uint64_t>(h) * 4);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  // Already-premultiplied complete rows can go straight to GL; its upload consumes client memory
  // before returning.
  if (premultiplied && x == 0 && w == image.width) {
    glTexSubImage2D(
        GL_TEXTURE_2D, 0, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE,
        image.pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 4);
    glActiveTexture(GL_TEXTURE0);
    return;
  }
  // Share scratch storage across uploads instead of retaining a second image in every cached
  // surface.
  static thread_local Image pixels;
  pixels.width = w;
  pixels.height = h;
  pixels.pixels.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
  for (int row = 0; row < h; ++row) {
    std::copy_n(
        image.pixels.data() +
            (static_cast<std::size_t>(row + y) * static_cast<std::size_t>(image.width) +
             static_cast<std::size_t>(x)) *
                4,
        static_cast<std::size_t>(w) * 4,
        pixels.pixels.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(w) * 4);
  }
  if (!premultiplied) {
    premultiply_flash_bitmap(pixels);
  }
  glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.pixels.data());
  glActiveTexture(GL_TEXTURE0);
}

Quad SurfaceTexture::quad(Vec2 origin, Vec2 ax, Vec2 ay, int x, int y, int w, int h) const {
  if (!texture_) {
    throw std::logic_error("UI surface was not uploaded");
  }
  if (w == 0) {
    w = width_;
  }
  if (h == 0) {
    h = height_;
  }
  Quad q;
  q.origin = floats(origin);
  q.axis_x = floats(ax);
  q.axis_y = floats(ay);
  q.texture = texture_;
  q.kind = 5;
  const auto width = static_cast<float>(storage_width_);
  const auto height = static_cast<float>(storage_height_);
  q.uv = {static_cast<float>(x) / width, static_cast<float>(y) / height,
          static_cast<float>(w) / width, static_cast<float>(h) / height};
  return q;
}

TerrainBuffer::TerrainBuffer(std::span<const Quad> quads) : count_(quads.size()) {
  glGenBuffers(1, &buffer_);
  glBindBuffer(GL_ARRAY_BUFFER, buffer_);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(quads.size_bytes()), quads.data(),
               GL_STATIC_DRAW);
  telemetry::add(telemetry::Counter::quad_upload_bytes, quads.size_bytes());
}

TerrainBuffer::~TerrainBuffer() {
  if (buffer_) {
    glDeleteBuffers(1, &buffer_);
  }
}

QuadLayer::~QuadLayer() {
  if (buffer) {
    glDeleteBuffers(1, &buffer);
  }
}

Renderer::Renderer(std::size_t capacity, bool persistent_streaming,
                   std::size_t persistent_minimum_bytes) {
  try {
    if (!capacity || capacity > 1000000) {
      throw std::invalid_argument("Invalid quad capacity");
    }

    const auto vs = renderer_detail::compile_shader(GL_VERTEX_SHADER, vertex_shader);
    GLuint fs{};
    try {
      fs = renderer_detail::compile_shader(GL_FRAGMENT_SHADER, fragment_shader);
    } catch (...) {
      glDeleteShader(vs);
      throw;
    }
    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok{};
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) {
      glDeleteProgram(program_);
      program_ = 0;
      throw std::runtime_error("Shader link failed");
    }

    {
      const auto mesh_vs = renderer_detail::compile_shader(GL_VERTEX_SHADER, mesh_vertex_shader);
      GLuint mesh_fs{};
      try {
        mesh_fs = renderer_detail::compile_shader(GL_FRAGMENT_SHADER, mesh_fragment_shader);
      } catch (...) {
        glDeleteShader(mesh_vs);
        throw;
      }
      mesh_program_ = glCreateProgram();
      glAttachShader(mesh_program_, mesh_vs);
      glAttachShader(mesh_program_, mesh_fs);
      glLinkProgram(mesh_program_);
      glDeleteShader(mesh_vs);
      glDeleteShader(mesh_fs);
      GLint mesh_ok{};
      glGetProgramiv(mesh_program_, GL_LINK_STATUS, &mesh_ok);
      if (!mesh_ok) {
        glDeleteProgram(mesh_program_);
        glDeleteProgram(program_);
        program_ = mesh_program_ = 0;
        throw std::runtime_error("Mesh shader link failed");
      }
      glUseProgram(mesh_program_);
      glUniform1i(glGetUniformLocation(mesh_program_, "sprites"), 0);
      mesh_viewport_ = glGetUniformLocation(mesh_program_, "viewport");
      glGenVertexArrays(1, &mesh_vao_);
      glBindVertexArray(mesh_vao_);
      glGenBuffers(1, &mesh_buffer_);
      glBindBuffer(GL_ARRAY_BUFFER, mesh_buffer_);
      const std::array<std::pair<GLint, std::size_t>, 8> mesh_attributes{
          {{2, offsetof(MeshBatchVertex, x)},
           {2, offsetof(MeshBatchVertex, u)},
           {1, offsetof(MeshBatchVertex, shade)},
           {4, offsetof(MeshBatchVertex, bounds)},
           {1, offsetof(MeshBatchVertex, page)},
           {3, offsetof(MeshBatchVertex, color)},
           {1, offsetof(MeshBatchVertex, textured)},
           {1, offsetof(MeshBatchVertex, repeat)}}};
      for (GLuint i = 0; i < mesh_attributes.size(); ++i) {
        glEnableVertexAttribArray(i);
        glVertexAttribPointer(i, mesh_attributes[i].first, GL_FLOAT, GL_FALSE,
                              sizeof(MeshBatchVertex),
                              reinterpret_cast<void*>(mesh_attributes[i].second));
      }
      glBindVertexArray(0);
    }

    {
      const auto model_vs = renderer_detail::compile_shader(GL_VERTEX_SHADER, model_vertex_shader);
      GLuint model_fs{};
      try {
        model_fs = renderer_detail::compile_shader(GL_FRAGMENT_SHADER, model_fragment_shader);
      } catch (...) {
        glDeleteShader(model_vs);
        throw;
      }
      model_program_ = glCreateProgram();
      glAttachShader(model_program_, model_vs);
      glAttachShader(model_program_, model_fs);
      glLinkProgram(model_program_);
      glDeleteShader(model_vs);
      glDeleteShader(model_fs);
      GLint model_ok{};
      glGetProgramiv(model_program_, GL_LINK_STATUS, &model_ok);
      if (!model_ok) {
        glDeleteProgram(model_program_);
        glDeleteProgram(mesh_program_);
        glDeleteProgram(program_);
        model_program_ = mesh_program_ = program_ = 0;
        throw std::runtime_error("Model shader link failed");
      }
      glUseProgram(model_program_);
      glUniform1i(glGetUniformLocation(model_program_, "sprites"), 0);
      model_viewport_ = glGetUniformLocation(model_program_, "viewport");
      // Uploaded once; instances only reference ranges of it.
      const auto geometry = model_geometry();
      glGenVertexArrays(1, &model_vao_);
      glBindVertexArray(model_vao_);
      glGenBuffers(1, &model_geometry_);
      glBindBuffer(GL_ARRAY_BUFFER, model_geometry_);
      glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(geometry.size_bytes()), geometry.data(),
                   GL_STATIC_DRAW);
      telemetry::add(telemetry::Counter::mesh_upload_bytes, geometry.size_bytes());
      const std::array<std::pair<GLint, std::size_t>, 4> attributes{
          {{3, offsetof(ModelVertex, x)},
           {3, offsetof(ModelVertex, nx)},
           {2, offsetof(ModelVertex, u)},
           {1, offsetof(ModelVertex, solid)}}};
      for (GLuint i = 0; i < attributes.size(); ++i) {
        glEnableVertexAttribArray(i);
        glVertexAttribPointer(i, attributes[i].first, GL_FLOAT, GL_FALSE, sizeof(ModelVertex),
                              reinterpret_cast<void*>(attributes[i].second));
      }
      for (GLuint i = 4; i < 12; ++i) {
        glEnableVertexAttribArray(i);
        glVertexAttribDivisor(i, 1);
      }
      glBindVertexArray(0);
    }

    glGenBuffers(1, &wall_geometry_);
    glGenTextures(1, &wall_texture_);
    glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &wall_texel_limit_);
    post_process_ = std::make_unique<RendererPostProcess>();
    gpu_timers_ = std::make_unique<RendererGpuTimers>();
    glUseProgram(program_);
    glUniform1i(glGetUniformLocation(program_, "sprites"), 0);
    viewport_location_ = glGetUniformLocation(program_, "viewport");
    standalone_location_ = glGetUniformLocation(program_, "standalone");
    surface_location_ = glGetUniformLocation(program_, "surface");
    ground_location_ = glGetUniformLocation(program_, "groundSpace");
    ground_origin_ = glGetUniformLocation(program_, "groundOrigin");
    ground_x_ = glGetUniformLocation(program_, "groundX");
    ground_y_ = glGetUniformLocation(program_, "groundY");
    ground_time_ = glGetUniformLocation(program_, "groundTime");
    clip_location_ = glGetUniformLocation(program_, "clipped");
    clip_rect_ = glGetUniformLocation(program_, "clipRect");
    clip_cut_ = glGetUniformLocation(program_, "clipCut");
    triangle_list_location_ = glGetUniformLocation(program_, "triangleList");
    glUniform1i(glGetUniformLocation(program_, "geometry"), 2);
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    stream_ =
        std::make_unique<RendererStream>(capacity, persistent_streaming, persistent_minimum_bytes);

    // Enables and divisors belong to this VAO and never change when RmlUi binds its own VAO.
    for (GLuint i = 0; i < quad_sizes.size(); ++i) {
      glEnableVertexAttribArray(i);
      glVertexAttribDivisor(i, 1);
    }
    base_instance_supported_ = GLEW_VERSION_4_2 || GLEW_ARB_base_instance;
    base_instance_ = base_instance_supported_;

    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_FRAMEBUFFER_SRGB);
  } catch (...) {
    release_gl_resources();
    throw;
  }
}

Renderer::~Renderer() {
  release_gl_resources();
}

void Renderer::release_gl_resources() noexcept {
  if (vao_) {
    glDeleteVertexArrays(1, &vao_);
  }
  if (program_) {
    glDeleteProgram(program_);
  }

  if (mesh_buffer_) {
    glDeleteBuffers(1, &mesh_buffer_);
  }
  if (mesh_vao_) {
    glDeleteVertexArrays(1, &mesh_vao_);
  }
  if (mesh_program_) {
    glDeleteProgram(mesh_program_);
  }

  if (model_geometry_) {
    glDeleteBuffers(1, &model_geometry_);
  }
  if (model_vao_) {
    glDeleteVertexArrays(1, &model_vao_);
  }
  if (model_program_) {
    glDeleteProgram(model_program_);
  }

  if (wall_geometry_) {
    glDeleteBuffers(1, &wall_geometry_);
  }
  if (wall_texture_) {
    glDeleteTextures(1, &wall_texture_);
  }

  vao_ = program_ = mesh_buffer_ = mesh_vao_ = mesh_program_ = model_geometry_ = model_vao_ =
      model_program_ = 0;
  wall_geometry_ = wall_texture_ = 0;
}

// Whether two quad lists hold the same instance attributes (the owner and texture of a quad do not
// reach the GPU, so they do not count).
bool same_instances(const std::vector<Quad>& uploaded, const std::vector<Quad>& quads) {
  if (uploaded.size() != quads.size()) {
    return false;
  }
  for (std::size_t i = 0; i < quads.size(); ++i) {
    for (std::size_t field = 0; field < quad_sizes.size(); ++field) {
      const auto bytes = static_cast<std::size_t>(quad_sizes[field]) * sizeof(float);
      if (std::memcmp(reinterpret_cast<const unsigned char*>(&uploaded[i]) + quad_offsets[field],
                      reinterpret_cast<const unsigned char*>(&quads[i]) + quad_offsets[field],
                      bytes) != 0) {
        return false;
      }
    }
  }
  return true;
}

// Points the quad program's instance attributes at a list starting offset bytes into the bound
// array buffer.
void Renderer::bind_quad_attributes(std::size_t offset) {
  for (GLuint i = 0; i < quad_sizes.size(); ++i) {
    glVertexAttribPointer(i, quad_sizes[i], GL_FLOAT, GL_FALSE, sizeof(Quad),
                          reinterpret_cast<void*>(offset + quad_offsets[i]));
  }
}

// Instances of a run start at its first quad. Without base instances the caller has already moved
// the pointers there.
void Renderer::draw_instanced(std::uint32_t mode, std::int32_t first_vertex, std::int32_t vertices,
                              std::int32_t instances, std::size_t first_instance) const {
  if (base_instance_) {
    glDrawArraysInstancedBaseInstance(mode, first_vertex, vertices, instances,
                                      static_cast<GLuint>(first_instance));
  } else {
    glDrawArraysInstanced(mode, first_vertex, vertices, instances);
  }
}

void Renderer::draw_batch(std::span<const Quad> list, std::size_t offset, int width, int height,
                          std::uint32_t buffer, bool measure_models) {
  const auto standalone = standalone_location_;
  // Whether the quad pointers of vao_ address this list in buffer; terrain and layers rebind them.
  bool quads_bound = false;
  // The standalone flag and the surface on texture unit 1 as this list last left them. Unknown at
  // entry (the caller and earlier passes may have changed them); a nested layer resets both.
  int standalone_value = -1;
  std::uint32_t surface_bound = ~std::uint32_t{};
  for (std::size_t first = 0; first < list.size();) {
    if (list[first].kind == 10) {
      const auto& command = list[first];
      const auto* terrain = static_cast<const TerrainBuffer*>(command.owner.get());
      if (!terrain) {
        throw std::logic_error("Missing terrain buffer");
      }
      glUniform1i(standalone, 0);
      standalone_value = 0;
      glUniform1i(ground_location_, 1);
      glUniform2fv(ground_origin_, 1, command.origin.data());
      glUniform2fv(ground_x_, 1, command.axis_x.data());
      glUniform2fv(ground_y_, 1, command.axis_y.data());
      glUniform1f(ground_time_, command.uv_offset[0]);
      glBindBuffer(GL_ARRAY_BUFFER, terrain->buffer());
      bind_quad_attributes(0);
      quads_bound = false;
      glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(terrain->count()));
      ++draw_calls_;
      telemetry::add(telemetry::Counter::quads, terrain->count());
      telemetry::add(telemetry::Counter::triangles, terrain->count() * 2);
      glUniform1i(ground_location_, 0);
      glBindBuffer(GL_ARRAY_BUFFER, buffer);
      ++first;
      continue;
    }
    if (list[first].kind == 14) {
      const auto& command = list[first];
      auto* layer = const_cast<QuadLayer*>(static_cast<const QuadLayer*>(command.owner.get()));
      if (!layer || layer->quads.size() > 1000000) {
        throw std::logic_error("Invalid quad layer");
      }
      if (!layer->buffer) {
        glGenBuffers(1, &layer->buffer);
      }
      glBindBuffer(GL_ARRAY_BUFFER, layer->buffer);
      // A layer that is unchanged since its last upload keeps the bytes already in the buffer.
      if (!same_instances(layer->uploaded, layer->quads)) {
        const auto bytes = static_cast<GLsizeiptr>(layer->quads.size() * sizeof(Quad));
        glBufferData(GL_ARRAY_BUFFER, bytes, layer->quads.data(), GL_STREAM_DRAW);
        telemetry::add(telemetry::Counter::quad_upload_bytes, static_cast<std::uint64_t>(bytes));
        layer->uploaded = layer->quads;
        for (auto& quad : layer->uploaded) {
          quad.owner.reset();
        }
      }
      // Every quad of the layer is clipped, walls (kind 17) included: the fragment test runs before
      // the kind branch.
      glUniform1i(clip_location_, 1);
      glUniform4f(clip_rect_, command.origin[0], command.origin[1], command.axis_x[0],
                  command.axis_y[1]);
      glUniform1f(clip_cut_, static_cast<float>(command.uv_offset[0]));
      draw_batch(layer->quads, 0, width, height, layer->buffer, false);
      quads_bound = false;
      standalone_value = -1;
      surface_bound = ~std::uint32_t{};
      glUniform1i(clip_location_, 0);
      glUniform1f(clip_cut_, 0);
      glBindBuffer(GL_ARRAY_BUFFER, buffer);
      ++first;
      continue;
    }
    if (list[first].kind == 9 && !list[first].owner) {
      // Model instances keep their painter-stream position. A contiguous block switches programs
      // once; consecutive copies of one model are one instanced draw (instances rasterize in
      // order).
      REALM_ZONE("render.model_submit");
      std::size_t end = first;
      while (end < list.size() && list[end].kind == 9 && !list[end].owner) {
        ++end;
      }
      glUseProgram(model_program_);
      glBindVertexArray(model_vao_);
      glBindBuffer(GL_ARRAY_BUFFER, buffer);
      glUniform2f(model_viewport_, static_cast<float>(width), static_cast<float>(height));
      glEnable(GL_CULL_FACE);
      glCullFace(GL_BACK);
      glFrontFace(GL_CCW);
      const std::array<std::pair<GLint, std::size_t>, 8> instance{{{2, offsetof(Quad, origin)},
                                                                   {2, offsetof(Quad, axis_x)},
                                                                   {2, offsetof(Quad, axis_y)},
                                                                   {2, offsetof(Quad, uv_offset)},
                                                                   {4, offsetof(Quad, uv)},
                                                                   {1, offsetof(Quad, page)},
                                                                   {4, offsetof(Quad, multiply)},
                                                                   {4, offsetof(Quad, offset)}}};
      // With base instances the pointers address the whole list once and each run starts at its own
      // quad.
      if (base_instance_) {
        for (GLuint i = 0; i < instance.size(); ++i) {
          glVertexAttribPointer(4 + i, instance[i].first, GL_FLOAT, GL_FALSE, sizeof(Quad),
                                reinterpret_cast<void*>(offset + instance[i].second));
        }
      }
      for (std::size_t run = first; run < end;) {
        const float model = list[run].offset[2];
        std::size_t next = run + 1;
        while (next < end && list[next].offset[2] == model) {
          ++next;
        }
        const auto range = model_range(static_cast<std::uint32_t>(model));
        const bool model_measure = measure_models && gpu_timers_->model_begin(next - run);
        if (!base_instance_) {
          for (GLuint i = 0; i < instance.size(); ++i) {
            glVertexAttribPointer(
                4 + i, instance[i].first, GL_FLOAT, GL_FALSE, sizeof(Quad),
                reinterpret_cast<void*>(offset + run * sizeof(Quad) + instance[i].second));
          }
        }
        draw_instanced(GL_TRIANGLES, static_cast<GLint>(range.first),
                       static_cast<GLsizei>(range.count), static_cast<GLsizei>(next - run), run);
        ++draw_calls_;
        telemetry::add(telemetry::Counter::models, next - run);
        telemetry::add(telemetry::Counter::triangles, (next - run) * (range.count / 3));
        if (measure_models) {
          gpu_timers_->model_end(model_measure);
        }
        run = next;
      }
      glDisable(GL_CULL_FACE);
      glUseProgram(program_);
      glBindVertexArray(vao_);
      glBindBuffer(GL_ARRAY_BUFFER, buffer);
      first = end;
      continue;
    }
    if (list[first].kind == 9) {
      // Reference path: prepared MeshDraw owners (project_model) draw from their spans of
      // mesh_buffer_, which collect_meshes filled in this same order. A run of consecutive meshes
      // is one draw over contiguous vertices.
      REALM_ZONE("render.mesh_submit");
      std::size_t end = first;
      while (end < list.size() && list[end].kind == 9 && list[end].owner) {
        ++end;
      }
      if (mesh_ranges_.size() - mesh_cursor_ < end - first) {
        throw std::logic_error("Mesh batch out of sync");
      }
      const auto run = std::span(mesh_ranges_).subspan(mesh_cursor_, end - first);
      mesh_cursor_ += end - first;
      std::size_t vertices{};
      std::size_t models{};
      for (const auto& range : run) {
        vertices += range.count;
        models += range.count != 0;
      }
      if (vertices) {
        const bool model_measure = measure_models && gpu_timers_->model_begin(models);
        telemetry::add(telemetry::Counter::models, models);
        telemetry::add(telemetry::Counter::triangles, vertices / 3);
        glUseProgram(mesh_program_);
        glBindVertexArray(mesh_vao_);
        glUniform2f(mesh_viewport_, static_cast<float>(width), static_cast<float>(height));
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(run.front().first),
                     static_cast<GLsizei>(vertices));
        ++draw_calls_;
        if (measure_models) {
          gpu_timers_->model_end(model_measure);
        }
        glUseProgram(program_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
      }
      first = end;
      continue;
    }
    const auto texture = list[first].texture;
    std::size_t end = first + 1;
    while (end < list.size() && list[end].texture == texture && list[end].kind != 9 &&
           list[end].kind != 10 && list[end].kind != 14) {
      ++end;
    }
    // Wall instances (kind 17) draw in the same program. A run holding any of them is a triangle
    // list, six vertices per instance: a wall's corners come from the store, and every other quad's
    // two triangles from its strip order. Attribution: walls are not model runs, so their GPU time
    // sits in the world bucket and models_ms excludes them. `models` counts wall instances,
    // `triangles` adds corners / 3 as submitted (back faces are culled in the shader). Each
    // instance uses max(6, max_vertices) vertices, so shorter walls and sprites carry padding.
    std::size_t walls{};
    GLsizei max_vertices = 0;
    std::uint64_t wall_triangles{};
    for (std::size_t i = first; i < end; ++i) {
      if (list[i].kind == 17) {
        const auto count = static_cast<GLsizei>(list[i].multiply[1]);
        ++walls;
        max_vertices = std::max(max_vertices, count);
        wall_triangles += static_cast<std::uint64_t>(count) / 3;
      }
    }
    const auto sprites_in_run = end - first - walls;
    if (standalone_value != (texture != 0)) {
      glUniform1i(standalone, texture != 0);
      standalone_value = texture != 0;
    }
    if (texture && texture != surface_bound) {
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, texture);
      glActiveTexture(GL_TEXTURE0);
      surface_bound = texture;
    }
    if (!base_instance_) {
      bind_quad_attributes(offset + first * sizeof(Quad));
    } else if (!quads_bound) {
      glBindBuffer(GL_ARRAY_BUFFER, buffer);
      bind_quad_attributes(offset);
      quads_bound = true;
    }
    telemetry::add(telemetry::Counter::quads, sprites_in_run);
    telemetry::add(telemetry::Counter::models, walls);
    telemetry::add(telemetry::Counter::triangles, 2 * sprites_in_run + wall_triangles);
    if (walls) {
      glUniform1i(triangle_list_location_, 1);
      draw_instanced(GL_TRIANGLES, 0, std::max<GLsizei>(6, max_vertices),
                     static_cast<GLsizei>(end - first), first);
      glUniform1i(triangle_list_location_, 0);
    } else {
      draw_instanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(end - first), first);
    }
    ++draw_calls_;
    first = end;
  }
}

void Renderer::draw(const Atlas& atlas, std::span<const Quad> quads, int width, int height,
                    int clip_width, std::span<const Quad> overlay, const ScreenEffects& effects) {
  const auto total = quads.size() + overlay.size();
  if (width <= 0 || height <= 0 || clip_width < 0 || clip_width > width || total > 1000000) {
    throw std::invalid_argument("Invalid draw dimensions or quad count");
  }

  stream_->ensure_capacity(total);
  REALM_ZONE("render.submit");
  poll_gpu();
  const bool measure = gpu_timers_->begin_frame();
  glViewport(0, 0, width, height);
  glDisable(GL_SCISSOR_TEST);
  glClearColor(clear_color_[0], clear_color_[1], clear_color_[2], clear_color_[3]);
  glClear(GL_COLOR_BUFFER_BIT);
  glEnable(GL_SCISSOR_TEST);
  glScissor(0, 0, clip_width, height);
  glUseProgram(program_);
  glUniform2f(viewport_location_, static_cast<float>(width), static_cast<float>(height));
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D_ARRAY, atlas.texture());
  glBindVertexArray(vao_);
  glUniform1i(ground_location_, 0);
  glUniform1i(clip_location_, 0);
  glUniform1f(clip_cut_, 0);

  upload_meshes(quads, overlay);
  bind_walls();
  const auto stream_buffer = stream_->upload(quads, overlay);
  if (measure) {
    gpu_timers_->phase(1);
  }

  glUniform1i(surface_location_, 1);
  draw_calls_ = 0;
  if (effects.filtered()) {
    // Only the map sprite is filtered: render the world offscreen, then resolve it filtered.
    post_process_->ensure_targets(width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, post_process_->framebuffer(0));
    glDisable(GL_SCISSOR_TEST);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
  }
  draw_batch(quads, 0, width, height, stream_buffer, measure);
  const bool screen_overlays = effects.vignette > 0 || effects.sidebar > 0;
  const bool overlay_filtered = effects.filtered() && effects.whole;
  // A whole-window filter takes in everything drawn over the map too, in the usual order: the
  // screen overlays, then the HUD overlay.
  if (overlay_filtered && screen_overlays) {
    draw_calls_ += post_process_->run_screen_overlays(width, height, clip_width, effects);
    glUseProgram(program_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, stream_buffer);
  }
  if (overlay_filtered && !overlay.empty()) {
    glScissor(0, 0, width, height);
    draw_batch(overlay, quads.size_bytes(), width, height, stream_buffer, measure);
  }
  if (measure) {
    gpu_timers_->phase(2);
  }

  if (effects.filtered()) {
    draw_calls_ +=
        post_process_->run_post(width, height, overlay_filtered ? width : clip_width, effects);
    glUseProgram(program_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, stream_buffer);
  }
  if (screen_overlays && !overlay_filtered) {
    draw_calls_ += post_process_->run_screen_overlays(width, height, clip_width, effects);
    glUseProgram(program_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, stream_buffer);
  }
  if (measure) {
    gpu_timers_->phase(3);
  }
  if (!overlay.empty() && !overlay_filtered) {
    glScissor(0, 0, width, height);
    draw_batch(overlay, quads.size_bytes(), width, height, stream_buffer, measure);
  }
  if (measure) {
    gpu_timers_->finish_sample();
  }

  telemetry::add(telemetry::Counter::draw_calls, draw_calls_);
  stream_->finish_frame(stream_buffer);

  glDisable(GL_SCISSOR_TEST);
  gpu_timers_->advance_frame();
}

// Mirrors draw_batch's visiting order exactly: a clipped layer is collected at its own stream
// position.
void Renderer::collect_meshes(std::span<const Quad> list) {
  for (const auto& quad : list) {
    if (quad.kind == 14) {
      if (const auto* layer = static_cast<const QuadLayer*>(quad.owner.get())) {
        collect_meshes(layer->quads);
      }
      continue;
    }
    if (quad.kind != 9 || !quad.owner) {
      continue;
    }
    const auto& mesh = *static_cast<const MeshDraw*>(quad.owner.get());
    const auto first = mesh_vertices_.size();
    for (const auto& group : mesh.groups) {
      // Whole triangles only: a run's single draw relies on every span starting on a triangle
      // boundary.
      const std::size_t begin = group.first;
      const std::size_t end = begin + (group.count - group.count % 3);
      if (end > mesh.vertices.size()) {
        throw std::logic_error("Invalid mesh group");
      }
      const float textured = group.solid ? 0.F : 1.F;
      const float repeat = group.repeat ? 1.F : 0.F;
      for (std::size_t i = begin; i < end; ++i) {
        const auto& v = mesh.vertices[i];
        mesh_vertices_.push_back(
            {v.x, v.y, v.u, v.v, v.shade, mesh.bounds, mesh.page, mesh.color, textured, repeat});
      }
    }
    mesh_ranges_.push_back({first, mesh_vertices_.size() - first});
  }
}

// The wall store's texture buffer stays on its texture unit for the whole submission (the main
// program samples it).
void Renderer::bind_walls() {
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_BUFFER, wall_texture_);
  glActiveTexture(GL_TEXTURE0);
}

// The one wall store a submission draws from, also inside layers. Invariant: one store per
// submission. Scene builds a frame's walls into one store and replaces it only at the start of a
// build, so a second store is a caller error (the texture buffer mirrors one store; a split would
// need one buffer per store and a run break per store).
void Renderer::find_walls(std::span<const Quad> list, const WallGeometryStore*& store) {
  for (const auto& quad : list) {
    if (quad.kind == 14) {
      if (const auto* layer = static_cast<const QuadLayer*>(quad.owner.get())) {
        find_walls(layer->quads, store);
      }
      continue;
    }
    if (quad.kind != 17) {
      continue;
    }
    const auto* found = static_cast<const WallGeometryStore*>(quad.owner.get());
    if (!found) {
      throw std::logic_error("Wall instance without its geometry store");
    }
    if (store && store != found) {
      throw std::logic_error("A submission draws walls from two geometry stores");
    }
    store = found;
  }
}

void Renderer::upload_walls(const WallGeometryStore& store) {
  const auto& corners = store.vertices();
  const auto count = corners.size();
  const auto generation = store.generation();
  const auto limit = static_cast<std::size_t>(wall_texel_limit_) / 2;  // two texels per corner
  if (count > limit) {
    throw std::runtime_error("Wall geometry exceeds the GPU texture buffer limit");
  }
  if (generation != wall_generation_ || count < wall_uploaded_) {
    wall_uploaded_ = 0;
  }
  if (generation == wall_generation_ && count == wall_uploaded_) {
    return;
  }
  REALM_ZONE("render.wall_upload");
  glBindBuffer(GL_TEXTURE_BUFFER, wall_geometry_);
  if (count > wall_capacity_) {
    wall_capacity_ = std::min(limit, std::max({count, 2 * wall_capacity_, std::size_t{4096}}));
    glBufferData(GL_TEXTURE_BUFFER, static_cast<GLsizeiptr>(wall_capacity_ * sizeof(WallGpuVertex)),
                 nullptr, GL_DYNAMIC_DRAW);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_BUFFER, wall_texture_);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, wall_geometry_);
    glActiveTexture(GL_TEXTURE0);
    wall_uploaded_ = 0;
  }
  if (count > wall_uploaded_) {
    const auto bytes = (count - wall_uploaded_) * sizeof(WallGpuVertex);
    glBufferSubData(GL_TEXTURE_BUFFER,
                    static_cast<GLintptr>(wall_uploaded_ * sizeof(WallGpuVertex)),
                    static_cast<GLsizeiptr>(bytes), corners.data() + wall_uploaded_);
    telemetry::add(telemetry::Counter::mesh_upload_bytes, bytes);
  }
  wall_generation_ = generation;
  wall_uploaded_ = count;
}

// One submission's meshes (quads, then overlay, both drawn in that order) share one upload into
// mesh_buffer_, and its wall instances' store is synced into the wall texture buffer.
void Renderer::upload_meshes(std::span<const Quad> quads, std::span<const Quad> overlay) {
  mesh_vertices_.clear();
  mesh_ranges_.clear();
  mesh_cursor_ = 0;
  collect_meshes(quads);
  collect_meshes(overlay);
  const WallGeometryStore* walls{};
  find_walls(quads, walls);
  find_walls(overlay, walls);
  if (walls) {
    upload_walls(*walls);
  }
  if (mesh_vertices_.empty()) {
    return;
  }
  REALM_ZONE("render.mesh_upload");
  const auto bytes = mesh_vertices_.size() * sizeof(MeshBatchVertex);
  glBindBuffer(GL_ARRAY_BUFFER, mesh_buffer_);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes), mesh_vertices_.data(),
               GL_STREAM_DRAW);
  telemetry::add(telemetry::Counter::mesh_upload_bytes, bytes);
}

// A later pass (RmlUi) leaves arbitrary GL state behind, so everything the quad program depends on
// is set again here.
void Renderer::draw_overlay(const Atlas& atlas, std::span<const Quad> overlay, int width,
                            int height) {
  if (width <= 0 || height <= 0 || overlay.size() > 1000000) {
    throw std::invalid_argument("Invalid draw dimensions or quad count");
  }

  if (overlay.empty()) {
    return;
  }
  REALM_ZONE("render.overlay_submit");
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, width, height);
  glEnable(GL_SCISSOR_TEST);
  glScissor(0, 0, width, height);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_STENCIL_TEST);
  glDisable(GL_FRAMEBUFFER_SRGB);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glEnable(GL_BLEND);
  glBlendEquation(GL_FUNC_ADD);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(program_);
  glUniform2f(viewport_location_, static_cast<float>(width), static_cast<float>(height));
  glUniform1i(surface_location_, 1);
  glUniform1i(ground_location_, 0);
  glUniform1i(clip_location_, 0);
  glUniform1f(clip_cut_, 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D_ARRAY, atlas.texture());
  glBindVertexArray(vao_);

  upload_meshes({}, overlay);
  bind_walls();
  const auto overlay_buffer = stream_->upload_overlay(overlay);

  const auto before = draw_calls_;
  draw_batch(overlay, 0, width, height, overlay_buffer, false);
  telemetry::add(telemetry::Counter::draw_calls, draw_calls_ - before);

  glDisable(GL_SCISSOR_TEST);
}

void Renderer::timing(telemetry::Recorder* recorder, std::uint64_t frame_id, bool normal) {
  gpu_timers_->timing(recorder, frame_id, normal);
}

void Renderer::poll_gpu() {
  gpu_timers_->poll();
}

std::size_t Renderer::pending_gpu() const {
  return gpu_timers_->pending();
}

std::size_t Renderer::skipped_gpu() const {
  return gpu_timers_->skipped();
}

double Renderer::gpu_ms() const {
  return gpu_timers_->gpu_ms();
}

bool Renderer::persistent_streaming() const {
  return stream_->persistent();
}

Image Renderer::capture(int width, int height, bool presented) const {
  Image image{width, height,
              std::vector<unsigned char>(static_cast<std::size_t>(width) *
                                         static_cast<std::size_t>(height) * 4)};
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadBuffer(presented ? GL_FRONT : GL_BACK);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, image.pixels.data());
  const auto stride = static_cast<std::size_t>(width) * 4;
  for (int y = 0; y < height / 2; ++y) {
    auto top =
        image.pixels.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) * stride);
    auto bottom = image.pixels.begin() +
                  static_cast<std::ptrdiff_t>(static_cast<std::size_t>(height - 1 - y) * stride);
    std::swap_ranges(top, top + static_cast<std::ptrdiff_t>(stride), bottom);
  }
  glReadBuffer(GL_BACK);
  return image;
}
}
