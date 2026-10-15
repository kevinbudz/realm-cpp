#include "realm/util/Telemetry.hpp"
#include "realm/objects/Combat.hpp"
#include "realm/objects/Movement.hpp"
#include "realm/objects/ItemData.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <numbers>

namespace realm {
namespace {
const ProjectileDef* definition(const ObjectDef& container, int id) {
  const auto it = std::find_if(container.projectiles.begin(), container.projectiles.end(),
                               [&](const auto& projectile) { return projectile.id == id; });
  return it == container.projectiles.end() ? nullptr : &*it;
}

bool targetable(const Entity& entity) {
  return !(entity.combat.condition & ((1U << 20U) | (1U << 18U)));
}
}

Combat::Combat(World& world) : world_(world), random_(0) {}

// Working storage sized by a crowd is handed back once the crowd is gone (a reset, or a population
// one-eighth of capacity).
template <class Vector>
void release_excess(Vector& values, std::size_t floor = 4096) {
  if (values.capacity() > floor && values.capacity() > values.size() * 8) {
    values.shrink_to_fit();
  }
}

void Combat::reset(std::uint32_t seed) {
  for (const auto& projectile : projectiles_) {
    world_.remove(projectile.entity_id);
  }
  projectiles_.clear();
  release_excess(projectiles_, 1024);
  index_ready_ = false;
  gathered_.clear();
  sorted_.clear();
  cell_of_.clear();
  cell_start_.clear();
  columns_ = rows_ = 0;
  release_excess(gathered_);
  release_excess(sorted_);
  release_excess(cell_of_);
  release_excess(cell_start_);
  random_ = FlashRandom(seed);
  next_bullet_id_ = fake_bullet_id_ = next_ability_time_ = 0;
}

void Combat::spawn(std::uint16_t type, int bullet_type, std::int32_t owner, std::int32_t bullet,
                   Vec2 start, double angle, std::int32_t time, int damage) {
  const auto* container = world_.catalog().object(type);
  if (!container) {
    return;
  }
  const auto* projectile = definition(*container, bullet_type);
  if (!projectile) {
    return;
  }
  const auto* visual = world_.catalog().object(projectile->object_id);
  if (!visual) {
    return;
  }
  FlyingProjectile shot;
  shot.entity_id = world_.allocate_fake_id();
  shot.bullet_id = bullet;
  shot.owner_id = owner;
  shot.start_time = time;
  shot.container_type = type;
  shot.bullet_type = bullet_type;
  shot.damage = damage;
  shot.start = start;
  shot.angle = angle;
  shot.definition = projectile;
  shot.damages_players = container->enemy;
  auto& entity = world_.add(shot.entity_id, visual->type, start,
                            projectile->size >= 0 ? projectile->size : container->size, true);
  entity.projectile = true;
  entity.motion.facing = angle;
  entity.motion.z = .5;
  if (const auto* tile = world_.tile(static_cast<int>(start.x), static_cast<int>(start.y))) {
    const auto* ground = world_.catalog().ground(tile->type);
    const auto* cover = world_.entity(tile->object_id);
    const auto* cover_def = cover ? world_.catalog().object(cover->type) : nullptr;
    if (!container->flying && ground && ground->sink &&
        (!cover_def || !cover_def->protect_sink)) {
      entity.motion.z = .1;
    } else if (const auto* owner_entity = world_.entity(owner);
               owner_entity && owner_entity->motion.sink_level) {
      entity.motion.z = .5 - .4 * owner_entity->motion.sink_level / 18.0;
    }
  }
  projectiles_.push_back(std::move(shot));
}

void Combat::attack(std::int32_t id, std::uint16_t type, double angle, std::int32_t time) {
  if (auto* entity = world_.entity(id)) {
    entity->combat.attack_angle = angle;
    entity->combat.attack_start = time;
    if (entity->player) {
      if (const auto* weapon = world_.catalog().object(type)) {
        const double rate = weapon->rate_of_fire;
        if (rate > 0) {
          entity->combat.attack_period = 1 / attack_frequency(*entity) / rate;
        }
      }
    }
  }
}

std::vector<net::Packet> Combat::receive(const net::Event& event, std::int32_t time) {
  using namespace net;
  std::vector<Packet> output;
  switch (event.id) {
    case flash::PacketId::ENEMYSHOOT: {
      const auto& shoot = std::get<incoming::EnemyShoot>(event.data);
      output.push_back(outgoing::encode(outgoing::ShootAck{time}));
      if (const auto* owner = world_.entity(shoot.owner_id)) {
        const auto type = owner->type;
        for (int i = 0; i < shoot.num_shots; ++i) {
          spawn(type, shoot.bullet_type, shoot.owner_id, shoot.bullet_id + i,
                {shoot.position.x, shoot.position.y},
                static_cast<double>(shoot.angle) + static_cast<double>(shoot.angle_increment) * i,
                time, shoot.damage);
        }
        attack(shoot.owner_id, type,
               static_cast<double>(shoot.angle) +
                   static_cast<double>(shoot.angle_increment) * (shoot.num_shots - 1) / 2,
               time);
      }
      break;
    }
    case flash::PacketId::SERVERPLAYERSHOOT: {
      const auto& shoot = std::get<incoming::ServerPlayerShoot>(event.data);
      const bool local = shoot.owner_id == world_.player_id();
      if (local) {
        output.push_back(outgoing::encode(outgoing::ShootAck{time}));
      }
      // GameServerConnection.onServerPlayerShoot: other players' shots are skipped with Ally Shots
      // off.
      if (world_.entity(shoot.owner_id) && (local || world_.preferences().ally_shots)) {
        for (std::size_t i = 0; i < shoot.damages.size(); ++i) {
          spawn(static_cast<std::uint16_t>(shoot.container_type), 0, shoot.owner_id,
                local ? shoot.bullet_id + static_cast<std::int32_t>(i) : 0,
                {shoot.position.x, shoot.position.y},
                shoot.angle + shoot.angle_increment * static_cast<double>(i), time,
                shoot.damages[i]);
        }
        attack(shoot.owner_id, static_cast<std::uint16_t>(shoot.container_type), shoot.angle, time);
      }
      break;
    }
    case flash::PacketId::ALLYSHOOT: {
      if (!world_.preferences().ally_shots) {
        break;
      }
      const auto& shoot = std::get<incoming::AllyShoot>(event.data);
      const auto* weapon =
          world_.catalog().object(static_cast<std::uint16_t>(shoot.container_type));
      const auto* owner = world_.entity(shoot.owner_id);
      if (weapon && owner) {
        const auto start = owner->motion.position;
        const int count = weapon->num_projectiles;
        const double gap = weapon->arc_gap;
        for (int i = 0; i < count; ++i) {
          spawn(weapon->type, 0, shoot.owner_id, fake_bullet_id_++, start,
                shoot.angle - gap * (count - 1) / 2 + gap * i, time, 0);
        }
        attack(shoot.owner_id, weapon->type, shoot.angle, time);
      }
      break;
    }
    case flash::PacketId::AOE: {
      const auto& aoe = std::get<incoming::Aoe>(event.data);
      world_.particles().aoe({aoe.position.x, aoe.position.y}, aoe.radius, aoe.color, time);
      if (auto* player = world_.entity(world_.player_id())) {
        if (!(player->combat.condition & (1U << 20U)) &&
            std::hypot(player->motion.position.x - aoe.position.x,
                       player->motion.position.y - aoe.position.y) < aoe.radius) {
          const std::array<std::uint8_t, 1> effects{aoe.effect};
          world_.report_damage(
              player->id,
              damage_with_defense(aoe.damage, player->combat.defense, false, player->combat.condition),
              time,
              aoe.effect ? std::span<const std::uint8_t>(effects)
                         : std::span<const std::uint8_t>{});
        }
        output.push_back(outgoing::encode(outgoing::AoeAck{
            time,
            {static_cast<float>(player->motion.position.x),
             static_cast<float>(player->motion.position.y)}}));
      }
      break;
    }
    default:
      break;
  }
  return output;
}

// Uniform grid over the enemies' bounding box, cell size a power of two tiles, capped at O(enemy
// count) cells so a huge map or stray far-away enemy cannot inflate it. Counting-sort layout; ties
// are resolved by dense order in update().
void Combat::build_target_index() {
  REALM_ZONE("combat.target_index");
  index_ready_ = true;
  ++stats_.builds;
  gathered_.clear();
  cell_of_.clear();
  double min_x = std::numeric_limits<double>::max();
  double min_y = min_x;
  double max_x = std::numeric_limits<double>::lowest();
  double max_y = max_x;
  // Enemy-definition entities only, in dense order (the same gathered_ sequence as a scan of every
  // entity).
  world_.each_enemy([&](const Entity& entity) {
    if (entity.projectile || !std::isfinite(entity.motion.position.x) ||
        !std::isfinite(entity.motion.position.y)) {
      return;
    }
    const auto* object = world_.catalog().object(entity.type);
    if (!object || !object->enemy) {
      return;
    }
    gathered_.push_back({entity.motion.position.x, entity.motion.position.y, entity.id});
    min_x = std::min(min_x, entity.motion.position.x);
    max_x = std::max(max_x, entity.motion.position.x);
    min_y = std::min(min_y, entity.motion.position.y);
    max_y = std::max(max_y, entity.motion.position.y);
  });

  stats_.indexed += gathered_.size();
  columns_ = rows_ = 0;
  if (gathered_.empty()) {
    sorted_.clear();
    cell_start_.clear();
    return;
  }

  const double cap = static_cast<double>(std::max<std::size_t>(1024, gathered_.size() * 4));
  double cell = 1;
  double columns = 0;
  double rows = 0;
  for (;;) {
    cell_inverse_ = 1 / cell;
    columns = std::floor((max_x - min_x) * cell_inverse_) + 1;
    rows = std::floor((max_y - min_y) * cell_inverse_) + 1;
    if (columns * rows <= cap) {
      break;
    }
    cell *= 2;
  }

  origin_x_ = min_x;
  origin_y_ = min_y;
  columns_ = static_cast<std::size_t>(columns);
  rows_ = static_cast<std::size_t>(rows);
  cell_start_.assign(columns_ * rows_ + 1, 0);
  cell_of_.resize(gathered_.size());
  for (std::size_t i = 0; i < gathered_.size(); ++i) {
    const auto cx = std::min(
        columns_ - 1, static_cast<std::size_t>((gathered_[i].x - origin_x_) * cell_inverse_));
    const auto cy =
        std::min(rows_ - 1, static_cast<std::size_t>((gathered_[i].y - origin_y_) * cell_inverse_));
    const auto cell_index = static_cast<std::uint32_t>(cy * columns_ + cx);
    cell_of_[i] = cell_index;
    ++cell_start_[cell_index + 1];
  }
  for (std::size_t i = 1; i < cell_start_.size(); ++i) {
    cell_start_[i] += cell_start_[i - 1];
  }
  sorted_.resize(gathered_.size());
  {
    auto cursor = cell_start_;
    for (std::size_t i = 0; i < gathered_.size(); ++i) {
      sorted_[cursor[cell_of_[i]]++] = gathered_[i];
    }
  }
  // Keep capacity bounded after a crowd leaves.
  release_excess(gathered_);
  release_excess(sorted_);
  release_excess(cell_of_);
  release_excess(cell_start_);
}

template <class Visit>
void Combat::query_target_index(Vec2 position, Visit&& visit) {
  if (!index_ready_) {
    build_target_index();
  }
  if (!columns_ || !std::isfinite(position.x) || !std::isfinite(position.y)) {
    return;
  }
  // The 0.5 tile test is inclusive and evaluated on differences, so widen the box slightly; exact
  // filtering follows.
  constexpr double reach = .5 + 1e-6;
  const double lo_x = std::floor((position.x - reach - origin_x_) * cell_inverse_);
  const double hi_x = std::floor((position.x + reach - origin_x_) * cell_inverse_);
  const double lo_y = std::floor((position.y - reach - origin_y_) * cell_inverse_);
  const double hi_y = std::floor((position.y + reach - origin_y_) * cell_inverse_);
  if (hi_x < 0 || hi_y < 0 || lo_x > static_cast<double>(columns_ - 1) ||
      lo_y > static_cast<double>(rows_ - 1)) {
    return;
  }

  const auto x0 = static_cast<std::size_t>(std::max(lo_x, 0.));
  const auto y0 = static_cast<std::size_t>(std::max(lo_y, 0.));
  const auto x1 = static_cast<std::size_t>(std::min(hi_x, static_cast<double>(columns_ - 1)));
  const auto y1 = static_cast<std::size_t>(std::min(hi_y, static_cast<double>(rows_ - 1)));
  for (std::size_t y = y0; y <= y1; ++y) {
    const auto first = cell_start_[y * columns_ + x0];
    const auto last = cell_start_[y * columns_ + x1 + 1];
    for (auto i = first; i < last; ++i) {
      ++stats_.examined;
      const auto& candidate = sorted_[i];
      if (std::abs(candidate.x - position.x) > .5 ||
          std::abs(candidate.y - position.y) > .5) {
        continue;
      }
      if (auto* entity = world_.entity(candidate.id)) {
        visit(*entity);
      }
    }
  }
}

std::vector<net::Packet> Combat::update(std::int32_t time) {
  index_ready_ = false;
  REALM_ZONE("combat.update");
  using namespace net;
  std::vector<Packet> output;
  for (std::size_t i = 0; i < projectiles_.size();) {
    auto& projectile = projectiles_[i];
    auto* visual = world_.entity(projectile.entity_id);
    const auto& definition = *projectile.definition;
    const auto elapsed = time - projectile.start_time;
    bool alive = visual && elapsed <= definition.motion.lifetime_ms;
    Vec2 position{};
    if (alive) {
      position = projectile_position(definition.motion, projectile.start, projectile.angle,
                                     projectile.bullet_id, elapsed);
      const auto* tile =
          position.x < 0 || position.y < 0 || position.x >= world_.width() ||
                  position.y >= world_.height()
              ? nullptr
              : world_.tile(static_cast<int>(position.x), static_cast<int>(position.y));
      const auto* cover = tile ? world_.entity(tile->object_id) : nullptr;
      const auto* cover_def = cover ? world_.catalog().object(cover->type) : nullptr;
      if (!tile || tile->type == 255 ||
          (cover_def && (!cover_def->enemy || projectile.damages_players) &&
           (cover_def->enemy_occupy || (!definition.passes_cover && cover_def->occupy)))) {
        if (projectile.damages_players) {
          output.push_back(outgoing::encode(outgoing::SquareHit{time, projectile.bullet_id}));
        }
        if (!projectile.damages_players && cover) {
          if (const auto* object = world_.catalog().object(visual->type)) {
            world_.particles().impact(object->texture, position, projectile.angle,
                                      definition.motion.speed, time);
          }
        }
        alive = false;
      } else {
        visual->motion.position = position;
      }
    }

    if (alive) {
      if (definition.particle_trail && time - projectile.last_trail >= 17) {
        world_.particles().trail(position, time);
        projectile.last_trail = time;
      }
      Entity* target{};
      double distance = std::numeric_limits<double>::max();
      // Nearest wins; an exact distance tie goes to the earlier entity in the dense list, which is
      // what the original in-order scan with a strict `<` selected, whatever order candidates are
      // visited in.
      auto consider = [&](Entity& entity) {
        if (!targetable(entity) || projectile.hit.contains(entity.id)) {
          return;
        }
        const double dx = std::abs(entity.motion.position.x - position.x);
        const double dy = std::abs(entity.motion.position.y - position.y);
        if (dx > .5 || dy > .5) {
          return;
        }
        const double dist = dx * dx + dy * dy;
        if (dist < distance || (dist == distance && target && &entity < target)) {
          target = &entity;
          distance = dist;
        }
      };
      {
        REALM_ZONE("combat.target_query");
        ++stats_.queries;
        if (projectile.damages_players) {
          if (auto* player = world_.entity(world_.player_id())) {
            consider(*player);
          }
        } else if (search_ == TargetSearch::scan) {
          for (auto& entity : world_.entities()) {
            if (const auto* object = world_.catalog().object(entity.type);
                object && object->enemy && !entity.projectile) {
              ++stats_.examined;
              consider(entity);
            }
          }
        } else {
          query_target_index(position, consider);
        }
      }

      if (target) {
        if (projectile.damages_players || projectile.owner_id == world_.player_id()) {
          if (target->id == world_.player_id()) {
            output.push_back(outgoing::encode(outgoing::PlayerHit{projectile.bullet_id}));
          } else {
            output.push_back(outgoing::encode(outgoing::EnemyHit{time, projectile.bullet_id, target->id}));
          }
          const int damage = damage_with_defense(projectile.damage, target->combat.defense,
                                                 definition.armor_piercing, target->combat.condition);
          // Projectile.damage plays the target's hit sound even when Ally Damage drops the report
          // for enemies.
          if (!world_.preferences().ally_damage && target->id != world_.player_id()) {
            world_.sound_hit(target->id);
          }
          world_.report_damage(target->id, damage, time, {}, definition.armor_piercing,
                               projectile.angle, definition.motion.speed);
        }
        if (definition.multi_hit) {
          projectile.hit.insert(target->id);
        } else {
          alive = false;
        }
      }
    }
    if (!alive) {
      world_.remove(projectile.entity_id);
      if (i != projectiles_.size() - 1) {
        projectiles_[i] = std::move(projectiles_.back());
      }
      projectiles_.pop_back();
    } else {
      ++i;
    }
  }
  release_excess(projectiles_, 1024);
  return output;
}

std::vector<net::Packet> Combat::fire(const ObjectDef& weapon, std::int32_t data, double angle,
                                      std::int32_t time, bool ability) {
  using namespace net;
  std::vector<Packet> output;
  const auto* player = world_.entity(world_.player_id());
  if (!player) {
    return output;
  }
  const auto* projectile = definition(weapon, 0);
  const int count = weapon.num_projectiles;
  if (!projectile || count <= 0 || count > 255) {
    return output;
  }
  // Player.doShoot: the first projectile of a volley plays the weapon's OldSound at 0.75 through
  // the pew-pew gate.
  if (!weapon.old_sound.empty()) {
    world_.sounds().play(weapon.old_sound, 0.75F, false);
  }
  const double multiplier = attack_multiplier(*player);
  const double mod = item_data::stat(data, item_data::damage);
  const int minimum = projectile->min_damage + static_cast<int>(projectile->min_damage * mod);
  const int maximum = projectile->max_damage + static_cast<int>(projectile->max_damage * mod);
  const auto owner = player->id;
  const auto start = player->motion.position;
  const auto first = next_bullet_id_;
  next_bullet_id_ = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(next_bullet_id_) -
                                                static_cast<std::uint32_t>(count));
  for (int i = 0; i < count; ++i) {
    const int damage = static_cast<int>(
        random_.range(static_cast<std::uint32_t>(minimum), static_cast<std::uint32_t>(maximum)) *
        multiplier);
    const auto bullet = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(first) -
                                                    static_cast<std::uint32_t>(i));
    spawn(weapon.type, 0, owner, bullet, start,
          angle - weapon.arc_gap * (count - 1) / 2 + weapon.arc_gap * i, time, damage);
  }
  output.push_back(outgoing::encode(
      outgoing::PlayerShoot{time,
                            {static_cast<float>(start.x), static_cast<float>(start.y)},
                            static_cast<float>(angle),
                            ability,
                            static_cast<std::uint8_t>(count)}));
  return output;
}

std::vector<net::Packet> Combat::shoot(double angle, std::int32_t time, bool ability) {
  auto* player = world_.entity(world_.player_id());
  if (!player || (player->combat.condition & (1U << 5U)) || !std::isfinite(angle)) {
    return {};
  }

  const auto it =
      player->profile.stats.find(static_cast<std::uint8_t>(flash::StatId::INVENTORY_0_STAT));
  if (it == player->profile.stats.end() || it->second < 0) {
    return {};
  }

  const auto* weapon = world_.catalog().object(static_cast<std::uint16_t>(it->second));
  if (!weapon || !definition(*weapon, 0)) {
    return {};
  }

  const auto data_it =
      player->profile.stats.find(static_cast<std::uint8_t>(flash::StatId::ITEMDATA_0_STAT));
  const auto data = data_it == player->profile.stats.end() ? -1 : data_it->second;
  const double rate = weapon->rate_of_fire * (1 + item_data::stat(data, item_data::rate_of_fire));
  if (rate <= 0) {
    return {};
  }

  player->combat.attack_period = 1 / attack_frequency(*player) / rate;
  if (time < player->combat.attack_start + player->combat.attack_period) {
    return {};
  }

  player->combat.attack_angle = angle;
  player->combat.attack_start = time;
  return fire(*weapon, data, angle, time, ability);
}

std::vector<net::Packet> Combat::use_ability(Vec2 target, std::int32_t time) {
  using namespace net;
  const auto* player = world_.entity(world_.player_id());
  if (!player || !std::isfinite(target.x) || !std::isfinite(target.y)) {
    return {};
  }

  const auto it =
      player->profile.stats.find(static_cast<std::uint8_t>(flash::StatId::INVENTORY_1_STAT));
  if (it == player->profile.stats.end() || it->second < 0) {
    return {};
  }

  const auto* ability = world_.catalog().object(static_cast<std::uint16_t>(it->second));
  if (!ability || !ability->usable) {
    return {};
  }
  // Player.useAltWeapon refuses in this order: a bad target or position, then the cooldown (both
  // "error"), then mana
  // ("no_mana").
  const bool off_map =
      ability->ability_target &&
      (target.x < 0 || target.y < 0 || target.x >= world_.width() || target.y >= world_.height());
  if (off_map || (ability->ability_teleport && !valid_player_position(world_, *player, target))) {
    world_.sounds().play("error");
    return {};
  }

  if (time < next_ability_time_) {
    world_.sounds().play("error");
    return {};
  }
  const auto mana = player->profile.stats.find(static_cast<std::uint8_t>(flash::StatId::MP_STAT));
  if (ability->mp_cost > (mana == player->profile.stats.end() ? 0 : mana->second)) {
    world_.sounds().play("no_mana");
    return {};
  }

  const auto data_it =
      player->profile.stats.find(static_cast<std::uint8_t>(flash::StatId::ITEMDATA_1_STAT));
  const auto data = data_it == player->profile.stats.end() ? -1 : data_it->second;
  next_ability_time_ = time + static_cast<int>(ability->cooldown_ms *
                                               (1 - item_data::stat(data, item_data::cooldown)));
  std::vector<Packet> output{outgoing::encode(outgoing::UseItem{
      time, {player->id, 1}, {static_cast<float>(target.x), static_cast<float>(target.y)}})};
  if (ability->ability_shoot) {
    auto shots =
        fire(*ability, data,
             std::atan2(target.y - player->motion.position.y, target.x - player->motion.position.x),
             time, true);
    output.insert(output.end(), std::make_move_iterator(shots.begin()),
                  std::make_move_iterator(shots.end()));
  }
  return output;
}
}
