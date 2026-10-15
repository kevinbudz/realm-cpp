// Differential checks: World::interactive_target and world_dyes (the per-frame nearby-object
// queries) must agree with the original full-scan implementations, kept here verbatim as oracles,
// including exact distance ties, the strict 1-tile boundary, equal-tile merchants, entity order,
// and non-finite positions written behind the world's back. The per-frame entity sets (moving,
// enemies, minimap markers, players, merchants, dynamic/static grid) must visit exactly what the
// original full passes visited, in the same order; World::update is compared with the original
// whole-list pass.
#include "realm/assets/Appearances.hpp"
#include "realm/minimap/Minimap.hpp"
#include "realm/objects/Party.hpp"
#include <cstring>
#include <map>
#include "flash_assets.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
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

const realm::Entity* reference_interactive(const realm::World& world) {
  const auto* player = world.entity(world.player_id());
  if (!player) {
    return nullptr;
  }
  const realm::Entity* closest{};
  double best = 1;
  for (const auto& e : world.entities()) {
    const auto* def = world.catalog().object(e.type);
    if (!def || !def->interactive) {
      continue;
    }
    const double distance = std::hypot(e.motion.position.x - player->motion.position.x,
                                       e.motion.position.y - player->motion.position.y);
    if (distance < best) {
      best = distance;
      closest = &e;
    }
  }
  return closest;
}

std::array<std::uint32_t, 2> reference_dyes(const realm::World& world,
                                            const realm::Entity& entity) {
  std::array<std::uint32_t, 2> result{entity.appearance.tex1, entity.appearance.tex2};
  if (!entity.player || entity.id != world.player_id()) {
    return result;
  }
  const int x = static_cast<int>(entity.motion.position.x),
            y = static_cast<int>(entity.motion.position.y);
  const int dx = entity.motion.position.x - x > .5 ? 1 : -1,
            dy = entity.motion.position.y - y > .5 ? 1 : -1;
  constexpr int nearby[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
  for (const auto& offset : nearby) {
    for (const auto& e : world.entities()) {
      const auto* d = world.catalog().object(e.type);
      if (!d || d->class_name != "Merchant" ||
          static_cast<int>(e.motion.position.x) != x + dx * offset[0] ||
          static_cast<int>(e.motion.position.y) != y + dy * offset[1]) {
        continue;
      }
      const double ex = e.motion.position.x - entity.motion.position.x,
                   ey = e.motion.position.y - entity.motion.position.y;
      if (ex * ex + ey * ey >= 1) {
        return result;
      }
      const auto it = e.profile.stats.find(
          static_cast<std::uint8_t>(realm::flash::StatId::MERCHANDISE_TYPE_STAT));
      const auto* item = it == e.profile.stats.end()
                             ? nullptr
                             : world.catalog().object(static_cast<std::uint16_t>(it->second));
      if (item && item->xml.child("Activate").text() == "Dye") {
        if (item->xml.has("Tex1")) {
          result[0] = static_cast<std::uint32_t>(item->xml.number("Tex1"));
        }
        if (item->xml.has("Tex2")) {
          result[1] = static_cast<std::uint32_t>(item->xml.number("Tex2"));
        }
      }
      return result;
    }
  }
  return result;
}

// The original World::update: every entity, dense order, skipping the focus player and the
// stationary.
void reference_update(realm::World& world, double dt_ms) {
  for (auto& e : world.entities()) {
    if (e.id == world.player_id()) {
      continue;
    }
    if (e.motion.velocity.x == 0 && e.motion.velocity.y == 0) {
      continue;
    }
    realm::Vec2 next{e.motion.position.x + e.motion.velocity.x * dt_ms,
                     e.motion.position.y + e.motion.velocity.y * dt_ms};
    if ((e.motion.velocity.x > 0 && next.x > e.motion.target.x) ||
        (e.motion.velocity.x < 0 && next.x < e.motion.target.x)) {
      next.x = e.motion.target.x;
      e.motion.velocity.x = 0;
    }
    if ((e.motion.velocity.y > 0 && next.y > e.motion.target.y) ||
        (e.motion.velocity.y < 0 && next.y < e.motion.target.y)) {
      next.y = e.motion.target.y;
      e.motion.velocity.y = 0;
    }
    if (auto* t = world.tile(static_cast<int>(std::floor(e.motion.position.x)),
                             static_cast<int>(std::floor(e.motion.position.y)));
        t && t->object_id == e.id) {
      t->object_id = -1;
    }
    e.motion.position = next;
    if (const auto* d = world.catalog().object(e.type); d && d->stationary) {
      if (auto* t = world.tile(static_cast<int>(std::floor(e.motion.position.x)),
                               static_cast<int>(std::floor(e.motion.position.y)))) {
        t->object_id = e.id;
      }
    }
  }
}

// The original minimap marker pass, Combat target gather and hover/party scans, verbatim over the
// whole dense list.
struct Marker {
  realm::Vec2 at;
  std::uint32_t color;
};

template <class Each>
std::vector<Marker> markers(const realm::World& world, const realm::Entity& focus, Each&& each) {
  std::vector<Marker> out;
  each([&](const realm::Entity& entity) {
    const auto* def = world.catalog().object(entity.type);
    if (!def || def->no_minimap || entity.id == focus.id || entity.projectile) {
      return;
    }
    std::uint32_t color{};
    if (entity.player) {
      color = !focus.profile.guild.empty() && focus.profile.guild == entity.profile.guild
                  ? 65280U
                  : 16776960U;
    } else if (def->class_name == "Character") {
      color = def->enemy ? 16711680U : 77U;
    } else if (def->class_name == "Portal" || def->class_name == "GuildHallPortal") {
      color = 255;
    } else {
      return;
    }
    out.push_back({entity.motion.position, color});
  });
  return out;
}

template <class Each>
std::vector<std::int32_t> gather_targets(const realm::World& world, Each&& each) {
  std::vector<std::int32_t> out;
  each([&](const realm::Entity& e) {
    if (e.projectile || !std::isfinite(e.motion.position.x) ||
        !std::isfinite(e.motion.position.y)) {
      return;
    }
    const auto* type = world.catalog().object(e.type);
    if (!type || !type->enemy) {
      return;
    }
    out.push_back(e.id);
  });
  return out;
}

std::array<std::int32_t, realm::Party::members_shown> reference_party(const realm::World& world) {
  std::array<std::int32_t, realm::Party::members_shown> members;
  members.fill(-1);
  const auto* player = world.entity(world.player_id());
  if (!player) {
    return members;
  }

  struct Candidate {
    double distance{};
    std::int32_t id{};
  };

  std::vector<Candidate> candidates;
  for (const auto& other : world.entities()) {
    if (!other.player || other.id == player->id) {
      continue;
    }
    const double dx = player->motion.position.x - other.motion.position.x,
                 dy = player->motion.position.y - other.motion.position.y,
                 distance = dx * dx + dy * dy;
    if (!(distance > 50 * 50)) {
      candidates.push_back({distance, other.id});
    }
  }
  std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
    return a.distance != b.distance ? a.distance < b.distance : a.id < b.id;
  });
  for (std::size_t i = 0; i < members.size() && i < candidates.size(); ++i) {
    members[i] = candidates[i].id;
  }
  return members;
}

std::vector<std::int32_t> reference_hover(const realm::World& world, const realm::Minimap& minimap,
                                          const realm::Entity& focus, realm::Vec2 pointer) {
  std::vector<std::int32_t> result;
  if (pointer.x < 0 || pointer.y < 0 || pointer.x >= 192 || pointer.y >= 192) {
    return result;
  }
  for (const auto& entity : world.entities()) {
    const auto* def = world.catalog().object(entity.type);
    if (!entity.player || entity.id == focus.id || !def || def->no_minimap) {
      continue;
    }
    const auto p = minimap.marker_position(world, focus, entity.motion.position);
    const double dx = pointer.x - p.x, dy = pointer.y - p.y;
    if (dx * dx + dy * dy < 25) {
      result.push_back(entity.id);
    }
  }
  return result;
}

bool same_bits(double a, double b) {
  return std::memcmp(&a, &b, sizeof a) == 0;
}

bool same_state(const realm::World& a, const realm::World& b) {
  if (a.entities().size() != b.entities().size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.entities().size(); ++i) {
    const auto& x = a.entities()[i];
    const auto& y = b.entities()[i];
    if (x.id != y.id || !same_bits(x.motion.position.x, y.motion.position.x) ||
        !same_bits(x.motion.position.y, y.motion.position.y) ||
        !same_bits(x.motion.velocity.x, y.motion.velocity.x) ||
        !same_bits(x.motion.velocity.y, y.motion.velocity.y) ||
        !same_bits(x.motion.target.x, y.motion.target.x) ||
        !same_bits(x.motion.target.y, y.motion.target.y)) {
      return false;
    }
  }
  for (int y = 0; y < a.height(); ++y) {
    for (int x = 0; x < a.width(); ++x) {
      if (a.tile(x, y)->object_id != b.tile(x, y)->object_id) {
        return false;
      }
    }
  }
  return true;
}

// Every set must hold exactly the entities its predicate selects, in dense order; the grid must
// hold exactly the static ones, each findable at its own position, and a rectangle query must visit
// a superset of the static entities inside, once each.
std::uint64_t verify_sets(const realm::World& world, std::mt19937& rng) {
  const auto& all = world.entities();
  const auto& catalog = world.catalog();
  const auto expect = [&](auto&& keep) {
    std::vector<const realm::Entity*> v;
    for (const auto& e : all) {
      if (keep(e, catalog.object(e.type))) {
        v.push_back(&e);
      }
    }
    return v;
  };
  const auto visited = [&](auto&& each) {
    std::vector<const realm::Entity*> v;
    each([&](const realm::Entity& e) { v.push_back(&e); });
    return v;
  };
  check(visited([&](auto f) { world.each_enemy(f); }) ==
            expect([](const realm::Entity&, const realm::ObjectDef* d) { return d && d->enemy; }),
        "enemy set differs from the scan");
  check(visited([&](auto f) { world.each_player(f); }) ==
            expect([](const realm::Entity& e, const realm::ObjectDef*) { return e.player; }),
        "player set differs from the scan");
  check(visited([&](auto f) { world.each_merchant(f); }) ==
            expect([](const realm::Entity&, const realm::ObjectDef* d) {
              return d && d->class_name == "Merchant";
            }),
        "merchant set differs from the scan");
  check(visited([&](auto f) { world.each_marker_candidate(f); }) ==
            expect([](const realm::Entity& e, const realm::ObjectDef* d) {
              return d && !d->no_minimap &&
                     (e.player || d->class_name == "Character" || d->class_name == "Portal" ||
                      d->class_name == "GuildHallPortal");
            }),
        "marker set differs from the scan");
  const auto is_static = [](const realm::Entity& e, const realm::ObjectDef* d) {
    return d && d->stationary && !e.projectile;
  };
  check(visited([&](auto f) { world.each_dynamic(f); }) ==
            expect([&](const realm::Entity& e, const realm::ObjectDef* d) {
              return !is_static(e, d);
            }),
        "dynamic set differs from the scan");
  const auto statics = expect(is_static);
  check(world.static_count() == statics.size(), "static grid count differs");
  for (const auto* e : statics) {
    int seen = 0;
    world.each_static_in(e->motion.position, e->motion.position,
                         [&](const realm::Entity& o) { seen += &o == e; });
    check(seen == 1, "static entity not in its own cell");
  }
  std::uniform_real_distribution<double> coordinate(-40, world.width() + 40);
  for (int q = 0; q < 6; ++q) {
    double x0 = coordinate(rng), x1 = coordinate(rng), y0 = coordinate(rng), y1 = coordinate(rng);
    if (x0 > x1) {
      std::swap(x0, x1);
    }
    if (y0 > y1) {
      std::swap(y0, y1);
    }
    std::map<const realm::Entity*, int> got;
    world.each_static_in({x0, y0}, {x1, y1}, [&](const realm::Entity& o) { ++got[&o]; });
    for (const auto& [e, n] : got) {
      check(n == 1 && is_static(*e, catalog.object(e->type)),
            "grid query visited a duplicate or a dynamic entity");
    }
    for (const auto* e : statics) {
      if (e->motion.position.x >= x0 && e->motion.position.x <= x1 && e->motion.position.y >= y0 &&
          e->motion.position.y <= y1) {
        check(got.contains(e), "grid query missed a static entity");
      }
    }
  }
  return statics.size();
}

void sets_differential(const realm::Catalog& catalog) {
  std::vector<std::uint16_t> players, enemies, statics, static_enemies, portals, merchants,
      projectiles, other;
  for (const auto& d : catalog.objects()) {
    if (d.player) {
      players.push_back(d.type);
    } else if (d.class_name == "Projectile") {
      projectiles.push_back(d.type);
    } else if (d.stationary) {
      statics.push_back(d.type);
      if (d.enemy || d.class_name == "Character") {
        static_enemies.push_back(d.type);
      }
    } else if (d.class_name == "Portal" || d.class_name == "GuildHallPortal") {
      portals.push_back(d.type);
    } else if (d.class_name == "Merchant") {
      merchants.push_back(d.type);
    } else if (d.enemy) {
      enemies.push_back(d.type);
    } else {
      other.push_back(d.type);
    }
  }
  check(!players.empty() && !enemies.empty() && !statics.empty() && !static_enemies.empty() &&
            !portals.empty() && !merchants.empty() && !projectiles.empty(),
        "set pools");
  realm::AnimationLibrary animations(realm::flash::source_root);
  realm::Minimap minimap(catalog, animations, realm::flash::source_root);
  std::mt19937 rng(5051);
  std::uniform_real_distribution<double> unit(0, 1);
  const auto pick = [&](const std::vector<std::uint16_t>& pool) {
    return pool[rng() % pool.size()];
  };
  std::uint64_t updates = 0, static_moves = 0, converging = 0, focus_switches = 0, resets = 0,
                static_seen = 0, markers_seen = 0, hovers = 0;
  for (int run = 0; run < 40; ++run) {
    realm::World world(catalog);
    const int w = 60 + static_cast<int>(rng() % 100), h = 50 + static_cast<int>(rng() % 90);
    world.initialize(w, h);
    ++resets;
    const auto position = [&]() -> realm::Vec2 {
      const auto r = rng() % 20;
      if (r == 0) {
        return {(unit(rng) * 2 - 1) * 1e5,
                (unit(rng) * 2 - 1) * 1e5};  // far outside: clamps to border cells
      }
      if (r < 3) {
        return {unit(rng) * (w + 60) - 30, unit(rng) * (h + 60) - 30};  // around the borders
      }
      return {static_cast<double>(rng() % w) + (rng() % 2 ? .5 : unit(rng)),
              static_cast<double>(rng() % h) + (rng() % 2 ? .5 : unit(rng))};
    };
    const auto ids = [&] {
      std::vector<std::int32_t> v;
      for (const auto& e : world.entities()) {
        v.push_back(e.id);
      }
      return v;
    };
    for (int step = 0; step < 400; ++step) {
      const auto op = rng() % 16;
      const auto live = ids();
      if (op < 5 || live.empty()) {
        const auto kind = rng() % 9;
        const auto type = kind == 0   ? pick(players)
                          : kind == 1 ? pick(enemies)
                          : kind <= 3 ? pick(statics)
                          : kind == 4 ? pick(static_enemies)
                          : kind == 5 ? pick(portals)
                          : kind == 6 ? pick(merchants)
                          : kind == 7 ? pick(projectiles)
                                      : pick(other);
        // Projectile visuals as Combat adds them; occasionally a stationary type flagged as a
        // projectile (stays dynamic).
        const bool projectile = kind == 7 ? rng() % 4 != 0 : rng() % 40 == 0;
        auto& e =
            world.add(1 + static_cast<std::int32_t>(rng() % 300), type, position(), -1, projectile);
        if (e.player && rng() % 2) {
          e.profile.guild = rng() % 2 ? "fixture" : "other";
        }
      } else if (op == 5) {
        world.remove(live[rng() % live.size()]);
      } else if (op == 6) {
        world.goto_entity(live[rng() % live.size()], position());
        ++static_moves;
      } else if (op <= 9) {
        // NEWTICK interpolation, often for static objects (a stationary definition given a
        // velocity), sometimes two of them converging on one tile in the same frame so the tile
        // claim order matters.
        realm::net::Tick tick;
        const int n = 1 + static_cast<int>(rng() % 12);
        for (int i = 0; i < n; ++i) {
          const auto id = live[rng() % live.size()];
          const auto* e = world.entity(id);
          const double spread = rng() % 3 == 0 ? 30 : 3;
          realm::net::Status st;
          st.id = id;
          st.position = {static_cast<float>(e->motion.position.x + (unit(rng) * 2 - 1) * spread),
                         static_cast<float>(e->motion.position.y + (unit(rng) * 2 - 1) * spread)};
          tick.statuses.push_back(st);
        }
        if (rng() % 3 == 0) {
          std::vector<std::int32_t> s;
          for (const auto& e : world.entities()) {
            if (world.is_static(e)) {
              s.push_back(e.id);
            }
          }
          if (s.size() >= 2) {
            const realm::net::Position at{static_cast<float>(rng() % w) + .5F,
                                          static_cast<float>(rng() % h) + .5F};
            for (int k = 0; k < 2; ++k) {
              realm::net::Status st;
              st.id = s[rng() % s.size()];
              st.position = at;
              tick.statuses.push_back(st);
            }
            ++converging;
          }
        }
        world.apply(tick, rng() % 2 ? 127.0 : 20 + unit(rng) * 200);
      } else if (op == 10) {
        // Movement.cpp writes the focus player's velocity directly; then the focus may change.
        if (auto* focus = world.entity(world.player_id())) {
          focus->motion.velocity = {(unit(rng) * 2 - 1) * .01, (unit(rng) * 2 - 1) * .01};
        }
        if (rng() % 2) {
          world.set_player_id(rng() % 5 == 0 ? -1 : live[rng() % live.size()]);
          ++focus_switches;
        }
      } else if (op <= 14) {
        const double dt = rng() % 4 == 0 ? 500 : 1 + unit(rng) * 40;
        realm::World reference(world);
        world.update(dt);
        reference_update(reference, dt);
        ++updates;
        check(same_state(world, reference), "World::update differs from the full pass");
      } else if (rng() % 10 == 0) {
        world.initialize(w, h);
        ++resets;
      }
      static_seen += verify_sets(world, rng);
      if (const auto* focus = world.entity(world.player_id())) {
        const auto a = markers(world, *focus, [&](auto f) {
          for (const auto& e : world.entities()) {
            f(e);
          }
        });
        const auto b = markers(world, *focus, [&](auto f) { world.each_marker_candidate(f); });
        check(a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
                                                 [](const Marker& x, const Marker& y) {
                                                   return same_bits(x.at.x, y.at.x) &&
                                                          same_bits(x.at.y, y.at.y) &&
                                                          x.color == y.color;
                                                 }),
              "minimap markers differ");
        markers_seen += a.size();
        realm::Party party;
        party.update(world, 1000);
        check(party.members() == reference_party(world), "party members differ");
        if (step % 20 == 0) {
          (void)minimap.update(world, *focus, 0);
          std::vector<realm::Vec2> near;
          world.each_player([&](const realm::Entity& e) {
            near.push_back(minimap.marker_position(world, *focus, e.motion.position));
          });
          for (int k = 0; k < 8; ++k) {
            realm::Vec2 pointer{unit(rng) * 200 - 4, unit(rng) * 200 - 4};
            if (!near.empty() && k % 2) {
              const auto at = near[rng() % near.size()];
              pointer = {at.x + unit(rng) * 8 - 4, at.y + unit(rng) * 8 - 4};
            }
            const auto got = minimap.hover_players(world, *focus, pointer);
            check(got == reference_hover(world, minimap, *focus, pointer), "minimap hover differs");
            hovers += got.size();
          }
        }
      }
      check(gather_targets(world,
                           [&](auto f) {
                             for (const auto& e : world.entities()) {
                               f(e);
                             }
                           }) == gather_targets(world, [&](auto f) { world.each_enemy(f); }),
            "target gather differs");
    }
  }
  std::cout << "entity sets: " << updates << " update differentials, " << static_moves
            << " teleports, " << converging << " converging static pairs, " << focus_switches
            << " focus switches, " << resets << " resets, " << static_seen << " static checks, "
            << markers_seen << " markers, " << hovers << " hover hits\n";
  // Cost on a Realm-sized static population: 120,000 static objects plus 600 actors.
  {
    realm::World world(catalog);
    world.initialize(1024, 1024);
    world.add(1, players.front(), {512, 512});
    world.set_player_id(1);
    for (int i = 0; i < 120000; ++i) {
      world.add(10 + i, pick(statics),
                {static_cast<double>(rng() % 1024) + .5, static_cast<double>(rng() % 1024) + .5});
    }
    for (int i = 0; i < 600; ++i) {
      world.add(200000 + i, i % 2 ? pick(enemies) : pick(players),
                {400 + unit(rng) * 200, 400 + unit(rng) * 200});
    }
    realm::net::Tick tick;
    for (int i = 0; i < 600; ++i) {
      realm::net::Status st;
      st.id = 200000 + i;
      st.position = {static_cast<float>(world.entity(st.id)->motion.position.x + 1),
                     static_cast<float>(world.entity(st.id)->motion.position.y)};
      tick.statuses.push_back(st);
    }
    world.apply(tick);
    using ms = std::chrono::duration<double, std::milli>;
    realm::World reference(world);
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 50; ++i) {
      world.update(1);
    }
    auto t1 = std::chrono::steady_clock::now();
    for (int i = 0; i < 50; ++i) {
      reference_update(reference, 1);
    }
    auto t2 = std::chrono::steady_clock::now();
    check(same_state(world, reference), "crowd update differs");
    std::size_t n = 0;
    auto t3 = std::chrono::steady_clock::now();
    for (int i = 0; i < 50; ++i) {
      n += gather_targets(world, [&](auto f) { world.each_enemy(f); }).size();
    }
    auto t4 = std::chrono::steady_clock::now();
    for (int i = 0; i < 50; ++i) {
      n -= gather_targets(world, [&](auto f) {
             for (const auto& e : world.entities()) {
               f(e);
             }
           }).size();
    }
    auto t5 = std::chrono::steady_clock::now();
    check(n == 0, "crowd gather differs");
    std::cout << "120,600 entities: update " << ms(t1 - t0).count() / 50 << " ms (full pass "
              << ms(t2 - t1).count() / 50 << "), enemy gather " << ms(t4 - t3).count() / 50
              << " ms (scan " << ms(t5 - t4).count() / 50 << ")\n";
  }
}
}

int main() {
  try {
    realm::Catalog catalog(realm::flash::source_root);
    sets_differential(catalog);
    std::vector<std::uint16_t> interactive, merchants, plain, dyes, items;
    for (const auto& d : catalog.objects()) {
      if (d.interactive) {
        interactive.push_back(d.type);
      }
      if (d.class_name == "Merchant") {
        merchants.push_back(d.type);
      }
      if (!d.interactive && !d.player && d.model.empty() && d.class_name == "Character") {
        plain.push_back(d.type);
      }
      if (d.xml.child("Activate").text() == "Dye" && d.xml.has("Tex1")) {
        dyes.push_back(d.type);
      }
      if (d.class_name == "Equipment") {
        items.push_back(d.type);
      }
    }
    check(!interactive.empty() && !merchants.empty() && !plain.empty() && !dyes.empty() &&
              !items.empty(),
          "catalog pools");
    const auto wizard = catalog.object("Wizard")->type;
    std::mt19937 rng(20261011);
    std::uint64_t targets = 0, dyed = 0, ties = 0, merchants_seen = 0;
    const auto pick = [&](const std::vector<std::uint16_t>& pool) {
      return pool[rng() % pool.size()];
    };
    for (int round = 0; round < 6000; ++round) {
      realm::World world(catalog);
      world.initialize(64, 64);
      // Player on a quarter-tile lattice, so the neighbour tile choice and exact 1.0 distances
      // occur often.
      const double px = 20 + static_cast<double>(rng() % 80) / 4.0,
                   py = 20 + static_cast<double>(rng() % 80) / 4.0;
      world.add(1, wizard, {px, py});
      world.set_player_id(1);
      const int count = static_cast<int>(rng() % 40);
      std::int32_t id = 10;
      std::vector<std::int32_t> ids;
      const bool lattice = rng() % 2;
      for (int i = 0; i < count; ++i) {
        const auto kind = rng() % 6;
        const auto type = kind < 2 ? pick(interactive) : kind < 4 ? pick(merchants) : pick(plain);
        const double ox = lattice ? static_cast<double>(static_cast<int>(rng() % 17) - 8) / 4.0
                                  : (static_cast<double>(rng() % 2000) - 1000.0) / 800.0;
        const double oy = lattice ? static_cast<double>(static_cast<int>(rng() % 17) - 8) / 4.0
                                  : (static_cast<double>(rng() % 2000) - 1000.0) / 800.0;
        auto& e = world.add(id, type, {px + ox, py + oy});
        if (catalog.object(type)->class_name == "Merchant" && rng() % 4) {
          e.profile.stats[static_cast<std::uint8_t>(realm::flash::StatId::MERCHANDISE_TYPE_STAT)] =
              rng() % 3 ? pick(dyes) : pick(items);
        }
        ids.push_back(id++);
      }
      if (rng() % 5 == 0 && !ids.empty()) {
        for (int k = 0; k < 2; ++k) {
          auto* e = world.entity(ids[rng() % ids.size()]);
          const double odd[]{std::nan(""), std::numeric_limits<double>::infinity(),
                             -std::numeric_limits<double>::infinity(), 1e300};
          (rng() % 2 ? e->motion.position.x : e->motion.position.y) = odd[rng() % 4];
        }
      }
      if (rng() % 3 == 0 && !ids.empty()) {
        world.remove(ids[rng() % ids.size()]);
      }
      const auto* expected = reference_interactive(world);
      const auto* actual = world.interactive_target();
      check(expected == actual, "interactive target differs");
      targets += actual != nullptr;
      const auto* player = world.entity(1);
      check(reference_dyes(world, *player) == realm::world_dyes(world, *player),
            "world dyes differ");
      dyed += reference_dyes(world, *player) !=
              std::array<std::uint32_t, 2>{player->appearance.tex1, player->appearance.tex2};
      for (const auto& e : world.entities()) {
        merchants_seen += catalog.object(e.type)->class_name == "Merchant";
      }
      // Non-focus entities are never dyed by nearby merchants.
      if (!ids.empty()) {
        const auto* other = world.entity(ids.front());
        if (other) {
          check(realm::world_dyes(world, *other) == reference_dyes(world, *other),
                "non-focus dyes differ");
        }
      }
    }
    // No player: nothing is interactive.
    {
      realm::World world(catalog);
      world.initialize(8, 8);
      world.add(5, pick(interactive), {3, 3});
      check(world.interactive_target() == nullptr, "no player");
    }
    // Exactly one tile away is excluded (strict); just inside is chosen; the earlier of two equal
    // distances wins.
    {
      realm::World world(catalog);
      world.initialize(16, 16);
      world.add(1, wizard, {8, 8});
      world.set_player_id(1);
      world.add(2, pick(interactive), {9, 8});
      check(world.interactive_target() == nullptr, "distance 1.0 excluded");
      world.add(3, pick(interactive), {8, 7.5});
      const auto* first = world.interactive_target();
      check(first && first->id == 3, "inside chosen");
      world.add(4, pick(interactive), {8.5, 8});
      check(world.interactive_target()->id == 3, "earlier of an exact tie wins");
      ++ties;
    }
    // Cost on a crowd: 26,000 entities, one interactive object beside the player.
    {
      realm::World world(catalog);
      world.initialize(256, 256);
      world.add(1, wizard, {128, 128});
      world.set_player_id(1);
      for (int i = 0; i < 26000; ++i) {
        world.add(10 + i, pick(plain),
                  {static_cast<double>(rng() % 250) + .25, static_cast<double>(rng() % 250) + .25});
      }
      world.add(99999, pick(interactive), {128.5, 128});
      const auto t0 = std::chrono::steady_clock::now();
      const realm::Entity* got{};
      for (int i = 0; i < 200; ++i) {
        got = world.interactive_target();
      }
      const auto t1 = std::chrono::steady_clock::now();
      const realm::Entity* want{};
      for (int i = 0; i < 200; ++i) {
        want = reference_interactive(world);
      }
      const auto t2 = std::chrono::steady_clock::now();
      check(got == want && got && got->id == 99999, "crowd target");
      using ms = std::chrono::duration<double, std::milli>;
      std::cout << "crowd of 26,000: interactive_target " << ms(t1 - t0).count() / 200
                << " ms, original scan " << ms(t2 - t1).count() / 200 << " ms\n";
    }
    std::cout << checks << " world query checks passed (" << targets << " targets, " << dyed
              << " dyed, " << merchants_seen << " merchants, " << ties << " tie cases)\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
