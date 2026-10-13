#pragma once
#include "realm/map/Camera.hpp"
#include <cstdint>

namespace realm {
struct ProjectileProperties {
  double speed{}, lifetime_ms{1}, magnitude{}, amplitude{}, frequency{};
  bool accelerate{}, decelerate{}, wavy{}, parametric{}, boomerang{};
};
[[nodiscard]] Vec2 projectile_position(const ProjectileProperties& props, Vec2 start, double angle,
                                       std::int32_t bullet_id, std::int32_t elapsed_ms);
}
