#include "realm/map/partyoverlay/QuestArrow.hpp"

namespace realm {
void QuestArrow::update(const World& world, const Quest& quest, const Camera& camera, Vec2 pointer,
                        bool valid, int width, int height, std::int32_t time) {
  if (width_ != width || height_ != height || scale_ != camera.scale()) {
    hover_ = false;
  }
  width_ = width;
  height_ = height;
  scale_ = camera.scale();
  // PartyOverlay.draw returns while there is no local player.
  if (!world.entity(world.player_id())) {
    reset();
    return;
  }
  if (hover_ && (!valid || !hit(pointer) || (target_ != -1 && !world.entity(target_)))) {
    hover_ = false;
  }
  // While hovered, PartyOverlay skips questArrow.draw, so the target and placement stay as they
  // were.
  if (!hover_) {
    refresh(world, quest, camera, time);
  }
  if (valid && !hover_ && hit(pointer)) {
    hover_ = true;
  }
  // QuestArrow.getToolTip: the full tooltip while hovered or new, else the portrait when enabled.
  // No tooltip while the arrow is hidden. The frame is set again by the render path if it draws.
  tooltip_ = Tooltip::none;
  tooltip_bounds_ = {};
  if (!visible_) {
    return;
  }
  if (hover_ || quest.is_new(time)) {
    tooltip_ = Tooltip::quest;
  } else if (world.preferences().show_quest_portraits) {
    tooltip_ = Tooltip::portrait;
  }
}

bool QuestArrow::hit(Vec2 pointer) const {
  if (!visible_) {
    return false;
  }
  if (GameObjectArrow::contains(position_, rotation_, size, pointer, scale_)) {
    return true;
  }
  // QuestToolTip and PortraitToolTip are children of the arrow in QuestArrow.as, so their frame
  // hovers and takes presses as the arrow does. An empty frame (no tooltip drawn) hits nothing.
  const auto& b = tooltip_bounds_;
  return pointer.x >= b.x && pointer.x < b.x + b.width && pointer.y >= b.y &&
         pointer.y < b.y + b.height;
}

void QuestArrow::refresh(const World& world, const Quest& quest, const Camera& camera,
                         std::int32_t time) {
  target_ = quest.target(time);
  visible_ = false;
  const auto* target = target_ != -1 ? world.entity(target_) : nullptr;
  if (!target) {
    return;
  }
  // GameObjectArrow.draw: the arrow shows only while the target is outside the playfield, whether
  // or not its square is known.
  const auto placed = GameObjectArrow::project(camera, target->motion.position);
  if (!placed) {
    return;
  }
  visible_ = true;
  position_ = placed->position;
  rotation_ = placed->rotation;
}
}
