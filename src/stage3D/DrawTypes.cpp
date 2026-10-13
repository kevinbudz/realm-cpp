#include "realm/stage3D/DrawTypes.hpp"
#include <cmath>
#include <numbers>

namespace realm {
namespace {
std::array<float, 2> floats(Vec2 p) { return {static_cast<float>(p.x), static_cast<float>(p.y)}; }
}
Quad sprite_quad(SpriteRegion s, Vec2 origin, Vec2 axis_x, Vec2 axis_y) {
  constexpr float inv = 1.0F / sprite_page_size;
  Quad q;
  q.origin = floats(origin);
  q.axis_x = floats(axis_x);
  q.axis_y = floats(axis_y);
  q.uv = {static_cast<float>(s.x) * inv, static_cast<float>(s.y) * inv, static_cast<float>(s.width) * inv,
          static_cast<float>(s.height) * inv};
  q.page = static_cast<float>(s.page);
  return q;
}
Quad projectile_quad(SpriteRegion sprite, Vec2 center, double angle, int size, double zoom, bool outline) {
  // Point3D.as: the path is a rotated square; outlined textures retain their
  // pixel scale and are clipped by the square instead of fitting inside it.
  const double radius = 8.0 * size / 100 / std::sin(24.0 * std::numbers::pi / 180);
  const double c = std::cos(angle), s = std::sin(angle), extent = radius * zoom;
  auto q = sprite_quad(sprite, {center.x + (s - c) * extent, center.y - (s + c) * extent},
                       {2 * c * extent, 2 * s * extent}, {-2 * s * extent, 2 * c * extent});
  if (outline) {
    constexpr double inv = 1.0 / sprite_page_size;
    q.uv = {static_cast<float>((sprite.x + sprite.width * .5 - radius) * inv),
            static_cast<float>((sprite.y + sprite.height * .5 - radius) * inv), static_cast<float>(2 * radius * inv),
            static_cast<float>(2 * radius * inv)};
    q.kind = 4;
  }
  return q;
}
Quad mesh_quad(std::shared_ptr<const MeshDraw> mesh) {
  Quad q;
  q.kind = 9;
  q.owner = std::move(mesh);
  return q;
}
Quad radial_shadow(Vec2 origin, Vec2 x, Vec2 y, std::array<float, 3> color, float center, float edge) {
  Quad q;
  q.origin = floats(origin);
  q.axis_x = floats(x);
  q.axis_y = floats(y);
  q.kind = 1;
  q.multiply = {color[0], color[1], color[2], center};
  q.offset[3] = edge;
  return q;
}
}
