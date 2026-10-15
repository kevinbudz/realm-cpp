#include <GL/glew.h>
// Differential checks of instanced model rendering (model_quad + persistent model_geometry,
// projected, culled and shaded on the GPU) against the CPU reference path (project_model).
// CPU part: the instance transform reproduces every reference vertex, the kept-face set and
// the flat shade. GPU part: both paths render the same frames (every model, camera/object
// rotations, zooms, with and without sprites, overlapping painter-ordered scenes with
// interleaved sprites) and the captures are compared pixel by pixel.
#include "realm/assets/AnimationLibrary.hpp"
#include "realm/objects/Catalog.hpp"
#include "realm/engine3d/Model3D.hpp"
#include "realm/engine3d/ConnectedObjects.hpp"
#include "realm/map/Scene.hpp"
#include "realm/map/World.hpp"
#include "realm/stage3D/Renderer.hpp"
#include "realm/stage3D/WallGeometry.hpp"
#include "flash_assets.hpp"
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <random>
#include <stdexcept>

namespace {
using namespace realm;
int checks{};
std::size_t slivers{};

void check(bool ok, const std::string& why) {
  ++checks;
  if (!ok) {
    throw std::runtime_error(why);
  }
}

Camera camera_at(Vec2 at, double angle, double zoom, int w = 800, int h = 600) {
  Camera c;
  c.configure(at, angle, w, h, zoom, true);
  return c;
}

constexpr double pi = 3.14159265358979323846;

// CPU emulation of the model vertex shader on the quad's float fields.
struct Emulated {
  std::vector<MeshVertex> kept;
  std::vector<bool> textured;
};

Emulated emulate(const Quad& q) {
  Emulated r;
  const auto range = model_range(static_cast<std::uint32_t>(q.offset[2]));
  const auto geometry = model_geometry().subspan(range.first, range.count);
  for (std::size_t t = 0; t + 2 < geometry.size(); t += 3) {
    Vec2 p[3];
    for (int k = 0; k < 3; ++k) {
      const auto& v = geometry[t + k];
      p[k] = {q.origin[0] + double(q.axis_x[0]) * v.x + double(q.axis_y[0]) * v.y +
                  double(q.uv_offset[0]) * v.z,
              q.origin[1] + double(q.axis_x[1]) * v.x + double(q.axis_y[1]) * v.y +
                  double(q.uv_offset[1]) * v.z};
    }
    // Stored fan order (i, 0, i+1): kept when the reference winding test is positive.
    if ((p[0].x - p[1].x) * (p[2].y - p[1].y) - (p[0].y - p[1].y) * (p[2].x - p[1].x) <= 0) {
      continue;
    }
    const auto& n = geometry[t];
    const float c = q.offset[0], s = q.offset[1];
    const float nx = n.nx * c - n.ny * s, ny = n.nx * s + n.ny * c;
    const float lit = .75F + .25F * std::max(0.0F, (nx + 3 * ny + 2 * n.nz) / 3.7416573867739413F);
    for (int k = 0; k < 3; ++k) {
      r.kept.push_back({float(p[k].x), float(p[k].y), geometry[t + k].u, geometry[t + k].v, lit});
    }
    r.textured.push_back(geometry[t].solid < .5F && q.multiply[3] > .5F);
  }
  return r;
}

double area(const MeshVertex* v) {
  return std::abs((v[0].x - v[1].x) * (v[2].y - v[1].y) - (v[0].y - v[1].y) * (v[2].x - v[1].x));
}

// Height of the triangle over its longest edge, in pixels: an edge-on face is a sliver however long
// it is.
double thickness(const MeshVertex* v) {
  double edge = 0;
  for (int k = 0; k < 3; ++k) {
    edge = std::max(
        edge, std::hypot(double(v[k].x) - v[(k + 1) % 3].x, double(v[k].y) - v[(k + 1) % 3].y));
  }
  return edge > 0 ? area(v) / edge : 0;
}

// Compares one reference/instance pair. Faces whose kept state differs must be edge-on slivers.
void compare_cpu(const std::string& name, const Camera& camera, Vec2 at, double rotation,
                 std::uint32_t color, const std::optional<SpriteRegion>& sprite) {
  const auto index = model_index(name);
  check(index.has_value(), "model index " + name);
  const auto ref = project_model(name, camera, at, rotation, color, sprite);
  const auto q = model_quad(*index, camera, at, rotation, color, sprite);
  check(q.kind == 9 && !q.owner && q.texture == 0, "instance quad has no owner");
  const auto e = emulate(q);
  std::vector<MeshVertex> want;
  std::vector<bool> want_textured;
  if (ref) {
    for (const auto& g : ref->groups) {
      for (std::uint32_t i = g.first; i < g.first + g.count; i += 3) {
        for (int k = 0; k < 3; ++k) {
          want.push_back(ref->vertices[i + k]);
        }
        want_textured.push_back(!g.solid);
      }
    }
  }
  if (ref) {
    check(std::abs(ref->color[0] - q.multiply[0]) < 1e-7F &&
              std::abs(ref->color[1] - q.multiply[1]) < 1e-7F &&
              std::abs(ref->color[2] - q.multiply[2]) < 1e-7F && ref->bounds == q.uv &&
              ref->page == q.page,
          "material fields " + name);
  }
  // Walk both kept lists; the geometry is the same fan in the same order, so they align except for
  // slivers.
  std::size_t a = 0, b = 0;
  while (a < want.size() || b < e.kept.size()) {
    const bool match =
        a < want.size() && b < e.kept.size() && std::abs(want[a].u - e.kept[b].u) == 0 &&
        std::abs(want[a].v - e.kept[b].v) == 0 && std::abs(want[a].x - e.kept[b].x) < 5e-3F &&
        std::abs(want[a].y - e.kept[b].y) < 5e-3F &&
        std::abs(want[a + 1].x - e.kept[b + 1].x) < 5e-3F &&
        std::abs(want[a + 2].y - e.kept[b + 2].y) < 5e-3F;
    if (match) {
      for (int k = 0; k < 3; ++k) {
        check(std::abs(want[a + k].x - e.kept[b + k].x) < 5e-3F &&
                  std::abs(want[a + k].y - e.kept[b + k].y) < 5e-3F,
              "vertex position " + name);
        check(std::abs(want[a + k].shade - e.kept[b + k].shade) < 2e-6F, "flat shade " + name);
      }
      check(want_textured[a / 3] == e.textured[b / 3], "material group " + name);
      a += 3;
      b += 3;
      continue;
    }
    // A face kept by only one side must have (near) zero screen area.
    if (a < want.size() && thickness(&want[a]) < 1e-3) {
      a += 3;
      ++slivers;
      continue;
    }
    if (b < e.kept.size() && thickness(&e.kept[b]) < 1e-3) {
      b += 3;
      ++slivers;
      continue;
    }
    if (std::getenv("MODEL_INSTANCE_DEBUG")) {
      std::cerr << "ref " << a << "/" << want.size() << " inst " << b << "/" << e.kept.size()
                << " rotation " << rotation << " zoom " << camera.scale() << " angle "
                << camera.angle() << "\n";
      if (a < want.size()) {
        std::cerr << " ref tri (" << want[a].x << "," << want[a].y << ") (" << want[a + 1].x << ","
                  << want[a + 1].y << ") (" << want[a + 2].x << "," << want[a + 2].y << ") area "
                  << area(&want[a]) << "\n";
      }
      if (b < e.kept.size()) {
        std::cerr << " inst tri (" << e.kept[b].x << "," << e.kept[b].y << ") (" << e.kept[b + 1].x
                  << "," << e.kept[b + 1].y << ") (" << e.kept[b + 2].x << "," << e.kept[b + 2].y
                  << ") area " << area(&e.kept[b]) << "\n";
      }
    }
    throw std::runtime_error("kept faces diverge for " + name);
  }
}
}

int main() {
  try {
    // Index, geometry and range bookkeeping.
    std::size_t vertices{};
    for (std::size_t i = 0; i < model_count(); ++i) {
      const auto name = model_name(i);
      const auto index = model_index(name);
      check(index && *index == i, "model_index round trip");
      const auto range = model_range(static_cast<std::uint32_t>(i));
      check(range.first == vertices && range.count == 3 * model_triangle_count(name),
            "contiguous model ranges");
      vertices += range.count;
    }
    check(vertices == model_geometry().size() && !model_index("Nonexistent") &&
              model_range(9999).count == 0,
          "geometry covers every model");
    // CPU differential: every model, 24 camera angles x object rotations x zooms, with and without
    // a sprite.
    const SpriteRegion sprite{1, 16, 32, 8, 8};
    std::mt19937 rng(9);
    std::uniform_real_distribution<double> unit(0, 1);
    std::size_t cases{};
    for (std::size_t i = 0; i < model_count(); ++i) {
      for (int a = 0; a < 24; ++a) {
        for (const double zoom : {1.0, 2.0, .75}) {
          for (int r = 0; r < 4; ++r) {
            const Vec2 at{30 + unit(rng) * 40, 30 + unit(rng) * 40};
            const double rotation = r == 0   ? 0
                                    : r == 1 ? 90
                                    : r == 2 ? a * 15.0
                                             : unit(rng) * 720 - 360;
            const auto camera = camera_at({at.x + unit(rng) * 8 - 4, at.y + unit(rng) * 6 - 3},
                                          a * pi / 12 + (r == 3 ? unit(rng) : 0), zoom);
            compare_cpu(std::string(model_name(i)), camera, at, rotation, 0x336699,
                        (a + r) % 2 ? std::optional<SpriteRegion>{sprite} : std::nullopt);
            ++cases;
          }
        }
      }
    }
    std::cout << "CPU instance/reference cases: " << cases
              << ", edge-on faces (<0.001 px thick) kept by one side only: " << slivers << "\n";

    // GPU differential.
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
      throw std::runtime_error(SDL_GetError());
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    constexpr int width = 800, height = 600;
    auto* window = SDL_CreateWindow("model instances", 0, 0, width, height,
                                    SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window) {
      throw std::runtime_error(SDL_GetError());
    }
    auto context = SDL_GL_CreateContext(window);
    if (!context) {
      throw std::runtime_error(SDL_GetError());
    }
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
      throw std::runtime_error("GLEW");
    }
    while (glGetError() != GL_NO_ERROR) {
    }
    std::size_t frames{}, drawn{}, differing{}, large{}, instances{};
    // The whole GPU section runs twice: base-instance draws, then the per-run pointer path. Each
    // capture of the first pass is kept and must match the same capture of the second pass exactly.
    std::vector<std::vector<unsigned char>> path_pixels;
    const auto gpu_rng = rng;  // both passes draw the same random cameras
    for (const bool base_instance : {true, false}) {
      rng = gpu_rng;
      Catalog catalog(flash::source_root);
      AnimationLibrary animations(flash::source_root);
      Atlas atlas(flash::source_root, animations.images(), animations.images().size());
      Renderer renderer(65536);
      renderer.set_base_instance_draws(base_instance);
      std::cout << "Quad draw path: "
                << (renderer.base_instance_draws() ? "base instance" : "per-run") << "\n";
      std::size_t path_index{};
      const bool recording = path_pixels.empty();
      const auto same_path = [&](const Image& image) {
        if (recording) {
          path_pixels.push_back(image.pixels);
          return;
        }
        check(path_index < path_pixels.size() && path_pixels[path_index] == image.pixels,
              "both quad draw paths render the same pixels");
        ++path_index;
      };
      // Every capture is compared across the two draw paths.
      const auto capture_frame = [&]() {
        auto image = renderer.capture(width, height);
        same_path(image);
        return image;
      };
      renderer.clear_color({54 / 255.F, 54 / 255.F, 54 / 255.F, 1});
      // ConnectedWall NoWallTextureRepeat clamps at the bitmap edge, while ordinary meshes still
      // tile.
      atlas.resize_dynamic(1);
      atlas.upload_dynamic(0, 0, 0, Image{2, 1, {255, 0, 0, 255, 0, 0, 255, 255}});
      auto fill = std::make_shared<MeshDraw>();
      fill->bounds = {0, 0, 2.0F / sprite_page_size, 1.0F / sprite_page_size};
      fill->page = static_cast<float>(atlas.dynamic_first());
      fill->vertices = {{100, 100, -.5F, 0, 1}, {300, 100, 1.5F, 0, 1}, {100, 200, -.5F, 1, 1},
                        {300, 100, 1.5F, 0, 1}, {300, 200, 1.5F, 1, 1}, {100, 200, -.5F, 1, 1}};
      fill->groups = {{false, 0, 6, false}};
      const auto sample = [](const Image& image, int x, int channel) {
        return image.pixels[static_cast<std::size_t>(150 * image.width + x) * 4 +
                            static_cast<std::size_t>(channel)];
      };
      for (const bool repeat : {false, true}) {
        fill->groups[0].repeat = repeat;
        const std::array quads{mesh_quad(fill)};
        renderer.draw(atlas, quads, width, height, width);
        const auto capture = capture_frame();
        check(sample(capture, 110, repeat ? 2 : 0) == 255 &&
                  sample(capture, 290, repeat ? 0 : 2) == 255,
              "mesh bitmap repetition is sampled per fragment");
      }
      // Consecutive meshes draw as one batch. It must match the same meshes split by transparent
      // quads (one draw each), with an empty mesh, a mixed mesh and a clipped layer in the run, and
      // also when the tail is an overlay or a draw_overlay pass. Every mesh covers its own area, so
      // a misplaced span shows up as a hole or a stray triangle.
      {
        auto mixed = std::make_shared<MeshDraw>(*fill);
        for (auto& v : mixed->vertices) {
          v.y += 250;
        }
        mixed->color = {1, 0, 0};
        mixed->groups = {{false, 0, 3, false}, {true, 3, 3, true}};
        auto solid = std::make_shared<MeshDraw>();
        solid->color = {0, 1, 0};
        solid->vertices = {{350, 120, 0, 0, 1}, {520, 150, 0, 0, .6F}, {420, 260, 0, 0, .8F}};
        solid->groups = {{true, 0, 3, true}};
        auto tail = std::make_shared<MeshDraw>(*fill);
        for (auto& v : tail->vertices) {
          v.x += 400;
          v.y += 250;
        }
        auto blue = std::make_shared<MeshDraw>();
        blue->color = {0, 0, 1};
        blue->vertices = {{560, 120, 0, 0, 1}, {760, 150, 0, 0, .6F}, {660, 260, 0, 0, .8F}};
        blue->groups = {{true, 0, 3, true}};
        auto layer = std::make_shared<QuadLayer>();
        layer->quads = {mesh_quad(blue)};
        Quad clip;
        clip.kind = 14;
        clip.owner = layer;
        clip.axis_x = {static_cast<float>(width), 0};
        clip.axis_y = {0, static_cast<float>(height)};
        Quad hidden;
        hidden.kind = 2;
        hidden.axis_x = {1, 0};
        hidden.axis_y = {0, 1};
        hidden.multiply = {0, 0, 0, 0};
        const std::array batched{mesh_quad(fill),
                                 mesh_quad(solid),
                                 mesh_quad(std::make_shared<MeshDraw>()),
                                 mesh_quad(mixed),
                                 clip,
                                 mesh_quad(tail)};
        std::vector<Quad> split;
        for (const auto& q : batched) {
          split.push_back(hidden);
          split.push_back(q);
        }
        split.push_back(hidden);
        renderer.draw(atlas, std::array{hidden}, width, height, width);
        const auto background = capture_frame();
        renderer.draw(atlas, split, width, height, width);
        const auto want = capture_frame();
        check(want.pixels != background.pixels, "batch check draws visible meshes");
        renderer.draw(atlas, batched, width, height, width);
        check(capture_frame().pixels == want.pixels, "mesh batch matches one draw per mesh");
        renderer.draw(atlas, std::span(batched).first(4), width, height, width,
                      std::span(batched).subspan(4));
        check(capture_frame().pixels == want.pixels, "mesh batch continues into the overlay");
        renderer.draw(atlas, std::span(batched).first(4), width, height, width);
        renderer.draw_overlay(atlas, std::span(batched).subspan(4), width, height);
        check(capture_frame().pixels == want.pixels, "mesh batch continues into draw_overlay");
      }
      // One real object (and its real sprite) per model.
      std::map<std::string, const ObjectDef*> objects;
      for (const auto& d : catalog.objects()) {
        if (!d.model.empty() && has_model(d.model) && !objects.contains(d.model)) {
          objects.emplace(d.model, &d);
        }
      }
      std::vector<SpriteRegion> sprites;
      for (const auto& d : catalog.objects()) {
        if (d.model.empty() && d.texture.sheet >= 0 && sprites.size() < 64) {
          sprites.push_back(
              atlas.sprite(static_cast<std::size_t>(d.texture.sheet), d.texture.index));
        }
      }
      auto material = [&](const ObjectDef& d) {
        std::optional<SpriteRegion> s;
        const auto& t = d.texture.select(0);
        if (t.sheet >= 0) {
          s = atlas.sprite(static_cast<std::size_t>(t.sheet), t.index);
        }
        return s;
      };

      struct Item {
        const ObjectDef* def;
        Vec2 at;
        double rotation;
        std::optional<SpriteRegion> sprite;
        int depth;
      };

      // Renders the same painter stream twice, models as reference meshes and as instances, and
      // compares.
      auto run = [&](const Camera& camera, const std::vector<Item>& items, bool interleave_sprites,
                     const std::string& label) {
        std::vector<Quad> ref, inst;
        int k = 0;
        for (const auto& item : items) {
          if (interleave_sprites && !sprites.empty()) {
            const auto p = camera.world_to_screen(item.at);
            const auto& s = sprites[static_cast<std::size_t>(k++) % sprites.size()];
            const auto q = sprite_quad(s, {p.x - 20, p.y - 30}, {40, 0}, {0, 40});
            ref.push_back(q);
            inst.push_back(q);
          }
          if (auto mesh = project_model(item.def->model, camera, item.at, item.rotation,
                                        item.def->color, item.sprite)) {
            ref.push_back(mesh_quad(std::move(mesh)));
          }
          inst.push_back(model_quad(*model_index(item.def->model), camera, item.at, item.rotation,
                                    item.def->color, item.sprite));
          ++instances;
        }
        renderer.draw(atlas, ref, width, height, width);
        const auto a = capture_frame();
        renderer.draw(atlas, inst, width, height, width);
        const auto b = capture_frame();
        std::size_t diff{}, big{}, covered{};
        for (std::size_t p = 0; p < a.pixels.size(); p += 4) {
          int delta = 0;
          bool background = true;
          for (int c = 0; c < 3; ++c) {
            delta = std::max(delta, std::abs(int(a.pixels[p + c]) - int(b.pixels[p + c])));
            background &= a.pixels[p + c] == 54;
          }
          covered += !background;
          diff += delta > 0;
          big += delta > 16;
        }
        ++frames;
        drawn += covered;
        differing += diff;
        large += big;
        // Edge rounding may flip a handful of boundary pixels; anything more is a
        // transform/cull/material error.
        if (big > 4 + covered / 20000) {
          std::cerr << label << ": " << big << " of " << covered << " pixels differ by >16\n";
          throw std::runtime_error("instance render differs from reference: " + label);
        }
        ++checks;
      };
      for (const auto& [model, def] : objects) {
        for (const int a : {0, 5, 9, 17}) {
          for (const double zoom : {1.0, 2.0}) {
            const auto camera = camera_at({50, 50}, a * pi / 12, zoom);
            std::vector<Item> items;
            for (int y = 0; y < 3; ++y) {
              for (int x = 0; x < 4; ++x) {
                items.push_back({def,
                                 {48.2 + x * 1.2 / zoom, 48.8 + y * 1.2 / zoom},
                                 (x + 4 * y) * 33.0,
                                 (x + y) % 3 ? material(*def) : std::nullopt,
                                 0});
              }
            }
            run(camera, items, false,
                model + " angle " + std::to_string(a) + " zoom " + std::to_string(zoom));
          }
        }
      }
      // Overlapping varied scenes in painter (depth, id) order with sprites interleaved, as Scene
      // submits them.
      std::vector<const ObjectDef*> pool;
      for (const auto& [m, d] : objects) {
        pool.push_back(d);
      }
      std::uniform_int_distribution<std::size_t> pick(0, pool.size() - 1);
      for (int scene = 0; scene < 24; ++scene) {
        const auto camera = camera_at({50, 50}, unit(rng) * 2 * pi, scene % 3 == 2 ? 2.0 : 1.0);
        std::vector<Item> items;
        for (int n = 0; n < 60; ++n) {
          const auto* d = pool[pick(rng)];
          const Vec2 at{46 + unit(rng) * 8, 47 + unit(rng) * 6};
          items.push_back({d, at, unit(rng) * 360, unit(rng) < .7 ? material(*d) : std::nullopt,
                           static_cast<int>(camera.world_to_screen(at).y)});
        }
        std::stable_sort(items.begin(), items.end(),
                         [](const Item& x, const Item& y) { return x.depth < y.depth; });
        run(camera, items, scene % 2 == 0, "overlap scene " + std::to_string(scene));
      }
      // Wall instances (kind 17) against the software wall meshes: ConnectedWall bodies and caps
      // (every shape, every rotation), CaveWall and Stalagmite, all drawn from one
      // WallGeometryStore. The field mixes void, ground-less and grass tiles, so faces turn black
      // where the neighbor is not ground, and it reaches the world edge.
      {
        World field(catalog);
        field.initialize(16, 16);
        const auto grass = catalog.ground("Grass")->type,
                   no_ground = catalog.object("Tomb Wall")->type;
        check(!catalog.ground(no_ground), "an object type has no ground");
        for (int y = 0; y < 16; ++y) {
          for (int x = 0; x < 16; ++x) {
            const int kind = (x * 7 + y * 3) % 6;
            field.tile(x, y)->type = kind == 0 ? std::uint16_t{255} : kind == 1 ? no_ground : grass;
          }
        }
        const auto sprite_of = [&](const ObjectDef& d) {
          const auto& t = d.texture.select(0);
          check(t.sheet >= 0, "wall texture is a sheet");
          return atlas.sprite(static_cast<std::size_t>(t.sheet), t.index);
        };

        struct WallPart {
          std::vector<ObjectFaceGeometry> faces;
          std::uint8_t material;
          double rotation;
          Vec2 position;
          int tile_x, tile_y;
          bool connected;
          SpriteRegion sprite;
          std::optional<WallRange> range;
        };

        std::vector<WallPart> parts;
        const auto& tomb = *catalog.object("Tomb Wall");
        const auto& cave = *catalog.object("Cave Wall");
        const auto& stalagmite = *catalog.object("Stalagmite");
        for (std::uint32_t pattern = 0; pattern < 16; ++pattern) {
          // Side bytes N, E, S, W from most to least significant: 1 connected, 2 not.
          std::uint32_t stat = 0;
          for (int bit = 0; bit < 4; ++bit) {
            stat = (stat << 8U) | (((pattern >> bit) & 1U) ? 1U : 2U);
          }
          for (const auto [tx, ty] : {std::pair{5, 5}, std::pair{10, 3}}) {
            const auto geometry = connected_object_geometry(tomb, stat, tx, ty, 0x51f3U + pattern);
            if (geometry.faces.empty()) {
              continue;
            }
            const Vec2 at{tx + .37, ty + .61};
            const double rotation = geometry.rotation * 90.0;
            parts.push_back({geometry.faces, 0, rotation, at, tx, ty, true, sprite_of(tomb), {}});
            parts.push_back({geometry.faces, 1, rotation, at, tx, ty, true, sprite_of(cave), {}});
          }
          for (const std::uint32_t seed : {3U, 77U}) {
            for (const auto [tx, ty] : {std::pair{5, 5}, std::pair{0, 9}}) {
              const auto geometry = connected_object_geometry(cave, stat, tx, ty, seed);
              parts.push_back({geometry.faces,
                               0,
                               geometry.rotation * 90.0,
                               {tx + .37, ty + .61},
                               tx,
                               ty,
                               true,
                               sprite_of(cave),
                               {}});
            }
          }
        }
        for (const std::uint32_t seed : {1U, 42U, 99U, 7U}) {
          parts.push_back({stalagmite_geometry(seed),
                           0,
                           stalagmite.rotation,
                           {7.37, 9.61},
                           7,
                           9,
                           false,
                           sprite_of(stalagmite),
                           {}});
        }
        auto wall_store = std::make_shared<WallGeometryStore>();
        // Appends every part's corners once per store generation, in order or reversed.
        const auto append_parts = [&](bool reverse) {
          std::vector<WallGpuVertex> corners;
          for (std::size_t n = 0; n < parts.size(); ++n) {
            auto& part = parts[reverse ? parts.size() - 1 - n : n];
            corners.clear();
            build_wall_gpu_vertices(part.faces, part.material, part.rotation, corners);
            part.range = wall_store->append(corners);
            check(part.range.has_value(), "the test field fits one store");
          }
        };
        append_parts(false);
        const auto mask = [&](const WallPart& p) {
          return p.connected ? connected_black_mask(field, p.tile_x, p.tile_y) : 0U;
        };
        // The same objects as software meshes (reference) or as instances, in painter order.
        const auto wall_list = [&](const Camera& camera, bool as_instances) {
          std::vector<Quad> list;
          for (const auto& p : parts) {
            if (as_instances) {
              if (p.range->count) {
                list.push_back(
                    wall_quad(wall_store, *p.range, camera, p.position, p.sprite, mask(p)));
              }
            } else if (auto mesh = project_object_faces(
                           p.faces, p.material, p.sprite, camera, p.position, 0, p.rotation,
                           p.connected ? &field : nullptr, p.tile_x, p.tile_y, p.connected)) {
              list.push_back(mesh_quad(std::move(mesh)));
            }
          }
          return list;
        };
        std::size_t wall_frames{}, wall_covered{}, wall_differing{}, wall_large{}, wall_quads{};
        int wall_max_delta{};
        double wall_worst{};
        const auto compare_captures = [&](const Image& want, const Image& got,
                                          const std::string& label) {
          std::size_t diff{}, big{}, covered{};
          int worst = 0;
          for (std::size_t p = 0; p < want.pixels.size(); p += 4) {
            int delta = 0;
            bool background = true;
            for (int c = 0; c < 3; ++c) {
              delta = std::max(delta, std::abs(int(want.pixels[p + c]) - int(got.pixels[p + c])));
              background &= want.pixels[p + c] == 54 && got.pixels[p + c] == 54;
            }
            // Covered: either capture has a wall here.
            if (background) {
              continue;
            }
            ++covered;
            diff += delta > 0;
            big += delta > 16;
            worst = std::max(worst, delta);
          }
          ++wall_frames;
          wall_covered += covered;
          wall_differing += diff;
          wall_large += big;
          wall_max_delta = std::max(wall_max_delta, worst);
          check(covered > 0, label + " draws walls");
          wall_worst = std::max(wall_worst, double(diff) / double(covered));
          // Target: at most 0.1% of the covered pixels differ (edge rasterization only).
          if (diff > covered / 1000) {
            throw std::runtime_error(label + ": " + std::to_string(diff) + " of " +
                                     std::to_string(covered) + " covered pixels differ");
          }
          ++checks;
        };
        Quad invisible;
        invisible.kind = 2;
        invisible.axis_x = {1, 0};
        invisible.axis_y = {0, 1};
        invisible.multiply = {0, 0, 0, 0};
        // A clipped layer over the whole window, holding the given quads.
        const auto clipped = [&](const std::vector<Quad>& quads) {
          auto layer = std::make_shared<QuadLayer>();
          layer->quads = quads;
          Quad clip;
          clip.kind = 14;
          clip.owner = layer;
          clip.axis_x = {static_cast<float>(width), 0};
          clip.axis_y = {0, static_cast<float>(height)};
          return std::vector<Quad>{clip};
        };
        for (const double angle : {0.0, .3, 1.1, 2.5, 4.0}) {
          for (const double zoom : {1.0, 2.5}) {
            const auto camera = camera_at({7, 7}, angle, zoom);
            const auto label =
                "walls angle " + std::to_string(angle) + " zoom " + std::to_string(zoom);
            const auto want = wall_list(camera, false);
            const auto got = wall_list(camera, true);
            wall_quads += got.size();
            renderer.draw(atlas, want, width, height, width);
            const auto reference = capture_frame();
            renderer.draw(atlas, got, width, height, width);
            compare_captures(reference, capture_frame(), label + " in the stream");
            renderer.draw(atlas, clipped(want), width, height, width);
            const auto layered = capture_frame();
            renderer.draw(atlas, clipped(got), width, height, width);
            compare_captures(layered, capture_frame(), label + " in a clipped layer");
            renderer.draw(atlas, std::span<const Quad>{}, width, height, width, want);
            const auto overlaid = capture_frame();
            renderer.draw(atlas, std::span<const Quad>{}, width, height, width, got);
            compare_captures(overlaid, capture_frame(), label + " as an overlay");
            renderer.draw(atlas, std::array{invisible}, width, height, width);
            renderer.draw_overlay(atlas, want, width, height);
            const auto after_overlay = capture_frame();
            renderer.draw(atlas, std::array{invisible}, width, height, width);
            renderer.draw_overlay(atlas, got, width, height);
            compare_captures(after_overlay, capture_frame(), label + " in draw_overlay");
          }
        }
        // A store reset starts a new generation: the parts re-append in reverse order, and the
        // instances must still draw what the meshes draw. Filler corners after them then outgrow
        // the first texture buffer allocation (4096 corners); the parts' ranges stay valid, and
        // their drawing must not change.
        const auto camera = camera_at({7, 7}, 1.1, 1.0);
        const auto want = wall_list(camera, false);
        wall_store = std::make_shared<WallGeometryStore>();
        append_parts(true);
        renderer.draw(atlas, want, width, height, width);
        const auto reference = capture_frame();
        renderer.draw(atlas, wall_list(camera, true), width, height, width);
        compare_captures(reference, capture_frame(), "walls after a store reset");
        const auto corners = wall_store->vertices();
        while (wall_store->vertices().size() <= 4096) {
          check(wall_store->append(corners).has_value(), "filler fits");
        }
        check(wall_store->vertices().size() > 4096, "the store outgrows the first allocation");
        renderer.draw(atlas, wall_list(camera, true), width, height, width);
        compare_captures(reference, capture_frame(), "walls after the store outgrew its buffer");
        GLint texel_limit{};
        glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &texel_limit);
        std::cout << "GPU walls: texture buffer texels " << texel_limit << ", frames "
                  << wall_frames << ", instances " << wall_quads << ", covered pixels "
                  << wall_covered << ", differing " << wall_differing << " ("
                  << (100.0 * wall_worst) << "% worst frame)"
                  << ", by >16: " << wall_large << ", max channel diff " << wall_max_delta
                  << ", store corners " << wall_store->vertices().size() << "\n";
      }
      // Store overflow inside one Scene frame: once the wall corners outgrow the store's limit, the
      // remaining walls are drawn as software meshes (Scene::emit_wall). The limited frame must
      // match the unlimited frame, where every wall is an instance, within the wall tolerance, and
      // it must hold both kinds so the fallback ran mid-frame.
      {
        World field(catalog);
        field.initialize(20, 20);
        const auto grass = catalog.ground("Grass")->type,
                   no_ground = catalog.object("Tomb Wall")->type;
        check(!catalog.ground(no_ground), "an object type has no ground");
        for (int y = 0; y < 20; ++y) {
          for (int x = 0; x < 20; ++x) {
            const int kind = (x * 5 + y * 7) % 9;
            field.tile(x, y)->type = kind == 0 ? std::uint16_t{255} : kind == 1 ? no_ground : grass;
          }
        }
        // Cave walls are one range per entity, so the store fills with them; the mixed connect
        // patterns make faces boundary-tested, so a fallback that drops the black mask would change
        // pixels.
        const auto& cave = *catalog.object("Cave Wall");
        std::uint64_t cave_id = 9000;
        for (int y = 4; y < 16; y += 2) {
          for (int x = 4; x < 16; x += 2) {
            const auto pattern = static_cast<std::uint32_t>((x * 3 + y) % 16);
            std::uint32_t stat = 0;
            for (int bit = 0; bit < 4; ++bit) {
              stat = (stat << 8U) | (((pattern >> bit) & 1U) ? 1U : 2U);
            }
            auto& wall =
                field.add(static_cast<std::int32_t>(cave_id++), cave.type, {x + .37, y + .61});
            wall.profile.stats[static_cast<std::uint8_t>(flash::StatId::CONNECT_STAT)] =
                static_cast<std::int32_t>(stat);
          }
        }
        const Vec2 at{10.5, 10.5};
        const auto camera = camera_at(at, .4, 1.0);
        const auto build = [&](Scene& scene) {
          std::vector<Quad> quads;
          scene.build(field, camera, atlas, animations, nullptr, nullptr, nullptr, at, .4, 1.0,
                      width, height, 1000, quads);
          return quads;
        };
        const auto count_kind = [](const std::vector<Quad>& quads, float kind) {
          return static_cast<std::size_t>(std::count_if(
              quads.begin(), quads.end(), [&](const Quad& q) { return q.kind == kind; }));
        };
        Scene unlimited;
        const auto full = build(unlimited);
        std::size_t corners = 0;
        for (const auto& q : full) {
          if (q.kind == 17) {
            corners = static_cast<const WallGeometryStore*>(q.owner.get())->vertices().size();
          }
        }
        check(count_kind(full, 17) > 0 && count_kind(full, 9) == 0,
              "the unlimited frame draws every wall as an instance");
        check(corners > 1, "the cave walls fill a store");
        Scene limited;
        limited.set_wall_store_limit(corners / 2);
        const auto split = build(limited);
        const auto split_instances = count_kind(split, 17), split_meshes = count_kind(split, 9);
        check(split_instances > 0 && split_meshes > 0,
              "the limited frame mixes instances and software meshes");
        const auto render = [&](const std::vector<Quad>& quads) {
          renderer.draw(atlas, quads, width, height, width);
          return renderer.capture(width, height);
        };
        const auto reference = render(full), overflowed = render(split);
        std::size_t diff{}, covered{};
        int worst = 0;
        for (std::size_t p = 0; p < reference.pixels.size(); p += 4) {
          int delta = 0;
          bool background = true;
          for (int c = 0; c < 3; ++c) {
            delta = std::max(
                delta, std::abs(int(reference.pixels[p + c]) - int(overflowed.pixels[p + c])));
            background &= reference.pixels[p + c] == 54 && overflowed.pixels[p + c] == 54;
          }
          if (background) {
            continue;
          }
          ++covered;
          diff += delta > 0;
          worst = std::max(worst, delta);
        }
        check(covered > 0, "the overflow frame draws walls");
        // The same tolerance as the wall comparisons above: at most 0.1% of covered pixels differ.
        if (diff > covered / 1000) {
          throw std::runtime_error(
              "store overflow frame: " + std::to_string(diff) + " of " + std::to_string(covered) +
              " covered pixels differ (max channel diff " + std::to_string(worst) + ")");
        }
        ++checks;
        std::cout << "GPU wall overflow: store corners " << corners << ", limit " << corners / 2
                  << ", instances " << split_instances << ", software meshes " << split_meshes
                  << ", covered pixels " << covered << ", differing " << diff
                  << ", max channel diff " << worst << "\n";
      }
      // Walls and sprites in one list draw as one triangle list. Sprites beside, between and after
      // wall instances must match the same sprites drawn alone, walls must match walls drawn alone,
      // and a UI surface between them splits the run without changing either side.
      {
        auto store = std::make_shared<WallGeometryStore>();
        const auto& tomb = *catalog.object("Tomb Wall");
        const auto& tomb_texture = tomb.texture.select(0);
        check(tomb_texture.sheet >= 0 && !sprites.empty(), "walls and sprites have textures");
        const auto wall_sprite =
            atlas.sprite(static_cast<std::size_t>(tomb_texture.sheet), tomb_texture.index);
        // Every side connected (a cross), so the instance has every face kind.
        const auto cross = connected_object_geometry(tomb, 0x01010101U, 5, 5, 11U);
        std::vector<WallGpuVertex> corners;
        build_wall_gpu_vertices(cross.faces, 0, cross.rotation * 90.0, corners);
        const auto cross_range = store->append(corners);
        check(cross_range && cross_range->count, "the cross has triangles");
        const auto camera = camera_at({7, 7}, .3, 1.0);
        const auto wall_at = [&](Vec2 at) {
          return wall_quad(store, *cross_range, camera, at, wall_sprite, 0);
        };
        const auto sprite_at = [&](std::size_t i, double x, double y) {
          return sprite_quad(sprites[i % sprites.size()], {x, y}, {40, 0}, {0, 40});
        };
        const std::vector<Quad> sprite_only{sprite_at(0, 60, 150), sprite_at(1, 110, 200),
                                            sprite_at(2, 70, 250), sprite_at(3, 160, 180)};
        const std::vector<Quad> wall_only{wall_at({9.6, 7.4}), wall_at({11.4, 9.1})};
        const std::vector<Quad> mixed{sprite_only[0], wall_only[0],   sprite_only[1],
                                      wall_only[1],   sprite_only[2], sprite_only[3]};
        const auto draw_image = [&](const std::vector<Quad>& list) {
          renderer.draw(atlas, list, width, height, width);
          return capture_frame();
        };
        const auto sprite_image = draw_image(sprite_only);
        const auto sprite_draws = renderer.draw_calls();
        const auto wall_image = draw_image(wall_only);
        const auto wall_draws = renderer.draw_calls();
        const auto mixed_image = draw_image(mixed);
        const auto mixed_draws = renderer.draw_calls();
        check(sprite_draws == 1 && wall_draws == 1 && mixed_draws == 1,
              "sprites and walls in one run are one draw");
        const auto is_background = [](const Image& image, std::size_t p) {
          return image.pixels[p] == 54 && image.pixels[p + 1] == 54 && image.pixels[p + 2] == 54;
        };
        std::size_t sprite_covered{}, wall_covered{};
        for (std::size_t p = 0; p < mixed_image.pixels.size(); p += 4) {
          const bool sprite_here = !is_background(sprite_image, p),
                     wall_here = !is_background(wall_image, p);
          check(!(sprite_here && wall_here), "the sprites and walls of the test do not overlap");
          const auto same = [&](const Image& want) {
            return std::equal(want.pixels.begin() + static_cast<std::ptrdiff_t>(p),
                              want.pixels.begin() + static_cast<std::ptrdiff_t>(p + 4),
                              mixed_image.pixels.begin() + static_cast<std::ptrdiff_t>(p));
          };
          if (sprite_here) {
            ++sprite_covered;
            check(same(sprite_image), "sprites next to walls render exactly as alone");
          } else if (wall_here) {
            ++wall_covered;
            check(same(wall_image), "walls render exactly as alone");
          } else {
            check(is_background(mixed_image, p), "nothing draws outside the sprites and walls");
          }
        }
        check(sprite_covered > 0 && wall_covered > 0, "the test covers sprites and walls");
        // A UI surface (texture other than the atlas) between walls: three draws, the surface exact
        // as alone.
        SurfaceTexture surface;
        surface.update(
            Image{2, 2, {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 0, 255}});
        const auto badge = surface.quad({20, 20}, {30, 0}, {0, 30});
        const auto badge_image = draw_image(std::vector<Quad>{badge});
        const std::vector<Quad> split{sprite_only[0], wall_only[0], sprite_only[1],
                                      badge,          wall_only[1], sprite_only[2]};
        const auto split_image = draw_image(split);
        check(renderer.draw_calls() == 3,
              "a surface between walls splits the run into three draws");
        std::size_t badge_covered{};
        for (std::size_t p = 0; p < split_image.pixels.size(); p += 4) {
          if (is_background(badge_image, p)) {
            continue;
          }
          ++badge_covered;
          check(std::equal(badge_image.pixels.begin() + static_cast<std::ptrdiff_t>(p),
                           badge_image.pixels.begin() + static_cast<std::ptrdiff_t>(p + 4),
                           split_image.pixels.begin() + static_cast<std::ptrdiff_t>(p)),
                "a surface between walls renders exactly as alone");
        }
        check(badge_covered > 0, "the surface draws");
        std::cout << "Mixed walls and sprites: sprite pixels " << sprite_covered << ", wall pixels "
                  << wall_covered << ", surface pixels " << badge_covered << ", one draw per run\n";
        // A clipped layer clips its walls like its sprites: the columns left of the cut match the
        // unclipped walls, the rest is background. The cut runs through the walls, so both sides
        // hold wall pixels.
        {
          std::size_t min_x = static_cast<std::size_t>(width), max_x = 0;
          for (std::size_t p = 0; p < wall_image.pixels.size(); p += 4) {
            if (is_background(wall_image, p)) {
              continue;
            }
            min_x = std::min(min_x, (p / 4) % width);
            max_x = std::max(max_x, (p / 4) % width);
          }
          const auto cut = static_cast<std::size_t>((min_x + max_x) / 2);
          auto layer = std::make_shared<QuadLayer>();
          layer->quads = wall_only;
          Quad clip;
          clip.kind = 14;
          clip.owner = layer;
          clip.axis_x = {static_cast<float>(cut), 0};
          clip.axis_y = {0, static_cast<float>(height)};
          renderer.draw(atlas, std::array{clip}, width, height, width);
          const auto clipped_image = capture_frame();
          std::size_t kept{}, removed{};
          for (std::size_t p = 0; p < wall_image.pixels.size(); p += 4) {
            if ((p / 4) % width < cut) {
              check(std::equal(wall_image.pixels.begin() + static_cast<std::ptrdiff_t>(p),
                               wall_image.pixels.begin() + static_cast<std::ptrdiff_t>(p + 4),
                               clipped_image.pixels.begin() + static_cast<std::ptrdiff_t>(p)),
                    "a clipped layer keeps its walls exactly left of the clip");
              kept += !is_background(wall_image, p);
            } else {
              check(is_background(clipped_image, p),
                    "a clipped layer removes its walls right of the clip");
              removed += !is_background(wall_image, p);
            }
          }
          check(kept > 0 && removed > 0, "the clip cuts through the walls");
        }
        // One wall store per submission: a submission drawing from two stores is refused, and the
        // next valid one draws.
        {
          auto other = std::make_shared<WallGeometryStore>();
          std::vector<WallGpuVertex> other_corners;
          build_wall_gpu_vertices(cross.faces, 0, cross.rotation * 90.0, other_corners);
          const auto other_range = other->append(other_corners);
          check(other_range && other_range->count, "the second store holds the cross");
          const std::vector<Quad> two_stores{
              wall_at({9.6, 7.4}),
              wall_quad(other, *other_range, camera, {11.4, 9.1}, wall_sprite, 0)};
          bool refused = false;
          try {
            renderer.draw(atlas, two_stores, width, height, width);
          } catch (const std::logic_error&) {
            refused = true;
          }
          check(refused, "a submission drawing from two wall stores is refused");
          check(draw_image(wall_only).pixels == wall_image.pixels,
                "the renderer draws the next submission after a refusal");
        }
        // Counters: each wall is one model instance, and triangles add its corners / 3 as submitted
        // (before the cull).
        {
          const auto dir =
              std::filesystem::temp_directory_path() / "realm-model-instances-counters";
          std::filesystem::create_directories(dir);
          telemetry::Recorder recorder(dir / "samples.jsonl", {});
          recorder.begin(0, telemetry::Clock::now());
          const std::vector<Quad> counted{sprite_only[0], wall_only[0], sprite_only[1],
                                          wall_only[1], sprite_only[2]};
          renderer.draw(atlas, counted, width, height, width);
          const auto wall_triangles = 2 * (cross_range->count / 3);
          check(recorder.counter(telemetry::Counter::models) == 2,
                "walls count as models, one per instance");
          check(recorder.counter(telemetry::Counter::quads) == 3,
                "sprites count as quads, walls do not");
          check(
              recorder.counter(telemetry::Counter::triangles) == 2 * 3 + wall_triangles,
              "triangles are two per sprite plus corners / 3 per wall, submitted before the cull");
          recorder.end({0, false, false, true, 0, 0, 0, 0, -1, width, height, 0, 0});
          recorder.finish();
        }
      }
    }
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::cout << "GPU frames " << frames << ", instances " << instances << ", covered pixels "
              << drawn << ", differing " << differing << " (by >16: " << large << ")\n";
    std::cout << "Model instance checks: " << checks << " passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Model instance test failed: " << e.what() << "\n";
    return 1;
  }
}
