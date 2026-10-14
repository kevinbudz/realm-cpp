#include "realm/map/partyoverlay/GameObjectArrow.hpp"
#include "realm/util/GraphicsUtil.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace realm {
std::optional<ArrowPlacement> GameObjectArrow::project(const Camera& camera, Vec2 target) {
  const auto camera_origin = camera.origin(), screen = camera.world_to_screen(target);
  const double x = screen.x - camera_origin.x, y = screen.y - camera_origin.y;
  const double left = -camera_origin.x, right = camera.playable_width() - camera_origin.x, top = -camera_origin.y;
  const double bottom = (camera.clip_rect().height * camera.scale()) - camera_origin.y;
  // RectangleUtil.lineSegmentIntersectXY, from the camera origin to the target.
  double t = 1;
  if (x <= left)
    t = std::min(t, left / x);
  else if (x >= right)
    t = std::min(t, right / x);
  if (y <= top)
    t = std::min(t, top / y);
  else if (y >= bottom)
    t = std::min(t, bottom / y);
  if (t == 1 && screen.x > 0 && screen.x < camera.playable_width() && screen.y > 0 &&
      screen.y < bottom + camera_origin.y)
    return {};
  ArrowPlacement a;
  a.position = {camera_origin.x + t * x, camera_origin.y + t * y};
  double rot = std::remainder(270 - 180 / std::numbers::pi * std::atan2(x, y), 360.0);
  const double margin = 5 * camera.scale();
  if (a.position.x < margin)
    rot = std::clamp(rot, -45.0, 45.0);
  else if (a.position.x > camera.playable_width() - margin)
    rot = rot > 0 ? std::max(rot, 135.0) : std::min(rot, -135.0);
  if (a.position.y < margin)
    rot = std::clamp(rot, 45.0, 135.0);
  else if (a.position.y > bottom + camera_origin.y - margin)
    rot = std::clamp(rot, -135.0, -45.0);
  a.rotation = rot * std::numbers::pi / 180;
  return a;
}
bool GameObjectArrow::contains(Vec2 position, double rotation, int size, Vec2 p, double scale) {
  const double dx = (p.x - position.x) / scale, dy = (p.y - position.y) / scale;
  const double x = dx * std::cos(rotation) + dy * std::sin(rotation),
               y = -dx * std::sin(rotation) + dy * std::cos(rotation);
  return x >= 2.5 && x <= size + 3.5 && std::abs(y) <= x - 3 + .71;
}
Image GameObjectArrow::image(double rotation, int size, std::uint32_t line, std::uint32_t fill, double density) {
  auto result = ui_image(canvas_size, canvas_size, density);
  const double length = size;
  std::array<Vec2, 3> points{Vec2{dist, 0}, Vec2{length + dist, length}, Vec2{length + dist, -length}};
  for (auto& p : points) {
    const double x = p.x;
    p = {origin + x * std::cos(rotation) - p.y * std::sin(rotation),
         origin + x * std::sin(rotation) + p.y * std::cos(rotation)};
  }
  ui_path(result, points, fill, line);
  ui_shadow(result, 4, canvas_size, canvas_size);
  return result;
}
Vec2 GameObjectArrow::tooltip_position(Vec2 anchor, double rotation, double width, double height, double arrow_scale) {
  // GameObjectArrow.positionTooltip: DIST + BIG_SIZE + 12 pixels out from the arrow.
  constexpr int reach_pixels = dist + big_size + 12;
  const double x0 = reach_pixels * arrow_scale * std::cos(rotation),
               y0 = reach_pixels * arrow_scale * std::sin(rotation);
  const double w = width, h = height;
  const double degrees = rotation * 180 / std::numbers::pi;
  Vec2 offset;
  if (degrees >= 45 && degrees <= 135)
    offset = {x0 + w / (2 * std::tan(rotation)) - w / 2, y0};
  else if (degrees <= -45 && degrees >= -135)
    offset = {x0 - w / (2 * std::tan(rotation)) - w / 2, y0 - h};
  else if (degrees < 45 && degrees > -45)
    offset = {x0, y0 + h * std::tan(rotation) / 2 - h / 2};
  else
    offset = {x0 - w, y0 - h * std::tan(rotation) / 2 - h / 2};
  return {anchor.x + offset.x, anchor.y + offset.y};
}
}
