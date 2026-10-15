#pragma once
// Flash: com/company/assembleegameclient/objects/Player.as (movement rules)
#include "realm/map/World.hpp"

namespace realm {
[[nodiscard]] double move_speed(const Entity& player);
[[nodiscard]] double attack_frequency(const Entity& player);
[[nodiscard]] double attack_multiplier(const Entity& player);
[[nodiscard]] int damage_with_defense(int damage, int defense, bool armor_piercing,
                                      std::uint32_t condition);
[[nodiscard]] bool valid_player_position(const World& world, const Entity& player, Vec2 position);
[[nodiscard]] Vec2 modify_player_move(const World& world, const Entity& player, Vec2 desired);
void move_player(World& world, Entity& player, Vec2 relative_input, double rotation_input,
                 double dt_ms, double& camera_angle);
void player_move_ack(World& world, Entity& player, std::int32_t time_ms = 0);
}
