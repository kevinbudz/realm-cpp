#pragma once
// The Vulkan side of RmlUi (phase 3). RmlGpuVk.cpp drives it with plain data, so the UI library
// needs no Vulkan headers. Geometry is drawn into the window in a LOAD pass that the frame's world
// draw left in place. Uploads record copies, which cannot run inside a pass, so the pass closes
// before any texture transfer and reopens on the next draw.
#include <cstddef>
#include <cstdint>

namespace realm::gfx::vk::ui {
// Rml::Vertex: position, premultiplied RGBA8 colour, texture coordinate (20 bytes).
struct Vertex {
  float x;
  float y;
  std::uint8_t r;
  std::uint8_t g;
  std::uint8_t b;
  std::uint8_t a;
  float u;
  float v;
};

struct Geometry {
  const Vertex* vertices = nullptr;
  std::size_t vertex_count = 0;
  const std::uint32_t* indices = nullptr;  // triangle list
  std::size_t index_count = 0;
};

// RmlUi's clip masks (the stencil buffer, as RenderInterface_GL3): a draw tests the mask (equal to
// the reference), or writes it. Set and SetInverse clear the stencil first, then write 1 or 0 inside
// the geometry. Intersect increments the stencil inside the geometry.
enum class Clip : std::uint8_t { none, test, mask_set, mask_set_inverse, mask_intersect };

struct Draw {
  Geometry geometry;
  std::uint32_t texture = 0;  // 0: untextured
  bool textured = false;
  Clip clip = Clip::none;
  std::uint32_t stencil_reference = 0;  // Clip::test: the value the stencil must equal
  float transform[16] = {};  // column-major: window pixels (after translate) to clip space
  float translate[2] = {0, 0};
  bool scissor = false;
  int region[4] = {0, 0, 0, 0};  // x, y, width, height in window pixels (top-left origin)
};

// The window size of this frame's UI draws (set before the first draw of a frame).
void begin_frame(int width, int height);
void draw(const Draw& draw);
// Ends the frame's UI pass, if one is open.
void end_frame();

// Surfaces for RmlUi's textures. Linear sampling for file and generated textures, nearest for the
// HUD art. Returns 0 when the texture cannot be created. Uploads are RGBA8, tightly packed rows.
[[nodiscard]] std::uint32_t create_texture(int width, int height, bool linear);
void upload_texture(std::uint32_t texture, int x, int y, int width, int height, const std::byte* rgba);
void release_texture(std::uint32_t texture) noexcept;
[[nodiscard]] int max_texture_size();
}  // namespace realm::gfx::vk::ui
