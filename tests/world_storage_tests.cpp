// World/Combat storage and lifetime checks, plus `--profile` allocation and memory measurements.
// The global allocator is counted here (this executable only) so allocation traffic, live bytes and the
// high-water mark of the production World/Combat can be compared before and after layout changes.
#include "realm/combat.hpp"
#include "realm/entity_index.hpp"
#include "realm/stat_map.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <malloc.h>
#include <new>
#include <random>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>
namespace {
struct Heap {
  std::size_t allocations{}, frees{}, live{}, peak{};
} heap;
bool counting{};
void* take(std::size_t size) {
  void* p = std::malloc(size ? size : 1);
  if (!p) throw std::bad_alloc();
  if (counting) {
    ++heap.allocations;
    heap.live += malloc_usable_size(p);
    heap.peak = std::max(heap.peak, heap.live);
  }
  return p;
}
void give(void* p) noexcept {
  if (!p) return;
  if (counting) {
    ++heap.frees;
    heap.live -= std::min(heap.live, malloc_usable_size(p));
  }
  std::free(p);
}
}
void* operator new(std::size_t n) { return take(n); }
void* operator new[](std::size_t n) { return take(n); }
void operator delete(void* p) noexcept { give(p); }
void operator delete[](void* p) noexcept { give(p); }
void operator delete(void* p, std::size_t) noexcept { give(p); }
void operator delete[](void* p, std::size_t) noexcept { give(p); }
namespace {
int checks{};
void check(bool ok, const char* why) {
  ++checks;
  if (!ok) throw std::runtime_error(why);
}
using realm::Combat;
using realm::World;
struct Defs {
  std::uint16_t player{}, enemy{}, model{}, single{}, multi{}, merchant{};
};
Defs find_defs(const realm::Catalog& catalog) {
  Defs f;
  f.player = catalog.object("Wizard")->type;
  for (const auto& object : catalog.objects()) {
    if (object.enemy && !object.stationary && !f.enemy) f.enemy = object.type;
    if (!f.model && !object.model.empty() && !object.interactive) f.model = object.type;
    if (!f.merchant && object.interactive) f.merchant = object.type;
    if (object.projectiles.empty()) continue;
    const auto& p = object.projectiles[0];
    if (p.id != 0 || !catalog.object(p.object_id) || p.motion.wavy || p.motion.parametric || p.motion.boomerang ||
        p.motion.amplitude || p.motion.accelerate || p.motion.decelerate || p.motion.speed <= 0 ||
        p.motion.lifetime_ms < 400)
      continue;
    if (p.multi_hit && !f.multi) f.multi = object.type;
    if (!p.multi_hit && !f.single) f.single = object.type;
  }
  return f;
}
void ground(World& world, int size) {
  world.initialize(size, size);
  const auto grass = world.catalog().ground("Grass")->type;
  for (int y = 0; y < size; ++y)
    for (int x = 0; x < size; ++x) world.tile(x, y)->type = grass;
}
void shoot(Combat& combat, std::uint16_t container, std::int32_t bullet, realm::Vec2 at, float angle, int count,
           std::int32_t time, std::int32_t owner) {
  realm::net::incoming::ServerPlayerShoot s;
  s.bullet_id = bullet;
  s.owner_id = owner;
  s.container_type = static_cast<std::int16_t>(container);
  s.position = {static_cast<float>(at.x), static_cast<float>(at.y)};
  s.angle = angle;
  s.angle_increment = .07F;
  s.damages.assign(static_cast<std::size_t>(count), 10);
  (void)combat.receive({realm::flash::PacketId::SERVERPLAYERSHOOT, s}, time);
}
// Population as the stress fixture builds it: named players with stats, enemies, models, over `span` tiles.
void populate(World& world, const Defs& f, int players, int enemies, int models, double span,
              std::int32_t first_id = 1000) {
  std::mt19937 rng(7);
  std::uniform_real_distribution<double> d(-span, span);
  const auto c = world.spawn_position();
  for (int i = 0; i < players + enemies + models; ++i) {
    const auto type = i < players ? f.player : i < players + enemies ? f.enemy : f.model;
    auto& e = world.add(first_id + i, type, {c.x + d(rng), c.y + d(rng)});
    // Other players carry about forty stats (hp/mp, attributes, inventory, appearance); enemies a handful.
    if (i < players + enemies) {
      e.profile.name = (i < players ? "Player" : "Enemy") + std::to_string(i);
      const int count = i < players ? 40 : 5;
      for (int k = 0; k < count; ++k) e.profile.stats[static_cast<std::uint8_t>(k < 5 ? k : 20 + k)] = i + k;
    }
  }
}
// The world's own invariant: every entity is found by id at its own dense index, and removal never leaves a stale id.
void check_consistent(const World& world) {
  const auto& all = world.entities();
  for (std::size_t i = 0; i < all.size(); ++i) {
    check(world.entity(all[i].id) == &all[i], "lookup finds each entity at its dense slot");
  }
}
struct Window {
  std::size_t allocations{}, frees{}, bytes{};
  double ms{};
};
// ---- correctness ----
void lifetime_checks(const realm::Catalog& catalog, const Defs& f) {
  World world(catalog);
  ground(world, 64);
  Combat combat(world);
  world.add(99, f.player, {32, 32});
  world.set_player_id(99);
  // Removal in every position keeps lookups exact; ids are reusable; a stale id is never found.
  std::mt19937 rng(11);
  std::vector<std::int32_t> live;
  std::int32_t next = 1000;
  for (int step = 0; step < 6000; ++step) {
    const auto action = rng() % 5;
    if (action < 3 || live.empty()) {
      world.add(next, (rng() & 1U) ? f.enemy : f.merchant,
                {1.5 + static_cast<double>(rng() % 60), 1.5 + static_cast<double>(rng() % 60)});
      live.push_back(next++);
    } else if (action == 3) {
      const auto at = rng() % live.size();
      const auto id = live[at];
      world.remove(id);
      check(!world.entity(id), "removed id is not found");
      live[at] = live.back();
      live.pop_back();
    } else {
      const auto id = live[rng() % live.size()];
      world.add(id, f.enemy, {5, 5});
      check(world.entity(id) && world.entity(id)->type == f.enemy, "re-adding an id replaces it");
    }
    if (step % 97 == 0) check_consistent(world);
    // interactive_target agrees with the definition-driven scan after arbitrary add/remove.
    if (step % 251 == 0) {
      const realm::Entity* expect{};
      double best = 1;
      const auto* player = world.entity(99);
      for (const auto& e : world.entities()) {
        const auto* d = catalog.object(e.type);
        if (!d || !d->interactive) continue;
        const double dist = std::hypot(e.motion.position.x - player->motion.position.x,
                                       e.motion.position.y - player->motion.position.y);
        if (dist < best) {
          best = dist;
          expect = &e;
        }
      }
      check(world.interactive_target() == expect, "interactive_target follows add/remove");
    }
  }
  check_consistent(world);
  // Projectiles hold ids, not pointers: removing the visual out from under one expires it cleanly.
  ground(world, 64);
  world.add(99, f.player, {32, 32});
  world.set_player_id(99);
  shoot(combat, f.single, 1, {10, 10}, 0, 3, 0, 99);
  check(combat.projectiles().size() == 3, "three projectiles");
  world.remove(combat.projectiles()[1].entity_id);
  for (int i = 0; i < 4000; ++i)
    world.add(5000 + i, f.enemy, {50, 50});  // force several reallocations of the dense list
  (void)combat.update(10);
  check(combat.projectiles().size() == 2, "projectile with a removed visual expires; others survive reallocation");
  for (const auto& p : combat.projectiles())
    check(world.entity(p.entity_id) && world.entity(p.entity_id)->projectile,
          "surviving projectile visuals resolve after reallocation");
  // Map reset and reset() leave nothing referring to the previous generation.
  const auto generation = world.generation();
  combat.reset(3);
  world.initialize(64, 64);
  check(world.generation() == generation + 1 && world.entities().empty(),
        "initialize clears and advances the generation");
  check(combat.projectiles().empty() && !world.entity(99) && !world.interactive_target(),
        "nothing survives a map reset");
  check(combat.update(20).empty(), "update after reset is empty");
}
// Repeated map teardown and population churn do not accumulate heap.
void bounded_checks(const realm::Catalog& catalog, const Defs& f) {
  counting = true;
  std::size_t baseline{};
  std::vector<std::size_t> plateau;
  {
    World world(catalog);
    Combat combat(world);
    ground(world, 128);
    world.add(1, f.player, {64, 64});
    world.set_player_id(1);
    baseline = heap.live;
    for (int round = 0; round < 12; ++round) {
      ground(world, 128);
      world.add(1, f.player, {64, 64});
      world.set_player_id(1);
      populate(world, f, 1500, 1500, 500, 45);
      for (int i = 0; i < 4000; ++i) shoot(combat, f.single, i, {64, 64}, static_cast<float>(i), 1, 0, 1);
      for (int t = 1; t <= 6; ++t) (void)combat.update(t * 20);
      combat.reset(round);
      plateau.push_back(heap.live);
    }
    ground(world, 128);
    world.add(1, f.player, {64, 64});
    world.set_player_id(1);
    plateau.push_back(heap.live);
    // After the crowd leaves the retained storage must be proportionate to what is resident again.
    if (std::getenv("STORAGE_VERBOSE")) {
      std::cerr << "baseline " << baseline << " final " << heap.live << "\n";
      for (auto v : plateau) std::cerr << "  plateau " << v << "\n";
    }
    check(heap.live < baseline + (256 << 10), "empty world after teardown returns the crowd's memory");
  }
  counting = false;
  for (std::size_t i = 2; i < plateau.size(); ++i)
    check(plateau[i] <= plateau[1] + (64 << 10), "repeated populated teardown plateaus");
}
// ---- containers checked against equivalent standard-library structures ----
void index_checks() {
  using realm::EntityIndex;
  for (const int mode : {0, 1, 2, 3}) {
    EntityIndex index;
    std::unordered_map<std::int32_t, std::uint32_t> oracle;
    std::mt19937 rng(100 + static_cast<unsigned>(mode));
    auto id_of = [&](std::uint32_t n) -> std::int32_t {
      switch (mode) {
        case 0:
          return static_cast<std::int32_t>(n % 6000);  // dense sequential
        case 1:
          return static_cast<std::int32_t>((n % 6000) * 256U);  // power-of-two stride
        case 2:
          return static_cast<std::int32_t>(0x7f000000U + n % 6000);  // local fake-id range
        default:
          return static_cast<std::int32_t>(n * 2654435761U % 200000) - 100000;  // negative and scattered
      }
    };
    for (int step = 0; step < 60000; ++step) {
      const auto id = id_of(static_cast<std::uint32_t>(rng()));
      switch (rng() % 8) {
        case 0:
        case 1:
        case 2: {
          const auto v = static_cast<std::uint32_t>(rng() % 100000);
          index.assign(id, v);
          oracle[id] = v;
          break;
        }
        case 3:
        case 4: {
          index.erase(id);
          oracle.erase(id);
          break;
        }
        default: {
          const auto found = index.find(id);
          const auto it = oracle.find(id);
          check(found == (it == oracle.end() ? EntityIndex::none : it->second), "index find matches the hash map");
        }
      }
      check(index.size() == oracle.size(), "index size matches");
    }
    for (const auto& [id, v] : oracle) check(index.find(id) == v, "every surviving id resolves");
    const auto grown = index.bytes();
    for (const auto& [id, v] : std::unordered_map(oracle)) {
      (void)v;
      index.erase(id);
      oracle.erase(id);
    }
    check(index.size() == 0 && index.capacity() <= 1024 * 4 && index.bytes() <= grown,
          "table shrinks after a crowd leaves");
    for (std::int32_t id = -5; id < 5000; ++id)
      check(index.find(id) == EntityIndex::none, "erased ids are gone (no tombstone ghosts)");
    index.assign(5, 1);
    index.clear();
    check(index.find(5) == EntityIndex::none && index.size() == 0, "clear forgets everything");
  }
  // A hostile run of colliding hash slots must still terminate and delete correctly.
  EntityIndex tight;
  for (std::uint32_t i = 0; i < 4000; ++i) tight.assign(static_cast<std::int32_t>(i * 65536U), i);
  for (std::uint32_t i = 0; i < 4000; i += 2) tight.erase(static_cast<std::int32_t>(i * 65536U));
  for (std::uint32_t i = 0; i < 4000; ++i)
    check(tight.find(static_cast<std::int32_t>(i * 65536U)) == ((i & 1U) ? i : EntityIndex::none),
          "strided ids survive deletes of their neighbours");
}
void stat_checks() {
  realm::StatMap stats;
  std::unordered_map<std::uint8_t, std::int32_t> oracle;
  std::mt19937 rng(9);
  for (int step = 0; step < 40000; ++step) {
    const auto key = static_cast<std::uint8_t>(rng() % 90);
    switch (rng() % 6) {
      case 0:
      case 1: {
        const auto v = static_cast<std::int32_t>(rng());
        stats[key] = v;
        oracle[key] = v;
        break;
      }
      case 2:
        check(stats.erase(key) == oracle.erase(key), "erase count matches");
        break;
      case 3: {
        const auto it = stats.find(key);
        const auto ref = oracle.find(key);
        check((it == stats.end()) == (ref == oracle.end()) && (it == stats.end() || it->second == ref->second),
              "find matches");
        check(stats.count(key) == oracle.count(key) && stats.contains(key) == (ref != oracle.end()),
              "count/contains match");
        break;
      }
      case 4: {
        bool threw{};
        try {
          (void)stats.at(key);
        } catch (const std::out_of_range&) {
          threw = true;
        }
        check(threw == !oracle.contains(key), "at throws exactly when absent");
        break;
      }
      default:
        check(stats.size() == oracle.size() && stats.empty() == oracle.empty(), "size matches");
    }
  }
  check(std::is_sorted(stats.begin(), stats.end(), [](const auto& a, const auto& b) { return a.first < b.first; }),
        "entries stay sorted");
  std::size_t seen{};
  for (const auto& [key, value] : stats) {
    check(oracle.at(key) == value, "iteration yields the same pairs");
    ++seen;
  }
  check(seen == oracle.size(), "iteration visits every stat once");
  check(sizeof(realm::StatMap::value_type) == 8, "eight bytes per stat");
  // value_or: hit, miss with fallback, enum-key overload, and a const read never inserts.
  enum class Probe : std::uint8_t { present = 7, absent = 9 };
  realm::StatMap probe;
  probe[7] = 42;
  probe[8] = -5;
  check(probe.value_or(7, -1) == 42, "value_or reads a present stat");
  check(probe.value_or(std::uint8_t{8}, 0) == -5, "value_or keeps negative values");
  check(probe.value_or(9, -1) == -1, "value_or returns the fallback when absent");
  check(probe.value_or(Probe::present, -1) == 42, "enum overload reads the same byte");
  check(probe.value_or(Probe::absent, 5) == 5, "enum overload returns the fallback when absent");
  check(probe.size() == 2 && !probe.contains(9), "value_or does not insert");
}
void hit_checks() {
  realm::HitSet hits;
  std::unordered_set<std::int32_t> oracle;
  std::mt19937 rng(3);
  for (int i = 0; i < 5000; ++i) {
    const auto id = static_cast<std::int32_t>(rng() % 700) - 100;
    hits.insert(id);
    oracle.insert(id);
    check(hits.contains(id), "inserted id is contained");
  }
  for (std::int32_t id = -150; id < 800; ++id)
    check(hits.contains(id) == oracle.contains(id), "membership matches the hash set");
  check(hits.size() == oracle.size() && std::is_sorted(hits.begin(), hits.end()), "unique, sorted");
}
// Removing an object destroys every effect that follows it and nothing else, whichever of the three filters is
// consulted.
void effect_removal_checks(const realm::Catalog& catalog, const Defs& f) {
  using namespace realm;
  World world(catalog);
  ground(world, 64);
  auto& particles = world.particles();
  std::mt19937 rng(21);
  world.add(1, f.player, {32, 32});
  world.set_player_id(1);
  std::vector<const ObjectDef*> emitting;
  for (const auto& d : catalog.objects())
    if (!d.texture.effect.kind.empty()) emitting.push_back(&d);
  check(!emitting.empty(), "catalog has objects with source emitters");
  std::vector<std::int32_t> live;
  std::int32_t next = 100;
  for (int step = 0; step < 3000; ++step) {
    switch (rng() % 6) {
      case 0:
      case 1: {
        const auto id = next++;
        world.add(id, (rng() % 3 == 0) ? emitting[rng() % emitting.size()]->type : f.enemy,
                  {20. + static_cast<double>(rng() % 20), 20. + static_cast<double>(rng() % 20)});
        live.push_back(id);
        break;
      }
      case 2:
        if (!live.empty()) {
          net::incoming::ShowEffect e;
          e.type = (rng() & 1U) ? 9 : 15;
          e.target_id = live[rng() % live.size()];
          e.position = e.type == 15 ? net::Position{.3F, 3.F} : net::Position{32.F, 32.F};
          e.color = 0xff00ff;
          particles.show(world, e, static_cast<std::int32_t>(step));
          break;
        }
        [[fallthrough]];
      case 3:
        if (!live.empty()) particles.level_up(live[rng() % live.size()], step);
        break;
      case 4:
        if (!live.empty()) {
          auto* e = world.entity(live[rng() % live.size()]);
          if (e) {
            e->combat.condition = (rng() & 1U) << 15U;
            particles.healing(*e);
          }
          break;
        }
        [[fallthrough]];
      default:
        if (!live.empty() && rng() % 2) {
          const auto at = rng() % live.size();
          const auto id = live[at];
          std::size_t others{};
          for (const auto& p : particles.particles()) others += p.target != id;
          std::size_t emitters_before = particles.emitter_count();
          (void)emitters_before;
          world.remove(id);
          live[at] = live.back();
          live.pop_back();
          check(!particles.targets(id), "no particle, emitter or flash still follows a removed object");
          check(particles.particles().size() == others, "effects following other objects survive the removal");
        }
    }
    if (step % 7 == 0) particles.update(world, step * 17);
  }
  // Removal straight after the effect appears, before any update could rebuild the filters (fresh map: no stale filter
  // bits).
  ground(world, 64);
  world.add(1, f.player, {32, 32});
  world.set_player_id(1);
  for (const auto* def : emitting) {
    world.add(7000, def->type, {30, 30});
    check(particles.targets(7000), "object with a source emitter has one");
    world.remove(7000);
    check(!particles.targets(7000), "emitter removed with its object before any update");
  }
  world.add(7001, f.enemy, {30, 30});
  world.entity(7001)->combat.condition = 1U << 15U;
  particles.healing(*world.entity(7001));
  check(particles.targets(7001), "healing emitter exists");
  world.remove(7001);
  check(!particles.targets(7001), "healing emitter removed with its object");
  world.add(7002, f.enemy, {30, 30});
  {
    net::incoming::ShowEffect e;
    e.type = 15;
    e.target_id = 7002;
    e.position = {.3F, 3.F};
    particles.show(world, e, 0);
  }
  check(particles.targets(7002), "flash exists");
  world.remove(7002);
  check(!particles.targets(7002), "flash removed with its object");
  // Re-adding an id while it still has effects replaces them (add() removes the old object first).
  world.add(9000, f.enemy, {10, 10});
  particles.level_up(9000, 0);
  world.add(9000, f.enemy, {11, 11});
  check(!particles.targets(9000), "re-adding an id drops the old object's effects");
}
// ---- profile ----
Window measure(auto&& body) {
  const auto a = heap.allocations, fr = heap.frees;
  const auto t0 = std::chrono::steady_clock::now();
  body();
  return {heap.allocations - a, heap.frees - fr, 0,
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count()};
}
void profile(const realm::Catalog& catalog, const Defs& f) {
  std::cout << "sizeof Entity=" << sizeof(realm::Entity) << " FlyingProjectile=" << sizeof(realm::FlyingProjectile)
            << " Particle=" << sizeof(realm::Particle) << " TileState=" << sizeof(realm::TileState) << '\n';
  struct Level {
    const char* name;
    int players, enemies, models, projectiles;
  };
  const Level levels[]{{"S2", 1000, 1000, 1000, 10000}, {"S3", 3000, 3000, 3000, 20000}};
  for (const auto& level : levels) {
    counting = true;
    const auto before = heap.live;
    World world(catalog);
    Combat combat(world);
    ground(world, 256);
    world.add(1, f.player, {128, 128});
    world.set_player_id(1);
    const auto empty = heap.live;
    heap.peak = heap.live;
    populate(world, f, level.players, level.enemies, level.models, 60);
    const auto actors = heap.live - empty;
    const auto residents = static_cast<double>(level.players + level.enemies + level.models);
    std::int32_t bullet = 0, time = 0;
    // Replenish to the target like the stress driver does (batches of 128).
    auto replenish = [&](std::int32_t at) {
      std::size_t need = static_cast<std::size_t>(level.projectiles) > combat.projectiles().size()
                             ? static_cast<std::size_t>(level.projectiles) - combat.projectiles().size()
                             : 0;
      for (std::size_t i = 0; i < need; i += 128) {
        const int n = static_cast<int>(std::min<std::size_t>(128, need - i));
        shoot(combat, (bullet / 128) % 2 ? f.multi : f.single, bullet, {128 - 2., 128 + ((i / 128) % 2 ? 1.2 : 0)},
              static_cast<float>((bullet / 128) % 8 * .3), n, at, 1);
        bullet += n;
      }
    };
    const auto proj_before = heap.live;
    replenish(0);
    const auto projectiles = heap.live - proj_before;
    std::vector<Window> windows;
    for (int t = 0; t < 30; ++t) {
      time += 20;
      windows.push_back(measure([&] {
        (void)combat.update(time);
        world.update(20);
        replenish(time);
      }));
    }
    std::sort(windows.begin(), windows.end(), [](const auto& a, const auto& b) { return a.ms < b.ms; });
    const auto& median = windows[windows.size() / 2];
    std::size_t total_allocs = 0;
    for (const auto& w : windows) total_allocs += w.allocations;
    std::cout << level.name << ": residents " << residents << " projectiles " << combat.projectiles().size()
              << " | empty world " << empty - before << " B, actors " << actors << " B ("
              << static_cast<double>(actors) / residents << " B/object), projectiles " << projectiles << " B ("
              << static_cast<double>(projectiles) / level.projectiles << " B/projectile)"
              << " | live " << heap.live - before << " peak " << heap.peak - before << " | steady tick: median "
              << median.ms << " ms, allocs/tick "
              << static_cast<double>(total_allocs) / static_cast<double>(windows.size()) << "\n";
    counting = false;
  }
  // Teardown cost and plateau, from `bounded_checks`' shape.
  counting = true;
  World world(catalog);
  Combat combat(world);
  const auto base = heap.live;
  for (int round = 0; round < 5; ++round) {
    ground(world, 256);
    world.add(1, f.player, {128, 128});
    world.set_player_id(1);
    const auto t = measure([&] { populate(world, f, 3000, 3000, 3000, 60); });
    std::cout << "populate 9000: " << t.ms << " ms, " << t.allocations << " allocations; live " << heap.live - base
              << "\n";
    const auto r = measure([&] { ground(world, 256); });
    std::cout << "teardown (initialize): " << r.ms << " ms, " << r.frees << " frees; live after " << heap.live - base
              << "\n";
  }
  counting = false;
}
}
int main(int argc, char** argv) {
  try {
    realm::Catalog catalog(realm::flash::source_root);
    const auto f = find_defs(catalog);
    check(f.player && f.enemy && f.model && f.single && f.multi && f.merchant, "fixture definitions exist");
    if (argc > 1 && !std::strcmp(argv[1], "--profile")) {
      profile(catalog, f);
      return 0;
    }
    index_checks();
    stat_checks();
    hit_checks();
    lifetime_checks(catalog, f);
    effect_removal_checks(catalog, f);
    bounded_checks(catalog, f);
    std::cout << checks << " world storage and lifetime checks passed\n";
  } catch (const std::exception& e) {
    counting = false;
    std::cerr << e.what() << '\n';
    return 1;
  }
}
