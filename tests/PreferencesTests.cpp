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
  f.apply([](Settings& s) { s.set_choice("allyDamage", 0); });
  // Ally Damage drops only the server's damage packets for others (Session), so the player's own
  // hits show.
  f.world.report_damage(2, 10, 0);
  check(f.world.take_status_events().size() == 1,
        "Ally Damage off still shows the player's own hits on others");
  f.world.report_damage(1, 10, 0);
  check(f.world.take_status_events().size() == 1, "damage to the player is always shown");
}

void notifications() {
  Fixture f;
  f.apply([](Settings& s) { s.set_choice("allyNotifs", 0); });
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

// Particles: Off, Minimal and All. Off plays no particle, but a flash (not a particle) still adds.
// Minimal keeps the gameplay effects at reduced density, and lowering the option drops what it
// gates.
void particles() {
  Fixture f;
  auto& p = f.world.particles();
  net::incoming::ShowEffect effect;
  effect.position = {2, 2};
  effect.color = 0xff0000;
  // Shows one effect of a type sourced from the player and returns the particles it made.
  const auto shown = [&](std::uint8_t type, net::incoming::ShowEffect base) {
    p.clear();
    base.type = type;
    base.target_id = 1;
    p.show(f.world, base, 0);
    return p.particles().size();
  };
  net::incoming::ShowEffect ring;  // type 10: start.x is the radius
  ring.position = {3, 0};
  net::incoming::ShowEffect collapse;  // type 12: distance(start, end) is the radius
  collapse.position = {2.5, 2.5};
  collapse.position2 = {4.5, 2.5};
  net::incoming::ShowEffect cone;  // type 13: end.x is the radius
  cone.position = {3.5, 2.5};
  cone.position2 = {3, 0};
  check(f.world.preferences().particles == ParticleQuality::all &&
            p.quality() == ParticleQuality::all,
        "Particles is All by default");
  check(shown(10, ring) == 12 && shown(12, collapse) == 24 && shown(13, cone) == 7,
        "All plays the full ring, collapse and cone counts");
  p.clear();
  p.trail({1, 1}, 0);
  check(!p.particles().empty(), "trail particles by default");
  p.clear();
  p.aoe({2, 2}, 128, 0xff0000, 0);
  const auto all_aoe = p.particles().size();
  check(all_aoe == 256, "All AoE at radius 128 has 256 sparks");

  // Off: no particles from any source, but a flash still adds.
  f.apply([](Settings& s) { s.set_choice("particles", 0); });
  check(p.quality() == ParticleQuality::off, "Off reaches the particle system");
  p.clear();
  p.trail({1, 1}, 0);
  p.hit(f.world, *f.world.entity(2), 5, 0);
  p.impact(f.catalog.object("Wizard")->texture, {1, 1}, 0, 0, 0);
  p.aoe({2, 2}, 128, 0xff0000, 0);
  check(p.particles().empty(), "Off: hit, impact, trail and AoE create nothing");
  check(shown(1, effect) == 0 && shown(10, ring) == 0, "Off: heal and ring create nothing");
  p.clear();
  net::incoming::ShowEffect flash_effect;
  flash_effect.type = 15;
  flash_effect.target_id = 1;
  flash_effect.position = {.5F, 3};
  flash_effect.color = 0xff0000;
  p.show(f.world, flash_effect, 0);
  check(p.particles().empty() && p.flashes().size() == 1, "Off still adds a flash");

  // Minimal: heals, rings and collapses at reduced density; teleports and lines are dropped.
  f.apply([](Settings& s) { s.set_choice("particles", 2); });
  check(f.world.preferences().particles == ParticleQuality::minimal &&
            p.quality() == ParticleQuality::minimal,
        "Minimal reaches the particle system");
  check(shown(2, effect) == 0 && shown(7, effect) == 0, "Minimal drops teleports and lines");
  check(shown(1, effect) == 10, "Minimal keeps a heal at 10 sparks");
  check(shown(10, ring) == 6 && shown(12, collapse) == 12 && shown(13, cone) == 7,
        "Minimal halves the ring and collapse fans and keeps the cone blast at 7");
  p.clear();
  p.aoe({2, 2}, 128, 0xff0000, 0);
  check(p.particles().size() == all_aoe / 4,
        "Minimal AoE at radius 128 keeps the shape with a quarter of the sparks");
  p.clear();
  p.trail({1, 1}, 0);
  p.hit(f.world, *f.world.entity(2), 5, 0);
  p.impact(f.catalog.object("Wizard")->texture, {1, 1}, 0, 0, 0);
  p.explode(f.world, *f.world.entity(2), 0);
  p.level_up(1, 0);
  check(p.particles().empty(),
        "Minimal: hit, impact, trail, death burst and level-up create nothing");

  // Lowering the option: Minimal stops the object-texture emitters and keeps the heal emitter;
  // Off clears every particle and emitter. Raising it again does not bring them back.
  f.apply([](Settings& s) { s.set_choice("particles", 1); });
  p.clear();
  TextureDef mist;
  mist.effect.kind = "XMLEffect";
  mist.effect.particle = "SwirlingMist";
  mist.effect.cooldown = .1;
  p.attach(*f.world.entity(2), mist);
  f.world.entity(1)->combat.condition = 1U << 15U;
  p.healing(*f.world.entity(1));
  effect.type = 1;
  effect.target_id = 1;
  p.show(f.world, effect, 0);
  check(p.emitter_count() == 2 && !p.particles().empty(),
        "All keeps the object emitter and the heal emitter");
  f.apply([](Settings& s) { s.set_choice("particles", 2); });
  check(p.emitter_count() == 1 && !p.particles().empty(),
        "Minimal stops object-texture emitters and keeps the heal emitter");
  f.apply([](Settings& s) { s.set_choice("particles", 0); });
  check(p.particles().empty() && p.emitter_count() == 0,
        "Off clears the particles and every emitter");
  f.apply([](Settings& s) { s.set_choice("particles", 1); });
  check(p.emitter_count() == 0, "raising the option does not restore stopped emitters");
  f.world.entity(1)->combat.condition = 0;
}

void tiers() {
  Fixture f;
  check(HudState::player(f.world, *f.world.entity(1)).inventory.tiers, "tier tags by default");
  f.apply([](Settings& s) { s.set_choice("showTierTag", 0); });
  check(!HudState::player(f.world, *f.world.entity(1)).inventory.tiers,
        "Show Tier Tag off reaches the HUD");
  check(f.world.preferences().hp_bars && f.world.preferences().shadows == ShadowQuality::high &&
            f.world.preferences().text_bubbles,
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
