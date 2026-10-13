#pragma once
#include "realm/map/Camera.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace realm {
inline constexpr int sprite_page_size = 2048;
struct SpriteRegion {
  int page{}, x{}, y{}, width{}, height{};
};

// Affine quads support billboards, rotated terrain, shadows, and HUD rectangles.
// Stream order is painter order. Atlas pages, tints, and shadows do not reorder it.
struct Quad {
  std::array<float, 2> origin{}, axis_x{}, axis_y{};
  std::array<float, 4> uv{};
  float page{};
  std::array<float, 4> multiply{1, 1, 1, 1}, offset{};
  float kind{};  // 0 sprite, 1 shadow, 2 solid, 3 repeat, 4 crop, 5 UI bitmap, 6 world bitmap, 7 particle square, 8
                 // thrown fill, 9 model; 10 retained terrain command; 11/12 cached linear/sine ground; 13 minimap map;
                 // 14 clipped QuadLayer command; 15 minimap marker; 16 minimap arrow
  std::array<float, 2> uv_offset{};
  std::shared_ptr<const void> owner;  // Keeps lazy GPU storage alive through submission.
  std::uint32_t texture{};  // zero: shared sprite atlas (static sprites or dynamic-page bitmaps, kinds 5/6/8 clamp to
                            // uv); otherwise a retained UI surface
};
// A model already projected to screen pixels, with back faces removed. Triangles
// are grouped by material; vertices are expanded three per triangle.
struct MeshVertex {
  float x, y, u, v, shade;
};
struct MeshGroup {
  bool solid{};
  std::uint32_t first{}, count{};
};
struct MeshDraw {
  std::vector<MeshVertex> vertices;
  std::vector<MeshGroup> groups;
  std::array<float, 4> bounds{};  // sprite UV rectangle in its atlas page; zero size without a sprite
  float page{};
  std::array<float, 3> color{1, 1, 1};  // flat color for Solid* groups and sprite-less models
};
// Takes its place in the painter-ordered stream at the model's depth.
[[nodiscard]] Quad mesh_quad(std::shared_ptr<const MeshDraw> mesh);
[[nodiscard]] Quad sprite_quad(SpriteRegion sprite, Vec2 origin, Vec2 axis_x, Vec2 axis_y);
[[nodiscard]] Quad projectile_quad(SpriteRegion sprite, Vec2 center, double angle, int size, double zoom, bool outline);
[[nodiscard]] Quad radial_shadow(Vec2 origin, Vec2 axis_x, Vec2 axis_y, std::array<float, 3> color, float alpha_center,
                                 float alpha_edge = 0);

}
