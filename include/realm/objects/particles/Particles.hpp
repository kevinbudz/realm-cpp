#pragma once
#include "realm/objects/Catalog.hpp"
#include "realm/messaging/Messages.hpp"
#include <array>
#include <span>
#include <optional>
namespace realm {
class World;
struct Entity;
enum class ParticleMotion : std::uint8_t {
  linear,
  spark,
  sparker,
  thrown_spark,
  heal,
  stream,
  flow,
  swirl,
  fountain,
  gas,
  bubble,
  xml,
  thrown_sprite
};
struct Particle {
  ParticleMotion motion{ParticleMotion::linear};
  Vec2 position{}, start{}, end{}, velocity{};
  double z{}, initial_z{}, vz{}, az{}, size{100}, initial_size{100}, angle{}, distance{}, speed{}, noise{}, period{1};
  std::int64_t born{}, last_emission{};
  int lifetime{1000};
  std::uint32_t color{0xffffff};
  std::int32_t target{-1};
  const TextureDef* texture{};
  const ParticleDef* definition{};
  int animation{-1};
  bool bottom_anchor{}, custom_size{};
  std::uint64_t order{};
};
// Conservative membership of entity ids (4096 bits, one hash bit per id). A clear bit proves no live particle, emitter
// or flash targets the id, so object removal and attachment skip scans of the 8192-entry particle pool; a set bit only
// means "scan".
class TargetFilter {
 public:
  void add(std::int32_t id) noexcept {
    const auto bit = slot(id);
    bits_[bit >> 6U] |= std::uint64_t{1} << (bit & 63U);
  }
  [[nodiscard]] bool may_contain(std::int32_t id) const noexcept {
    const auto bit = slot(id);
    return bits_[bit >> 6U] >> (bit & 63U) & 1U;
  }
  void clear() noexcept { bits_.fill(0); }

 private:
  static std::uint32_t slot(std::int32_t id) noexcept { return (static_cast<std::uint32_t>(id) * 2654435761U) >> 20U; }
  std::array<std::uint64_t, 64> bits_{};
};
struct EntityFlash {
  std::int32_t target{};
  std::int64_t born{};
  int period{}, repeats{};
  std::uint32_t color{};
};
class Particles {
 public:
  static constexpr std::size_t maximum_particles = 8192, maximum_emitters = 1024, maximum_spawns = 2048,
                               maximum_flashes = 256;
  static constexpr int maximum_lifetime = 10000;
  Particles();
  void clear();
  bool emit(Particle particle);
  void set_enabled(bool value);
  [[nodiscard]] bool enabled() const { return enabled_; }
  void attach(const Entity& entity, const TextureDef& texture);
  void healing(const Entity& entity);
  void remove_target(std::int32_t id);
  void update(const World& world, std::int32_t time);
  void show(const World& world, const net::incoming::ShowEffect& effect, std::int32_t time);
  void aoe(Vec2 position, double radius, std::uint32_t color, std::int32_t time);
  void hit(const World& world, const Entity& target, int amount, std::int32_t time, double angle = 0, double speed = 0,
           bool directional = false, int count = 10);
  void impact(const TextureDef& texture, Vec2 position, double angle, double speed, std::int32_t time);
  void trail(Vec2 position, std::int32_t time, int count = 3);
  void level_up(std::int32_t target, std::int32_t time);
  [[nodiscard]] std::span<const Particle> particles() const { return particles_; }
  [[nodiscard]] std::span<const EntityFlash> flashes() const { return flashes_; }
  [[nodiscard]] std::size_t emitter_count() const { return emitters_.size(); }
  // Exact (unfiltered) scan: does any live particle, emitter or flash still target this object? Diagnostics and tests.
  [[nodiscard]] bool targets(std::int32_t id) const;
  [[nodiscard]] std::size_t dropped() const { return dropped_; }
  [[nodiscard]] Vec2 camera_offset(std::int32_t time) const;
  [[nodiscard]] std::int64_t time() const { return now_; }
  [[nodiscard]] const TextureDef* texture(const Particle& particle, std::int32_t time) const;

 private:
  struct Emitter {
    std::int32_t target{};
    const EffectDef* props{};
    double last{}, credit{};
  };
  double random();
  double plus_minus(double n) { return (random() * 2 - 1) * n; }

  Particle spark(Vec2 at, std::uint32_t color, double size, int life, double z, Vec2 velocity, std::int64_t time);
  void path(Vec2 start, Vec2 end, std::uint32_t color, int life, double size, std::int32_t time,
            ParticleMotion motion = ParticleMotion::sparker);
  void heal(const Entity& target, std::uint32_t color, std::int32_t time, int count);
  std::optional<std::uint32_t> color(const TextureDef& texture, std::uint32_t id);
  std::vector<Particle> particles_;
  std::vector<Emitter> emitters_;
  std::vector<EntityFlash> flashes_;
  // Maintained by every insertion, rebuilt from the live containers once per update so stale bits do not accumulate.
  TargetFilter particle_targets_, emitter_targets_, flash_targets_;
  void rebuild_target_filters();
  std::uint32_t rng_{0x12345678U};
  std::uint64_t order_{};
  std::int64_t now_{}, last_update_{-1}, jitter_start_{-1};
  std::size_t spawns_{}, dropped_{};
  bool enabled_{true};
};
}
