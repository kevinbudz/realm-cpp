#include "realm/objects/Movement.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace realm {
namespace {
constexpr std::uint32_t weak = 1U << 1U;
constexpr std::uint32_t slowed = 1U << 2U;
constexpr std::uint32_t dazed = 1U << 4U;
constexpr std::uint32_t confused = 1U << 9U;
constexpr std::uint32_t paralyzed = 1U << 12U;
constexpr std::uint32_t speedy = 1U << 13U;
constexpr std::uint32_t damaging = 1U << 16U;
constexpr std::uint32_t berserk = 1U << 17U;
constexpr std::uint32_t invincible = 1U << 20U;
constexpr std::uint32_t invulnerable = 1U << 21U;
constexpr std::uint32_t armored = 1U << 22U;
constexpr std::uint32_t armor_broken = 1U << 23U;

bool full(const World& world, Vec2 pos) {
  const auto* tile = world.tile(static_cast<int>(pos.x), static_cast<int>(pos.y));
  if (!tile || tile->type == 255) {
    return true;
  }
  const auto* object = world.entity(tile->object_id);
  const auto* def = object ? world.catalog().object(object->type) : nullptr;
  return def && def->full_occupy;
}

Vec2 step(const World& world, const Entity& player, Vec2 desired) {
  const auto start = player.motion.position;
  const bool x_cross = (std::fmod(start.x, .5) == 0 && desired.x != start.x) ||
                       static_cast<int>(start.x / .5) != static_cast<int>(desired.x / .5);
  const bool y_cross = (std::fmod(start.y, .5) == 0 && desired.y != start.y) ||
                       static_cast<int>(start.y / .5) != static_cast<int>(desired.y / .5);
  if ((!x_cross && !y_cross) || valid_player_position(world, player, desired)) {
    return desired;
  }
  Vec2 border{};
  if (x_cross) {
    border.x = desired.x > start.x ? static_cast<int>(desired.x * 2) / 2.0
                                   : static_cast<int>(start.x * 2) / 2.0;
    if (static_cast<int>(border.x) > static_cast<int>(start.x)) {
      border.x -= .01;
    }
  }
  if (y_cross) {
    border.y = desired.y > start.y ? static_cast<int>(desired.y * 2) / 2.0
                                   : static_cast<int>(start.y * 2) / 2.0;
    if (static_cast<int>(border.y) > static_cast<int>(start.y)) {
      border.y -= .01;
    }
  }
  if (!x_cross) {
    return {desired.x, border.y};
  }
  if (!y_cross) {
    return {border.x, desired.y};
  }
  const Vec2 along_x{desired.x, border.y};
  const Vec2 along_y{border.x, desired.y};
  const bool prefer_x = std::abs(desired.x - border.x) > std::abs(desired.y - border.y);
  const auto first = prefer_x ? along_x : along_y;
  const auto second = prefer_x ? along_y : along_x;
  if (valid_player_position(world, player, first)) {
    return first;
  }
  if (valid_player_position(world, player, second)) {
    return second;
  }
  return border;
}
}

double move_speed(const Entity& player) {
  if (player.combat.condition & slowed) {
    return .004 * player.motion.move_multiplier;
  }
  double speed = .004 + player.motion.speed / 75.0 * (.0096 - .004);
  if (player.combat.condition & speedy) {
    speed *= 1.5;
  }
  return speed * player.motion.move_multiplier;
}

double attack_frequency(const Entity& player) {
  if (player.combat.condition & dazed) {
    return .0015;
  }
  const double frequency = .0015 + player.combat.dexterity / 75.0 * (.008 - .0015);
  return (player.combat.condition & berserk) ? frequency * 1.5 : frequency;
}

double attack_multiplier(const Entity& player) {
  if (player.combat.condition & weak) {
    return .5;
  }
  const double multiplier = .5 + player.combat.attack / 75.0 * 1.5;
  return (player.combat.condition & damaging) ? multiplier * 1.5 : multiplier;
}

int damage_with_defense(int damage, int defense, bool piercing, std::uint32_t condition) {
  if (condition & invulnerable) {
    return 0;
  }
  if (piercing || (condition & armor_broken)) {
    defense = 0;
  } else if (condition & armored) {
    defense *= 2;
  }
  return std::max(damage * 3 / 20, damage - defense);
}

bool valid_player_position(const World& world, const Entity& player, Vec2 position) {
  if (!std::isfinite(position.x) || !std::isfinite(position.y)) {
    return false;
  }

  const auto* current = world.tile(static_cast<int>(player.motion.position.x),
                                   static_cast<int>(player.motion.position.y));
  const auto* next = position.x < 0 || position.y < 0 || position.x >= world.width() ||
                             position.y >= world.height()
                         ? nullptr
                         : world.tile(static_cast<int>(position.x), static_cast<int>(position.y));
  if (current != next && !world.walkable(position.x, position.y)) {
    return false;
  }

  const double xf = position.x - static_cast<int>(position.x);
  const double yf = position.y - static_cast<int>(position.y);
  if (xf < .5) {
    if (full(world, {position.x - 1, position.y})) {
      return false;
    }
    if (yf < .5 && (full(world, {position.x, position.y - 1}) ||
                    full(world, {position.x - 1, position.y - 1}))) {
      return false;
    }
    if (yf > .5 && (full(world, {position.x, position.y + 1}) ||
                    full(world, {position.x - 1, position.y + 1}))) {
      return false;
    }
  } else if (xf > .5) {
    if (full(world, {position.x + 1, position.y})) {
      return false;
    }
    if (yf < .5 && (full(world, {position.x, position.y - 1}) ||
                    full(world, {position.x + 1, position.y - 1}))) {
      return false;
    }
    if (yf > .5 && (full(world, {position.x, position.y + 1}) ||
                    full(world, {position.x + 1, position.y + 1}))) {
      return false;
    }
  } else {
    if (yf < .5 && full(world, {position.x, position.y - 1})) {
      return false;
    }
    if (yf > .5 && full(world, {position.x, position.y + 1})) {
      return false;
    }
  }
  return true;
}

Vec2 modify_player_move(const World& world, const Entity& player, Vec2 desired) {
  if (!std::isfinite(desired.x) || !std::isfinite(desired.y) ||
      (player.combat.condition & paralyzed)) {
    return player.motion.position;
  }
  const double dx = desired.x - player.motion.position.x;
  const double dy = desired.y - player.motion.position.y;
  if (std::abs(dx) < .4 && std::abs(dy) < .4) {
    return step(world, player, desired);
  }

  double size = .4 / std::max(std::abs(dx), std::abs(dy));
  double done{};
  auto result = player.motion.position;
  while (done < 1) {
    if (done + size >= 1) {
      size = 1 - done;
    }
    result = step(world, player, {result.x + dx * size, result.y + dy * size});
    done += size;
  }
  return result;
}

void move_player(World& world, Entity& player, Vec2 input, double rotation, double dt,
                 double& angle) {
  if (!std::isfinite(dt) || dt < 0 || dt > 1000) {
    throw std::invalid_argument("Invalid player frame interval");
  }

  if (player.combat.condition & confused) {
    input = {-input.y, -input.x};
    rotation = -rotation;
  }
  angle += dt * .003 * rotation;
  player.motion.velocity = {};
  if (input.x || input.y) {
    const double facing = angle + std::atan2(input.y, input.x);
    const double speed = move_speed(player);
    player.motion.velocity = {speed * std::cos(facing), speed * std::sin(facing)};
  }
  player.motion.velocity.x -= player.motion.push.x;
  player.motion.velocity.y -= player.motion.push.y;
  player.motion.position =
      modify_player_move(world, player,
                         {player.motion.position.x + dt * player.motion.velocity.x,
                          player.motion.position.y + dt * player.motion.velocity.y});
  if (player.motion.velocity.x || player.motion.velocity.y) {
    player.motion.facing = std::atan2(player.motion.velocity.y, player.motion.velocity.x);
  }
}

void player_move_ack(World& world, Entity& player, std::int32_t time) {
  const auto* tile = world.tile(static_cast<int>(player.motion.position.x),
                                static_cast<int>(player.motion.position.y));
  const auto* ground = tile ? world.catalog().ground(tile->type) : nullptr;
  if (!ground) {
    return;
  }

  if (ground->sinking) {
    player.motion.sink_level = std::min(player.motion.sink_level + 1, 18);
    player.motion.move_multiplier =
        .1 + (1 - player.motion.sink_level / 18.0) * (ground->speed - .1);
  } else {
    player.motion.sink_level = 0;
    player.motion.move_multiplier = ground->speed;
  }
  const auto* object = world.entity(tile->object_id);
  const auto* def = object ? world.catalog().object(object->type) : nullptr;

  if (ground->damage && !(player.combat.condition & invincible) && (!def || !def->protect_ground)) {
    world.report_damage(player.id, ground->damage, time);
  }
  player.motion.push =
      ground->push ? Vec2{ground->animate_x / 1000, ground->animate_y / 1000} : Vec2{};
}
}
