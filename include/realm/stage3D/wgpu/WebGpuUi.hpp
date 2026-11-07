#pragma once
// The WebGPU side of RmlUi (phase W5b). RmlGpuWgpu.cpp drives it with plain data, so the UI library needs no
// WebGPU headers. The work happens in the frame's renderer (WgpuUi, owned by WgpuRenderer): the UI passes are
// recorded into the frame's encoder between the world (draw) and the HUD overlay (draw_overlay), as the Vulkan
// seam does. Textures written during a frame close the open UI pass and are copied in order.
#include <cstddef>
#include <cstdint>

namespace realm::gfx::wgpu::ui {
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

// RmlUi's clip masks (the stencil buffer, as RenderInterface_GL3): a draw tests the mask (equal to the
// reference), or writes it. Set and SetInverse clear the stencil inside the scissor first, then write 1 or 0
// inside the geometry. Intersect increments the stencil inside the geometry.
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

// The window size of this frame's UI draws. Opens the frame's UI state; draws before it are dropped.
void begin_frame(int width, int height);
void draw(const Draw& draw);
// Ends the frame's UI pass, if one is open.
void end_frame();

// RmlUi layers. push_layer clears a new layer and makes it the target of the draws that follow; pop_layer returns
// to the layer below. push_layer returns 0 when no layer can be made. Handle 0 is the window.
[[nodiscard]] std::uint32_t push_layer();
void pop_layer();
// A compiled filter of RmlUi: a blur (sigma in pixels) or an opacity (group opacity). A composite multiplies its
// opacities and applies the product once to the output; a blur leaves it at 1.
struct Filter {
  float sigma = 0;
  float opacity = 1;
};
// Composites a layer into a destination (0: the window) through the filters, inside the region (window pixels,
// top-left origin) when scissor is set. replace: no blending (RmlUi BlendMode::Replace).
struct Composite {
  std::uint32_t source = 0;
  std::uint32_t destination = 0;
  bool replace = false;
  bool scissor = false;
  int region[4] = {0, 0, 0, 0};
  const Filter* filters = nullptr;
  std::size_t filter_count = 0;
};
void composite(const Composite& composite);

// Surfaces for RmlUi's textures. Linear sampling for file and generated textures, nearest for the HUD art. Returns 0
// when the texture cannot be created. Uploads are RGBA8, tightly packed rows.
[[nodiscard]] std::uint32_t create_texture(int width, int height, bool linear);
void upload_texture(std::uint32_t texture, int x, int y, int width, int height, const std::byte* rgba);
void release_texture(std::uint32_t texture) noexcept;
[[nodiscard]] int max_texture_size();
}  // namespace realm::gfx::wgpu::ui
