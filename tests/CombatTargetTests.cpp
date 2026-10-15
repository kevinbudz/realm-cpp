// Differential checks: the grid candidate search must pick exactly the target the original full
// entity scan picked, including exact-distance ties, cell/map boundaries, removals that reorder the
// dense list, and multi-hit history.
#include "realm/objects/Combat.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>

namespace {
int checks{};

void check(bool ok, const char* why) {
  ++checks;
  if (!ok) {
    throw std::runtime_error(why);
  }
}

using realm::Combat;
using realm::TargetSearch;
using realm::World;

struct Fixture {
  const realm::Catalog& catalog;
  std::uint16_t enemy{}, other{}, single{}, multi{};
};

struct Rig {
  World world;
  Combat combat;

  Rig(const realm::Catalog& c, TargetSearch search, int size) : world(c), combat(world) {
    world.initialize(size, size);
    const auto grass = c.ground("Grass")->type;
    for (int y = 0; y < size; ++y) {
      for (int x = 0; x < size; ++x) {
        world.tile(x, y)->type = grass;
      }
    }
    combat.set_target_search(search);
    world.add(99, c.object("Wizard")->type, {size / 2., size / 2.});
    world.set_player_id(99);
  }
};

std::uint8_t enemy_hit_id() {
  return realm::net::outgoing::encode(realm::net::outgoing::EnemyHit{}).id;
}

std::uint64_t mix(std::uint64_t h, std::uint64_t v) {
  h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
  return h * 0x100000001b3ULL;
}

// Everything observable from one update: wire bytes in order, surviving projectiles with hit
// history, entity order, cues.
std::uint64_t digest(Rig& rig, const std::vector<realm::net::Packet>& packets) {
  std::uint64_t h = 1469598103934665603ULL;
  for (const auto& p : packets) {
    h = mix(h, static_cast<std::uint64_t>(p.id));
    for (const auto b : p.body) {
      h = mix(h, b);
    }
  }
  for (const auto& p : rig.combat.projectiles()) {
    h = mix(h, static_cast<std::uint64_t>(p.entity_id));
    h = mix(h, static_cast<std::uint64_t>(p.bullet_id));
    std::vector<std::int32_t> hit(p.hit.begin(), p.hit.end());
    std::sort(hit.begin(), hit.end());
    for (const auto id : hit) {
      h = mix(h, static_cast<std::uint64_t>(id));
    }
  }
  for (const auto& e : rig.world.entities()) {
    h = mix(h, static_cast<std::uint64_t>(e.id));
  }
  return mix(h, rig.world.damage_cues().size());
}

void shoot(Rig& rig, std::uint16_t container, std::int32_t bullet, realm::Vec2 at, float angle,
           int count, std::int32_t time) {
  realm::net::incoming::ServerPlayerShoot s;
  s.bullet_id = bullet;
  s.owner_id = 99;
  s.container_type = static_cast<std::int16_t>(container);
  s.position = {static_cast<float>(at.x), static_cast<float>(at.y)};
  s.angle = angle;
  s.angle_increment = .07F;
  s.damages.assign(static_cast<std::size_t>(count), 10);
  (void)rig.combat.receive({realm::flash::PacketId::SERVERPLAYERSHOOT, s}, time);
}

// Scenario knobs: positions are quantised to `lattice` so many exact ties and boundary values
// occur.
struct Scenario {
  int size, enemies, others, rounds, shots;
  double lattice, span;
  bool outliers;
};

std::vector<std::uint64_t> run(const Fixture& f, TargetSearch search, const Scenario& sc,
                               std::uint32_t seed, std::uint64_t* examined, std::uint64_t* hits) {
  Rig rig(f.catalog, search, sc.size);
  std::mt19937 rng(seed);
  std::vector<std::uint64_t> trace;
  auto coordinate = [&](double centre) {
    std::uniform_real_distribution<double> d(centre - sc.span, centre + sc.span);
    const double v = d(rng);
    return sc.lattice > 0 ? std::round(v / sc.lattice) * sc.lattice : v;
  };
  const double centre = sc.size / 2.;
  std::int32_t next_id = 1000;
  std::vector<std::int32_t> enemy_ids;
  for (int i = 0; i < sc.enemies; ++i) {
    auto& e = rig.world.add(next_id, f.enemy, {coordinate(centre), coordinate(centre)});
    enemy_ids.push_back(e.id);
    ++next_id;
  }
  for (int i = 0; i < sc.others; ++i) {
    rig.world.add(next_id++, f.other, {coordinate(centre), coordinate(centre)});
  }
  if (sc.outliers) {
    // Positions the grid must clamp or ignore. Set after add so tile assignment never sees them.
    const std::pair<double, double> odd[]{{-.3, 5.},
                                          {sc.size + .2, 5.},
                                          {5., -.4},
                                          {5., sc.size + .4},
                                          {1e6, 1e6},
                                          {-1e5, 3.},
                                          {std::nan(""), 4.},
                                          {std::numeric_limits<double>::infinity(), 4.},
                                          {.5, .5},
                                          {sc.size - .5, sc.size - .5},
                                          {0, 0}};
    for (const auto& [x, y] : odd) {
      auto& e = rig.world.add(next_id, f.enemy, {4, 4});
      e.motion.position = {x, y};
      enemy_ids.push_back(next_id++);
    }
  }
  std::int32_t time = 0, bullet = 1;
  for (int round = 0; round < sc.rounds; ++round) {
    time += 40;
    for (int s = 0; s < sc.shots; ++s) {
      const auto container = (rng() & 1U) ? f.multi : f.single;
      shoot(rig, container, bullet, {coordinate(centre), coordinate(centre)},
            static_cast<float>(std::uniform_real_distribution<double>(0, 6.28)(rng)),
            1 + static_cast<int>(rng() % 3), time);
      bullet += 4;
    }
    // Churn between updates: move, condition, remove, add (reorders the dense list through
    // swap-removal).
    for (int k = 0; k < 3 && !enemy_ids.empty(); ++k) {
      const auto id = enemy_ids[rng() % enemy_ids.size()];
      auto* e = rig.world.entity(id);
      if (!e) {
        continue;
      }
      switch (rng() % 5) {
        case 0:
          e->motion.position = {coordinate(centre), coordinate(centre)};
          break;
        case 1:
          e->combat.condition = (rng() & 1U) ? (1U << 20U) : (1U << 18U);
          break;
        case 2:
          e->combat.condition = 0;
          break;
        case 3:
          rig.world.remove(id);
          break;
        default:
          rig.world.add(next_id, f.enemy, {coordinate(centre), coordinate(centre)});
          enemy_ids.push_back(next_id++);
          break;
      }
    }
    const auto before = rig.combat.target_stats().examined;
    const auto packets = rig.combat.update(time);
    (void)before;
    trace.push_back(digest(rig, packets));
    for (const auto& p : packets) {
      if (p.id == enemy_hit_id() && hits) {
        ++*hits;
      }
    }
  }
  if (examined) {
    *examined = rig.combat.target_stats().examined;
  }
  return trace;
}

void compare(const Fixture& f, const char* name, const Scenario& sc, std::uint32_t seeds,
             bool expect_fewer) {
  for (std::uint32_t seed = 1; seed <= seeds; ++seed) {
    std::uint64_t grid_examined{}, scan_examined{}, grid_hits{}, scan_hits{};
    const auto grid = run(f, TargetSearch::grid, sc, seed, &grid_examined, &grid_hits);
    const auto scan = run(f, TargetSearch::scan, sc, seed, &scan_examined, &scan_hits);
    check(grid == scan, name);
    check(grid_hits == scan_hits, "hit counts match");
    if (seed == 1 && expect_fewer) {
      check(grid_examined * 4 < scan_examined, "grid examines far fewer entities than the scan");
    }
    if (seed == 1) {
      check(scan_hits > 0, "scenario produces hit packets");
    }
  }
}

// `--profile`: candidate counts and update cost for spread, dense, boundary and pathological
// populations, grid versus scan.
struct Profile {
  const char* name;
  int size, enemies, projectiles;
  double lattice, span, centre;
};

void profile(const Fixture& f) {
  const Profile cases[]{
      {"spread: 3000 enemies / 20000 shots over 256x256", 256, 3000, 20000, 0, 127, 128},
      {"dense: 3000 enemies / 20000 shots over 32x32", 64, 3000, 20000, 0, 16, 32},
      {"boundary: integer and half-tile lattice, 64x64", 64, 3000, 20000, .5, 31, 32},
      {"pathological: 3000 enemies in one 0.9 tile box", 64, 3000, 20000, 0, .45, 32},
  };
  std::cout << "case | search | median update ms | queries | examined/query | hit packets\n";
  for (const auto& c : cases) {
    for (const auto search : {TargetSearch::scan, TargetSearch::grid}) {
      Rig rig(f.catalog, search, c.size);
      std::mt19937 rng(5);
      std::uniform_real_distribution<double> d(c.centre - c.span, c.centre + c.span);
      auto q = [&](double v) { return c.lattice > 0 ? std::round(v / c.lattice) * c.lattice : v; };
      for (int i = 0; i < c.enemies; ++i) {
        rig.world.add(1000 + i, f.enemy, {q(d(rng)), q(d(rng))});
      }
      for (int i = 0; i < c.projectiles; ++i) {
        shoot(rig, f.multi, i * 4, {q(d(rng)), q(d(rng))}, static_cast<float>(d(rng)), 1, 0);
      }
      std::vector<double> times;
      std::uint64_t hits = 0;
      for (int u = 0; u < 5; ++u) {
        const auto t0 = std::chrono::steady_clock::now();
        const auto packets = rig.combat.update(u * 10);
        times.push_back(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
                .count());
        for (const auto& p : packets) {
          hits += p.id == enemy_hit_id();
        }
      }
      std::sort(times.begin(), times.end());
      const auto& st = rig.combat.target_stats();
      std::cout << c.name << " | " << (search == TargetSearch::grid ? "grid" : "scan") << " | "
                << times[2] << " | " << st.queries << " | "
                << static_cast<double>(st.examined) / static_cast<double>(st.queries) << " | "
                << hits << '\n';
    }
  }
}
}

int main(int argc, char** argv) {
  try {
    realm::Catalog catalog(realm::flash::source_root);
    Fixture f{catalog};
    for (const auto& object : catalog.objects()) {
      if (object.enemy && !object.stationary && !f.enemy) {
        f.enemy = object.type;
      }
      if (!object.enemy && !object.stationary && !object.player && !f.other &&
          object.type != catalog.object("Wizard")->type) {
        f.other = object.type;
      }
      if (object.projectiles.empty()) {
        continue;
      }
      const auto& p = object.projectiles[0];
      if (p.id != 0 || !catalog.object(p.object_id) || p.motion.wavy || p.motion.parametric ||
          p.motion.boomerang || p.motion.amplitude || p.motion.accelerate || p.motion.decelerate ||
          p.motion.speed <= 0 || p.motion.lifetime_ms < 400) {
        continue;
      }
      if (p.multi_hit && !f.multi) {
        f.multi = object.type;
      }
      if (!p.multi_hit && !f.single) {
        f.single = object.type;
      }
    }
    check(f.enemy && f.other && f.single && f.multi,
          "enemy, bystander, single-hit and multi-hit fixtures exist");
    if (argc > 1 && !std::strcmp(argv[1], "--profile")) {
      profile(f);
      return 0;
    }

    {  // Hand-built exact ties: the earlier entity in the dense list wins, also after swap-removal
       // reorders it.
      for (const auto search : {TargetSearch::grid, TargetSearch::scan}) {
        Rig rig(catalog, search, 16);
        rig.world.add(10, f.enemy, {5.25, 5});
        rig.world.add(11, f.enemy, {4.75, 5});
        rig.world.add(12, f.enemy, {5, 5.25});
        shoot(rig, f.single, 1, {5, 5}, 0, 1, 0);
        auto packets = rig.combat.update(0);
        check(packets.size() == 1 && packets[0].id == enemy_hit_id(), "tie produces one enemy hit");
        realm::net::Reader first(packets[0].body);
        (void)first.i32();
        (void)first.i32();
        check(first.i32() == 10, "tie goes to the first entity in dense order");
        rig.world.remove(10);  // entity 12 is swapped into slot 1+: now order is 12, 11
        shoot(rig, f.single, 5, {5, 5}, 0, 1, 100);
        packets = rig.combat.update(100);
        check(packets.size() == 1, "tie after reorder produces one hit");
        realm::net::Reader second(packets[0].body);
        (void)second.i32();
        (void)second.i32();
        const auto expected = rig.world.entity(12) < rig.world.entity(11) ? 12 : 11;
        check(second.i32() == expected, "tie follows reordered dense list");
      }
    }
    compare(f, "spread population matches the scan", {64, 300, 60, 40, 25, 0, 26, false}, 3, true);
    compare(f, "lattice ties and cell boundaries match", {32, 250, 50, 40, 20, .25, 3, false}, 5,
            false);
    compare(f, "dense cluster with churn matches", {24, 400, 80, 30, 40, .125, 1.5, false}, 3,
            false);
    compare(f, "outliers, NaN and map edges match", {24, 120, 30, 40, 20, .25, 11, true}, 5, false);
    compare(f, "multi-hit crossings match", {20, 200, 40, 50, 30, .5, 8, false}, 3, false);
    {  // No enemies at all, and a populated map reset: nothing to hit, nothing stale.
      Rig rig(catalog, TargetSearch::grid, 16);
      shoot(rig, f.single, 1, {5, 5}, 0, 3, 0);
      check(rig.combat.update(0).empty(), "no enemies, no hits");
      rig.world.add(20, f.enemy, {5, 5});
      shoot(rig, f.single, 9, {5, 5}, 0, 1, 50);
      check(
          rig.combat.update(50).size() == 4,
          "newly added enemy is found by all four live projectiles without any invalidation call");
      rig.combat.reset(1);
      rig.world.initialize(16, 16);
      check(rig.combat.update(100).empty(), "map reset leaves no candidates");
    }
    std::cout << checks << " collision search differential checks passed\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
