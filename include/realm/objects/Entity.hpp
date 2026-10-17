#pragma once
// Flash: com/company/assembleegameclient/objects/GameObject.as

#include "realm/map/Camera.hpp"
#include "realm/objects/StatMap.hpp"
#include <cstdint>
#include <limits>
#include <string>

namespace realm {
struct EntityMotion {
  Vec2 position{};
  Vec2 target{};
  Vec2 velocity{};
  Vec2 push{};
  double z{};
  double facing{};
  // Projectile Direction: the heading a projectile is moving along. Facing keeps the launch angle,
  // which animated projectile textures read.
  double travel{};
  double move_multiplier{1};
  int speed{};
  int sink_level{};
};

struct EntityCombat {
  std::int32_t attack_start{std::numeric_limits<std::int32_t>::min() / 2};
  double attack_period{500};
  double attack_angle{};
  int attack{};
  int dexterity{};
  int hp{};
  int max_hp{};
  int defense{};
  std::uint32_t condition{};
};

struct EntityAppearance {
  int size{100};
  int alt_texture{};
  int texture{};
  std::uint32_t tex1{};
  std::uint32_t tex2{};
  // Optional explicit native model transform; ordinary XML objects retain Rotation.
  double model_rotation{std::numeric_limits<double>::quiet_NaN()};
};

struct EntityProfile {
  std::string name;
  std::string guild;
  // Cold stats (inventory, currency, account data) are allocated only when actually received.
  StatMap stats;
};

struct Entity {
  std::int32_t id{};
  std::uint16_t type{};
  bool player{};
  bool projectile{};
  EntityMotion motion;
  EntityCombat combat;
  EntityAppearance appearance;
  EntityProfile profile;
};
}
