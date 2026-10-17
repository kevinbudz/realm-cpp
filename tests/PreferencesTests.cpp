#include "realm/objects/Combat.hpp"
#include "realm/hud/Hud.hpp"
#include "realm/stage3D/DrawTypes.hpp"
#include "realm/ui/tooltip/ItemTooltip.hpp"
#include "realm/ui/tooltip/PlayerTooltip.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>

// The Options screen's flags as the game systems apply them: Parameters.data_ checks in
// GameServerConnection, GameObject, Projectile and ParticleEffect, without a GL context.
namespace {
int checks{};

void check(bool ok, const std::string& why) {
  ++checks;
  if (!ok) {
    throw std::runtime_error(why);
  }
}
}

using namespace realm;

namespace {
constexpr std::uint8_t stat(flash::StatId id) {
  return static_cast<std::uint8_t>(id);
}

struct Fixture {
  Catalog catalog{flash::source_root};
  World world{catalog};
  Combat combat{world};

  Fixture() {
    world.initialize(8, 8, "preferences");
    const auto grass = catalog.ground("Grass")->type;
    for (int y = 0; y < 8; ++y) {
      for (int x = 0; x < 8; ++x) {
        world.tile(x, y)->type = grass;
      }
    }
    world.add(1, catalog.object("Wizard")->type, {2.5, 2.5});
    world.set_player_id(1);
    world.entity(1)->combat.hp = world.entity(1)->combat.max_hp = 100;
    world.add(2, catalog.object("Wizard")->type, {4.5, 2.5});
    world.entity(2)->combat.hp = world.entity(2)->combat.max_hp = 100;
    combat.reset(1);
  }

  void tick(std::int32_t id, std::vector<net::Stat> stats) {
    net::Tick t;
    net::Status s;
    s.id = id;
    s.position = {static_cast<float>(world.entity(id)->motion.position.x),
                  static_cast<float>(world.entity(id)->motion.position.y)};
    s.stats = std::move(stats);
    t.statuses.push_back(s);
    world.apply(t);
  }

  void apply(const std::function<void(Settings&)>& change) {
    Settings s;
    change(s);
    world.set_preferences(s.preferences());
  }
};

void damage() {
  Fixture f;
  f.world.report_damage(2, 10, 0);
  check(f.world.take_status_events().size() == 1, "ally damage is shown by default");
  f.apply([](Settings& s) { s.set_value("allyDamage", 0); });
  f.world.report_damage(2, 10, 0);
  check(f.world.take_status_events().empty() && f.world.damage_cues().size() == 1,
        "Ally Damage off drops damage to others");
  f.world.report_damage(1, 10, 0);
  check(f.world.take_status_events().size() == 1, "damage to the player is always shown");
}

void notifications() {
  Fixture f;
  f.apply([](Settings& s) { s.set_value("allyNotifs", 0); });
  check(f.world.allows_notification(1) && !f.world.allows_notification(2) &&
            f.world.allows_notification(77),
        "Ally Notifications gates other players only");
  f.tick(2, {{stat(flash::StatId::LEVEL_STAT), 5},
             {stat(flash::StatId::EXP_STAT), 10},
             {stat(flash::StatId::CHAR_FAME_STAT), 3}});
  (void)f.world.take_status_events();
  f.tick(2, {{stat(flash::StatId::EXP_STAT), 35}});
  check(f.world.take_status_events().empty(), "other players' experience is silent");
  f.tick(2, {{stat(flash::StatId::LEVEL_STAT), 6}});
  check(f.world.take_status_events().empty() && f.world.particles().particles().empty(),
        "other players' level-up text and effect are dropped");
  f.tick(1, {{stat(flash::StatId::LEVEL_STAT), 5}, {stat(flash::StatId::EXP_STAT), 10}});
  (void)f.world.take_status_events();
  f.tick(1, {{stat(flash::StatId::EXP_STAT), 40}});
  auto events = f.world.take_status_events();
  check(events.size() == 1 && events[0].text == "+30 EXP", "own experience is still shown");
  f.tick(1, {{stat(flash::StatId::LEVEL_STAT), 6}});
  events = f.world.take_status_events();
  check(events.size() == 1 && events[0].text == "Level Up!" &&
            !f.world.particles().particles().empty(),
        "own level-up is still shown");
  f.apply([](Settings&) {});
  f.tick(2, {{stat(flash::StatId::EXP_STAT), 70}});
  events = f.world.take_status_events();
  check(events.size() == 1 && events[0].text == "+35 EXP",
        "other players' experience returns with the option on");
}

void shots() {
  Fixture f;
  const ObjectDef* weapon{};
  for (const auto& object : f.catalog.objects()) {
    if (!object.projectiles.empty() && object.xml.number("RateOfFire") > 0 &&
        f.catalog.object(object.projectiles[0].object_id)) {
      weapon = &object;
      break;
    }
  }
  check(weapon, "weapon fixture exists");
  const auto fire = [&](bool ally, std::int32_t owner) {
    net::incoming::ServerPlayerShoot shoot;
    shoot.owner_id = owner;
    shoot.container_type = weapon->type;
    shoot.position = {4.5, 2.5};
    shoot.angle = 0;
    shoot.damages = {5};
    shoot.bullet_id = 1;
    (void)f.combat.receive({flash::PacketId::SERVERPLAYERSHOOT, shoot}, 0);
    (void)ally;
  };
  fire(false, 2);
  check(f.combat.projectiles().size() == 1, "ally shots are drawn by default");
  f.combat.reset(1);
  f.apply([](Settings& s) { s.set_value("allyShots", 0); });
  fire(false, 2);
  check(f.combat.projectiles().empty(), "Ally Shots off skips other players' SERVERPLAYERSHOOT");
  const auto ack = f.combat.receive({flash::PacketId::SERVERPLAYERSHOOT,
                                     [&] {
                                       net::incoming::ServerPlayerShoot s;
                                       s.owner_id = 1;
                                       s.container_type = weapon->type;
                                       s.position = {2.5, 2.5};
                                       s.damages = {5};
                                       s.bullet_id = 3;
                                       return s;
                                     }()},
                                    0);
  check(ack.size() == 1 && f.combat.projectiles().size() == 1,
        "the player's own shot is acknowledged and drawn");
  net::incoming::AllyShoot ally;
  ally.owner_id = 2;
  ally.container_type = weapon->type;
  ally.angle = 0;
  (void)f.combat.receive({flash::PacketId::ALLYSHOOT, ally}, 0);
  check(f.combat.projectiles().size() == 1, "Ally Shots off skips ALLYSHOOT");
  f.apply([](Settings&) {});
  (void)f.combat.receive({flash::PacketId::ALLYSHOOT, ally}, 0);
  check(f.combat.projectiles().size() > 1, "ALLYSHOOT returns with the option on");
}

void particles() {
  Fixture f;
  auto& p = f.world.particles();
  p.trail({1, 1}, 0);
  check(!p.particles().empty(), "trail particles by default");
  p.clear();
  f.apply([](Settings& s) { s.set_value("particles", 0); });
  check(!p.enabled(), "the option reaches the particle system");
  p.trail({1, 1}, 0);
  p.hit(f.world, *f.world.entity(2), 5, 0);
  p.impact(f.catalog.object("Wizard")->texture, {1, 1}, 0, 0, 0);
  check(p.particles().empty(), "hit, impact and trail effects stop");
  net::incoming::ShowEffect effect;
  effect.type = 2;
  effect.position = {2, 2};
  effect.color = 0xff0000;
  p.show(f.world, effect, 0);
  check(p.particles().size() > 0, "ShowEffect is gameplay information and always plays");
  p.clear();
  p.aoe({2, 2}, 2, 0xff0000, 0);
  check(p.particles().size() > 0, "AoE rings always play");
  p.clear();
  f.world.particles().level_up(1, 0);
  check(!p.particles().empty(), "level-up effect always plays");
}

void ally_effects() {
  Fixture f;
  auto& p = f.world.particles();
  net::incoming::ShowEffect effect;
  effect.type = 2;
  effect.position = {4.5, 2.5};
  effect.color = 0xff0000;
  effect.target_id = 2;
  check(!f.world.preferences().ally_particles, "Ally Particles is off by default");
  p.show(f.world, effect, 0);
  check(p.particles().empty(), "Ally Particles off skips effects from other players");
  effect.target_id = 1;
  p.show(f.world, effect, 0);
  check(!p.particles().empty(), "the player's own effects always play");
  p.clear();
  f.apply([](Settings& s) { s.set_value("allyParticles", 1); });
  effect.target_id = 2;
  p.show(f.world, effect, 0);
  check(!p.particles().empty(), "Ally Particles on shows other players' effects");
}

// Projectile.getDirectionAngle: with Projectile Direction on, a wavy shot's heading is the chord
// from its position now to its position 16 ms on; off, it keeps the launch angle.
void direction() {
  Fixture f;
  const ObjectDef* wavy{};
  for (const auto& object : f.catalog.objects()) {
    const auto found = std::find_if(object.projectiles.begin(), object.projectiles.end(),
                                    [](const auto& p) { return p.id == 0 && p.motion.wavy; });
    if (found != object.projectiles.end() && f.catalog.object(found->object_id)) {
      wavy = &object;
      break;
    }
  }
  check(wavy, "wavy weapon fixture exists");
  net::incoming::ServerPlayerShoot shoot;
  shoot.owner_id = 1;
  shoot.container_type = wavy->type;
  shoot.position = {2.5, 2.5};
  shoot.angle = 0;
  shoot.damages = {5};
  shoot.bullet_id = 1;
  (void)f.combat.receive({flash::PacketId::SERVERPLAYERSHOOT, shoot}, 0);
  check(f.combat.projectiles().size() == 1, "the wavy shot is in flight");
  const auto& flight = f.combat.projectiles().front();
  auto* visual = f.world.entity(flight.entity_id);
  check(visual && visual->motion.travel == flight.angle,
        "Projectile Direction off keeps the launch angle");
  (void)f.combat.update(100);
  check(visual->motion.travel == flight.angle, "Projectile Direction off never turns the shot");
  f.apply([](Settings& s) { s.set_value("smartProjectiles", 1); });
  (void)f.combat.update(100);
  const auto ahead = projectile_position(flight.definition->motion, flight.start, flight.angle,
                                         flight.bullet_id, 116);
  const auto now = projectile_position(flight.definition->motion, flight.start, flight.angle,
                                       flight.bullet_id, 100);
  check(std::fabs(visual->motion.travel - std::atan2(ahead.y - now.y, ahead.x - now.x)) < 1e-12 &&
            std::fabs(visual->motion.travel - flight.angle) > 1e-3,
        "Projectile Direction faces the wavy shot's travel");
  check(visual->motion.facing == flight.angle, "the launch angle still drives animation");
}

void tiers() {
  Fixture f;
  check(HudState::player(f.world, *f.world.entity(1)).inventory.tiers, "tier tags by default");
  const auto player_on = player_tooltip_target(f.world, 2);
  check(player_on && player_on->show_tier_tag, "the player tooltip shows tier tags by default");
  const ObjectDef* tiered{};
  for (const auto& object : f.catalog.objects()) {
    if (object.tier >= 0 && object.slot_type >= 1 && object.slot_type <= 25 &&
        !object.consumable && !object.xml.has("NoTierTag")) {
      tiered = &object;
      break;
    }
  }
  check(tiered, "tiered equipment fixture exists");
  const InventoryItem gear{tiered->type, -1};
  const auto item_on = item_tooltip_target(f.world, gear, TooltipOwner::other_player);
  check(item_on && !item_tooltip_content(f.catalog, *item_on).tier.empty(),
        "the item tooltip shows its tier tag by default");
  f.apply([](Settings& s) { s.set_value("showTierTag", 0); });
  check(!HudState::player(f.world, *f.world.entity(1)).inventory.tiers,
        "Show Tier Tag off reaches the HUD");
  // The player tooltip's target carries the setting, so a change rebuilds a cached tooltip.
  const auto player_off = player_tooltip_target(f.world, 2);
  check(player_off && !player_off->show_tier_tag && !(*player_off == *player_on),
        "Show Tier Tag off reaches the player tooltip and changes its target");
  const auto item_off = item_tooltip_target(f.world, gear, TooltipOwner::other_player);
  check(item_off && !item_tooltip_content(f.catalog, *item_off).tier.empty(),
        "the item tooltip keeps its tier tag with Show Tier Tag off");
  check(f.world.preferences().hp_bars && f.world.preferences().draw_shadows &&
            f.world.preferences().text_bubbles,
        "unrelated flags unchanged");
}

// Player Opacity: the local player is never faded, and another player fades only when the toggle
// of their category is on. Locked (starred) takes precedence over Guild.
void player_opacity() {
  Fixture f;
  auto& world = f.world;
  const auto opacity = [&](std::int32_t id) { return world.player_opacity(*world.entity(id)); };
  check(opacity(1) == 1 && opacity(2) == 1, "Player Opacity is 100% by default");
  f.apply([](Settings& s) { s.set_value("playerAlpha", 40); });
  check(opacity(1) == 1 && std::fabs(opacity(2) - .4F) < 1e-6,
        "Fade Other Players fades a player outside your guild, never the local player");
  f.apply([](Settings& s) {
    s.set_value("playerAlpha", 40);
    s.set_value("fadePlayers", 0);
  });
  check(opacity(2) == 1, "Fade Other Players off leaves other players opaque");
  world.entity(1)->profile.guild = "Realm";
  world.entity(2)->profile.guild = "Realm";
  f.apply([](Settings& s) { s.set_value("playerAlpha", 40); });
  check(opacity(2) == 1, "guild members are opaque with Fade Guild Members off");
  f.apply([](Settings& s) {
    s.set_value("playerAlpha", 40);
    s.set_value("fadePlayers", 0);
    s.set_value("fadeGuild", 1);
  });
  check(std::fabs(opacity(2) - .4F) < 1e-6, "Fade Guild Members fades guild members on its own");
  world.set_starred_objects({2});
  check(world.starred(2) && !world.starred(1), "the starred set reaches the World");
  check(opacity(2) == 1, "Locked takes precedence: a starred guild member needs Fade Locked Players");
  f.apply([](Settings& s) {
    s.set_value("playerAlpha", 40);
    s.set_value("fadePlayers", 0);
    s.set_value("fadeLocked", 1);
  });
  check(std::fabs(opacity(2) - .4F) < 1e-6, "Fade Locked Players fades starred players");
  world.set_starred_objects({1, 2});
  check(opacity(1) == 1, "the local player is never faded, even when starred");
  f.apply([](Settings& s) {
    s.set_value("playerAlpha", 0);
    s.set_value("fadeLocked", 1);
  });
  check(opacity(2) <= 0 && opacity(1) == 1, "0% reaches zero, which the Scene skips");
  Quad q;
  q.multiply = {1, 1, 1, 1};
  q.offset[3] = .5F;
  fade_quad(q, .25F);
  check(q.multiply == std::array<float, 4>{1, 1, 1, .25F} && q.offset[3] == .125F,
        "fading scales the alpha terms and leaves the colour alone");
}

void player_alpha_rules() {
  Preferences p;
  p.player_alpha = .4F;
  check(player_alpha(p, true, false, false) == 1 && player_alpha(p, true, true, true) == 1,
        "the local player is never faded");
  check(player_alpha(p, false, false, false) == .4F && player_alpha(p, false, false, true) == 1 &&
            player_alpha(p, false, true, false) == 1,
        "by default Regular players fade and guild and locked players do not");
  p.fade_guild = true;
  check(player_alpha(p, false, false, true) == .4F && player_alpha(p, false, true, true) == 1,
        "Locked takes precedence over Guild");
  p.fade_players = false;
  p.fade_locked = true;
  check(player_alpha(p, false, false, false) == 1 && player_alpha(p, false, true, false) == .4F &&
            player_alpha(p, false, false, true) == .4F,
        "each category follows its own toggle");
  p.player_alpha = 0;
  check(player_alpha(p, false, true, false) == 0, "0% hides a faded player");
}
}

int main() {
  try {
    damage();
    notifications();
    shots();
    particles();
    ally_effects();
    direction();
    tiers();
    player_opacity();
    player_alpha_rules();
    std::cout << "Preference checks: " << checks << '\n';
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
