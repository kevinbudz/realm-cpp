#pragma once
#include "realm/map/Quest.hpp"
#include "realm/map/World.hpp"
#include "realm/map/partyoverlay/GameObjectArrow.hpp"
#include <cstdint>

namespace realm {
// QuestArrow.as: the edge-of-screen arrow toward the quest object, shown only while that object is off the playfield.
// Hovering it freezes its target and placement (PartyOverlay skips its draw) and switches to the full tooltip. Off
// hover, the tooltip is the full QuestToolTip while the quest is new, else the portrait when showQuestPortraits is on.
class QuestArrow {
 public:
  enum class Tooltip : std::uint8_t { none, quest, portrait };
  // QuestArrow is always BIG_SIZE, with GameObjectArrow(0xF98441, 0xC52222).
  static constexpr int size = GameObjectArrow::big_size;
  static constexpr std::uint32_t line_color = 0xf98441, fill_color = 0xc52222;
  void reset() { *this = QuestArrow{}; }
  // PartyOverlay.draw: `valid` is whether the pointer may hover (no menu or panel above the playfield). Hover and
  // placement move with the camera, the window size and the target's availability.
  void update(const World& world, const Quest& quest, const Camera& camera, Vec2 pointer, bool valid, int width,
              int height, std::int32_t time);
  [[nodiscard]] bool visible() const { return visible_; }
  [[nodiscard]] bool hovered() const { return hover_; }
  // The quest object's id (-1 when there is no target), frozen while hovered.
  [[nodiscard]] std::int32_t target() const { return target_; }
  [[nodiscard]] Vec2 position() const { return position_; }
  [[nodiscard]] double rotation() const { return rotation_; }
  [[nodiscard]] Tooltip tooltip() const { return tooltip_; }
  [[nodiscard]] bool hit(Vec2 pointer) const;
  [[nodiscard]] static Image image(double rotation, double density) {
    return GameObjectArrow::image(rotation, size, line_color, fill_color, density);
  }

 private:
  void refresh(const World& world, const Quest& quest, const Camera& camera, std::int32_t time);
  std::int32_t target_{-1};
  Vec2 position_{};
  double rotation_{}, scale_{1};
  int width_{}, height_{};
  bool visible_{}, hover_{};
  Tooltip tooltip_{Tooltip::none};
};
}
