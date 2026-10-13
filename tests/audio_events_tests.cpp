// CPU-only checks that game events queue the Flash sounds they should, using the real Flash catalog.
#include "flash_assets.hpp"
#include "realm/audio.hpp"
#include "realm/combat.hpp"
#include "realm/inventory.hpp"
#include "realm/session.hpp"
#include "realm/sound_cues.hpp"
#include "realm/world.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int checks{};
void check(bool ok, const char* why) {
  ++checks;
  if (!ok) throw std::runtime_error(why);
}

// Drains a queue and returns its cues in push order.
std::vector<realm::SoundCue> cues_of(realm::SoundCues& queue) {
  std::vector<realm::SoundCue> out;
  queue.drain(out);
  return out;
}

// Names with a "preload:" prefix for loads, so one comparison covers both kinds.
std::vector<std::string> names_of(realm::SoundCues& queue) {
  std::vector<std::string> names;
  for (const auto& cue : cues_of(queue)) names.push_back(cue.preload ? "preload:" + cue.name : cue.name);
  return names;
}

bool same(const std::vector<std::string>& actual, const std::vector<std::string>& expected) {
  return actual == expected;
}

// Combat.shoot needs a rate-limit-free player; the same fixture rules as combat_tests.cpp, plus OldSound.
const realm::ObjectDef* find_weapon(const realm::Catalog& catalog, const char* sound) {
  for (const auto& object : catalog.objects())
    if (object.old_sound == sound && !object.projectiles.empty() && object.rate_of_fire > 0 &&
        object.num_projectiles == 1 && object.projectiles[0].id == 0 && catalog.object(object.projectiles[0].object_id))
      return &object;
  return nullptr;
}

const realm::ObjectDef* find_ability(const realm::Catalog& catalog) {
  for (const auto& object : catalog.objects())
    if (object.usable && object.mp_cost > 0 && !object.ability_target && !object.ability_teleport &&
        !object.ability_shoot)
      return &object;
  return nullptr;
}

void cue_queue_cap() {
  realm::SoundCues queue;
  for (int i = 0; i < 300; ++i) queue.play("cue" + std::to_string(i));
  check(queue.size() == realm::SoundCues::capacity && queue.dropped() == 44,
        "cue queue keeps 256 distinct cues and counts 44 drops");
  std::vector<realm::SoundCue> out;
  check(queue.drain(out) == 44 && out.size() == 256, "drain reports overflow since the last drain");
  check(queue.drain(out) == 0 && out.empty() && queue.size() == 0, "second drain is empty without overflow");
  // A flood of one identical cue collapses to one entry and never counts as overflow.
  realm::SoundCues flood;
  for (int i = 0; i < 300; ++i) flood.play("monster/default_hit");
  check(flood.size() == 1 && flood.dropped() == 0 && flood.coalesced() == 299, "identical cues in one frame collapse");
  flood.play("monster/default_hit", 0.75F, false);
  flood.preload("monster/default_hit");
  check(flood.size() == 3, "same name with another volume, gate or load flag stays a distinct cue");
  flood.drain(out);
  flood.play("monster/default_hit");
  check(flood.size() == 1, "a drained frame starts a new collapse window");
  // Distinct cues that arrive after a flood still fit: the flood no longer pushes them out.
  realm::SoundCues mixed;
  for (int i = 0; i < 1000; ++i) mixed.play("monster/default_hit");
  mixed.play("level_up");
  check(mixed.size() == 2 && mixed.dropped() == 0, "a later distinct cue survives a same-frame flood");
  queue.preload("monster/default_hit");
  queue.play("weapon", 0.75F, false);
  const auto pair = cues_of(queue);
  check(pair.size() == 2 && pair[0].preload && pair[0].name == "monster/default_hit" && pair[0].volume == 1.F &&
            pair[0].is_fx,
        "preload cue carries its name and the default volume");
  check(!pair[1].preload && pair[1].volume == 0.75F && !pair[1].is_fx,
        "play cue keeps volume and the pew-pew gate flag");
}

void catalog_sounds(const realm::Catalog& catalog) {
  const auto* wizard = catalog.object("Wizard");
  check(wizard && wizard->player, "Wizard is a player class");
  check(wizard->hit_sound == "player/wizard_hit" && wizard->death_sound == "player/wizard_death",
        "explicit player HitSound and DeathSound");
  // Character.as defaults apply only when the XML omits the tag, so find a Character or Player without them.
  const realm::ObjectDef* plain = nullptr;
  for (const auto& object : catalog.objects())
    if ((object.class_name == "Character" || object.class_name == "Player") && !object.xml.has("HitSound") &&
        !object.xml.has("DeathSound")) {
      plain = &object;
      break;
    }
  check(plain && plain->hit_sound == "monster/default_hit" && plain->death_sound == "monster/default_death",
        "Character defaults when the XML omits the sounds");
  const auto* pirate = catalog.object("Pirate");
  check(pirate && pirate->class_name == "Character", "Pirate is a character class");
  check(pirate->hit_sound == "monster/pirates_hit" && pirate->death_sound == "monster/pirates_death",
        "explicit HitSound and DeathSound");
  const auto* bag = catalog.object("Loot Bag");
  check(bag && bag->container && bag->loot && bag->hit_sound.empty() && bag->death_sound.empty(),
        "loot container has no character sounds");
  for (const auto& object : catalog.objects())
    if (object.class_name == "Portal") {
      check(object.hit_sound.empty() && object.death_sound.empty() && object.sounds.empty(),
            "non-character objects have no hit or death sound");
      break;
    }
  const auto* dagger = find_weapon(catalog, "daggerSwing");
  check(dagger && dagger->old_sound == "daggerSwing", "OldSound parsed on a weapon");
}

void hit_sounds(const realm::Catalog& catalog) {
  realm::World world(catalog);
  world.initialize(8, 8);
  const auto* wizard = catalog.object("Wizard");
  const auto* pirate = catalog.object("Pirate");
  world.add(99, wizard->type, {2.5, 2.5});
  world.set_player_id(99);
  world.add(55, pirate->type, {3.5, 3.5});
  check(world.sounds().size() == 0, "adding entities queues no sounds by itself");
  world.report_damage(55, 0, 100);  // Character.damage plays even for a zero-amount hit
  check(same(names_of(world.sounds()), {"monster/pirates_hit"}), "enemy hit plays its HitSound");
  world.report_damage(99, 12, 100);
  check(same(names_of(world.sounds()), {"player/wizard_hit"}), "own hit plays the player's HitSound");
  realm::Preferences quiet;
  quiet.ally_damage = false;
  world.set_preferences(quiet);
  world.report_damage(55, 5, 200);
  check(world.sounds().size() == 0, "Ally Damage off drops the other object's damage report and its hit sound");
  world.report_damage(99, 5, 200);
  check(same(names_of(world.sounds()), {"player/wizard_hit"}), "own damage still plays with Ally Damage off");
  world.sound_hit(55);
  check(same(names_of(world.sounds()), {"monster/pirates_hit"}),
        "sound_hit plays the enemy's sound for an own projectile");
}

void death_sounds(const realm::Catalog& catalog) {
  realm::World world(catalog);
  world.initialize(8, 8);
  const auto* pirate = catalog.object("Pirate");
  world.add(55, pirate->type, {3.5, 3.5});
  world.add(56, pirate->type, {4.5, 3.5});
  realm::net::Update explode;
  explode.drops.push_back({55, true});
  world.apply(explode);
  check(same(names_of(world.sounds()), {"monster/pirates_death"}), "exploding Character plays its DeathSound");
  check(world.entity(55) == nullptr, "dropped object is removed");
  realm::net::Update removal;
  removal.drops.push_back({56, false});
  world.apply(removal);
  check(world.sounds().size() == 0 && world.entity(56) == nullptr, "non-explode removal plays nothing");
}

void level_up_sounds(const realm::Catalog& catalog) {
  realm::World world(catalog);
  world.initialize(8, 8);
  const auto* wizard = catalog.object("Wizard");
  const auto level = static_cast<std::uint8_t>(realm::flash::StatId::LEVEL_STAT);
  realm::net::Update join;
  join.objects.push_back({wizard->type, {99, {2.5F, 2.5F}, {{level, std::int32_t{1}}}}});
  join.objects.push_back({wizard->type, {77, {4.5F, 2.5F}, {{level, std::int32_t{1}}}}});
  world.apply(join);
  world.set_player_id(99);
  (void)names_of(world.sounds());  // join preloads are not under test here
  realm::net::Tick own;
  own.player_stats.push_back({level, std::int32_t{2}});
  world.apply(own);
  check(same(names_of(world.sounds()), {"level_up"}), "own level-up plays level_up");
  realm::net::Tick other;
  other.statuses.push_back({77, {4.5F, 2.5F}, {{level, std::int32_t{2}}}});
  world.apply(other, 127);
  check(world.sounds().size() == 0, "another player's level-up shows the notification without a sound");
}

void loot_sounds(const realm::Catalog& catalog) {
  realm::World world(catalog);
  world.initialize(40, 40);
  const auto* wizard = catalog.object("Wizard");
  const auto* bag = catalog.object("Loot Bag");
  realm::net::Update join;
  join.objects.push_back({wizard->type, {99, {2.5F, 2.5F}, {}}});
  world.apply(join);
  world.set_player_id(99);
  (void)names_of(world.sounds());
  realm::net::Update near;
  near.objects.push_back({bag->type, {60, {4.0F, 2.5F}, {}}});
  world.apply(near);
  check(same(names_of(world.sounds()), {"loot_appears"}),
        "loot container within 10 units of the player plays loot_appears");
  realm::net::Update far;
  far.objects.push_back({bag->type, {61, {20.0F, 2.5F}, {}}});
  world.apply(far);
  check(world.sounds().size() == 0, "distant loot container is silent");
}

void preload_on_sighting(const realm::Catalog& catalog) {
  realm::World world(catalog);
  world.initialize(8, 8);
  const auto* pirate = catalog.object("Pirate");
  realm::net::Update first;
  first.objects.push_back({pirate->type, {55, {3.5F, 3.5F}, {}}});
  world.apply(first);
  check(same(names_of(world.sounds()), {"preload:monster/pirates_hit", "preload:monster/pirates_death"}),
        "first sighting of a type preloads its hit and death sounds");
  realm::net::Update second;
  second.objects.push_back({pirate->type, {56, {4.5F, 3.5F}, {}}});
  world.apply(second);
  check(world.sounds().size() == 0, "later sightings of the same type do not preload again");
}

void weapon_volley(const realm::Catalog& catalog) {
  realm::World world(catalog);
  world.initialize(8, 8);
  const auto grass = catalog.ground("Grass")->type;
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 8; ++x) world.tile(x, y)->type = grass;
  world.add(99, catalog.object("Wizard")->type, {2.5, 2.5});
  world.set_player_id(99);
  const auto* dagger = find_weapon(catalog, "daggerSwing");
  world.entity(99)->profile.stats[8] = static_cast<std::int32_t>(dagger->type);  // INVENTORY_0
  world.entity(99)->combat.attack_start = -100000;
  realm::Combat combat(world);
  combat.reset(7);
  const auto outgoing = combat.shoot(0, 3000);
  check(outgoing.size() == 1, "local shot is sent");
  const auto cues = cues_of(world.sounds());
  check(cues.size() == 1 && cues[0].name == "daggerSwing" && cues[0].volume == 0.75F && !cues[0].is_fx,
        "first projectile plays OldSound at 0.75 without the pew-pew gate");
}

void ability_failures(const realm::Catalog& catalog) {
  realm::World world(catalog);
  world.initialize(8, 8);
  const auto grass = catalog.ground("Grass")->type;
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 8; ++x) world.tile(x, y)->type = grass;
  world.add(99, catalog.object("Wizard")->type, {2.5, 2.5});
  world.set_player_id(99);
  const auto* ability = find_ability(catalog);
  check(ability != nullptr, "usable MP ability fixture exists");
  auto* player = world.entity(99);
  player->profile.stats[9] = static_cast<std::int32_t>(ability->type);  // INVENTORY_1
  realm::Combat combat(world);
  combat.reset(3);
  (void)cues_of(world.sounds());
  check(combat.use_ability({2.5, 2.5}, 5000).empty() && same(names_of(world.sounds()), {"no_mana"}),
        "insufficient MP plays no_mana");
  player->profile.stats[static_cast<std::uint8_t>(realm::flash::StatId::MP_STAT)] = 100000;
  const auto sent = combat.use_ability({2.5, 2.5}, 5000);
  check(!sent.empty() && world.sounds().size() == 0, "usable ability sends USEITEM without a sound");
  check(combat.use_ability({2.5, 2.5}, 5001).empty() && same(names_of(world.sounds()), {"error"}),
        "ability inside cooldown plays error");
}

// MapUserInput.useSpecial through the same latch the frame loop uses: a held key retries while it fails, and after a
// success it stays quiet until released, so the cooldown does not replay "error" every frame.
void special_key_latch(const realm::Catalog& catalog) {
  realm::World world(catalog);
  world.initialize(8, 8);
  world.add(99, catalog.object("Wizard")->type, {2.5, 2.5});
  world.set_player_id(99);
  const auto* ability = find_ability(catalog);
  check(ability != nullptr, "usable MP ability fixture exists");
  auto* player = world.entity(99);
  player->profile.stats[9] = static_cast<std::int32_t>(ability->type);
  player->profile.stats[static_cast<std::uint8_t>(realm::flash::StatId::MP_STAT)] = 100000;
  realm::Combat combat(world);
  combat.reset(5);
  realm::SpecialKeyLatch latch;
  int attempts_first = 0, successes_first = 0, attempts_retry = 0, successes_retry = 0;
  std::size_t errors_first = 0, errors_retry = 0;
  // One frame: a held key attempts when the latch allows it; every frame drains its cues.
  auto frame = [&](std::int32_t t, bool held, int& attempts, int& successes, std::size_t& errors) {
    if (!held) {
      latch.released();
      return;
    }
    if (latch.attempts()) {
      ++attempts;
      const bool ok = !combat.use_ability({2.5, 2.5}, t).empty();
      latch.record(ok);
      if (ok) ++successes;
    }
    for (const auto& cue : cues_of(world.sounds()))
      if (cue.name == "error") ++errors;
  };
  // Press at 5000 and hold through several frames: one success, no error cues, no further attempts.
  for (std::int32_t t = 5000; t < 5060; t += 16) frame(t, true, attempts_first, successes_first, errors_first);
  check(attempts_first == 1 && successes_first == 1 && errors_first == 0,
        "held key attempts once and latches after success");
  frame(5060, false, attempts_first, successes_first, errors_first);
  check(!latch.latched(), "release clears the latch");
  // Press again inside the cooldown: failures retry each frame while held, then one success latches the key again.
  for (std::int32_t t = 5100; t < 5700; t += 16) frame(t, true, attempts_retry, successes_retry, errors_retry);
  check(successes_retry == 1, "held key succeeds once the cooldown ends");
  check(errors_retry >= 1 && attempts_retry == static_cast<int>(errors_retry) + 1,
        "every failed attempt replays error while held");
  check(latch.latched(), "success latches the held key");
  std::cout << "  special key: first press attempts=1 errors=0; held through cooldown retries=" << attempts_retry
            << " errors=" << errors_retry << " successes=" << successes_retry << '\n';
}

void inventory_failure(const realm::Catalog& catalog) {
  realm::World world(catalog);
  world.initialize(8, 8);
  world.add(99, catalog.object("Wizard")->type, {2.5, 2.5});
  world.set_player_id(99);
  realm::Inventory inventory(world);
  inventory.result(0);
  check(world.sounds().size() == 0, "successful INVRESULT is silent");
  inventory.result(1);
  check(same(names_of(world.sounds()), {"error"}), "failed INVRESULT plays error even with nothing pending");
}

void playsound_packet(const realm::Catalog& catalog) {
  realm::World world(catalog);
  world.initialize(8, 8);
  realm::Session session(world, [](const realm::net::Packet&) {}, realm::SessionOptions{});
  realm::net::Writer body;
  body.utf("monster/default_death");
  session.replay(realm::net::Packet{static_cast<std::uint8_t>(realm::flash::PacketId::PLAYSOUND), body.bytes()}, 10);
  check(same(names_of(world.sounds()), {"monster/default_death"}), "PLAYSOUND plays the server's sound name");
}

void map_change_cancels(const realm::Catalog& catalog) {
  (void)catalog;
  // A load that cannot succeed keeps the play pending until the next service pass, so the cancel is deterministic.
  realm::audio::PlayerConfig config;
  config.output = false;
  config.source.disk_root.clear();
  config.pending_timeout = std::chrono::seconds(10);
  realm::audio::Player player(config);
  player.play("monster/default_hit");
  check(player.counters().plays_deferred == 1 && player.counters().pending_now == 1, "play waits for its load");
  player.cancel_pending();
  check(player.counters().pending_now == 0 && player.counters().pending_dropped == 1, "map change drops waiting plays");
}

}  // namespace

int main() {
  try {
    realm::Catalog catalog(realm::flash::source_root);
    cue_queue_cap();
    catalog_sounds(catalog);
    hit_sounds(catalog);
    death_sounds(catalog);
    level_up_sounds(catalog);
    loot_sounds(catalog);
    preload_on_sighting(catalog);
    weapon_volley(catalog);
    ability_failures(catalog);
    special_key_latch(catalog);
    inventory_failure(catalog);
    playsound_packet(catalog);
    map_change_cancels(catalog);
    std::cout
        << "Audio events: " << checks
        << " checks passed (queue cap, catalog Hit/Death/OldSound/Loot, hit with ally filter, death, own level-up, "
           "loot, preload, weapon volley, ability and inventory errors, PLAYSOUND, map-change cancel)\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
