#include <GL/glew.h>
// Differential camera-visibility check. Scene::build culls entities and particles by their ground point with fixed
// pixel margins. With culling disabled (the oracle) every object is built; every quad or mesh the oracle produces that
// reaches the viewport must also be produced, identically and in the same painter order, by the culled build. Covers
// every catalog object (all model families, tall/large sizes, players, enemies, projectiles, walls), camera rotation,
// zoom and resize. Second oracle: Scene::set_visibility_scan(true) keeps culling but tests every entity, as before the
// static grid. With a large static population (most far outside the footprint, many near the margins, every static
// model just past each edge) and statics moved between cells by teleports, interpolation and removal, the grid build
// must equal that scan byte for byte.
#include "realm/model.hpp"
#include "realm/scene.hpp"
#include "flash_assets.hpp"
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>
#include <stdexcept>
namespace {
using namespace realm;
struct Box {
  double x0, y0, x1, y1;
};
void grow(Box& b, double x, double y) {
  b.x0 = std::min(b.x0, x);
  b.y0 = std::min(b.y0, y);
  b.x1 = std::max(b.x1, x);
  b.y1 = std::max(b.y1, y);
}
Box bounds(const Quad& q) {
  Box b{1e300, 1e300, -1e300, -1e300};
  if (q.kind == 9 && q.owner) {
    for (const auto& v : std::static_pointer_cast<const MeshDraw>(q.owner)->vertices) grow(b, v.x, v.y);
    return b;
  }
  if (q.kind == 9) {
    // A model instance covers its projected vertices (all faces: a superset of what survives back-face culling).
    const auto range = model_range(static_cast<std::uint32_t>(q.offset[2]));
    for (const auto& v : model_geometry().subspan(range.first, range.count))
      grow(b, q.origin[0] + q.axis_x[0] * v.x + q.axis_y[0] * v.y + q.uv_offset[0] * v.z,
           q.origin[1] + q.axis_x[1] * v.x + q.axis_y[1] * v.y + q.uv_offset[1] * v.z);
    return b;
  }
  for (const double u : {0.0, 1.0})
    for (const double v : {0.0, 1.0})
      grow(b, q.origin[0] + u * q.axis_x[0] + v * q.axis_y[0], q.origin[1] + u * q.axis_x[1] + v * q.axis_y[1]);
  return b;
}
// A ground shadow belongs to its entity and is built with it, so (like objects on
// non-visible squares, which draw nothing) it is required only when its ground point is clearly inside the entity cull
// rectangle. Its size scales with entity size x shadow size x sprite height and can span thousands of pixels; every
// other quad must be present whenever any part of it reaches the viewport.
bool reaches(const Quad& q, int w, int h, double zoom) {
  const auto b = bounds(q);
  if (!(b.x1 >= b.x0 && b.x1 > 0 && b.x0 < w && b.y1 > 0 && b.y0 < h)) return false;
  if (q.kind != 1) return true;
  const double cx = q.origin[0] + (q.axis_x[0] + q.axis_y[0]) / 2, cy = q.origin[1] + (q.axis_x[1] + q.axis_y[1]) / 2,
               slack = 40 * zoom;
  return cx > -400 * zoom + slack && cx < w + 400 * zoom - slack && cy > -100 * zoom + slack &&
         cy < h + 400 * zoom - slack;
}
bool same(const Quad& a, const Quad& b) {
  if (a.origin != b.origin || a.axis_x != b.axis_x || a.axis_y != b.axis_y || a.uv != b.uv || a.page != b.page ||
      a.multiply != b.multiply || a.offset != b.offset || a.kind != b.kind || a.uv_offset != b.uv_offset ||
      a.texture != b.texture)
    return false;
  if (a.kind == 9 && a.owner && b.owner) {
    const auto& m = *std::static_pointer_cast<const MeshDraw>(a.owner);
    const auto& n = *std::static_pointer_cast<const MeshDraw>(b.owner);
    if (m.vertices.size() != n.vertices.size()) return false;
    return m.vertices.empty() ||
           std::memcmp(m.vertices.data(), n.vertices.data(), m.vertices.size() * sizeof(MeshVertex)) == 0;
  }
  return true;
}
int checks{};
}
int main() {
  try {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) throw std::runtime_error(SDL_GetError());
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    auto* window = SDL_CreateWindow("visibility", 0, 0, 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window) throw std::runtime_error(SDL_GetError());
    auto context = SDL_GL_CreateContext(window);
    if (!context) throw std::runtime_error(SDL_GetError());
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) throw std::runtime_error("GLEW");
    while (glGetError() != GL_NO_ERROR) {
    }
    {
      Catalog catalog(flash::source_root);
      AnimationLibrary animations(flash::source_root);
      Atlas atlas(flash::source_root, animations.images(), animations.images().size());
      std::vector<const ObjectDef*> pool, models, projectiles;
      if (std::getenv("VISIBILITY_EXTENTS")) {
        for (std::size_t i = 0; i < model_count(); ++i) {
          const auto e = model_extent(model_name(i));
          std::cout << "model " << model_name(i) << " radius " << e.radius << " top " << e.top << " bottom " << e.bottom
                    << "\n";
        }
      }
      for (const auto& d : catalog.objects()) {
        if (d.texture.sheet < 0 && d.texture.animated < 0 && d.texture.random.empty() && !has_model(d.model)) continue;
        pool.push_back(&d);
        if (has_model(d.model)) models.push_back(&d);
        if (d.class_name == "Projectile") projectiles.push_back(&d);
      }
      {
        int largest = 0, largest_min = 0;
        std::string who;
        for (const auto& d : catalog.objects()) {
          if (d.max_size > largest) {
            largest = d.max_size;
            who = d.id;
          }
          largest_min = std::max(largest_min, d.min_size);
        }
        std::cout << "catalog: largest legal Size " << largest << " (" << who << "), largest MinSize " << largest_min
                  << "\n";
      }
      if (pool.empty() || models.empty() || projectiles.empty()) throw std::runtime_error("empty catalog pools");
      std::vector<const ObjectDef*> static_pool, static_models, actors;
      for (const auto* d : pool) {
        if (d->stationary) {
          static_pool.push_back(d);
          if (has_model(d->model)) static_models.push_back(d);
        } else
          actors.push_back(d);
      }
      if (static_pool.empty() || static_models.empty() || actors.empty())
        throw std::runtime_error("empty static pools");
      std::size_t grid_quads = 0, grid_builds = 0, static_entities = 0, static_moves = 0, footprint_accepted = 0;
      std::vector<const ObjectDef*> all_statics, lifted_statics;
      for (const auto& d : catalog.objects())
        if (d.stationary) {
          all_statics.push_back(&d);
          if (d.z != 0) lifted_statics.push_back(&d);
        }
      if (lifted_statics.empty()) throw std::runtime_error("no static definition with a z offset");
      {
        double reach = 0, z0 = 0, z1 = 0;
        std::string who;
        for (const auto* d : all_statics) {
          z0 = std::min(z0, d->z);
          z1 = std::max(z1, d->z);
          if (!d->model.empty()) {
            const auto e = model_extent(d->model);
            const double r = e.radius + std::max({0.0, e.top, -e.bottom});
            if (r > reach) {
              reach = r;
              who = d->id;
            }
          }
        }
        std::cout << "static bounds: " << all_statics.size() << " stationary definitions, z " << z0 << ".." << z1
                  << ", largest model reach " << reach << " tiles (" << who << ")\n";
      }
      struct Config {
        int width, height;
        double zoom, angle;
      };
      std::mt19937 rng(std::getenv("VISIBILITY_SEED") ? static_cast<unsigned>(std::atoi(std::getenv("VISIBILITY_SEED")))
                                                      : 77011U);
      std::uniform_real_distribution<double> unit(0, 1);
      std::vector<Config> configs{{800, 600, 1, 0},          {800, 600, 1, .7},    {1280, 720, 1.2, 2.1},
                                  {1920, 1080, 1.8, 4.0},    {1920, 1080, 1.8, 0}, {3840, 2160, 3.6, 1.3},
                                  {1000, 1000, 1.6667, 5.5}, {480, 400, .8, 3.3},  {1366, 768, 1.28, .31}};
      for (int i = 0; i < 12; ++i)
        configs.push_back({static_cast<int>(600 + rng() % 1800), static_cast<int>(400 + rng() % 1000),
                           .5 + unit(rng) * 3, unit(rng) * 6.283185307});
      std::size_t compared = 0, total_oracle = 0, dropped_invisible = 0, rounds = 0;
      // Legal catalog sizes reach 400; 500 adds headroom. Sprites beyond ~600 can outgrow the margins.
      std::vector<std::int32_t> sizes{20, 60, 100, 150, 200, 300, 400, 500};
      if (const char* extra = std::getenv("VISIBILITY_EXTRA_SIZE")) sizes.push_back(std::atoi(extra));
      for (const auto& cfg : configs)
        for (int round = 0; round < 3; ++round, ++rounds) {
          World world(catalog);
          world.initialize(256, 256);
          Camera camera;
          camera.configure({128, 128}, cfg.angle, cfg.width, cfg.height, cfg.zoom, true);
          // Everything within and well beyond the cull margins, in every direction.
          const double reach = std::max(cfg.width, cfg.height) / cfg.zoom / 50.0 * 1.4 + 14;
          std::int32_t id = 10;
          for (int i = 0; i < 1200; ++i) {
            const ObjectDef* d = i % 7 == 0    ? models[rng() % models.size()]
                                 : i % 11 == 0 ? projectiles[rng() % projectiles.size()]
                                               : pool[rng() % pool.size()];
            double x = 128 + (unit(rng) * 2 - 1) * reach, y = 128 + (unit(rng) * 2 - 1) * reach;
            if (i % 2) {  // half the population is sampled on screen, within a few hundred pixels of the cull margins
              const auto at =
                  camera.screen_to_world({(unit(rng) * 1.9 - .45) * cfg.width + (unit(rng) - .5) * 400 * cfg.zoom,
                                          (unit(rng) * 2.2 - .7) * cfg.height});
              x = at.x;
              y = at.y;
            }
            auto& e = world.add(id++, d->type, {x, y},
                                rng() % 3 == 0 && d->class_name != "Projectile" ? sizes[rng() % sizes.size()] : -1);
            e.projectile = d->class_name == "Projectile" && rng() % 2;
            e.motion.facing = unit(rng) * 6.28;
            if (!d->model.empty() && rng() % 2) e.appearance.model_rotation = unit(rng) * 360;
            if (e.projectile && rng() % 2) e.motion.z = .5 * unit(rng);
          }
          // Every model definition just outside each edge of the cull rectangle: a wide footprint or tall body can
          // still reach the screen.
          for (const auto* d : models)
            for (int side = 0; side < 4; ++side) {
              const double jitter = unit(rng) * 30 * cfg.zoom, along = unit(rng);
              const Vec2 at = side == 0   ? Vec2{-400 * cfg.zoom - jitter, along * cfg.height}
                              : side == 1 ? Vec2{cfg.width + 400 * cfg.zoom + jitter, along * cfg.height}
                              : side == 2 ? Vec2{along * cfg.width, -100 * cfg.zoom - jitter}
                                          : Vec2{along * cfg.width, cfg.height + 400 * cfg.zoom + jitter};
              const auto w = camera.screen_to_world(at);
              auto& e = world.add(id++, d->type, {w.x, w.y});
              if (rng() % 2) e.appearance.model_rotation = unit(rng) * 360;
            }
          if (round == 2)
            world.add(-7, pool[0]->type,
                      {128 + reach * .6, 128});  // negative id: the original single-sort painter path
          for (int step = 0; step < 3; ++step) world.particles().update(world, 1000 + step * 100);
          Scene culled, all;
          all.set_culling(false);
          std::vector<Quad> a, b;
          culled.build(world, camera, atlas, animations, nullptr, nullptr, nullptr, {128, 128}, cfg.angle, cfg.zoom,
                       cfg.width, cfg.height, 1300, a);
          all.build(world, camera, atlas, animations, nullptr, nullptr, nullptr, {128, 128}, cfg.angle, cfg.zoom,
                    cfg.width, cfg.height, 1300, b);
          std::vector<const Quad*> want, got;
          for (const auto& q : b)
            if (reaches(q, cfg.width, cfg.height, cfg.zoom)) want.push_back(&q);
          for (const auto& q : a)
            if (reaches(q, cfg.width, cfg.height, cfg.zoom)) got.push_back(&q);
          total_oracle += b.size();
          dropped_invisible += b.size() - a.size();
          if (std::getenv("VISIBILITY_REPORT")) {
            std::size_t g = 0;
            for (const auto* w : want) {
              std::size_t k = g;
              while (k < got.size() && !same(*w, *got[k])) ++k;
              if (k < got.size()) {
                g = k + 1;
                continue;
              }
              const auto box = bounds(*w);
              std::cout << "MISSING cfg " << cfg.width << "x" << cfg.height << " z" << cfg.zoom << " kind " << w->kind
                        << " size " << (box.x1 - box.x0) << "x" << (box.y1 - box.y0) << " penetration "
                        << std::min({box.x1, cfg.width - box.x0, box.y1, cfg.height - box.y0}) << " px box [" << box.x0
                        << "," << box.y0 << ".." << box.x1 << "," << box.y1 << "]\n";
            }
            continue;
          }
          if (want.size() != got.size()) {
            std::size_t i = 0;
            while (i < want.size() && i < got.size() && same(*want[i], *got[i])) ++i;
            const auto& q = *want[std::min(i, want.size() - 1)];
            const auto box = bounds(q);
            {
              const Entity* near = nullptr;
              double best = 1e300;
              for (const auto& e : world.entities()) {
                const auto p = camera.world_to_screen(e.motion.position, e.motion.z);
                const double d = std::hypot(p.x - (box.x0 + box.x1) / 2, p.y - box.y1);
                if (d < best) {
                  best = d;
                  near = &e;
                }
              }
              for (std::size_t k = i; k < std::min(i + 3, std::min(want.size(), got.size())); ++k) {
                const auto wb = bounds(*want[k]), gb = bounds(*got[k]);
                std::cerr << "  want[" << k << "] kind " << want[k]->kind << " [" << wb.x0 << "," << wb.y0 << ".."
                          << wb.x1 << "," << wb.y1 << "] uv " << want[k]->uv[0] << "," << want[k]->uv[1] << " mult "
                          << want[k]->multiply[0] << " off " << want[k]->offset[0] << " | got kind " << got[k]->kind
                          << " [" << gb.x0 << "," << gb.y0 << ".." << gb.x1 << "," << gb.y1 << "] uv " << got[k]->uv[0]
                          << "," << got[k]->uv[1] << " mult " << got[k]->multiply[0] << " off " << got[k]->offset[0]
                          << "\n";
              }
              {
                const auto& c = culled.submitted_counts();
                const auto& o = all.submitted_counts();
                std::cerr << "submitted culled";
                for (auto v : c) std::cerr << " " << v;
                std::cerr << " | all";
                for (auto v : o) std::cerr << " " << v;
                std::cerr << "\n";
              }
              {
                std::size_t where = got.size();
                for (std::size_t k = 0; k < got.size(); ++k)
                  if (same(*want[i], *got[k])) where = k;
                std::cerr << "identical quad in culled stream at index " << where << " of " << got.size() << "\n";
                where = b.size();
                for (std::size_t k = 0; k < a.size(); ++k)
                  if (same(*want[i], a[k])) where = k;
                std::cerr << "in unfiltered culled stream at " << where << " of " << a.size() << "\n";
              }
              std::cerr << "round " << round << " nearby:\n";
              for (const auto& e : world.entities()) {
                const auto p = camera.world_to_screen(e.motion.position, e.motion.z);
                if (std::hypot(p.x - (box.x0 + box.x1) / 2, p.y - box.y1) < 60)
                  std::cerr << "  id " << e.id << " " << catalog.object(e.type)->id << " size " << e.appearance.size
                            << " p(" << p.x << "," << p.y << ") base "
                            << (p.x > -400 * cfg.zoom && p.x < cfg.width + 400 * cfg.zoom && p.y > -100 * cfg.zoom &&
                                p.y < cfg.height + 400 * cfg.zoom)
                            << "\n";
              }
              if (near) {
                const auto* d = catalog.object(near->type);
                const auto p = camera.world_to_screen(near->motion.position, near->motion.z);
                std::cerr << "nearest entity " << d->id << " class " << d->class_name << " size "
                          << near->appearance.size << " z " << near->motion.z << " ground (" << p.x << "," << p.y
                          << ") projectile " << near->projectile << " model '" << d->model << "' draw_under "
                          << d->draw_under << " on_ground " << d->draw_on_ground << "\n";
              }
            }
            std::cerr << "config " << cfg.width << "x" << cfg.height << " zoom " << cfg.zoom << " angle " << cfg.angle
                      << ": oracle has " << want.size() << " quads reaching the viewport, culled build " << got.size()
                      << "; first difference at " << i << " kind " << q.kind << " bounds [" << box.x0 << "," << box.y0
                      << " .. " << box.x1 << "," << box.y1 << "]\n";
            return 1;
          }
          for (std::size_t i = 0; i < want.size(); ++i) {
            ++checks;
            if (!same(*want[i], *got[i])) {
              std::cerr << "quad " << i << " differs in content or painter order (config " << cfg.width << "x"
                        << cfg.height << ")\n";
              return 1;
            }
          }
          compared += want.size();
          // The footprint itself, without the 8-tile cell rounding: every static object the exact cull test accepts
          // (sprite margins at its definition's z, or its model extent) must lie inside it. Sampled densely around
          // every edge.
          {
            Scene probe;
            const auto [low, high] = probe.static_footprint(catalog, camera, cfg.zoom, cfg.width, cfg.height);
            const double unit_px = camera.pixels_per_unit();
            for (int k = 0; k < 4000; ++k) {
              const auto* d = k % 3 == 0   ? lifted_statics[rng() % lifted_statics.size()]
                              : k % 3 == 1 ? static_models[rng() % static_models.size()]
                                           : all_statics[rng() % all_statics.size()];
              const double band = 12 * unit_px;
              const double edge_x = rng() % 2 ? -400 * cfg.zoom : cfg.width + 400 * cfg.zoom,
                           edge_y = rng() % 2 ? -100 * cfg.zoom : cfg.height + 400 * cfg.zoom;
              const Vec2 screen = rng() % 2 ? Vec2{edge_x + (unit(rng) * 2 - 1) * band,
                                                   unit(rng) * (cfg.height + 500 * cfg.zoom) - 100 * cfg.zoom}
                                            : Vec2{unit(rng) * (cfg.width + 800 * cfg.zoom) - 400 * cfg.zoom,
                                                   edge_y + (unit(rng) * 2 - 1) * band};
              const auto at = camera.screen_to_world(screen);
              const auto p = camera.world_to_screen(at, d->z);
              bool accepted = p.x > -400 * cfg.zoom && p.x < cfg.width + 400 * cfg.zoom && p.y > -100 * cfg.zoom &&
                              p.y < cfg.height + 400 * cfg.zoom;
              if (!accepted && !d->model.empty()) {
                const auto e = model_extent(d->model);
                const double side = e.radius * unit_px, up = side + std::max(0.0, e.top) * unit_px,
                             down = side + std::max(0.0, -e.bottom) * unit_px;
                const auto g = camera.world_to_screen(at);
                accepted = g.x + side > 0 && g.x - side < cfg.width && g.y + down > 0 && g.y - up < cfg.height;
              }
              if (!accepted) continue;
              ++footprint_accepted;
              ++checks;
              if (!(at.x >= low.x && at.x <= high.x && at.y >= low.y && at.y <= high.y)) {
                std::cerr << "static footprint misses an accepted " << d->id << " (z " << d->z << ") at screen ("
                          << screen.x << "," << screen.y << "), config " << cfg.width << "x" << cfg.height << " zoom "
                          << cfg.zoom << " angle " << cfg.angle << "\n";
                return 1;
              }
            }
          }
          {
            World big(catalog);
            big.initialize(256, 256);
            std::vector<std::int32_t> statics;
            std::int32_t sid = 100000;
            const auto place = [&]() -> Vec2 {
              if (rng() % 2) return {128 + (unit(rng) * 2 - 1) * reach * 2, 128 + (unit(rng) * 2 - 1) * reach * 2};
              return camera.screen_to_world({(unit(rng) * 1.6 - .3) * cfg.width + (unit(rng) - .5) * 1200 * cfg.zoom,
                                             (unit(rng) * 1.6 - .3) * cfg.height + (unit(rng) - .5) * 900 * cfg.zoom});
            };
            for (int i = 0; i < 4000; ++i) {
              const auto* d = static_pool[rng() % static_pool.size()];
              const auto at = place();
              auto& e = big.add(sid, d->type, at, rng() % 3 == 0 ? sizes[rng() % sizes.size()] : -1);
              if (!d->model.empty() && rng() % 2) e.appearance.model_rotation = unit(rng) * 360;
              statics.push_back(sid++);
            }
            for (const auto* d : static_models)
              for (int side = 0; side < 4; ++side) {
                const double jitter = unit(rng) * 60 * cfg.zoom, along = unit(rng);
                const Vec2 at = side == 0   ? Vec2{-400 * cfg.zoom - jitter, along * cfg.height}
                                : side == 1 ? Vec2{cfg.width + 400 * cfg.zoom + jitter, along * cfg.height}
                                : side == 2 ? Vec2{along * cfg.width, -100 * cfg.zoom - jitter}
                                            : Vec2{along * cfg.width, cfg.height + 400 * cfg.zoom + jitter};
                big.add(sid, d->type, camera.screen_to_world(at));
                statics.push_back(sid++);
              }
            for (int i = 0; i < 300; ++i) {
              const auto* d = actors[rng() % actors.size()];
              big.add(sid++, d->type, place(), -1, d->class_name == "Projectile");
            }
            static_entities += big.static_count();
            Scene grid, scan;
            scan.set_visibility_scan(true);
            for (int phase = 0; phase < 4; ++phase) {
              std::vector<Quad> g, o;
              grid.build(big, camera, atlas, animations, nullptr, nullptr, nullptr, {128, 128}, cfg.angle, cfg.zoom,
                         cfg.width, cfg.height, 1300 + phase * 40, g);
              scan.build(big, camera, atlas, animations, nullptr, nullptr, nullptr, {128, 128}, cfg.angle, cfg.zoom,
                         cfg.width, cfg.height, 1300 + phase * 40, o);
              if (g.size() != o.size()) {
                std::cerr << "static grid: " << g.size() << " quads, full scan " << o.size() << " (config " << cfg.width
                          << "x" << cfg.height << " zoom " << cfg.zoom << " phase " << phase << ")\n";
                return 1;
              }
              for (std::size_t i = 0; i < g.size(); ++i) {
                ++checks;
                if (!same(g[i], o[i])) {
                  std::cerr << "static grid quad " << i << " differs from the full scan (config " << cfg.width << "x"
                            << cfg.height << " phase " << phase << ")\n";
                  return 1;
                }
              }
              grid_quads += g.size();
              ++grid_builds;
              // Move statics between cells: teleports, NEWTICK interpolation followed by update(), removal and re-adds.
              for (int k = 0; k < 300; ++k) {
                big.goto_entity(statics[rng() % statics.size()], place());
                ++static_moves;
              }
              net::Tick tick;
              for (int k = 0; k < 300; ++k) {
                const auto moved = statics[rng() % statics.size()];
                const auto* e = big.entity(moved);
                if (!e) continue;
                net::Status st;
                st.id = moved;
                st.position = {static_cast<float>(e->motion.position.x + (unit(rng) * 2 - 1) * 24),
                               static_cast<float>(e->motion.position.y + (unit(rng) * 2 - 1) * 24)};
                tick.statuses.push_back(st);
              }
              big.apply(tick, 127);
              big.update(40 + unit(rng) * 100);
              static_moves += tick.statuses.size();
              for (int k = 0; k < 100; ++k) big.remove(statics[rng() % statics.size()]);
              for (int k = 0; k < 100; ++k) {
                const auto* d = static_pool[rng() % static_pool.size()];
                big.add(statics[rng() % statics.size()], d->type, place());
              }
            }
          }
        }
      std::cout << rounds << " camera configurations, " << compared
                << " viewport-reaching quads identical in content and order (" << total_oracle << " oracle quads, "
                << dropped_invisible << " culled away, none reaching the viewport), " << checks << " checks\n";
      std::cout << "static grid: " << grid_builds << " builds over " << static_entities << " static entities ("
                << static_moves << " moves), " << grid_quads << " quads identical to the culled full scan; "
                << footprint_accepted << " accepted near-edge static placements inside the footprint\n";
    }
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
