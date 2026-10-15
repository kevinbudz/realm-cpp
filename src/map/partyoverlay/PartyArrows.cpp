#include "realm/map/partyoverlay/PartyArrows.hpp"
#include <algorithm>

namespace realm {
std::optional<PartyArrow> PartyArrows::project(const Camera& camera, Vec2 target) {
  const auto placed = GameObjectArrow::project(camera, target);
  if (!placed) {
    return {};
  }
  PartyArrow arrow;
  arrow.position = placed->position;
  arrow.rotation = placed->rotation;
  return arrow;
}

bool PartyArrows::drawn(const World& world, const Entity& member, const Camera& camera,
                        int height) {
  const auto* tile = world.tile(static_cast<int>(std::floor(member.motion.position.x)),
                                static_cast<int>(std::floor(member.motion.position.y)));
  if (!tile || tile->type == 255) {
    return false;
  }
  // Map.draw marks known squares around its vertically offset world center.
  const auto center = camera.screen_to_world({camera.origin().x, height / 2.0});
  const double dx = center.x - std::floor(member.motion.position.x) - .5;
  const double dy = center.y - std::floor(member.motion.position.y) - .5;
  return dx * dx + dy * dy <= camera.max_distance() * camera.max_distance();
}

bool PartyArrows::contains(const PartyArrow& arrow, Vec2 p) const {
  if (arrow.players.empty()) {
    return false;
  }
  return GameObjectArrow::contains(
      arrow.position, arrow.rotation,
      arrow.hover ? GameObjectArrow::big_size : GameObjectArrow::small_size, p, scale_);
}

const PartyArrow* PartyArrows::hit(Vec2 p) const {
  // Display-list hit order is the reverse of party slots.
  for (auto it = arrows_.rbegin(); it != arrows_.rend(); ++it) {
    if (contains(*it, p)) {
      return &*it;
    }
  }
  return nullptr;
}

void PartyArrows::update(const World& world, const Party& party, const Camera& camera, Vec2 pointer,
                         bool valid, int width, int height) {
  if (width_ != width || height_ != height || scale_ != camera.scale()) {
    cancel_hover();
  }
  width_ = width;
  height_ = height;
  scale_ = camera.scale();
  if (!world.entity(world.player_id())) {
    reset();
    return;
  }
  if (hovered_ >= 0) {
    const auto& arrow = arrows_[static_cast<std::size_t>(hovered_)];
    if (!valid || !contains(arrow, pointer) || arrow.players.empty() ||
        !world.entity(arrow.players.front())) {
      cancel_hover();
    }
  }
  for (std::size_t i = 0; i < arrows_.size(); ++i) {
    if (static_cast<int>(i) == hovered_) {
      continue;  // The hovered slot/group is frozen.
    }
    auto& arrow = arrows_[i];
    arrow.players.clear();
    arrow.hover = false;
    const auto* member = world.entity(party.members()[i]);
    if (!member || !member->player || member->id == world.player_id() ||
        drawn(world, *member, camera, height)) {
      continue;
    }
    const auto projected = project(camera, member->motion.position);
    if (!projected) {
      continue;
    }
    arrow.position = projected->position;
    arrow.rotation = projected->rotation;
    arrow.players.push_back(member->id);
    for (std::size_t j = 0; j < i; ++j) {
      auto& previous = arrows_[j];
      if (previous.players.empty()) {
        continue;
      }
      const double dx = arrow.position.x - previous.position.x;
      const double dy = arrow.position.y - previous.position.y;
      if (dx * dx + dy * dy < 64 * scale_ * scale_) {
        if (!previous.hover) {
          previous.players.push_back(member->id);
        }
        arrow.players.clear();
        break;
      }
    }
  }
  if (valid && hovered_ < 0) {
    if (const auto* arrow = hit(pointer)) {
      hovered_ = static_cast<int>(arrow - arrows_.data());
      arrows_[static_cast<std::size_t>(hovered_)].hover = true;
    }
  }
}

Image PartyArrows::image(double rotation, bool hover, double density) {
  // PlayerArrow: white line, 0x3fc752 fill.
  return GameObjectArrow::image(rotation,
                                hover ? GameObjectArrow::big_size : GameObjectArrow::small_size,
                                0xffffff, 0x3fc752, density);
}
}
