#include "realm/objects/Combat.hpp"
#include "realm/hud/Hud.hpp"
#include "flash_assets.hpp"
#include <functional>
#include <iostream>
#include <stdexcept>
// The Options screen's flags as the game systems apply them: Parameters.data_ checks in
// GameServerConnection, GameObject, Projectile and ParticleEffect, without a GL context.
namespace {
int checks{};
void check(bool ok, const std::string& why) {
  ++checks;
  if (!ok) throw std::runtime_error(why);
}
}
using namespace realm;
namespace {
constexpr std::uint8_t stat(flash::StatId id) { return static_cast<std::uint8_t>(id); }
struct Fixture {
  Catalog catalog{flash::source_root};
  World world{catalog};
  Combat combat{world};
  Fixture() {
    world.initialize(8, 8, "preferences");
    const auto grass = catalog.ground("Grass")->type;
    for (int y = 0; y < 8; ++y)
      for (int x = 0; x < 8; ++x) world.tile(x, y)->type = grass;
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
  f.apply([](Settings& s) { s.set_choice("allyDamage", 0); });
  f.world.report_damage(2, 10, 0);
  check(f.world.take_status_events().empty() && f.world.damage_cues().size() == 1,
        "Ally Damage off drops damage to others");
  f.world.report_damage(1, 10, 0);
  check(f.world.take_status_events().size() == 1, "damage to the player is always shown");
}
void notifications() {
  Fixture f;
  f.apply([](Settings& s) { s.set_choice("allyNotifs", 0); });
  check(f.world.allows_notification(1) && !f.world.allows_notification(2) && f.world.allows_notification(77),
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
  check(events.size() == 1 && events[0].text == "Level Up!" && !f.world.particles().particles().empty(),
        "own level-up is still shown");
  f.apply([](Settings&) {});
  f.tick(2, {{stat(flash::StatId::EXP_STAT), 70}});
  events = f.world.take_status_events();
  check(events.size() == 1 && events[0].text == "+35 EXP", "other players' experience returns with the option on");
}
void shots() {
  Fixture f;
  const ObjectDef* weapon{};
  for (const auto& object : f.catalog.objects())
    if (!object.projectiles.empty() && object.xml.number("RateOfFire") > 0 &&
        f.catalog.object(object.projectiles[0].object_id)) {
      weapon = &object;
      break;
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
  f.apply([](Settings& s) { s.set_choice("allyShots", 0); });
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
  check(ack.size() == 1 && f.combat.projectiles().size() == 1, "the player's own shot is acknowledged and drawn");
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
  f.apply([](Settings& s) { s.set_choice("particles", 0); });
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
void tiers() {
  Fixture f;
  check(HudState::player(f.world, *f.world.entity(1)).inventory.tiers, "tier tags by default");
  f.apply([](Settings& s) { s.set_choice("showTierTag", 0); });
  check(!HudState::player(f.world, *f.world.entity(1)).inventory.tiers, "Show Tier Tag off reaches the HUD");
  check(f.world.preferences().hp_bars && f.world.preferences().draw_shadows && f.world.preferences().text_bubbles,
        "unrelated flags unchanged");
}
}
int main() {
  try {
    damage();
    notifications();
    shots();
    particles();
    tiers();
    std::cout << "Preference checks: " << checks << '\n';
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
