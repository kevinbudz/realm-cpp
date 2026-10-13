#include "realm/objects/ProjectileProperties.hpp"
#include <numbers>

namespace realm {
Vec2 projectile_position(const ProjectileProperties& p, Vec2 start, double angle, std::int32_t id,
                         std::int32_t elapsed_ms) {
  if (!std::isfinite(p.lifetime_ms) || p.lifetime_ms <= 0)
    throw std::invalid_argument("Projectile lifetime must be positive");
  constexpr double pi = std::numbers::pi;
  const double elapsed = elapsed_ms;
  double speed = p.speed;
  if (p.accelerate) speed *= elapsed / p.lifetime_ms;
  if (p.decelerate) speed *= 2 - elapsed / p.lifetime_ms;
  double dist = elapsed * (speed / 10000);
  const double phase = id % 2 == 0 ? 0 : pi;
  if (p.wavy) {
    const double theta = angle + pi / 64 * std::sin(phase + 6 * pi * elapsed / 1000);
    start.x += dist * std::cos(theta);
    start.y += dist * std::sin(theta);
  } else if (p.parametric) {
    const double t = elapsed / p.lifetime_ms * 2 * pi;
    const double x = std::sin(t) * (id % 2 ? 1 : -1);
    const double y = std::sin(2 * t) * (id % 4 < 2 ? 1 : -1);
    const double s = std::sin(angle), c = std::cos(angle);
    start.x += (x * c - y * s) * p.magnitude;
    start.y += (x * s + y * c) * p.magnitude;
  } else {
    // Mirrors Projectile.as: deliberately uses the original speed for halfway.
    if (p.boomerang) {
      const double halfway = p.lifetime_ms * (p.speed / 10000) / 2;
      if (dist > halfway) dist = halfway - (dist - halfway);
    }
    start.x += dist * std::cos(angle);
    start.y += dist * std::sin(angle);
    if (p.amplitude != 0) {
      const double d = p.amplitude * std::sin(phase + elapsed / p.lifetime_ms * p.frequency * 2 * pi);
      start.x += d * std::cos(angle + pi / 2);
      start.y += d * std::sin(angle + pi / 2);
    }
  }
  return start;
}
}
