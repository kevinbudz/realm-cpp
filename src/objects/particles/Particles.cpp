#include "realm/objects/particles/Particles.hpp"
#include "realm/map/Map.hpp"
#include "realm/util/Telemetry.hpp"
#include "flash_particle_palettes.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
namespace realm {
namespace {
constexpr double tau = 2 * std::numbers::pi;
bool finite(Vec2 p) {
  return std::isfinite(p.x) && std::isfinite(p.y) && std::abs(p.x) <= 1048576 && std::abs(p.y) <= 1048576;
}
Vec2 lerp(Vec2 a, Vec2 b, double t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}; }
double distance(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }
int bounded_count(double value, int limit = 256) {
  return std::isfinite(value) ? static_cast<int>(std::clamp(value, 0.0, static_cast<double>(limit))) : 0;
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
// The Particles option stops the optional effects it gates (hit and wall-impact bursts, projectile trails, Healing
// and the object-texture effects); ShowEffect, AoE and level-up effects are gameplay information and always play.
void Particles::set_enabled(bool value) { enabled_ = value; }
double Particles::random() {
  rng_ ^= rng_ << 13U;
  rng_ ^= rng_ >> 17U;
  rng_ ^= rng_ << 5U;
  return rng_ / 4294967296.0;
}
bool Particles::emit(Particle p) {
  if (particles_.size() >= maximum_particles || spawns_ >= maximum_spawns) {
    ++dropped_;
    return false;
  }
  if (!finite(p.position) || !finite(p.velocity) || !std::isfinite(p.z) || !std::isfinite(p.size)) return false;
  p.lifetime = std::clamp(p.lifetime, 1, maximum_lifetime);
  p.initial_size = p.size = std::clamp(p.size, 0.0, 640.0);
  p.initial_z = p.z;
  p.order = order_++;
  particles_.push_back(p);
  if (p.target != -1) particle_targets_.add(p.target);
  ++spawns_;
  return true;
}
Particle Particles::spark(Vec2 at, std::uint32_t c, double size, int life, double z, Vec2 velocity, std::int64_t time) {
  Particle p;
  p.motion = ParticleMotion::spark;
  p.position = p.start = at;
  p.color = c;
  p.size = size;
  p.lifetime = life;
  p.z = z;
  p.velocity = velocity;
  p.born = time;
  return p;
}
std::optional<std::uint32_t> Particles::color(const TextureDef& t, std::uint32_t id) {
  const auto& s = t.select(id);
  const auto lookup = [&](const auto& starts, int sheet, int index) -> flash::PaletteRange {
    if (sheet < 0 || index < 0 || static_cast<std::size_t>(sheet + 1) >= starts.size()) return {};
    const auto first = starts[static_cast<std::size_t>(sheet)], last = starts[static_cast<std::size_t>(sheet + 1)];
    return static_cast<unsigned>(index) < last - first ? flash::particle_palettes[first + static_cast<unsigned>(index)]
                                                       : flash::PaletteRange{};
  };
  auto range = s.animated >= 0 ? lookup(flash::particle_animated_starts, s.animated, s.index)
                               : lookup(flash::particle_sheet_starts, s.sheet, s.index);
  if (!range.count) return {};
  return flash::particle_colors[range.offset + static_cast<unsigned>(random() * range.count)];
}
void Particles::attach(const Entity& e, const TextureDef& t) {
  // Nearly every object (every projectile) has no emitter: the filter answers that without scanning the emitter list.
  const bool maybe = emitter_targets_.may_contain(e.id);
  if (!maybe && t.effect.kind.empty()) return;
  const auto it = !maybe ? emitters_.end() : std::find_if(emitters_.begin(), emitters_.end(), [&](const auto& emitter) {
    return emitter.target == e.id && emitter.props->kind != "Healing";
  });
  if (t.effect.kind.empty()) {
    if (it != emitters_.end()) emitters_.erase(it);
    return;
  }
  // ParticleEffect.fromProps returns null while the option is off; an existing effect keeps running.
  if (!enabled_ && it == emitters_.end()) return;
  if (it != emitters_.end()) {
    if (it->props != &t.effect) *it = {e.id, &t.effect, static_cast<double>(now_), 0};
    return;
  }
  if (emitters_.size() < maximum_emitters) {
    emitters_.push_back({e.id, &t.effect, static_cast<double>(now_), 0});
    emitter_targets_.add(e.id);
  } else
    ++dropped_;
}
void Particles::healing(const Entity& e) {
  static const EffectDef props = [] {
    EffectDef value;
    value.kind = "Healing";
    return value;
  }();
  const bool maybe = emitter_targets_.may_contain(e.id);
  if (!maybe && !(e.combat.condition & (1U << 15U))) return;
  const auto it = !maybe ? emitters_.end() : std::find_if(emitters_.begin(), emitters_.end(), [&](const auto& emitter) {
    return emitter.target == e.id && emitter.props == &props;
  });
  if (!(e.combat.condition & (1U << 15U))) {
    if (it != emitters_.end()) emitters_.erase(it);
    return;
  }
  if (it == emitters_.end() && enabled_ && emitters_.size() < maximum_emitters) {
    emitters_.push_back({e.id, &props, static_cast<double>(now_) - 500, 0});
    emitter_targets_.add(e.id);
  }
}
// Object removal runs for every expiring projectile, so the three scans (up to 8192 particles of 224 bytes) are gated
// by the target filters. Untargeted particles carry id -1, so that id (never a real object) always takes the exact
// path.
void Particles::remove_target(std::int32_t id) {
  if (emitter_targets_.may_contain(id)) std::erase_if(emitters_, [&](const auto& e) { return e.target == id; });
  if (id == -1 || particle_targets_.may_contain(id))
    std::erase_if(particles_, [&](const auto& p) { return p.target == id; });
  if (flash_targets_.may_contain(id)) std::erase_if(flashes_, [&](const auto& f) { return f.target == id; });
}
bool Particles::targets(std::int32_t id) const {
  return std::any_of(emitters_.begin(), emitters_.end(), [&](const auto& e) { return e.target == id; }) ||
         std::any_of(particles_.begin(), particles_.end(), [&](const auto& p) { return p.target == id; }) ||
         std::any_of(flashes_.begin(), flashes_.end(), [&](const auto& f) { return f.target == id; });
}
void Particles::rebuild_target_filters() {
  particle_targets_.clear();
  emitter_targets_.clear();
  flash_targets_.clear();
  for (const auto& p : particles_)
    if (p.target != -1) particle_targets_.add(p.target);
  for (const auto& e : emitters_) emitter_targets_.add(e.target);
  for (const auto& f : flashes_) flash_targets_.add(f.target);
}
void Particles::path(Vec2 start, Vec2 end, std::uint32_t c, int life, double size, std::int32_t time,
                     ParticleMotion motion) {
  if (!finite(start) || !finite(end)) return;
  Particle p;
  p.motion = motion;
  p.position = p.start = start;
  p.end = end;
  p.born = time;
  p.last_emission = time - 17;
  p.color = c;
  p.size = size;
  p.lifetime = life;
  emit(p);
}
void Particles::heal(const Entity& e, std::uint32_t c, std::int32_t time, int count) {
  for (int i = 0; i < count; ++i) {
    Particle p;
    p.motion = ParticleMotion::heal;
    p.target = e.id;
    p.angle = count == 1 ? random() * tau : tau * i / count;
    p.distance = .3 + .4 * random();
    p.position = {e.motion.position.x + p.distance * std::cos(p.angle),
                  e.motion.position.y + p.distance * std::sin(p.angle)};
    p.color = c;
    p.size = (3 + static_cast<int>(random() * 5)) * 20;
    p.z = random() * .3;
    p.vz = (.1 + random() * .1) * 8;
    p.born = time;
    emit(p);
  }
}
void Particles::aoe(Vec2 at, double radius, std::uint32_t c, std::int32_t time) {
  if (!std::isfinite(radius) || radius < 0) return;
  radius = std::min(radius, 128.0);
  const int num = bounded_count(4 + radius * 2);
  for (int i = 0; i < num; ++i)
    path(at, {at.x + radius * std::cos(tau * i / num), at.y + radius * std::sin(tau * i / num)}, c, 200, 200, time);
}
void Particles::show(const World& world, const net::incoming::ShowEffect& e, std::int32_t time) {
  const auto* target = world.entity(e.target_id);
  const Vec2 a{e.position.x, e.position.y}, b{e.position2.x, e.position2.y};
  if (!finite(a) || !finite(b)) return;
  const auto c = e.color;
  const Vec2 origin = target ? target->motion.position : Vec2{};
  if (e.type != 0 && e.type != 2 && e.type != 3 && e.type != 4 && e.type != 14 && e.type != 16 && !target) return;
  switch (e.type) {
    case 1:
      heal(*target, c, time, 10);
      break;
    case 2:
      for (int i = 0; i < 20; ++i) {
        const double angle = random() * tau, r = .7 * random();
        auto p = spark({a.x + r * std::cos(angle), a.y + r * std::sin(angle)}, 255, 50,
                       500 + bounded_count(random() * 1000, 1000), 0, {}, time);
        p.motion = ParticleMotion::linear;
        p.vz = .8;
        emit(p);
      }
      break;
    case 3:
      for (int i = 0; i < 5; ++i) {
        auto p = spark(a, c, (3 + static_cast<int>(random() * 5)) * 20, 1500 + bounded_count(random() * 3000, 3000),
                       1.85, {}, time);
        p.motion = ParticleMotion::stream;
        p.end = b;
        p.period = .25 + random() * .5;
        emit(p);
      }
      break;
    case 4:
      path(target            ? origin
           : e.has_position2 ? a
                             : b,
           a, c, e.has_position2 ? static_cast<int>(std::clamp(b.x, 100.0, 10000.0)) : 1500, 200, time,
           ParticleMotion::thrown_spark);
      break;
    case 5:
      aoe(origin, a.x, c, time);
      break;
    case 6:
      for (int i = 0; i < 10; ++i) emit(spark(origin, c, 100, 400, .75, {plus_minus(4), plus_minus(4)}, time));
      break;
    case 7:
    case 11: {
      const double d = distance(origin, a);
      const int n = e.type == 7 ? 30 : bounded_count(d * 3);
      for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / n;
        auto pos = lerp(a, origin, t);
        const double noise = e.type == 11 ? d / 200 * std::min(i, n - i) : 0;
        pos.x += plus_minus(noise);
        pos.y += plus_minus(noise);
        emit(spark(pos, c, e.type == 11 ? b.x : 100, e.type == 11 ? static_cast<int>(1000 - t * 900) : 700, .5,
                   e.type == 11 ? Vec2{} : Vec2{plus_minus(1), plus_minus(1)}, time));
      }
      break;
    }
    case 8:
    case 10:
    case 12:
    case 13: {
      const int n = e.type == 10 ? 12 : e.type == 13 ? 7 : 24;
      const double radius = std::min(128.0, e.type == 10   ? std::max(0.0, a.x)
                                            : e.type == 13 ? std::max(0.0, b.x)
                                                           : distance(a, b));
      const Vec2 center = e.type == 10 || e.type == 13 ? origin : a;
      for (int i = 0; i < n; ++i) {
        const double angle = e.type == 13 ? std::atan2(a.y - origin.y, a.x - origin.x) - std::numbers::pi / 6 +
                                                i * (std::numbers::pi / 3) / n
                                          : tau * i / n;
        const Vec2 end{center.x + radius * std::cos(angle), center.y + radius * std::sin(angle)};
        path(e.type == 12   ? end
             : e.type == 10 ? lerp(center, end, .9)
                            : center,
             e.type == 12 ? center : end, c,
             e.type == 8    ? 100 + bounded_count(random() * 200, 200)
             : e.type == 13 ? 100
                            : 200,
             e.type == 12   ? 300
             : e.type == 13 ? 200
             : e.type == 10 ? 0
                            : 100,
             time);
      }
      break;
    }
    case 9:
      for (int i = 0; i < 5; ++i) {
        auto p = spark(a, c, (3 + static_cast<int>(random() * 5)) * 20, maximum_lifetime, .5, {}, time);
        p.motion = ParticleMotion::flow;
        p.target = e.target_id;
        p.speed = random() * 5;
        p.distance = distance(a, origin);
        emit(p);
      }
      break;
    case 14:
      jitter_start_ = time;
      break;
    case 15:
      if (a.x > 0 && a.y > 0) {
        if (flash_targets_.may_contain(e.target_id))
          std::erase_if(flashes_, [&](const auto& f) { return f.target == e.target_id; });
        const int period = static_cast<int>(std::clamp(a.x * 1000, 1.0, 10000.0));
        const int repeats = static_cast<int>(std::clamp(a.y, 1.0, 3600000.0 / period));
        if (flashes_.size() < maximum_flashes) {
          flashes_.push_back({e.target_id, time, period, repeats, c});
          flash_targets_.add(e.target_id);
        }
      }
      break;
    case 16:
      if (c <= 65535)
        if (const auto* d = world.catalog().object(static_cast<std::uint16_t>(c))) {
          Particle p;
          p.motion = ParticleMotion::thrown_sprite;
          p.position = p.start = b;
          p.end = a;
          p.born = time;
          p.lifetime = 1500;
          p.texture = &d->texture;
          p.size = 80;
          emit(p);
        }
      break;
    default:
      break;
  }
}
void Particles::hit(const World& world, const Entity& e, int amount, std::int32_t time, double angle, double speed,
                    bool directional, int count) {
  if (!enabled_ || static_cast<std::int64_t>(e.combat.hp) - amount <= 0) return;
  const auto* d = world.catalog().object(e.type);
  if (!d) return;
  const auto& texture = e.player ? world.catalog().player_texture(e.type, e.appearance.texture) : d->texture;
  const auto it = texture.alternate.find(e.appearance.alt_texture);
  const auto& t = it == texture.alternate.end() ? texture : it->second;
  for (int i = 0; i < count; ++i) {
    const auto sample = color(t, static_cast<std::uint32_t>(e.id));
    if (!sample) return;
    auto p = spark(e.motion.position, random() < d->blood_probability ? d->blood_color : *sample, e.appearance.size,
                   200 + bounded_count(random() * 100, 100), .5, {}, time);
    p.motion = ParticleMotion::linear;
    p.velocity = directional ? Vec2{(-speed / 600 * std::cos(angle) + plus_minus(.2)) * 8,
                                    (-speed / 600 * std::sin(angle) + plus_minus(.2)) * 8}
                             : Vec2{plus_minus(.5) * 8, plus_minus(.5) * 8};
    emit(p);
  }
}
void Particles::impact(const TextureDef& t, Vec2 at, double angle, double speed, std::int32_t time) {
  if (!enabled_) return;
  for (int i = 0; i < 3; ++i) {
    const auto sample = color(t, 0);
    if (!sample) return;
    auto p = spark(
        at, *sample, 100, 200 + bounded_count(random() * 100, 100), .5,
        {(-speed / 600 * std::cos(angle) + plus_minus(.2)) * 8, (-speed / 600 * std::sin(angle) + plus_minus(.2)) * 8},
        time);
    p.motion = ParticleMotion::linear;
    emit(p);
  }
}
void Particles::trail(Vec2 at, std::int32_t time, int count) {
  if (!enabled_) return;
  for (int i = 0; i < count; ++i) emit(spark(at, 0xff00ff, 100, 600, .5, {plus_minus(3), plus_minus(3)}, time));
}
void Particles::level_up(std::int32_t target, std::int32_t time) {
  for (int j = 0; j < 2; ++j)
    for (int i = 0; i < 20; ++i) {
      Particle p;
      p.motion = ParticleMotion::swirl;
      p.target = target;
      p.born = time;
      p.lifetime = 2000;
      p.color = 0xff00ff00;
      p.angle = tau * i / 20 + j * std::numbers::pi;
      p.distance = static_cast<double>(i) / 20;
      p.size = 100;
      emit(p);
    }
}
Vec2 Particles::camera_offset(std::int32_t time) const {
  if (jitter_start_ < 0) return {};
  const auto t = std::max<std::int64_t>(0, static_cast<std::int64_t>(time) - jitter_start_);
  const double amplitude = std::min(.5, static_cast<double>(t) * .5 / 10000.0);
  // Presentation-only noise, independent of combat's server-seeded RNG.
  return {std::sin(time * 12.9898) * amplitude, std::sin(time * 78.233) * amplitude};
}
const TextureDef* Particles::texture(const Particle& p, std::int32_t time) const {
  if (!p.definition || p.animation < 0) return p.texture;
  double age = static_cast<double>(static_cast<std::int64_t>(time) - p.born);
  for (const auto& f : p.definition->animations[static_cast<std::size_t>(p.animation)].frames) {
    if (age <= f.duration_ms) return &f.texture;
    age -= f.duration_ms;
  }
  return p.texture;
}
void Particles::update(const World& world, std::int32_t time) {
  now_ = time;
  if (last_update_ == time) return;
  const double dt = last_update_ < 0 ? 0 : std::clamp(static_cast<double>(now_ - last_update_), 0.0, 150.0) / 1000;
  last_update_ = time;
  std::erase_if(flashes_, [&](const auto& f) {
    return !world.entity(f.target) || now_ - f.born > static_cast<std::int64_t>(f.period) * f.repeats;
  });
  REALM_ZONE("particles.update");
  if (particles_.capacity() < maximum_particles)
    particles_.reserve(maximum_particles);  // a copied pool does not keep its capacity
  const auto initial = particles_.size();
  for (std::size_t i = 0; i < initial; ++i) {
    // In place: emit() never grows the pool past the capacity reserved above, so this reference stays valid
    // while sparkers emit (copying each 224-byte particle out and back cost more than updating it).
    auto& p = particles_[i];
    const auto age = now_ - p.born;
    const double t = static_cast<double>(age) / 1000, phase = static_cast<double>(age) / p.lifetime;
    const auto* target = p.target == -1 ? nullptr : world.entity(p.target);
    bool alive = age >= 0 && age < p.lifetime && (p.target == -1 || target);
    if (alive) switch (p.motion) {
        case ParticleMotion::linear:
        case ParticleMotion::spark:
        case ParticleMotion::fountain:
        case ParticleMotion::bubble:
        case ParticleMotion::xml:
          p.position = {p.start.x + p.velocity.x * t, p.start.y + p.velocity.y * t};
          p.z = p.initial_z + p.vz * t + p.az * t * t / 2;
          if (p.motion == ParticleMotion::spark) p.size = p.initial_size * (1 - phase);
          if (p.motion == ParticleMotion::fountain && p.z <= 0) alive = false;
          if (p.motion == ParticleMotion::bubble && p.z >= 1 && p.noise == 0) alive = false;
          break;
        case ParticleMotion::sparker:
        case ParticleMotion::thrown_spark:
        case ParticleMotion::thrown_sprite:
          p.position = lerp(p.start, p.end, phase);
          if (p.motion != ParticleMotion::sparker) p.z = std::sin(phase * std::numbers::pi) * 2;
          if (p.motion != ParticleMotion::thrown_sprite) {
            const auto steps = std::min<std::int64_t>(3, std::max<std::int64_t>(0, (now_ - p.last_emission) / 17));
            for (std::int64_t n = 0; n < steps; ++n)
              emit(spark(p.position, p.color, 100 * (p.z + 1), p.motion == ParticleMotion::sparker ? 600 : 400, p.z,
                         {plus_minus(1), plus_minus(1)}, now_));
            if (steps) p.last_emission = now_;
          }
          break;
        case ParticleMotion::heal:
          p.position = {target->motion.position.x + p.distance * std::cos(p.angle),
                        target->motion.position.y + p.distance * std::sin(p.angle)};
          p.z = p.initial_z + p.vz * t;
          break;
        case ParticleMotion::stream: {
          const double d = distance(p.start, p.end);
          p.position = lerp(p.start, p.end, phase);
          if (d > 0) {
            const double v = std::sin(static_cast<double>(p.lifetime - age) / 1000.0 / p.period) * .25;
            p.position.x += (p.end.y - p.start.y) / d * v;
            p.position.y -= (p.end.x - p.start.x) / d * v;
          }
          break;
        }
        case ParticleMotion::flow: {
          const double d = distance(p.position, target->motion.position);
          if (d < .5) {
            alive = false;
            break;
          }
          p.speed += 8 * dt;
          p.distance -= p.speed * dt;
          const double remaining = std::max(0.0, std::min(d - p.speed * dt, p.distance));
          p.position = lerp(target->motion.position, p.position, remaining / d);
          break;
        }
        case ParticleMotion::swirl: {
          p.z = phase * 2 - 1 + p.distance;
          const double a = p.angle + tau * phase;
          p.position = {target->motion.position.x + .5 * std::cos(a), target->motion.position.y + .5 * std::sin(a)};
          break;
        }
        case ParticleMotion::gas: {
          p.position.x += p.velocity.x * random() * p.noise * dt;
          p.position.y += p.velocity.y * random() * p.noise * dt;
          p.size = p.initial_size * (1 - phase);
          const auto* tile =
              finite(p.position) ? world.tile(static_cast<int>(p.position.x), static_cast<int>(p.position.y)) : nullptr;
          const auto* object = tile ? world.entity(tile->object_id) : nullptr;
          const auto* def = object ? world.catalog().object(object->type) : nullptr;
          if (def && def->stationary) alive = false;
          break;
        }
      }
    if (!alive || !finite(p.position) || !std::isfinite(p.z)) p.lifetime = 0;
  }
  std::erase_if(particles_, [](const auto& p) { return p.lifetime == 0; });
  for (auto& emitter : emitters_) {
    const auto* e = world.entity(emitter.target);
    if (!e) continue;
    const auto& props = *emitter.props;
    const auto& kind = props.kind;
    double period = kind == "Healing"                      ? 500
                    : kind == "Fountain" || kind == "Vent" ? 50
                    : kind == "Bubbles"                    ? std::max(1.0, (1 - props.rate) * 400 + 1)
                    : kind == "XMLEffect"                  ? std::max(1.0, props.cooldown * 1000)
                                                           : 0;
    int count{};
    if (period > 0) {
      count = bounded_count(std::max(0.0, static_cast<double>(now_) - emitter.last) / period, 3);
      if (count) emitter.last = static_cast<double>(now_);
    } else {
      emitter.credit += std::clamp(props.rate, 0.0, 1000.0) * dt * (kind == "Gas" ? 60 : 1);
      count = bounded_count(emitter.credit, 64);
      emitter.credit -= std::floor(emitter.credit);
    }
    for (int n = 0; n < count; ++n) {
      if (kind == "Healing") {
        heal(*e, 0xffffff, time, 1);
        continue;
      }
      Particle p;
      p.position = p.start = e->motion.position;
      p.born = now_;
      p.color = props.color;
      if (kind == "Fountain") {
        const double a = random() * tau;
        p.motion = ParticleMotion::fountain;
        p.velocity = {.8 * std::cos(a), .8 * std::sin(a)};
        p.z = .75;
        p.vz = 6.5;
        p.az = -9.8;
        p.lifetime = 1500;
        p.color = 4285909;
        p.size = 100;
      } else if (kind == "Vent" || kind == "Bubbles") {
        const double a = random() * std::numbers::pi, d = random() * .4, r = random();
        p.motion = ParticleMotion::bubble;
        p.position = p.start = {e->motion.position.x + d * std::cos(a), e->motion.position.y + d * std::sin(a)};
        p.size = 75 + random() * 50;
        p.z = 0;
        if (kind == "Vent") {
          p.color = 2542335;
          p.vz = static_cast<int>(2.5 - r * 1.5);
          p.lifetime = 1000;
        } else {
          p.vz = (props.speed - props.speed * (r * (1 - props.speed_variance))) * 10;
          p.lifetime =
              static_cast<int>(std::clamp(props.life * 1000 * (1 - r * (1 - props.life_variance)), 1.0, 10000.0));
          p.noise = 1;
          if (props.spread > 0)
            p.velocity = {(random() * props.spread - .1) * 2.5, (random() * props.spread - .1) * 2.5};
        }
      } else if (kind == "Gas") {
        const double r = random();
        p.motion = ParticleMotion::gas;
        p.size = (random() + .3) * 200;
        p.z = .75;
        p.noise = props.spread;
        const double speed = props.speed * (1 - r * (1 - props.speed_variance));
        p.velocity = {plus_minus(speed), plus_minus(speed)};
        p.lifetime = static_cast<int>(std::clamp(props.life * 1000 * (1 - r * props.life_variance), 1.0, 10000.0));
      } else if (kind == "XMLEffect") {
        p.definition = world.catalog().particle(props.particle);
        if (!p.definition) continue;
        const auto& d = *p.definition;
        p.motion = ParticleMotion::xml;
        p.size = d.size;
        p.z = d.z;
        p.texture = &d.texture;
        p.bottom_anchor = true;
        p.lifetime = static_cast<int>(std::clamp(d.duration_ms, 1.0, 10000.0));
        const double angle = random() * tau;
        p.velocity = {.5 * std::cos(angle), .5 * std::sin(angle)};
        for (std::size_t a = 0; a < d.animations.size(); ++a)
          if (random() <= d.animations[a].probability) {
            p.animation = static_cast<int>(a);
            break;
          }
      } else if (kind == "CustomParticles") {
        p.motion = ParticleMotion::linear;
        p.target = e->id;
        p.custom_size = true;
        p.size = props.size;
        p.z = static_cast<int>(e->motion.z + props.z_offset);
        p.position = p.start = {e->motion.position.x + props.range_x * plus_minus(1),
                                e->motion.position.y + props.range_y * plus_minus(1)};
        p.velocity = {props.speed + props.speed_variance * plus_minus(1),
                      props.speed + props.speed_variance * plus_minus(1)};
        p.vz = props.rise + props.rise_variance * plus_minus(1);
        p.az = props.rise_acc;
        p.lifetime =
            static_cast<int>(std::clamp((props.life + props.life_variance * plus_minus(1)) * 1000, 1.0, 10000.0));
      } else
        continue;
      emit(p);
    }
  }
  spawns_ = 0;
  rebuild_target_filters();
}
}
