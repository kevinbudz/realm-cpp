#pragma once
#include "realm/map/Camera.hpp"
#include <cstdint>

namespace realm {
struct ProjectileProperties {
  double speed{};
  double lifetime_ms{1};
  double magnitude{};
  double amplitude{};
  double frequency{};
  bool accelerate{};
  bool decelerate{};
  bool wavy{};
  bool parametric{};
  bool boomerang{};
};

// Trigonometry of a launch angle, computed once per projectile at spawn (the same expressions
// projectile_position would evaluate every tick, so the results are bit-identical).
struct ProjectileAim {
  double angle{};
  double cos{};
  double sin{};
  double perpendicular_cos{};
  double perpendicular_sin{};
};

[[nodiscard]] ProjectileAim projectile_aim(double angle);

[[nodiscard]] Vec2 projectile_position(const ProjectileProperties& properties, Vec2 start,
                                       const ProjectileAim& aim,
                                       std::int32_t bullet_id, std::int32_t elapsed_ms);
[[nodiscard]] Vec2 projectile_position(const ProjectileProperties& properties, Vec2 start,
                                       double angle,
                                       std::int32_t bullet_id, std::int32_t elapsed_ms);
}
