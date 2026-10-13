#pragma once

#include "realm/camera.hpp"
#include "realm/stat_map.hpp"
#include <cstdint>
#include <limits>
#include <string>

namespace realm {
struct EntityMotion {
  Vec2 position{}, target{}, velocity{}, push{};
  double z{}, facing{}, move_multiplier{1};
  int speed{}, sink_level{};
};

struct EntityCombat {
  std::int32_t attack_start{std::numeric_limits<std::int32_t>::min() / 2};
  double attack_period{500}, attack_angle{};
  int attack{}, dexterity{}, hp{}, max_hp{}, defense{};
  std::uint32_t condition{};
};

struct EntityAppearance {
  int size{100}, alt_texture{}, texture{};
  std::uint32_t tex1{}, tex2{};
  // Optional explicit native model transform; ordinary XML objects retain Rotation.
  double model_rotation{std::numeric_limits<double>::quiet_NaN()};
};

struct EntityProfile {
  std::string name, guild;
  // Cold stats (inventory, currency, account data) are allocated only when actually received.
  StatMap stats;
};

struct Entity {
  std::int32_t id{};
  std::uint16_t type{};
  bool player{}, projectile{};
  EntityMotion motion;
  EntityCombat combat;
  EntityAppearance appearance;
  EntityProfile profile;
};
}
