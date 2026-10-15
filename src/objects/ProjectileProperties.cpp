#include "realm/objects/ProjectileProperties.hpp"
#include <numbers>
#include <stdexcept>

namespace realm {
Vec2 projectile_position(const ProjectileProperties& properties, Vec2 start, double angle,
                         std::int32_t id,
                         std::int32_t elapsed_ms) {
  if (!std::isfinite(properties.lifetime_ms) || properties.lifetime_ms <= 0) {
    throw std::invalid_argument("Projectile lifetime must be positive");
  }

  constexpr double pi = std::numbers::pi;
  const double elapsed = elapsed_ms;
  double speed = properties.speed;
  if (properties.accelerate) {
    speed *= elapsed / properties.lifetime_ms;
  }
  if (properties.decelerate) {
    speed *= 2 - elapsed / properties.lifetime_ms;
  }
  double distance = elapsed * (speed / 10000);
  const double phase = id % 2 == 0 ? 0 : pi;
  if (properties.wavy) {
    const double theta = angle + pi / 64 * std::sin(phase + 6 * pi * elapsed / 1000);
    start.x += distance * std::cos(theta);
    start.y += distance * std::sin(theta);
  } else if (properties.parametric) {
    const double t = elapsed / properties.lifetime_ms * 2 * pi;
    const double x = std::sin(t) * (id % 2 ? 1 : -1);
    const double y = std::sin(2 * t) * (id % 4 < 2 ? 1 : -1);
    const double sine = std::sin(angle);
    const double cosine = std::cos(angle);
    start.x += (x * cosine - y * sine) * properties.magnitude;
    start.y += (x * sine + y * cosine) * properties.magnitude;
  } else {
    // Mirrors Projectile.as: deliberately uses the original speed for halfway.
    if (properties.boomerang) {
      const double halfway = properties.lifetime_ms * (properties.speed / 10000) / 2;
      if (distance > halfway) {
        distance = halfway - (distance - halfway);
      }
    }
    start.x += distance * std::cos(angle);
    start.y += distance * std::sin(angle);
    if (properties.amplitude != 0) {
      const double offset = properties.amplitude *
                            std::sin(phase + elapsed / properties.lifetime_ms * properties.frequency *
                                                 2 * pi);
      start.x += offset * std::cos(angle + pi / 2);
      start.y += offset * std::sin(angle + pi / 2);
    }
  }
  return start;
}
}
