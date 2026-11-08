#include "realm/objects/ProjectileProperties.hpp"
#include "realm/util/PortableMath.hpp"
#include <numbers>
#include <stdexcept>

namespace realm {
ProjectileAim projectile_aim(double angle) {
  constexpr double pi = std::numbers::pi;
  ProjectileAim aim;
  aim.angle = angle;
  const realm::math::SinCos along = realm::math::sincos(angle);
  const realm::math::SinCos across = realm::math::sincos(angle + pi / 2);
  aim.cos = along.cosine;
  aim.sin = along.sine;
  aim.perpendicular_cos = across.cosine;
  aim.perpendicular_sin = across.sine;
  return aim;
}

Vec2 projectile_position(const ProjectileProperties& properties, Vec2 start, double angle,
                         std::int32_t id, std::int32_t elapsed_ms) {
  return projectile_position(properties, start, projectile_aim(angle), id, elapsed_ms);
}

Vec2 projectile_position(const ProjectileProperties& properties, Vec2 start,
                         const ProjectileAim& aim, std::int32_t id, std::int32_t elapsed_ms) {
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
    const double theta = aim.angle + pi / 64 * realm::math::sin(phase + 6 * pi * elapsed / 1000);
    const realm::math::SinCos heading = realm::math::sincos(theta);
    start.x += distance * heading.cosine;
    start.y += distance * heading.sine;
  } else if (properties.parametric) {
    const double t = elapsed / properties.lifetime_ms * 2 * pi;
    const double x = realm::math::sin(t) * (id % 2 ? 1 : -1);
    const double y = realm::math::sin(2 * t) * (id % 4 < 2 ? 1 : -1);
    const double sine = aim.sin;
    const double cosine = aim.cos;
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
    start.x += distance * aim.cos;
    start.y += distance * aim.sin;
    if (properties.amplitude != 0) {
      const double offset =
          properties.amplitude * realm::math::sin(phase + elapsed / properties.lifetime_ms *
                                                              properties.frequency * 2 * pi);
      start.x += offset * aim.perpendicular_cos;
      start.y += offset * aim.perpendicular_sin;
    }
  }
  return start;
}
}
