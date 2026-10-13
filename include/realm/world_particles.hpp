#pragma once
#include "realm/particles.hpp"
#include <cstdint>

namespace realm {
struct Entity;
struct ObjectDef;
class World;

// World-generated particle effects and their lifecycle, separate from the entity store.
class WorldParticleEffects {
 public:
  void clear() { particles_.clear(); }
  void set_enabled(bool enabled) { particles_.set_enabled(enabled); }
  void add_entity(const Entity& entity, const ObjectDef* definition);
  void remove_entity(std::int32_t id) { particles_.remove_target(id); }
  void damaged(const World& world, const Entity& target, int amount, std::int32_t time, double hit_angle,
               double hit_speed);
  void level_up(std::int32_t id);
  void condition_changed(const Entity& entity);
  void texture_changed(const Entity& entity, const ObjectDef* definition);
  [[nodiscard]] Particles& particles() { return particles_; }
  [[nodiscard]] const Particles& particles() const { return particles_; }

 private:
  Particles particles_;
};
}
