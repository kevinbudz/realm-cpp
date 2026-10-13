#pragma once
#include "realm/sound_cues.hpp"
#include "realm/projectile.hpp"
#include <cstdint>
#include <vector>

namespace realm {
struct ObjectDef;

// Selects and queues sounds caused by world object events. Its per-type preload cache and pending cues live for the
// lifetime of a World, including across map initialization.
class WorldSoundEvents {
 public:
  void game_object_added(const ObjectDef* definition, Vec2 position, const Vec2* player_position);
  void game_object_dropped(const ObjectDef* definition, bool exploded);
  void hit(const ObjectDef* definition);
  void own_player_level_up();
  [[nodiscard]] SoundCues& cues() { return cues_; }

 private:
  bool mark_preloaded(std::uint16_t type);

  SoundCues cues_;
  std::vector<std::uint64_t> preloaded_types_;
};
}
