#include "realm/map/partyoverlay/PartyArrows.hpp"
#include "realm/util/GraphicsUtil.hpp"
#include <algorithm>
#include <numbers>

namespace realm {
std::optional<PartyArrow> PartyArrows::project(const Camera& camera, Vec2 target) {
  const auto origin = camera.origin(), screen = camera.world_to_screen(target);
  const double x = screen.x - origin.x, y = screen.y - origin.y;
  const double left = -origin.x, right = camera.playable_width() - origin.x, top = -origin.y;
  const double bottom = (camera.clip_rect().height * camera.scale()) - origin.y;
  // RectangleUtil.lineSegmentIntersectXY, from the camera origin to the member.
  double t = 1;
  if (x <= left)
    t = std::min(t, left / x);
  else if (x >= right)
    t = std::min(t, right / x);
  if (y <= top)
    t = std::min(t, top / y);
  else if (y >= bottom)
    t = std::min(t, bottom / y);
  if (t == 1 && screen.x > 0 && screen.x < camera.playable_width() && screen.y > 0 && screen.y < bottom + origin.y)
    return {};
  PartyArrow a;
  a.position = {origin.x + t * x, origin.y + t * y};
  double rot = std::remainder(270 - 180 / std::numbers::pi * std::atan2(x, y), 360.0);
  const double margin = 5 * camera.scale();
  if (a.position.x < margin)
    rot = std::clamp(rot, -45.0, 45.0);
  else if (a.position.x > camera.playable_width() - margin)
    rot = rot > 0 ? std::max(rot, 135.0) : std::min(rot, -135.0);
  if (a.position.y < margin)
    rot = std::clamp(rot, 45.0, 135.0);
  else if (a.position.y > bottom + origin.y - margin)
    rot = std::clamp(rot, -135.0, -45.0);
  a.rotation = rot * std::numbers::pi / 180;
  return a;
}
bool PartyArrows::drawn(const World& world, const Entity& p, const Camera& camera, int height) {
  const auto* tile =
      world.tile(static_cast<int>(std::floor(p.motion.position.x)), static_cast<int>(std::floor(p.motion.position.y)));
  if (!tile || tile->type == 255) return false;
  // Map.draw marks known squares around its vertically offset world center.
  const auto center = camera.screen_to_world({camera.origin().x, height / 2.0});
  const double dx = center.x - std::floor(p.motion.position.x) - .5,
               dy = center.y - std::floor(p.motion.position.y) - .5;
  return dx * dx + dy * dy <= camera.max_distance() * camera.max_distance();
}
bool PartyArrows::contains(const PartyArrow& a, Vec2 p) const {
  if (a.players.empty()) return false;
  const double dx = (p.x - a.position.x) / scale_, dy = (p.y - a.position.y) / scale_;
  const double x = dx * std::cos(a.rotation) + dy * std::sin(a.rotation),
               y = -dx * std::sin(a.rotation) + dy * std::cos(a.rotation);
  const double size = a.hover ? 11 : 8;
  return x >= 2.5 && x <= size + 3.5 && std::abs(y) <= x - 3 + .71;
}
const PartyArrow* PartyArrows::hit(Vec2 p) const {
  // Display-list hit order is the reverse of party slots.
  for (auto it = arrows_.rbegin(); it != arrows_.rend(); ++it)
    if (contains(*it, p)) return &*it;
  return nullptr;
}
void PartyArrows::update(const World& world, const Party& party, const Camera& camera, Vec2 pointer, bool valid,
                         int width, int height) {
  if (width_ != width || height_ != height || scale_ != camera.scale()) cancel_hover();
  width_ = width;
  height_ = height;
  scale_ = camera.scale();
  if (!world.entity(world.player_id())) {
    reset();
    return;
  }
  if (hovered_ >= 0) {
    const auto& a = arrows_[static_cast<std::size_t>(hovered_)];
    if (!valid || !contains(a, pointer) || a.players.empty() || !world.entity(a.players.front())) cancel_hover();
  }
  for (std::size_t i = 0; i < arrows_.size(); ++i) {
    if (static_cast<int>(i) == hovered_) continue;  // The hovered slot/group is frozen.
    auto& arrow = arrows_[i];
    arrow.players.clear();
    arrow.hover = false;
    const auto* p = world.entity(party.members()[i]);
    if (!p || !p->player || p->id == world.player_id() || drawn(world, *p, camera, height)) continue;
    const auto projected = project(camera, p->motion.position);
    if (!projected) continue;
    arrow.position = projected->position;
    arrow.rotation = projected->rotation;
    arrow.players.push_back(p->id);
    for (std::size_t j = 0; j < i; ++j) {
      auto& previous = arrows_[j];
      if (previous.players.empty()) continue;
      const double dx = arrow.position.x - previous.position.x, dy = arrow.position.y - previous.position.y;
      if (dx * dx + dy * dy < 64 * scale_ * scale_) {
        if (!previous.hover) previous.players.push_back(p->id);
        arrow.players.clear();
        break;
      }
    }
  }
  if (valid && hovered_ < 0)
    if (const auto* a = hit(pointer)) {
      hovered_ = static_cast<int>(a - arrows_.data());
      arrows_[static_cast<std::size_t>(hovered_)].hover = true;
    }
}
Image PartyArrows::image(double rotation, bool hover, double density) {
  auto result = ui_image(canvas_size, canvas_size, density);
  const double size = hover ? 11 : 8;
  std::array<Vec2, 3> points{Vec2{3, 0}, Vec2{size + 3, size}, Vec2{size + 3, -size}};
  for (auto& p : points) {
    const double x = p.x;
    p = {origin + x * std::cos(rotation) - p.y * std::sin(rotation),
         origin + x * std::sin(rotation) + p.y * std::cos(rotation)};
  }
  ui_path(result, points, 0x3fc752, 0xffffff);
  ui_shadow(result, 4, canvas_size, canvas_size);
  return result;
}
}
