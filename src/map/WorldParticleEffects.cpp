#include "realm/map/WorldParticleEffects.hpp"
#include "realm/objects/ObjectLibrary.hpp"
#include "realm/map/Map.hpp"

namespace realm {
void WorldParticleEffects::add_entity(const Entity& entity, const ObjectDef* definition) {
  if (definition) particles_.attach(entity, definition->texture);
}

void WorldParticleEffects::damaged(const World& world, const Entity& target, int amount, std::int32_t time,
                                   double hit_angle, double hit_speed) {
  particles_.hit(world, target, amount, time, hit_angle, hit_speed, hit_speed >= 0);
}

void WorldParticleEffects::level_up(std::int32_t id) {
  particles_.level_up(id, static_cast<std::int32_t>(particles_.time()));
}

void WorldParticleEffects::condition_changed(const Entity& entity) {
  if (entity.player) particles_.healing(entity);
}

void WorldParticleEffects::texture_changed(const Entity& entity, const ObjectDef* definition) {
  if (!definition) return;
  const auto alternate = definition->texture.alternate.find(entity.appearance.alt_texture);
  particles_.attach(entity, alternate == definition->texture.alternate.end() ? definition->texture : alternate->second);
}
}
