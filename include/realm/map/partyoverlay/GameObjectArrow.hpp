#pragma once
#include "realm/map/Camera.hpp"
#include "realm/util/Image.hpp"
#include <cstdint>
#include <optional>

namespace realm {
struct ArrowPlacement {
  Vec2 position{};
  double rotation{};  // radians, the triangle points toward the target
};
// GameObjectArrow.as: the edge-of-screen triangle shared by PlayerArrow and QuestArrow. Its tip sits DIST pixels from
// the origin and the triangle is `size` pixels long (SMALL_SIZE or BIG_SIZE).
class GameObjectArrow {
 public:
  static constexpr int small_size = 8, big_size = 11, dist = 3;
  static constexpr int canvas_size = 48, origin = 24;
  // GameObjectArrow.draw: the placement for a world position that lies outside the playfield. Nullopt when on screen.
  [[nodiscard]] static std::optional<ArrowPlacement> project(const Camera& camera, Vec2 target);
  // Hit test for the triangle at `scale` drawable pixels per arrow pixel.
  [[nodiscard]] static bool contains(Vec2 position, double rotation, int size, Vec2 pointer, double scale);
  [[nodiscard]] static Image image(double rotation, int size, std::uint32_t line, std::uint32_t fill, double density);
  // GameObjectArrow.positionTooltip: the content origin of a `width` x `height` tooltip beside an arrow at `anchor`.
  [[nodiscard]] static Vec2 tooltip_position(Vec2 anchor, double rotation, double width, double height,
                                             double arrow_scale);
};
}
