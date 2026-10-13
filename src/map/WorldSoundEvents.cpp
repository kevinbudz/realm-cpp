#include "realm/map/WorldSoundEvents.hpp"
#include "realm/objects/Catalog.hpp"

namespace realm {
bool WorldSoundEvents::mark_preloaded(std::uint16_t type) {
  const auto word = static_cast<std::size_t>(type) / 64;
  if (preloaded_types_.size() <= word) preloaded_types_.resize(word + 1);
  const auto mask = std::uint64_t{1} << (type & 63U);
  if (preloaded_types_[word] & mask) return false;
  preloaded_types_[word] |= mask;
  return true;
}

void WorldSoundEvents::game_object_added(const ObjectDef* definition, Vec2 position, const Vec2* player_position) {
  if (!definition) return;
  if (mark_preloaded(definition->type)) {
    if (!definition->hit_sound.empty()) cues_.preload(definition->hit_sound);
    if (!definition->death_sound.empty()) cues_.preload(definition->death_sound);
    for (const auto& sound : definition->sounds) cues_.preload(sound);
  }
  if (definition->loot && definition->container && player_position) {
    const double dx = player_position->x - position.x, dy = player_position->y - position.y;
    if (dx * dx + dy * dy < 100) cues_.play("loot_appears");
  }
}

void WorldSoundEvents::game_object_dropped(const ObjectDef* definition, bool exploded) {
  if (exploded && definition && !definition->death_sound.empty()) cues_.play(definition->death_sound);
}

void WorldSoundEvents::hit(const ObjectDef* definition) {
  if (definition && !definition->hit_sound.empty()) cues_.play(definition->hit_sound);
}

void WorldSoundEvents::own_player_level_up() { cues_.play("level_up"); }
}
