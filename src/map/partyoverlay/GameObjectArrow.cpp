#include "realm/map/partyoverlay/GameObjectArrow.hpp"
#include "realm/ui/GraphicsUtil.hpp"
#include "realm/util/PortableMath.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace realm {
std::optional<ArrowPlacement> GameObjectArrow::project(const Camera& camera, Vec2 target) {
  const auto camera_origin = camera.origin();
  const auto screen = camera.world_to_screen(target);
  const double x = screen.x - camera_origin.x;
  const double y = screen.y - camera_origin.y;
  const double left = -camera_origin.x;
  const double right = camera.playable_width() - camera_origin.x;
  const double top = -camera_origin.y;
  const double bottom = (camera.clip_rect().height * camera.scale()) - camera_origin.y;
  // RectangleUtil.lineSegmentIntersectXY, from the camera origin to the target.
  double t = 1;
  if (x <= left) {
    t = std::min(t, left / x);
  } else if (x >= right) {
    t = std::min(t, right / x);
  }
  if (y <= top) {
    t = std::min(t, top / y);
  } else if (y >= bottom) {
    t = std::min(t, bottom / y);
  }
  if (t == 1 && screen.x > 0 && screen.x < camera.playable_width() && screen.y > 0 &&
      screen.y < bottom + camera_origin.y) {
    return {};
  }
  ArrowPlacement placement;
  placement.position = {camera_origin.x + t * x, camera_origin.y + t * y};
  double rotation = std::remainder(270 - 180 / std::numbers::pi * realm::math::atan2(x, y), 360.0);
  // Screen pixels at every zoom, as the arrow itself is drawn at zoom 1.
  const double margin = 5;
  if (placement.position.x < margin) {
    rotation = std::clamp(rotation, -45.0, 45.0);
  } else if (placement.position.x > camera.playable_width() - margin) {
    rotation = rotation > 0 ? std::max(rotation, 135.0) : std::min(rotation, -135.0);
  }
  if (placement.position.y < margin) {
    rotation = std::clamp(rotation, 45.0, 135.0);
  } else if (placement.position.y > bottom + camera_origin.y - margin) {
    rotation = std::clamp(rotation, -135.0, -45.0);
  }
  placement.rotation = rotation * std::numbers::pi / 180;
  return placement;
}

bool GameObjectArrow::contains(Vec2 position, double rotation, int size, Vec2 p, double scale) {
  const double dx = (p.x - position.x) / scale;
  const double dy = (p.y - position.y) / scale;
  const realm::math::SinCos turn = realm::math::sincos(rotation);
  const double x = dx * turn.cosine + dy * turn.sine;
  const double y = -dx * turn.sine + dy * turn.cosine;
  return x >= 2.5 && x <= size + 3.5 && std::abs(y) <= x - 3 + .71;
}

Image GameObjectArrow::image(double rotation, int size, std::uint32_t line, std::uint32_t fill,
                             double density) {
  auto result = ui_image(canvas_size, canvas_size, density);
  const double length = size;
  std::array<Vec2, 3> points{Vec2{dist, 0}, Vec2{length + dist, length},
                             Vec2{length + dist, -length}};
  const realm::math::SinCos turn = realm::math::sincos(rotation);
  for (auto& p : points) {
    const double x = p.x;
    p = {origin + x * turn.cosine - p.y * turn.sine, origin + x * turn.sine + p.y * turn.cosine};
  }
  ui_path(result, points, fill, line);
  ui_shadow(result, 4, canvas_size, canvas_size);
  return result;
}

Vec2 GameObjectArrow::tooltip_position(Vec2 anchor, double rotation, double width, double height,
                                       double arrow_scale) {
  // GameObjectArrow.positionTooltip: DIST + BIG_SIZE + 12 pixels out from the arrow.
  constexpr int reach_pixels = dist + big_size + 12;
  const realm::math::SinCos turn = realm::math::sincos(rotation);
  const double x0 = reach_pixels * arrow_scale * turn.cosine;
  const double y0 = reach_pixels * arrow_scale * turn.sine;
  const double w = width;
  const double h = height;
  const double degrees = rotation * 180 / std::numbers::pi;
  Vec2 offset;
  if (degrees >= 45 && degrees <= 135) {
    offset = {x0 + w / (2 * realm::math::tan(rotation)) - w / 2, y0};
  } else if (degrees <= -45 && degrees >= -135) {
    offset = {x0 - w / (2 * realm::math::tan(rotation)) - w / 2, y0 - h};
  } else if (degrees < 45 && degrees > -45) {
    offset = {x0, y0 + h * realm::math::tan(rotation) / 2 - h / 2};
  } else {
    offset = {x0 - w, y0 - h * realm::math::tan(rotation) / 2 - h / 2};
  }
  return {anchor.x + offset.x, anchor.y + offset.y};
}
}
