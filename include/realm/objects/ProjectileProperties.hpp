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

[[nodiscard]] Vec2 projectile_position(const ProjectileProperties& properties, Vec2 start,
                                       double angle,
                                       std::int32_t bullet_id, std::int32_t elapsed_ms);
}
