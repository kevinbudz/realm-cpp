#include <GL/glew.h>
// Per-frame CPU cost of connected walls. "scene" times Scene::build over a dense field of ConnectedWall, CaveWall and
// Stalagmite objects with a slowly rotating camera. "project" times project_object_faces alone over the same cached
// geometries (allocating each mesh), and "project_pooled" the same calls into reused meshes. A checksum of every
// emitted quad or mesh is printed: builds can be compared for byte-identical output, and project and project_pooled
// must agree. Timing tool only (not a ctest). Usage: realm_wall_bench [frames]
//
// Real-map mode: realm_wall_bench map PATH.jm TILE_X TILE_Y [frames=1000] [--no-render]
// Loads a .jm with World::load_jm and views it at tile (TILE_X, TILE_Y) the way the client's offline --map path does
// (non-live Terrain with its images in the atlas, Appearances, two dynamic atlas pages, cached terrain, clip width =
// camera width minus the HUD column). Each measured frame times Scene::build, then (unless --no-render) times
// Renderer::draw on the same quads. Reports visible entities by class, quads by kind, and the draw runs
// Renderer::draw_batch would form. No StatusArt (names, signs, condition icons are not drawn).
#include "realm/engine3d/ConnectedObjects.hpp"
#include "realm/map/Scene.hpp"
#include "realm/stage3D/Renderer.hpp"
#include "flash_assets.hpp"
#include <SDL.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace realm;
using clock_type = std::chrono::steady_clock;

#ifdef NDEBUG
constexpr const char* optimized = "yes";
#else
constexpr const char* optimized = "no";
#endif
constexpr std::uint64_t fnv_offset = 1469598103934665603ULL;
constexpr int warmup_frames = 30;
constexpr int view_width = 800, view_height = 600;
constexpr double zoom = 1.0;
const Vec2 center{24, 24};
const SpriteRegion sprite{0, 0, 0, 8, 8};

struct Placed {
  std::int32_t id;
  std::uint16_t type;
  int x, y;
};

// A solid 40x40 block with holes (every 11th diagonal and a cross) so connection patterns vary. Tomb Walls fill the
// left half, Cave Walls the right, and a Stalagmite sits on every 29th diagonal.
std::vector<Placed> fill_field(World& world, const Catalog& catalog) {
  const auto grass = catalog.ground("Grass")->type;
  for (int y = 0; y < world.height(); ++y)
    for (int x = 0; x < world.width(); ++x) world.tile(x, y)->type = grass;
  const auto tomb = catalog.object("Tomb Wall")->type, cave = catalog.object("Cave Wall")->type,
             stalagmite = catalog.object("Stalagmite")->type;
  std::vector<Placed> placed;
  std::int32_t id = 1;
  for (int y = 4; y < 44; ++y)
    for (int x = 4; x < 44; ++x) {
      if ((x * 7 + y * 13) % 11 == 0 || (x == 24 && y % 5 == 0) || (y == 24 && x % 6 == 0)) continue;
      const auto type = (x * 31 + y * 17) % 29 == 0 ? stalagmite : x < 24 ? tomb : cave;
      world.add(id, type, {x + .5, y + .5});
      placed.push_back({id++, type, x, y});
    }
  return placed;
}

// FNV-1a over the fields of each object; members are hashed one by one so struct padding never enters the sum.
void mix(std::uint64_t& hash, const void* data, std::size_t bytes) {
  const auto* p = static_cast<const unsigned char*>(data);
  for (std::size_t i = 0; i < bytes; ++i) {
    hash ^= p[i];
    hash *= 1099511628211ULL;
  }
}
template <class T>
void mix_value(std::uint64_t& hash, const T& value) {
  mix(hash, &value, sizeof(T));
}
void mix_mesh(std::uint64_t& hash, const MeshDraw& mesh) {
  for (const auto& v : mesh.vertices) {
    mix_value(hash, v.x);
    mix_value(hash, v.y);
    mix_value(hash, v.u);
    mix_value(hash, v.v);
    mix_value(hash, v.shade);
  }
  for (const auto& g : mesh.groups) {
    mix_value(hash, g.solid);
    mix_value(hash, g.first);
    mix_value(hash, g.count);
    mix_value(hash, g.repeat);
  }
  mix_value(hash, mesh.bounds);
  mix_value(hash, mesh.page);
  mix_value(hash, mesh.color);
}
void mix_quad(std::uint64_t& hash, const Quad& q) {
  mix_value(hash, q.origin);
  mix_value(hash, q.axis_x);
  mix_value(hash, q.axis_y);
  mix_value(hash, q.uv);
  mix_value(hash, q.page);
  mix_value(hash, q.multiply);
  mix_value(hash, q.offset);
  mix_value(hash, q.kind);
  mix_value(hash, q.uv_offset);
  mix_value(hash, q.texture);
  if (q.kind == 9 && q.owner) mix_mesh(hash, *static_cast<const MeshDraw*>(q.owner.get()));
}

struct Summary {
  double mean{}, p95{};
};
Summary summarize(std::vector<double> us) {
  if (us.empty()) return {};
  std::sort(us.begin(), us.end());
  Summary s;
  for (const double v : us) s.mean += v;
  s.mean /= static_cast<double>(us.size());
  // Nearest-rank 95th percentile.
  s.p95 = us[static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(us.size()))) - 1];
  return s;
}

void run_scene(const World& world, Atlas& atlas, const AnimationLibrary& animations, Renderer& renderer, int frames) {
  Scene scene;
  Camera camera;
  std::vector<Quad> quads;
  std::vector<double> us, render_us;
  std::uint64_t hash = fnv_offset, mesh_quads_total{}, instances_total{}, quads_total{}, draw_calls_total{};
  for (int frame = 0; frame < warmup_frames + frames; ++frame) {
    const double angle = frame * .01;
    camera.configure(center, angle, view_width, view_height, zoom);
    quads.clear();
    const auto start = clock_type::now();
    scene.build(world, camera, atlas, animations, nullptr, nullptr, nullptr, center, angle, zoom, view_width,
                view_height, 1000 + frame * 16, quads);
    const auto stop = clock_type::now();
    if (frame < warmup_frames) continue;
    us.push_back(std::chrono::duration<double, std::micro>(stop - start).count());
    // Hashing and rendering run outside the timed region.
    for (const auto& q : quads) mix_quad(hash, q);
    quads_total += quads.size();
    mesh_quads_total += static_cast<std::uint64_t>(std::count_if(quads.begin(), quads.end(), [](const Quad& q) {
      return q.kind == 9 && q.owner;
    }));
    instances_total += static_cast<std::uint64_t>(std::count_if(quads.begin(), quads.end(), [](const Quad& q) {
      return q.kind == 17;
    }));
    // Renderer::draw CPU submit time (includes the driver; software GL here, so rasterization may be included).
    const auto render_start = clock_type::now();
    renderer.draw(atlas, quads, view_width, view_height, view_width);
    render_us.push_back(std::chrono::duration<double, std::micro>(clock_type::now() - render_start).count());
    draw_calls_total += renderer.draw_calls();
  }
  const auto r = summarize(render_us);
  std::printf("render:         frames=%d mean_us=%.1f p95_us=%.1f draw_calls/frame=%.1f\n", frames, r.mean, r.p95,
              static_cast<double>(draw_calls_total) / frames);
  const auto s = summarize(us);
  std::printf("scene:          frames=%d mean_us=%.1f p95_us=%.1f quads/frame=%.1f mesh_quads/frame=%.1f "
              "wall_instances/frame=%.1f checksum=%016llx\n",
              frames, s.mean, s.p95, static_cast<double>(quads_total) / frames,
              static_cast<double>(mesh_quads_total) / frames, static_cast<double>(instances_total) / frames,
              static_cast<unsigned long long>(hash));
}

// Geometry and inputs as Scene derives them for each wall, built once outside the timed loops.
struct WallInput {
  const ObjectDef* def;
  ConnectedObjectGeometry connected;
  std::vector<ObjectFaceGeometry> stalagmite;
  Vec2 position;
  int tile_x, tile_y;
  bool is_stalagmite, has_cap;
};

std::vector<WallInput> make_inputs(const World& world, const Catalog& catalog, const std::vector<Placed>& placed) {
  std::vector<WallInput> inputs;
  for (const auto& p : placed) {
    WallInput in{};
    in.def = catalog.object(p.type);
    in.position = {p.x + .5, p.y + .5};
    in.tile_x = p.x;
    in.tile_y = p.y;
    in.has_cap = in.def->class_name == "ConnectedWall";
    if (in.def->class_name == "Stalagmite") {
      in.is_stalagmite = true;
      in.stalagmite = stalagmite_geometry(static_cast<std::uint32_t>(p.id) * 2654435761U ^ 0x7f4a7c15U);
    } else {
      const auto seed = static_cast<std::uint32_t>(p.id) * 2654435761U ^ static_cast<std::uint32_t>(p.x) * 1259U ^
                        static_cast<std::uint32_t>(p.y) * 2957U;
      in.connected = connected_object_geometry(*in.def, connected_object_connections(world, p.type, p.x, p.y), p.x, p.y,
                                               seed);
    }
    inputs.push_back(std::move(in));
  }
  return inputs;
}

// One wall's calls: its body (material 0) and, for ConnectedWall, its cap (material 1). Stalagmites and connected
// walls differ in the world and tile arguments Scene passes.
struct Call {
  const std::vector<ObjectFaceGeometry>* faces;
  std::uint8_t material;
  const SpriteRegion* region;
  Vec2 position;
  double rotation;
  const World* world;
  int tile_x, tile_y;
  bool connected;
};
template <class Visit>
void for_each_call(const World& world, const std::vector<WallInput>& inputs, Visit&& visit) {
  for (const auto& in : inputs) {
    const bool connected = !in.is_stalagmite;
    const Call body{in.is_stalagmite ? &in.stalagmite : &in.connected.faces, 0,           &sprite,
                    in.position,
                    in.is_stalagmite ? in.def->rotation : in.connected.rotation * 90.0,
                    connected ? &world : nullptr,
                    connected ? in.tile_x : 0,
                    connected ? in.tile_y : 0,
                    connected};
    visit(body);
    if (in.has_cap) {
      Call cap = body;
      cap.material = 1;
      visit(cap);
    }
  }
}

void run_project(const World& world, const std::vector<WallInput>& inputs, int frames) {
  Camera camera;
  std::vector<double> us;
  // Kept alive until the next frame so their release is untimed, as Scene's quads are.
  std::vector<std::shared_ptr<const MeshDraw>> kept;
  kept.reserve(2 * inputs.size());
  std::uint64_t hash = fnv_offset, meshes_total{}, calls_total{};
  for (int frame = 0; frame < warmup_frames + frames; ++frame) {
    const double angle = frame * .01;
    camera.configure(center, angle, view_width, view_height, zoom);
    kept.clear();
    const auto start = clock_type::now();
    for_each_call(world, inputs, [&](const Call& c) {
      kept.push_back(project_object_faces(*c.faces, c.material, *c.region, camera, c.position, 0, c.rotation, c.world,
                                          c.tile_x, c.tile_y, c.connected));
    });
    const auto stop = clock_type::now();
    if (frame < warmup_frames) continue;
    us.push_back(std::chrono::duration<double, std::micro>(stop - start).count());
    std::size_t meshes = 0;
    for (const auto& mesh : kept) {
      if (!mesh) continue;
      mix_mesh(hash, *mesh);
      ++meshes;
    }
    meshes_total += meshes;
    calls_total += kept.size();
  }
  const auto s = summarize(us);
  std::printf("project:        frames=%d mean_us=%.1f p95_us=%.1f calls/frame=%.1f meshes/frame=%.1f "
              "checksum=%016llx\n",
              frames, s.mean, s.p95, static_cast<double>(calls_total) / frames,
              static_cast<double>(meshes_total) / frames, static_cast<unsigned long long>(hash));
}

// The same calls in the same order, each into a MeshDraw slot that stays allocated across frames.
void run_project_pooled(const World& world, const std::vector<WallInput>& inputs, int frames) {
  Camera camera;
  std::vector<double> us;
  std::vector<MeshDraw> slots(2 * inputs.size());
  std::vector<std::size_t> filled;
  filled.reserve(slots.size());
  std::uint64_t hash = fnv_offset, meshes_total{}, calls_total{};
  for (int frame = 0; frame < warmup_frames + frames; ++frame) {
    const double angle = frame * .01;
    camera.configure(center, angle, view_width, view_height, zoom);
    filled.clear();
    std::size_t calls = 0;
    const auto start = clock_type::now();
    for_each_call(world, inputs, [&](const Call& c) {
      if (project_object_faces_into(slots[calls], *c.faces, c.material, *c.region, camera, c.position, 0, c.rotation,
                                    c.world, c.tile_x, c.tile_y, c.connected))
        filled.push_back(calls);
      ++calls;
    });
    const auto stop = clock_type::now();
    if (frame < warmup_frames) continue;
    us.push_back(std::chrono::duration<double, std::micro>(stop - start).count());
    for (const auto index : filled) mix_mesh(hash, slots[index]);
    meshes_total += filled.size();
    calls_total += calls;
  }
  const auto s = summarize(us);
  std::printf("project_pooled: frames=%d mean_us=%.1f p95_us=%.1f calls/frame=%.1f meshes/frame=%.1f "
              "checksum=%016llx\n",
              frames, s.mean, s.p95, static_cast<double>(calls_total) / frames,
              static_cast<double>(meshes_total) / frames, static_cast<unsigned long long>(hash));
}
// Real-map mode ---------------------------------------------------------------------------------------------------

struct MapOptions {
  std::string path;
  int tile_x{}, tile_y{};
  int frames = 1000;
  bool render = true;
  std::vector<std::string> drop;  // ObjectDef class names removed from the world after loading (decomposition only)
};

MapOptions parse_map_options(int argc, char** argv) {
  if (argc < 5)
    throw std::invalid_argument(
        "usage: realm_wall_bench map PATH.jm TILE_X TILE_Y [frames] [--no-render] [--drop=ClassName]...");
  MapOptions o;
  o.path = argv[2];
  o.tile_x = std::stoi(argv[3]);
  o.tile_y = std::stoi(argv[4]);
  bool frames_given = false;
  for (int i = 5; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--no-render") {
      o.render = false;
    } else if (arg.rfind("--drop=", 0) == 0) {
      o.drop.push_back(arg.substr(7));
    } else if (!frames_given) {
      o.frames = std::stoi(arg);
      frames_given = true;
    } else {
      throw std::invalid_argument("unexpected argument: " + arg);
    }
  }
  if (o.frames <= 0) throw std::invalid_argument("frames must be positive");
  return o;
}

// Renderer::draw_batch's split of a quad list: a kind 17 run is one instanced draw; a run of kind 9 without an owner is
// one draw per model id (counted here as one run); kind 9 with an owner is one mesh draw; anything else is one draw per
// run of equal texture. kind_switches counts boundaries between kind 17 and everything else.
struct DrawRuns {
  std::uint64_t wall{}, sprite{}, model{}, mesh{}, terrain_or_layer{}, kind_switches{};
  // Wall runs: instances drawn, corners the GPU shades if every instance is drawn `most` corners long (Renderer's
  // glDrawArraysInstanced(GL_TRIANGLES, 0, most, n)), and corners the walls really own.
  std::uint64_t wall_instances{}, wall_corners_padded{}, wall_corners_used{};
  DrawRuns& operator+=(const DrawRuns& o) {
    wall += o.wall;
    sprite += o.sprite;
    model += o.model;
    mesh += o.mesh;
    terrain_or_layer += o.terrain_or_layer;
    kind_switches += o.kind_switches;
    wall_instances += o.wall_instances;
    wall_corners_padded += o.wall_corners_padded;
    wall_corners_used += o.wall_corners_used;
    return *this;
  }
};
DrawRuns count_draw_runs(const std::vector<Quad>& list) {
  DrawRuns r;
  const auto n = list.size();
  const auto kind = [&](std::size_t i) { return static_cast<int>(list[i].kind); };
  for (std::size_t first = 0; first < n;) {
    const int k = kind(first);
    std::size_t end = first + 1;
    if (k == 10 || k == 14) {
      ++r.terrain_or_layer;
    } else if (k == 9 && !list[first].owner) {
      while (end < n && kind(end) == 9 && !list[end].owner) ++end;
      ++r.model;
    } else if (k == 9) {
      while (end < n && kind(end) == 9 && list[end].owner) ++end;
      ++r.mesh;
    } else {
      // Sprite runs take walls too: a run holding any wall instance is one triangle list of max(6, most) vertices per
      // instance (Renderer::draw_batch), so its padding is counted over every quad of the run.
      const auto texture = list[first].texture;
      while (end < n && list[end].texture == texture && kind(end) != 9 && kind(end) != 10 && kind(end) != 14) ++end;
      std::uint64_t most = 0, used = 0, walls = 0;
      for (std::size_t i = first; i < end; ++i)
        if (kind(i) == 17) {
          const auto count = static_cast<std::uint64_t>(list[i].multiply[1]);
          ++walls;
          most = std::max(most, count);
          used += count;
        }
      if (walls) {
        ++r.wall;
        r.wall_instances += walls;
        r.wall_corners_padded += std::max<std::uint64_t>(6, most) * (end - first);
        r.wall_corners_used += used;
      } else {
        ++r.sprite;
      }
    }
    first = end;
  }
  for (std::size_t i = 1; i < n; ++i) r.kind_switches += (kind(i) == 17) != (kind(i - 1) == 17);
  return r;
}

// Scene::build's first culling test (its model_reaches exception is counted separately as models_outside).
bool in_cull_rect(const Camera& camera, const Entity& e, int width, int height) {
  const auto p = camera.world_to_screen(e.motion.position, e.motion.z);
  return p.x > -400 && p.x < width + 400 && p.y > -100 && p.y < height + 400;
}

void run_map(const World& world, const Catalog& catalog, Atlas& atlas, const AnimationLibrary& animations,
             const Appearances& appearances, const Terrain& terrain, Renderer* renderer, Vec2 position, int frames) {
  Scene scene;
  scene.set_cached_terrain(true);
  scene.set_dynamic_atlas_pages(2, 12, 120);
  Camera camera;
  std::vector<Quad> quads;
  std::vector<double> us, render_us;
  std::array<std::uint64_t, 32> kinds{};
  std::map<std::string, std::uint64_t> visible;
  std::uint64_t hash = fnv_offset, quads_total{}, draw_calls_total{}, models_outside{};
  DrawRuns runs;
  int clip_width = 0;
  for (int frame = 0; frame < warmup_frames + frames; ++frame) {
    const double angle = frame * .01;
    camera.configure(position, angle, view_width, view_height, zoom);
    clip_width = static_cast<int>(camera.playable_width());
    quads.clear();
    const auto start = clock_type::now();
    scene.build(world, camera, atlas, animations, nullptr, &terrain, &appearances, position, angle, zoom, clip_width,
                view_height, 1000 + frame * 16, quads);
    const auto stop = clock_type::now();
    if (frame < warmup_frames) continue;
    us.push_back(std::chrono::duration<double, std::micro>(stop - start).count());
    for (const auto& q : quads) {
      ++kinds[static_cast<std::size_t>(std::clamp(static_cast<int>(q.kind), 0, 31))];
      mix_quad(hash, q);
    }
    quads_total += quads.size();
    runs += count_draw_runs(quads);
    for (const auto& e : world.entities()) {
      const auto* def = catalog.object(e.type);
      if (in_cull_rect(camera, e, clip_width, view_height))
        ++visible[def ? def->class_name : "?"];
      else if (def && !def->model.empty())
        ++models_outside;
    }
    if (renderer) {
      const auto render_start = clock_type::now();
      renderer->draw(atlas, quads, view_width, view_height, clip_width);
      render_us.push_back(std::chrono::duration<double, std::micro>(clock_type::now() - render_start).count());
      draw_calls_total += renderer->draw_calls();
    }
  }
  const auto per = [&](std::uint64_t total) { return static_cast<double>(total) / frames; };
  const auto s = summarize(us);
  std::printf("scene:   mean_us=%.1f p95_us=%.1f quads/frame=%.1f checksum=%016llx\n", s.mean, s.p95,
              per(quads_total), static_cast<unsigned long long>(hash));
  std::printf("quads/frame by kind:");
  for (std::size_t k = 0; k < kinds.size(); ++k)
    if (kinds[k]) std::printf(" k%zu=%.1f", k, per(kinds[k]));
  std::printf("\n");
  std::uint64_t visible_total{};
  std::vector<std::pair<std::uint64_t, std::string>> by_class;
  for (const auto& [name, count] : visible) {
    visible_total += count;
    by_class.push_back({count, name});
  }
  std::sort(by_class.rbegin(), by_class.rend());
  std::printf("visible/frame by class (cull rect):");
  for (const auto& [count, name] : by_class) std::printf(" %s=%.1f", name.c_str(), per(count));
  std::printf(" total=%.1f models_outside_rect=%.1f\n", per(visible_total), per(models_outside));
  std::printf("draw runs/frame: with_walls=%.1f sprite=%.1f model=%.1f mesh=%.1f terrain_or_layer=%.1f "
              "wall_sprite_boundaries=%.1f\n",
              per(runs.wall), per(runs.sprite), per(runs.model), per(runs.mesh), per(runs.terrain_or_layer),
              per(runs.kind_switches));
  std::printf("wall runs/frame: instances=%.1f corners_used=%.1f corners_padded=%.1f (padding x%.2f)\n",
              per(runs.wall_instances), per(runs.wall_corners_used), per(runs.wall_corners_padded),
              runs.wall_corners_used ? static_cast<double>(runs.wall_corners_padded) /
                                           static_cast<double>(runs.wall_corners_used)
                                     : 0.0);
  if (renderer) {
    const auto r = summarize(render_us);
    std::printf("render:  mean_us=%.1f p95_us=%.1f draw_calls/frame=%.1f\n", r.mean, r.p95,
                static_cast<double>(draw_calls_total) / frames);
  } else {
    std::printf("render:  skipped (--no-render)\n");
  }
}

void run_map_mode(const Catalog& catalog, const AnimationLibrary& animations, const MapOptions& options) {
  World world(catalog);
  world.load_jm(options.path);
  if (options.tile_x < 0 || options.tile_y < 0 || options.tile_x >= world.width() || options.tile_y >= world.height())
    throw std::invalid_argument("tile outside the map");
  if (!options.drop.empty()) {
    // World::remove keeps the static grid and tile links consistent; ids are collected first.
    std::vector<std::int32_t> doomed;
    for (const auto& e : world.entities()) {
      const auto* def = catalog.object(e.type);
      if (def && std::find(options.drop.begin(), options.drop.end(), def->class_name) != options.drop.end())
        doomed.push_back(e.id);
    }
    for (const auto id : doomed) world.remove(id);
    std::printf("dropped %zu entities of class", doomed.size());
    for (const auto& name : options.drop) std::printf(" %s", name.c_str());
    std::printf("\n");
  }
  // The client builds Appearances and Terrain for --map before the atlas, with the terrain images after the animations.
  Appearances appearances(flash::source_root, animations);
  Terrain terrain(world, flash::source_root, animations.images().size(), -1, false);
  auto images = animations.images();
  images.insert(images.end(), terrain.images().begin(), terrain.images().end());
  Atlas atlas(flash::source_root, images, animations.images().size(), 2);
  std::map<std::string, std::size_t> by_class;
  for (const auto& e : world.entities()) {
    const auto* def = catalog.object(e.type);
    ++by_class[def ? def->class_name : "?"];
  }
  std::printf("map: %s size=%dx%d entities(whole map):", options.path.c_str(), world.width(), world.height());
  for (const auto& [name, count] : by_class) std::printf(" %s=%zu", name.c_str(), count);
  std::printf("\nview: tile=(%d,%d) position=(%.1f,%.1f) %dx%d zoom=%.1f warmup=%d frames=%d renderer=%s\n",
              options.tile_x, options.tile_y, options.tile_x + .5, options.tile_y + .5, view_width, view_height, zoom,
              warmup_frames, options.frames, options.render ? "on" : "off");
  std::unique_ptr<Renderer> renderer;
  if (options.render) renderer = std::make_unique<Renderer>(65536);
  run_map(world, catalog, atlas, animations, appearances, terrain, renderer.get(),
          Vec2{options.tile_x + .5, options.tile_y + .5}, options.frames);
}
}  // namespace

int main(int argc, char** argv) {
  try {
    const bool map_mode = argc > 1 && std::string(argv[1]) == "map";
    const auto map = map_mode ? parse_map_options(argc, argv) : MapOptions{};
    const int frames = map_mode ? map.frames : argc > 1 ? std::stoi(argv[1]) : 300;
    if (frames <= 0) throw std::invalid_argument("frames must be positive");
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) throw std::runtime_error(SDL_GetError());
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    auto* window = SDL_CreateWindow("wall bench", 0, 0, 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window) throw std::runtime_error(SDL_GetError());
    auto context = SDL_GL_CreateContext(window);
    if (!context) throw std::runtime_error(SDL_GetError());
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) throw std::runtime_error("GLEW");
    while (glGetError() != GL_NO_ERROR) {
    }
    {
      using namespace realm;
      Catalog catalog(flash::source_root);
      AnimationLibrary animations(flash::source_root);
      if (map_mode) {
        run_map_mode(catalog, animations, map);
      } else {
        Atlas atlas(flash::source_root, animations.images(), animations.images().size());
        World world(catalog);
        world.initialize(48, 48);
        const auto placed = fill_field(world, catalog);
        std::size_t tombs{}, caves{}, stalagmites{};
        for (const auto& p : placed) {
          const auto& cls = catalog.object(p.type)->class_name;
          tombs += cls == "ConnectedWall";
          caves += cls == "CaveWall";
          stalagmites += cls == "Stalagmite";
        }
        std::printf("build: NDEBUG=%s field=48x48 walls=%zu (ConnectedWall=%zu CaveWall=%zu Stalagmite=%zu) view=%dx%d "
                    "zoom=%.1f\n",
                    optimized, placed.size(), tombs, caves, stalagmites, view_width, view_height, zoom);
        Renderer renderer(65536);
        run_scene(world, atlas, animations, renderer, frames);
        const auto inputs = make_inputs(world, catalog, placed);
        run_project(world, inputs, frames);
        run_project_pooled(world, inputs, frames);
      }
    }
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
