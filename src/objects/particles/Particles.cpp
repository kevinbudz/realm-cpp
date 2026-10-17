#include "realm/objects/particles/Particles.hpp"
#include "realm/map/World.hpp"
#include "realm/util/Telemetry.hpp"
#include "flash_particle_palettes.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace realm {
namespace {
constexpr double tau = 2 * std::numbers::pi;

EmitterKind emitter_kind(const std::string& kind) {
  return kind == "Healing"           ? EmitterKind::healing
         : kind == "Fountain"        ? EmitterKind::fountain
         : kind == "Vent"            ? EmitterKind::vent
         : kind == "Bubbles"         ? EmitterKind::bubbles
         : kind == "XMLEffect"       ? EmitterKind::xml
         : kind == "Gas"             ? EmitterKind::gas
         : kind == "CustomParticles" ? EmitterKind::custom
                                     : EmitterKind::other;
}

bool finite(Vec2 position) {
  return std::isfinite(position.x) && std::isfinite(position.y) && std::abs(position.x) <= 1048576 &&
         std::abs(position.y) <= 1048576;
}

Vec2 lerp(Vec2 start, Vec2 end, double t) {
  return {start.x + (end.x - start.x) * t, start.y + (end.y - start.y) * t};
}

double distance(Vec2 start, Vec2 end) {
  return std::hypot(start.x - end.x, start.y - end.y);
}

int bounded_count(double value, int limit = 256) {
  return std::isfinite(value) ? static_cast<int>(std::clamp(value, 0.0, static_cast<double>(limit)))
                              : 0;
}
}

Particles::Particles() {
  particles_.reserve(maximum_particles);
  emitters_.reserve(maximum_emitters);
  flashes_.reserve(maximum_flashes);
}

void Particles::clear() {
  particles_.clear();
  emitters_.clear();
  flashes_.clear();
  particle_targets_.clear();
  emitter_targets_.clear();
  flash_targets_.clear();
  now_ = 0;
  last_update_ = jitter_start_ = -1;
  spawns_ = dropped_ = 0;
  order_ = 0;
  rng_ = 0x12345678U;
}

// The Particles option stops the optional effects it gates (hit and wall-impact bursts, projectile
// trails, Healing and the object-texture effects); ShowEffect, AoE and level-up effects are
// gameplay information and always play.
void Particles::set_enabled(bool value) {
  enabled_ = value;
}

double Particles::random() {
  rng_ ^= rng_ << 13U;
  rng_ ^= rng_ >> 17U;
  rng_ ^= rng_ << 5U;
  return rng_ / 4294967296.0;
}

bool Particles::emit(Particle particle) {
  if (particles_.size() >= maximum_particles || spawns_ >= maximum_spawns) {
    ++dropped_;
    return false;
  }
  if (!finite(particle.position) || !finite(particle.velocity) || !std::isfinite(particle.z) || !std::isfinite(particle.size)) {
    return false;
  }
  particle.lifetime = std::clamp(particle.lifetime, 1, maximum_lifetime);
  particle.initial_size = particle.size = std::clamp(particle.size, 0.0, 640.0);
  particle.initial_z = particle.z;
  particle.order = order_++;
  const auto target = particle.target;
  particles_.push_back(std::move(particle));
  if (target != -1) {
    particle_targets_.add(target);
  }
  ++spawns_;
  return true;
}

Particle Particles::spark(Vec2 at, std::uint32_t color, double size, int life, double z, Vec2 velocity,
                          std::int64_t time) {
  Particle particle;
  particle.motion = ParticleMotion::spark;
  particle.position = particle.start = at;
  particle.color = color;
  particle.size = size;
  particle.lifetime = life;
  particle.z = z;
  particle.velocity = velocity;
  particle.born = time;
  return particle;
}

std::optional<std::uint32_t> Particles::color(const TextureDef& texture, std::uint32_t id) {
  const auto& selected = texture.select(id);
  const auto lookup = [&](const auto& starts, int sheet, int index) -> flash::PaletteRange {
    if (sheet < 0 || index < 0 || static_cast<std::size_t>(sheet + 1) >= starts.size()) {
      return {};
    }
    const auto first = starts[static_cast<std::size_t>(sheet)];
    const auto last = starts[static_cast<std::size_t>(sheet + 1)];
    return static_cast<unsigned>(index) < last - first
               ? flash::particle_palettes[first + static_cast<unsigned>(index)]
               : flash::PaletteRange{};
  };
  auto range = selected.animated >= 0 ? lookup(flash::particle_animated_starts, selected.animated, selected.index)
                               : lookup(flash::particle_sheet_starts, selected.sheet, selected.index);
  if (!range.count) {
    return {};
  }
  return flash::particle_colors[range.offset + static_cast<unsigned>(random() * range.count)];
}

void Particles::attach(const Entity& entity, const TextureDef& texture) {
  // Nearly every object (every projectile) has no emitter: the filter answers that without scanning
  // the emitter list.
  const bool maybe = emitter_targets_.may_contain(entity.id);
  if (!maybe && texture.effect.kind.empty()) {
    return;
  }
  const auto it = !maybe
                      ? emitters_.end()
                      : std::find_if(emitters_.begin(), emitters_.end(), [&](const auto& emitter) {
                          return emitter.target == entity.id && emitter.props->kind != "Healing";
                        });

  if (texture.effect.kind.empty()) {
    if (it != emitters_.end()) {
      emitters_.erase(it);
    }
    return;
  }
  // ParticleEffect.fromProps returns null while the option is off; an existing effect keeps
  // running.
  if (!enabled_ && it == emitters_.end()) {
    return;
  }

  if (it != emitters_.end()) {
    if (it->props != &texture.effect) {
      *it = {entity.id, &texture.effect, static_cast<double>(now_), 0,
             emitter_kind(texture.effect.kind)};
    }
    return;
  }
  if (emitters_.size() < maximum_emitters) {
    emitters_.push_back({entity.id, &texture.effect, static_cast<double>(now_), 0,
                         emitter_kind(texture.effect.kind)});
    emitter_targets_.add(entity.id);
  } else {
    ++dropped_;
  }
}

void Particles::healing(const Entity& entity) {
  static const EffectDef props = [] {
    EffectDef value;
    value.kind = "Healing";
    return value;
  }();
  const bool maybe = emitter_targets_.may_contain(entity.id);
  if (!maybe && !(entity.combat.condition & (1U << 15U))) {
    return;
  }
  const auto it = !maybe
                      ? emitters_.end()
                      : std::find_if(emitters_.begin(), emitters_.end(), [&](const auto& emitter) {
                          return emitter.target == entity.id && emitter.props == &props;
                        });
  if (!(entity.combat.condition & (1U << 15U))) {
    if (it != emitters_.end()) {
      emitters_.erase(it);
    }
    return;
  }
  if (it == emitters_.end() && enabled_ && emitters_.size() < maximum_emitters) {
    emitters_.push_back({entity.id, &props, static_cast<double>(now_) - 500, 0, EmitterKind::healing});
    emitter_targets_.add(entity.id);
  }
}

// Object removal runs for every expiring projectile, so the three scans (up to 8192 particles of
// 224 bytes) are gated by the target filters. Untargeted particles carry id -1, so that id (never a
// real object) always takes the exact path.
void Particles::remove_target(std::int32_t id) {
  if (emitter_targets_.may_contain(id)) {
    std::erase_if(emitters_, [&](const auto& emitter) { return emitter.target == id; });
  }

  if (id == -1 || particle_targets_.may_contain(id)) {
    std::erase_if(particles_, [&](const auto& particle) { return particle.target == id; });
  }

  if (flash_targets_.may_contain(id)) {
    std::erase_if(flashes_, [&](const auto& flash) { return flash.target == id; });
  }
}

bool Particles::targets(std::int32_t id) const {
  return std::any_of(emitters_.begin(), emitters_.end(),
                     [&](const auto& emitter) { return emitter.target == id; }) ||
         std::any_of(particles_.begin(), particles_.end(),
                     [&](const auto& particle) { return particle.target == id; }) ||
         std::any_of(flashes_.begin(), flashes_.end(),
                     [&](const auto& flash) { return flash.target == id; });
}

void Particles::path(Vec2 start, Vec2 end, std::uint32_t color, int life, double size,
                     std::int32_t time, ParticleMotion motion) {
  if (!finite(start) || !finite(end)) {
    return;
  }
  Particle particle;
  particle.motion = motion;
  particle.position = particle.start = start;
  particle.end = end;
  particle.born = time;
  particle.last_emission = time - 17;
  particle.color = color;
  particle.size = size;
  particle.lifetime = life;
  emit(particle);
}

void Particles::heal(const Entity& target, std::uint32_t color, std::int32_t time, int count) {
  for (int i = 0; i < count; ++i) {
    Particle particle;
    particle.motion = ParticleMotion::heal;
    particle.target = target.id;
    particle.angle = count == 1 ? random() * tau : tau * i / count;
    particle.angle_cos = std::cos(particle.angle);
    particle.angle_sin = std::sin(particle.angle);
    particle.distance = .3 + .4 * random();
    particle.position = {target.motion.position.x + particle.distance * particle.angle_cos,
                  target.motion.position.y + particle.distance * particle.angle_sin};
    particle.color = color;
    particle.size = (3 + static_cast<int>(random() * 5)) * 20;
    particle.z = random() * .3;
    particle.vz = (.1 + random() * .1) * 8;
    particle.born = time;
    emit(particle);
  }
}

void Particles::aoe(Vec2 at, double radius, std::uint32_t color, std::int32_t time) {
  if (!std::isfinite(radius) || radius < 0) {
    return;
  }
  radius = std::min(radius, 128.0);
  const int num = bounded_count(4 + radius * 2);
  for (int i = 0; i < num; ++i) {
    path(at, {at.x + radius * std::cos(tau * i / num), at.y + radius * std::sin(tau * i / num)}, color,
         200, 200, time);
  }
}

void Particles::show(const World& world, const net::incoming::ShowEffect& effect, std::int32_t time) {
  const auto* target = world.entity(effect.target_id);
  const Vec2 start{effect.position.x, effect.position.y};
  const Vec2 end{effect.position2.x, effect.position2.y};
  if (!finite(start) || !finite(end)) {
    return;
  }

  const auto color = effect.color;
  const Vec2 origin = target ? target->motion.position : Vec2{};
  if (effect.type != 0 && effect.type != 2 && effect.type != 3 && effect.type != 4 && effect.type != 14 && effect.type != 16 &&
      !target) {
    return;
  }

  switch (effect.type) {
    case 1:
      heal(*target, color, time, 10);
      break;
    case 2:
      for (int i = 0; i < 20; ++i) {
        const double angle = random() * tau;
        const double radius = .7 * random();
        auto particle = spark({start.x + radius * std::cos(angle),
                               start.y + radius * std::sin(angle)},
                              255, 50, 500 + bounded_count(random() * 1000, 1000), 0, {}, time);
        particle.motion = ParticleMotion::linear;
        particle.vz = .8;
        emit(particle);
      }
      break;
    case 3:
      for (int i = 0; i < 5; ++i) {
        auto particle = spark(start, color, (3 + static_cast<int>(random() * 5)) * 20,
                       1500 + bounded_count(random() * 3000, 3000), 1.85, {}, time);
        particle.motion = ParticleMotion::stream;
        particle.end = end;
        particle.period = .25 + random() * .5;
        emit(particle);
      }
      break;
    case 4:
      path(target            ? origin
           : effect.has_position2 ? start
                             : end,
           start, color, effect.has_position2 ? static_cast<int>(std::clamp(end.x, 100.0, 10000.0)) : 1500, 200,
           time, ParticleMotion::thrown_spark);
      break;
    case 5:
      aoe(origin, start.x, color, time);
      break;
    case 6:
      for (int i = 0; i < 10; ++i) {
        emit(spark(origin, color, 100, 400, .75, {plus_minus(4), plus_minus(4)}, time));
      }
      break;
    case 7:
    case 11: {
      const double length = distance(origin, start);
      const int n = effect.type == 7 ? 30 : bounded_count(length * 3);
      for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / n;
        auto position = lerp(start, origin, t);
        const double noise = effect.type == 11 ? length / 200 * std::min(i, n - i) : 0;
        position.x += plus_minus(noise);
        position.y += plus_minus(noise);
        emit(spark(position, color, effect.type == 11 ? end.x : 100,
                   effect.type == 11 ? static_cast<int>(1000 - t * 900) : 700, .5,
                   effect.type == 11 ? Vec2{} : Vec2{plus_minus(1), plus_minus(1)}, time));
      }
      break;
    }
    case 8:
    case 10:
    case 12:
    case 13: {
      const int n = effect.type == 10 ? 12 : effect.type == 13 ? 7 : 24;
      const double radius = std::min(128.0, effect.type == 10   ? std::max(0.0, start.x)
                                            : effect.type == 13 ? std::max(0.0, end.x)
                                                           : distance(start, end));
      const Vec2 center = effect.type == 10 || effect.type == 13 ? origin : start;
      for (int i = 0; i < n; ++i) {
        const double angle = effect.type == 13
                                 ? std::atan2(start.y - origin.y, start.x - origin.x) -
                                       std::numbers::pi / 6 + i * (std::numbers::pi / 3) / n
                                 : tau * i / n;
        const Vec2 end{center.x + radius * std::cos(angle), center.y + radius * std::sin(angle)};
        path(effect.type == 12   ? end
             : effect.type == 10 ? lerp(center, end, .9)
                            : center,
             effect.type == 12 ? center : end, color,
             effect.type == 8    ? 100 + bounded_count(random() * 200, 200)
             : effect.type == 13 ? 100
                            : 200,
             effect.type == 12   ? 300
             : effect.type == 13 ? 200
             : effect.type == 10 ? 0
                            : 100,
             time);
      }
      break;
    }
    case 9:
      for (int i = 0; i < 5; ++i) {
        auto particle =
            spark(start, color, (3 + static_cast<int>(random() * 5)) * 20, maximum_lifetime, .5, {}, time);
        particle.motion = ParticleMotion::flow;
        particle.target = effect.target_id;
        particle.speed = random() * 5;
        particle.distance = distance(start, origin);
        emit(particle);
      }
      break;
    case 14:
      jitter_start_ = time;
      break;
    case 15:
      if (start.x > 0 && start.y > 0) {
        if (flash_targets_.may_contain(effect.target_id)) {
          std::erase_if(flashes_,
                        [&](const auto& flash) { return flash.target == effect.target_id; });
        }
        const int period = static_cast<int>(std::clamp(start.x * 1000, 1.0, 10000.0));
        const int repeats = static_cast<int>(std::clamp(start.y, 1.0, 3600000.0 / period));

        if (flashes_.size() < maximum_flashes) {
          flashes_.push_back({effect.target_id, time, period, repeats, color});
          flash_targets_.add(effect.target_id);
        }
      }
      break;
    case 16:
      if (color <= 65535) {
        if (const auto* object = world.catalog().object(static_cast<std::uint16_t>(color))) {
          Particle particle;
          particle.motion = ParticleMotion::thrown_sprite;
          particle.position = particle.start = end;
          particle.end = start;
          particle.born = time;
          particle.lifetime = 1500;
          particle.texture = &object->texture;
          particle.size = 80;
          emit(particle);
        }
      }
      break;
    default:
      break;
  }
}

void Particles::hit(const World& world, const Entity& target, int amount, std::int32_t time,
                    double angle, double speed, bool directional, int count) {
  if (!enabled_ || static_cast<std::int64_t>(target.combat.hp) - amount <= 0) {
    return;
  }
  burst(world, target, time, count, directional, angle, speed);
}

void Particles::explode(const World& world, const Entity& target, std::int32_t time) {
  if (enabled_) {
    burst(world, target, time, 30, false, 0, 0);
  }
}

void Particles::burst(const World& world, const Entity& target, std::int32_t time, int count,
                      bool directional, double angle, double speed) {
  const auto* definition = world.catalog().object(target.type);
  if (!definition) {
    return;
  }
  const auto& texture =
      target.player ? world.catalog().player_texture(target.type, target.appearance.texture) : definition->texture;
  const auto it = texture.alternate.find(target.appearance.alt_texture);
  const auto& selected = it == texture.alternate.end() ? texture : it->second;

  for (int i = 0; i < count; ++i) {
    const auto sample = color(selected, static_cast<std::uint32_t>(target.id));
    if (!sample) {
      return;
    }
    auto particle = spark(target.motion.position, random() < definition->blood_probability ? definition->blood_color : *sample,
                   target.appearance.size, 200 + bounded_count(random() * 100, 100), .5, {}, time);
    particle.motion = ParticleMotion::linear;
    particle.velocity = directional ? Vec2{(-speed / 600 * std::cos(angle) + plus_minus(.2)) * 8,
                                    (-speed / 600 * std::sin(angle) + plus_minus(.2)) * 8}
                             : Vec2{plus_minus(.5) * 8, plus_minus(.5) * 8};
    emit(particle);
  }
}

void Particles::impact(const TextureDef& texture, Vec2 at, double angle, double speed,
                       std::int32_t time) {
  if (!enabled_) {
    return;
  }
  for (int i = 0; i < 3; ++i) {
    const auto sample = color(texture, 0);
    if (!sample) {
      return;
    }
    auto particle = spark(at, *sample, 100, 200 + bounded_count(random() * 100, 100), .5,
                   {(-speed / 600 * std::cos(angle) + plus_minus(.2)) * 8,
                    (-speed / 600 * std::sin(angle) + plus_minus(.2)) * 8},
                   time);
    particle.motion = ParticleMotion::linear;
    emit(particle);
  }
}

void Particles::trail(Vec2 at, std::int32_t time, int count) {
  if (!enabled_) {
    return;
  }
  for (int i = 0; i < count; ++i) {
    emit(spark(at, 0xff00ff, 100, 600, .5, {plus_minus(3), plus_minus(3)}, time));
  }
}

void Particles::level_up(std::int32_t target, std::int32_t time) {
  for (int j = 0; j < 2; ++j) {
    for (int i = 0; i < 20; ++i) {
      Particle particle;
      particle.motion = ParticleMotion::swirl;
      particle.target = target;
      particle.born = time;
      particle.lifetime = 2000;
      particle.color = 0xff00ff00;
      particle.angle = tau * i / 20 + j * std::numbers::pi;
      particle.distance = static_cast<double>(i) / 20;
      particle.size = 100;
      emit(particle);
    }
  }
}

Vec2 Particles::camera_offset(std::int32_t time) const {
  if (jitter_start_ < 0) {
    return {};
  }
  const auto elapsed = std::max<std::int64_t>(0, static_cast<std::int64_t>(time) - jitter_start_);
  const double amplitude = std::min(.5, static_cast<double>(elapsed) * .5 / 10000.0);
  // Presentation-only noise, independent of combat's server-seeded RNG.
  return {std::sin(time * 12.9898) * amplitude, std::sin(time * 78.233) * amplitude};
}

const TextureDef* Particles::texture(const Particle& particle, std::int32_t time) const {
  if (!particle.definition || particle.animation < 0) {
    return particle.texture;
  }
  double age = static_cast<double>(static_cast<std::int64_t>(time) - particle.born);
  for (const auto& frame : particle.definition->animations[static_cast<std::size_t>(particle.animation)].frames) {
    if (age <= frame.duration_ms) {
      return &frame.texture;
    }
    age -= frame.duration_ms;
  }
  return particle.texture;
}

void Particles::update(const World& world, std::int32_t time) {
  now_ = time;
  if (last_update_ == time) {
    return;
  }
  const double dt = last_update_ < 0
                        ? 0
                        : std::clamp(static_cast<double>(now_ - last_update_), 0.0, 150.0) / 1000;
  last_update_ = time;

  // One pass: the survivors refill the flash filter.
  flash_targets_.clear();
  std::erase_if(flashes_, [&](const auto& flash) {
    const bool expired = !world.entity(flash.target) ||
                         now_ - flash.born > static_cast<std::int64_t>(flash.period) * flash.repeats;
    if (!expired) {
      flash_targets_.add(flash.target);
    }
    return expired;
  });
  REALM_ZONE("particles.update");
  if (particles_.capacity() < maximum_particles) {
    particles_.reserve(maximum_particles);  // a copied pool did not keep its capacity
  }

  // Refilled below from the survivors; particles emitted during the loop add their own bits.
  particle_targets_.clear();
  const auto initial = particles_.size();
  for (std::size_t i = 0; i < initial; ++i) {
    // In place: emit() never grows the pool past the capacity reserved above, so this reference
    // stays valid while sparkers emit (copying each 224-byte particle out and back cost more than
    // updating it).
    auto& particle = particles_[i];
    const auto age = now_ - particle.born;
    const double seconds = static_cast<double>(age) / 1000;
    const double phase = static_cast<double>(age) / particle.lifetime;
    const auto* target = particle.target == -1 ? nullptr : world.entity(particle.target);
    bool alive = age >= 0 && age < particle.lifetime && (particle.target == -1 || target);
    if (alive) {
      switch (particle.motion) {
        case ParticleMotion::linear:
        case ParticleMotion::spark:
        case ParticleMotion::fountain:
        case ParticleMotion::bubble:
        case ParticleMotion::xml:
          particle.position = {particle.start.x + particle.velocity.x * seconds,
                               particle.start.y + particle.velocity.y * seconds};
          particle.z = particle.initial_z + particle.vz * seconds +
                       particle.az * seconds * seconds / 2;
          if (particle.motion == ParticleMotion::spark) {
            particle.size = particle.initial_size * (1 - phase);
          }
          if (particle.motion == ParticleMotion::fountain && particle.z <= 0) {
            alive = false;
          }
          if (particle.motion == ParticleMotion::bubble && particle.z >= 1 && particle.noise == 0) {
            alive = false;
          }
          break;
        case ParticleMotion::sparker:
        case ParticleMotion::thrown_spark:
        case ParticleMotion::thrown_sprite:
          particle.position = lerp(particle.start, particle.end, phase);
          if (particle.motion != ParticleMotion::sparker) {
            particle.z = std::sin(phase * std::numbers::pi) * 2;
          }
          if (particle.motion != ParticleMotion::thrown_sprite) {
            const auto steps =
                std::min<std::int64_t>(3, std::max<std::int64_t>(0, (now_ - particle.last_emission) / 17));
            for (std::int64_t n = 0; n < steps; ++n) {
              emit(spark(particle.position, particle.color, 100 * (particle.z + 1),
                         particle.motion == ParticleMotion::sparker ? 600 : 400, particle.z,
                         {plus_minus(1), plus_minus(1)}, now_));
            }
            if (steps) {
              particle.last_emission = now_;
            }
          }
          break;
        case ParticleMotion::heal:
          particle.position = {target->motion.position.x + particle.distance * particle.angle_cos,
                        target->motion.position.y + particle.distance * particle.angle_sin};
          particle.z = particle.initial_z + particle.vz * seconds;
          break;
        case ParticleMotion::stream: {
          const double length = distance(particle.start, particle.end);
          particle.position = lerp(particle.start, particle.end, phase);
          if (length > 0) {
            const double v =
                std::sin(static_cast<double>(particle.lifetime - age) / 1000.0 / particle.period) * .25;
            particle.position.x += (particle.end.y - particle.start.y) / length * v;
            particle.position.y -= (particle.end.x - particle.start.x) / length * v;
          }
          break;
        }
        case ParticleMotion::flow: {
          const double length = distance(particle.position, target->motion.position);
          if (length < .5) {
            alive = false;
            break;
          }
          particle.speed += 8 * dt;
          particle.distance -= particle.speed * dt;
          const double remaining = std::max(0.0, std::min(length - particle.speed * dt, particle.distance));
          particle.position = lerp(target->motion.position, particle.position, remaining / length);
          break;
        }
        case ParticleMotion::swirl: {
          particle.z = phase * 2 - 1 + particle.distance;
          const double angle = particle.angle + tau * phase;
          particle.position = {target->motion.position.x + .5 * std::cos(angle),
                        target->motion.position.y + .5 * std::sin(angle)};
          break;
        }
        case ParticleMotion::gas: {
          particle.position.x += particle.velocity.x * random() * particle.noise * dt;
          particle.position.y += particle.velocity.y * random() * particle.noise * dt;
          particle.size = particle.initial_size * (1 - phase);
          const auto* tile = finite(particle.position) ? world.tile(static_cast<int>(particle.position.x),
                                                             static_cast<int>(particle.position.y))
                                                : nullptr;
          const auto* object = tile ? world.entity(tile->object_id) : nullptr;
          const auto* definition = object ? world.catalog().object(object->type) : nullptr;
          if (definition && definition->stationary) {
            alive = false;
          }
          break;
        }
      }
    }
    if (!alive || !finite(particle.position) || !std::isfinite(particle.z)) {
      particle.lifetime = 0;
    }
  }

  std::erase_if(particles_, [&](const auto& particle) {
    if (particle.lifetime == 0) {
      return true;
    }
    if (particle.target != -1) {
      particle_targets_.add(particle.target);
    }
    return false;
  });

  emitter_targets_.clear();
  for (auto& emitter : emitters_) {
    emitter_targets_.add(emitter.target);
    const auto* entity = world.entity(emitter.target);
    if (!entity) {
      continue;
    }
    const auto& props = *emitter.props;
    const auto kind = emitter.kind;
    double period = kind == EmitterKind::healing                    ? 500
                    : kind == EmitterKind::fountain || kind == EmitterKind::vent ? 50
                    : kind == EmitterKind::bubbles ? std::max(1.0, (1 - props.rate) * 400 + 1)
                    : kind == EmitterKind::xml     ? std::max(1.0, props.cooldown * 1000)
                                                   : 0;
    int count{};
    if (period > 0) {
      count = bounded_count(std::max(0.0, static_cast<double>(now_) - emitter.last) / period, 3);
      if (count) {
        emitter.last = static_cast<double>(now_);
      }
    } else {
      emitter.credit += std::clamp(props.rate, 0.0, 1000.0) * dt *
                        (kind == EmitterKind::gas ? 60 : 1);
      count = bounded_count(emitter.credit, 64);
      emitter.credit -= std::floor(emitter.credit);
    }
    for (int n = 0; n < count; ++n) {
      if (kind == EmitterKind::healing) {
        heal(*entity, 0xffffff, time, 1);
        continue;
      }
      Particle particle;
      particle.position = particle.start = entity->motion.position;
      particle.born = now_;
      particle.color = props.color;
      if (kind == EmitterKind::fountain) {
        const double angle = random() * tau;
        particle.motion = ParticleMotion::fountain;
        particle.velocity = {.8 * std::cos(angle), .8 * std::sin(angle)};
        particle.z = .75;
        particle.vz = 6.5;
        particle.az = -9.8;
        particle.lifetime = 1500;
        particle.color = 4285909;
        particle.size = 100;
      } else if (kind == EmitterKind::vent || kind == EmitterKind::bubbles) {
        const double angle = random() * std::numbers::pi;
        const double radius = random() * .4;
        const double roll = random();
        particle.motion = ParticleMotion::bubble;
        particle.position = particle.start = {entity->motion.position.x + radius * std::cos(angle),
                                              entity->motion.position.y + radius * std::sin(angle)};
        particle.size = 75 + random() * 50;
        particle.z = 0;
        if (kind == EmitterKind::vent) {
          particle.color = 2542335;
          particle.vz = static_cast<int>(2.5 - roll * 1.5);
          particle.lifetime = 1000;
        } else {
          particle.vz = (props.speed - props.speed * (roll * (1 - props.speed_variance))) * 10;
          particle.lifetime = static_cast<int>(
              std::clamp(props.life * 1000 * (1 - roll * (1 - props.life_variance)), 1.0, 10000.0));
          particle.noise = 1;
          if (props.spread > 0) {
            particle.velocity = {(random() * props.spread - .1) * 2.5,
                          (random() * props.spread - .1) * 2.5};
          }
        }
      } else if (kind == EmitterKind::gas) {
        const double roll = random();
        particle.motion = ParticleMotion::gas;
        particle.size = (random() + .3) * 200;
        particle.z = .75;
        particle.noise = props.spread;
        const double speed = props.speed * (1 - roll * (1 - props.speed_variance));
        particle.velocity = {plus_minus(speed), plus_minus(speed)};
        particle.lifetime = static_cast<int>(
            std::clamp(props.life * 1000 * (1 - roll * props.life_variance), 1.0, 10000.0));
      } else if (kind == EmitterKind::xml) {
        particle.definition = world.catalog().particle(props.particle);
        if (!particle.definition) {
          continue;
        }
        const auto& particle_def = *particle.definition;
        particle.motion = ParticleMotion::xml;
        particle.size = particle_def.size;
        particle.z = particle_def.z;
        particle.texture = &particle_def.texture;
        particle.bottom_anchor = true;
        particle.lifetime = static_cast<int>(std::clamp(particle_def.duration_ms, 1.0, 10000.0));
        const double angle = random() * tau;
        particle.velocity = {.5 * std::cos(angle), .5 * std::sin(angle)};
        for (std::size_t index = 0; index < particle_def.animations.size(); ++index) {
          if (random() <= particle_def.animations[index].probability) {
            particle.animation = static_cast<int>(index);
            break;
          }
        }
      } else if (kind == EmitterKind::custom) {
        particle.motion = ParticleMotion::linear;
        particle.target = entity->id;
        particle.custom_size = true;
        particle.size = props.size;
        particle.z = static_cast<int>(entity->motion.z + props.z_offset);
        particle.position = particle.start = {entity->motion.position.x + props.range_x * plus_minus(1),
                                entity->motion.position.y + props.range_y * plus_minus(1)};
        particle.velocity = {props.speed + props.speed_variance * plus_minus(1),
                      props.speed + props.speed_variance * plus_minus(1)};
        particle.vz = props.rise + props.rise_variance * plus_minus(1);
        particle.az = props.rise_acc;
        particle.lifetime = static_cast<int>(
            std::clamp((props.life + props.life_variance * plus_minus(1)) * 1000, 1.0, 10000.0));
      } else {
        continue;
      }
      emit(particle);
    }
  }

  spawns_ = 0;
}
}
