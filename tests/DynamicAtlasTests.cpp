#include <GL/glew.h>
// Composed world bitmaps (appearances, names, condition icons, floating status text) packed into the atlas'
// dynamic pages must render exactly like the per-image SurfaceTexture path they replace. A real scene (players with
// dyes, names and conditions, enemies, rotated projectiles, textured and thrown particles, floating status text) is
// built twice per frame, with the dynamic atlas on and with Scene::set_dynamic_atlas(false), rendered by the real
// shader and compared pixel for pixel. A one-page atlas under a churning, dye-shuffled population then forces
// evictions, re-entry and in-frame overflow (fallback to SurfaceTexture), and every frame must still match. The dynamic
// stream must also need fewer draw calls.
#include "realm/map/Scene.hpp"
#include "realm/objects/StatusArt.hpp"
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
std::uint64_t checks{};
void check(bool ok, const std::string& why) {
  ++checks;
  if (!ok) throw std::runtime_error(why);
}
constexpr int width = 960, height = 640;
// Nearest sampling at an exact texel boundary can legitimately pick either neighbour, so the zooms are chosen to keep
// pixel centers off the boundaries.
constexpr double kZooms[]{1.2203, 1.5219, 1.8301};
std::uint64_t content_checks{}, texel_checks{}, quad_checks{};
struct Pools {
  std::vector<const ObjectDef*> players, enemies, projectiles, thrown, props;
};
struct Totals {
  std::size_t frames{}, differing_frames{}, differing{}, large{}, calls_dynamic{}, calls_surface{}, standalone{};
  int worst{}, tile{};
  std::size_t unexplained{}, explained{}, tied{};
};
struct Rig {
  Catalog& catalog;
  AnimationLibrary& animations;
  Appearances& appearances;
  StatusArt& status;
  Renderer& renderer;
  Pools pools;
};
Pools make_pools(const Catalog& catalog) {
  Pools pools;
  for (const auto& d : catalog.objects()) {
    const bool textured = d.texture.sheet >= 0 || d.texture.animated >= 0;
    if (d.player)
      pools.players.push_back(&d);
    else if (d.class_name == "Projectile" && textured)
      pools.projectiles.push_back(&d);
    else if (d.enemy && textured && d.model.empty() && !d.draw_on_ground)
      pools.enemies.push_back(&d);
    else if (textured && d.model.empty() && !d.draw_on_ground && d.class_name != "Wall" && !d.stationary)
      pools.props.push_back(&d);
    if (textured && d.model.empty() && d.class_name != "Wall") pools.thrown.push_back(&d);
  }
  if (pools.players.empty() || pools.enemies.empty() || pools.projectiles.empty() || pools.thrown.empty() ||
      pools.props.empty())
    throw std::runtime_error("empty catalog pools");
  return pools;
}
std::uint32_t dye(std::mt19937& rng) { return rng() % 3 == 0 ? 0 : 0x1000000U | (rng() & 0xffffffU); }
// A varied population spread over the viewport; `shuffle_dyes` gives every entity a fresh composed appearance (churn).
void populate(World& world, const Rig& rig, std::mt19937& rng, int players, int enemies, int projectiles, int props,
              std::int32_t size_hint, std::vector<std::int32_t>* made = nullptr) {
  std::uniform_real_distribution<double> unit(0, 1);
  std::int32_t id = world.entities().empty() ? 10
                                             : std::max_element(
                                                   world.entities().begin(), world.entities().end(),
                                                   [](const Entity& a, const Entity& b) {
                                                     return a.id < b.id;
                                                   })->id +
                                                   1;
  const auto first = id;
  const auto place = [&] { return Vec2{40 + unit(rng) * 14, 40 + unit(rng) * 10}; };
  const auto sized = [&] {
    return rng() % 3 == 0 ? static_cast<int>(60 + rng() % static_cast<unsigned>(size_hint)) : -1;
  };
  for (int i = 0; i < players; ++i) {
    const auto* d = rig.pools.players[rng() % rig.pools.players.size()];
    auto& e = world.add(id++, d->type, place(), sized());
    e.profile.name = "Hero" + std::to_string(rng() % 100000) + (i % 5 == 0 ? "ThePaladinOfLongNames" : "");
    e.appearance.tex1 = dye(rng);
    e.appearance.tex2 = dye(rng);
    e.motion.facing = unit(rng) * 6.28;
    e.combat.hp = e.combat.max_hp = 100;
    e.combat.hp = (i % 7 == 0) ? 10 : 100;
    if (i % 2 == 0) e.combat.condition = static_cast<std::uint32_t>(rng()) & 0x3fffffU;
    if (i % 4 == 0) {
      e.motion.velocity = {unit(rng) - .5, unit(rng) - .5};
    }
  }
  for (int i = 0; i < enemies; ++i) {
    const auto* d = rig.pools.enemies[rng() % rig.pools.enemies.size()];
    auto& e = world.add(id++, d->type, place(), sized());
    e.profile.name = d->show_name ? "Boss" + std::to_string(i) : "";
    e.motion.facing = unit(rng) * 6.28;
    e.combat.hp = e.combat.max_hp = 50;
    e.appearance.tex1 = dye(rng);
    e.appearance.tex2 = dye(rng);
    if (i % 3 == 0) e.combat.condition = static_cast<std::uint32_t>(rng()) & 0x3fffffU;
    if (i % 5 == 0) e.motion.sink_level = static_cast<int>(rng() % 12);
  }
  for (int i = 0; i < props; ++i) {
    const auto* d = rig.pools.props[rng() % rig.pools.props.size()];
    auto& e = world.add(id++, d->type, place(), sized());
    e.appearance.tex1 = dye(rng);
    e.appearance.tex2 = dye(rng);
    e.motion.facing = unit(rng) * 6.28;
  }
  for (int i = 0; i < projectiles; ++i) {
    const auto* d = rig.pools.projectiles[rng() % rig.pools.projectiles.size()];
    auto& e = world.add(id++, d->type, place(), rng() % 4 == 0 ? static_cast<int>(60 + rng() % 140) : -1);
    e.motion.facing = unit(rng) * 6.28;
    e.appearance.tex1 = dye(rng);
    e.appearance.tex2 = dye(rng);
    if (rng() % 3 == 0) e.motion.z = .4 * unit(rng);
  }
  if (made)
    for (auto i = first; i < id; ++i) made->push_back(i);
}
void particles(World& world, const Rig& rig, std::mt19937& rng, std::int32_t time, int count) {
  std::uniform_real_distribution<double> unit(0, 1);
  for (int i = 0; i < count; ++i) {
    const auto* d = rig.pools.thrown[rng() % rig.pools.thrown.size()];
    Particle p;
    p.motion = i % 3 == 0 ? ParticleMotion::thrown_sprite : ParticleMotion::linear;
    p.position = p.start = {40 + unit(rng) * 14, 40 + unit(rng) * 10};
    p.end = {p.start.x + unit(rng) * 3, p.start.y + unit(rng) * 3};
    p.born = time;
    p.lifetime = 4000;
    p.texture = &d->texture;
    p.size = 40 + static_cast<double>(rng() % 200);
    p.bottom_anchor = i % 2 == 0;
    p.z = unit(rng) * .5;
    (void)world.particles().emit(p);
  }
}
// Reads a region of one atlas layer back through a framebuffer layer (glGetTexImage of the whole array would copy over
// 100 MB).
std::vector<unsigned char> read_region(const Atlas& atlas, const SpriteRegion& region) {
  static GLuint fbo{};
  if (!fbo) glGenFramebuffers(1, &fbo);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
  glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, atlas.texture(), 0, region.page);
  std::vector<unsigned char> got(static_cast<std::size_t>(region.width) * static_cast<std::size_t>(region.height) * 4);
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  glReadPixels(region.x, region.y, region.width, region.height, GL_RGBA, GL_UNSIGNED_BYTE, got.data());
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  return got;
}
// Exact content of the dynamic layers: every live residency entry's region must hold its image, premultiplied as
// upload_dynamic does, byte for byte (read back through a framebuffer layer), and every atlas quad of the dynamic
// layers must sample inside a live region.
void verify_residency(const Scene& scene, const Rig& rig, const Atlas& atlas, const std::vector<Quad>& quads,
                      const std::vector<Quad>& overlay, const std::string& label) {
  const auto resident = scene.resident_images(&rig.appearances, atlas);
  check(resident.size() == scene.residency().entries(), label + ": every residency entry is reported");
  for (const auto& [region, image] : resident) {
    if (!image) {
      check(false, label + ": a resident entry lost its image");
      continue;
    }
    check(region.page >= atlas.dynamic_first() && region.width == image->width && region.height == image->height,
          label + ": region matches its image");
    const auto got = read_region(atlas, region);
    check(glGetError() == GL_NO_ERROR, label + ": readback");
    Image expected = *image;
    premultiply_flash_bitmap(expected);
    check(got == expected.pixels, label + ": resident texels equal the premultiplied image (page " +
                                      std::to_string(region.page) + " at " + std::to_string(region.x) + "," +
                                      std::to_string(region.y) + ")");
    ++content_checks;
    texel_checks += got.size() / 4;
  }
  const auto inside = [&](const Quad& q) {
    constexpr double eps = .01, scale = Atlas::page_size;
    const double x0 = q.uv[0] * scale, y0 = q.uv[1] * scale, x1 = x0 + q.uv[2] * scale, y1 = y0 + q.uv[3] * scale;
    for (const auto& r : resident)
      if (r.region.page == static_cast<int>(q.page) && x0 >= r.region.x - eps && y0 >= r.region.y - eps &&
          x1 <= r.region.x + r.region.width + eps && y1 <= r.region.y + r.region.height + eps)
        return true;
    return false;
  };
  for (const auto* list : {&quads, &overlay})
    for (const auto& q : *list) {
      if (q.texture != 0 || q.kind > 8.5F || static_cast<int>(q.page) < atlas.dynamic_first()) continue;
      check(inside(q), label + ": a dynamic-layer quad samples outside every live region (kind " +
                           std::to_string(static_cast<int>(q.kind)) + ")");
      ++quad_checks;
    }
}
struct Shot {
  Image image;
  std::size_t calls{}, standalone{};
  std::vector<Quad> stream;
};
// The overlay mirrors Main.cpp: floating status text through status_quad with status_surface as the fallback (dynamic)
// or alone (oracle).
Shot shoot(Scene& scene, bool dynamic, World& world, const Rig& rig, Atlas& atlas,
           const std::vector<StatusArt::Label>& texts, Vec2 at, double angle, double zoom, std::int32_t time) {
  Camera camera;
  camera.configure(at, angle, width, height, zoom);
  std::vector<Quad> quads;
  quads.reserve(8192);
  scene.build(world, camera, atlas, rig.animations, nullptr, nullptr, &rig.appearances, at, angle, zoom, width, height,
              time, quads);
  std::vector<Quad> overlay;
  std::size_t i = 0;
  static std::uint64_t serial{};
  const auto tag = "frame " + std::to_string(++serial);
  if (dynamic) verify_residency(scene, rig, atlas, quads, overlay, tag + " after build");
  for (const auto& label : texts) {
    const Vec2 origin{20.31 + 60 * static_cast<double>(i % 12), 40.27 + 30 * static_cast<double>(i / 12)};
    ++i;
    const double w = label.image->width / label.image->pixel_scale * zoom,
                 h = label.image->height / label.image->pixel_scale * zoom;
    const auto placed = dynamic ? scene.status_quad(label.image, atlas, origin, {w, 0}, {0, h}) : std::nullopt;
    std::shared_ptr<SurfaceTexture> surface;
    if (!placed) surface = scene.status_surface(label.image);
    auto quad = placed ? *placed : surface->quad(origin, {w, 0}, {0, h});
    quad.owner = surface;
    overlay.push_back(quad);
  }
  if (dynamic) verify_residency(scene, rig, atlas, quads, overlay, tag + " after status quads");
  Shot shot;
  for (const auto& q : quads) shot.standalone += q.texture != 0;
  for (const auto& q : overlay) shot.standalone += q.texture != 0;
  rig.renderer.draw(atlas, quads, width, height, width, overlay);
  shot.stream = quads;
  shot.stream.insert(shot.stream.end(), overlay.begin(), overlay.end());
  shot.calls = rig.renderer.draw_calls();
  shot.image = rig.renderer.capture(width, height);
  return shot;
}
// Nearest sampling picks a texel from the interpolated uv; when this pixel's center lies within `kTie` texels of a
// texel boundary of a bitmap quad covering it (computed in double from the quad), the two paths may legitimately choose
// different neighbours (the atlas uv carries about 1e-4 texel of float error). The pixel then blends a different texel
// under or over other layers, which no neighbouring pixel need reproduce.
constexpr double kTie = 2e-3;
bool tie_at(const std::vector<Quad>& stream, int x, int y) {
  for (const auto& q : stream) {
    if (q.kind < 5.5F || q.kind > 8.5F) continue;
    const double det = q.axis_x[0] * q.axis_y[1] - q.axis_x[1] * q.axis_y[0], dx = x + .5 - q.origin[0],
                 dy = y + .5 - q.origin[1];
    if (!det) continue;
    double s = (dx * q.axis_y[1] - dy * q.axis_y[0]) / det, t = (q.axis_x[0] * dy - q.axis_x[1] * dx) / det;
    if (s < 0 || s > 1 || t < 0 || t > 1) continue;
    if (q.kind > 7.5F) {
      const double px = s - .5, py = t - .5;
      s = px * q.uv_offset[0] + py * q.uv_offset[1] + .5;
      t = -px * q.uv_offset[1] + py * q.uv_offset[0] + .5;
    }
    const double tx = s * q.uv[2] * Atlas::page_size, ty = t * q.uv[3] * Atlas::page_size;
    if (std::abs(tx - std::round(tx)) < kTie || std::abs(ty - std::round(ty)) < kTie) return true;
  }
  return false;
}
// Both paths run the same shader on the same texels, but the atlas path samples a uv computed against a 2048 page
// (about 1e-4 texel of float error), clips by clamping instead of the [0,1] test, and cuts a projectile on the CPU. So
// a nearest-sampling tie or a rasterized quad corner can move an edge by one pixel, never anything else: every
// differing pixel must be edge-explained, i.e. equal to the other frame's value at one of its 8 neighbours. A wrong,
// stale or overwritten slot (verified exactly above) would leave unexplained pixels.
void compare(const Shot& a, const Shot& b, Totals& totals, const std::string& label) {
  static int printed_unexplained{};
  check(a.image.pixels.size() == b.image.pixels.size() && !a.image.pixels.empty(), label + ": captures");
  const auto at = [](const Image& image, int x, int y) {
    std::uint32_t v;
    std::memcpy(&v, image.pixels.data() + (static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)) * 4, 4);
    return v;
  };
  std::size_t differing{}, unexplained{};
  int worst{};
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) {
      if (at(a.image, x, y) == at(b.image, x, y)) continue;
      const std::size_t p = (static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)) * 4;
      int delta{};
      for (std::size_t c = 0; c < 4; ++c)
        delta = std::max(delta, std::abs(int(a.image.pixels[p + c]) - int(b.image.pixels[p + c])));
      ++differing;
      worst = std::max(worst, delta);
      totals.large += delta > 16;
      bool explained = false;
      for (int dy = -1; dy <= 1 && !explained; ++dy)
        for (int dx = -1; dx <= 1 && !explained; ++dx) {
          const int nx = x + dx, ny = y + dy;
          if ((!dx && !dy) || nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
          explained = at(a.image, x, y) == at(b.image, nx, ny) || at(b.image, x, y) == at(a.image, nx, ny);
        }
      if (explained) {
        ++totals.explained;
        continue;
      }
      if (tie_at(a.stream, x, y)) {
        ++totals.tied;
        continue;
      }
      ++unexplained;
      if (printed_unexplained++ < 12)
        std::cout << "  UNEXPLAINED " << label << " pixel (" << x << "," << y << ") dynamic " << int(a.image.pixels[p])
                  << "," << int(a.image.pixels[p + 1]) << "," << int(a.image.pixels[p + 2]) << " surface "
                  << int(b.image.pixels[p]) << "," << int(b.image.pixels[p + 1]) << "," << int(b.image.pixels[p + 2])
                  << "\n";
      if (printed_unexplained <= 3)
        for (const auto* shot : {&a, &b}) {
          std::cout << "    " << (shot == &a ? "dynamic" : "surface") << " quads covering the pixel, painter order:\n";
          for (std::size_t i = 0; i < shot->stream.size(); ++i) {
            const auto& q = shot->stream[i];
            if (q.kind > 8.5F) continue;
            double x0 = 1e30, x1 = -1e30, y0 = 1e30, y1 = -1e30;
            for (const double u : {0.0, 1.0})
              for (const double v : {0.0, 1.0}) {
                const double qx = q.origin[0] + u * q.axis_x[0] + v * q.axis_y[0],
                             qy = q.origin[1] + u * q.axis_x[1] + v * q.axis_y[1];
                x0 = std::min(x0, qx);
                x1 = std::max(x1, qx);
                y0 = std::min(y0, qy);
                y1 = std::max(y1, qy);
              }
            if (x + .5 < x0 || x + .5 > x1 || y + .5 < y0 || y + .5 > y1) continue;
            std::cout << "      #" << i << " kind " << q.kind << " page " << q.page << " texture " << q.texture
                      << " uv " << q.uv[0] * 2048 << "," << q.uv[1] * 2048 << "," << q.uv[2] * 2048 << ","
                      << q.uv[3] * 2048 << " box " << x0 << "," << y0 << ".." << x1 << "," << y1 << " mult "
                      << q.multiply[3];
            if (q.kind > 5.5F) {
              // Where this pixel center falls in the bitmap's own texels, and how far that is from a texel boundary (a
              // tie when ~0).
              const double det = q.axis_x[0] * q.axis_y[1] - q.axis_x[1] * q.axis_y[0], dx = x + .5 - q.origin[0],
                           dy = y + .5 - q.origin[1];
              double s = (dx * q.axis_y[1] - dy * q.axis_y[0]) / det, t = (q.axis_x[0] * dy - q.axis_x[1] * dx) / det;
              if (q.kind > 7.5F) {
                const double px = s - .5, py = t - .5;
                s = px * q.uv_offset[0] + py * q.uv_offset[1] + .5;
                t = -px * q.uv_offset[1] + py * q.uv_offset[0] + .5;
              }
              const double tx = s * q.uv[2] * 2048, ty = t * q.uv[3] * 2048;
              std::cout << " texel " << tx << "," << ty << " boundary distance " << std::abs(tx - std::round(tx)) << ","
                        << std::abs(ty - std::round(ty));
            }
            std::cout << "\n";
          }
        }
    }
  ++totals.frames;
  totals.differing += differing;
  totals.differing_frames += differing > 0;
  totals.worst = std::max(totals.worst, worst);
  totals.unexplained += unexplained;
  totals.calls_dynamic += a.calls;
  totals.calls_surface += b.calls;
  totals.standalone += a.standalone;
  check(unexplained == 0, label + ": " + std::to_string(unexplained) + " differing pixels are not edge-explained");
}
void roomy(World& world, Rig& rig, Atlas& atlas, const std::vector<StatusArt::Label>& texts, Totals& totals,
           Totals& unit_totals) {
  std::mt19937 rng(4401);
  Scene dynamic, surface;
  dynamic.set_status_art(&rig.status);
  surface.set_status_art(&rig.status);
  surface.set_dynamic_atlas(false);
  std::vector<std::int32_t> ids;
  populate(world, rig, rng, 60, 90, 70, 40, 400, &ids);
  struct View {
    Vec2 at;
    double angle, zoom;
  };
  const View views[]{{{47, 45}, 0, 1}, {{47, 45}, .7, 1.3717}, {{46, 44}, 2.3, 1}, {{48, 46}, 4.1, .8233}};
  std::int32_t time = 1000;
  for (int step = 0; step < 18; ++step) {
    time += 130;
    if (step % 6 == 0) particles(world, rig, rng, time, 24);
    world.particles().update(world, time);
    // Entities turn and flip conditions so animation frames, icons and rotations all vary.
    std::uniform_real_distribution<double> unit(0, 1);
    for (const auto id : ids) {
      auto* e = world.entity(id);
      if (e->projectile)
        e->motion.facing += .3;
      else if (rng() % 9 == 0)
        e->motion.facing = unit(rng) * 6.28;
      if (rng() % 11 == 0) e->combat.condition ^= 1U << (rng() % 22);
    }
    const auto& v = views[step % 4];
    const auto a = shoot(dynamic, true, world, rig, atlas, texts, v.at, v.angle, v.zoom, time);
    const auto b = shoot(surface, false, world, rig, atlas, texts, v.at, v.angle, v.zoom, time);
    compare(a, b, v.zoom == 1 ? unit_totals : totals, "roomy frame " + std::to_string(step));
  }
  check(dynamic.residency().entries() > 20 && dynamic.residency().failures() == 0 && dynamic.atlas_fallbacks() == 0,
        "roomy atlas: everything fits, no fallbacks");
  std::cout << "roomy: resident " << dynamic.residency().entries() << " (" << dynamic.residency().used_bytes() / 1024
            << " KiB), evictions " << dynamic.residency().evictions() << ", fallbacks " << dynamic.atlas_fallbacks()
            << "\n";
}
void churn(Rig& rig, Atlas& one_page, const std::vector<StatusArt::Label>& texts, Totals& totals,
           std::uint64_t window) {
  // A new dye on everything every few frames: the working set cycles through far more than 16 MiB, while a frame itself
  // mostly fits.
  std::mt19937 rng(9917);
  Scene dynamic, surface;
  dynamic.set_status_art(&rig.status);
  surface.set_status_art(&rig.status);
  surface.set_dynamic_atlas(false);
  dynamic.set_atlas_protect_frames(window);
  std::uint64_t fallback_frames{};
  std::int32_t time = 2000;
  for (int step = 0; step < 24; ++step) {
    time += 170;
    World world(rig.catalog);
    world.initialize(96, 96);
    // Every 6th frame is far too big for one page: the overflow must fall back, not evict what this frame already
    // queued.
    const bool flood = step % 6 == 5;
    populate(world, rig, rng, flood ? 90 : 30, flood ? 160 : 50, flood ? 120 : 40, flood ? 80 : 20, flood ? 440 : 200);
    if (step % 3 == 0) particles(world, rig, rng, time, 30);
    world.particles().update(world, time);
    const auto before = dynamic.atlas_fallbacks();
    const auto a = shoot(dynamic, true, world, rig, one_page, texts, {47, 45}, step * .4, kZooms[step % 3], time);
    const auto b = shoot(surface, false, world, rig, one_page, texts, {47, 45}, step * .4, kZooms[step % 3], time);
    compare(a, b, totals, "churn frame " + std::to_string(step));
    fallback_frames += dynamic.atlas_fallbacks() > before;
    // A world per frame changes the generation: GPU residency restarts with it. Evictions come within a frame and
    // across the cache.
  }
  check(dynamic.residency().evictions() > 0 && dynamic.atlas_fallbacks() > 0 && fallback_frames > 0 &&
            totals.standalone > 0,
        "churn: evictions happen and in-frame overflow falls back to surface quads");
  std::cout << "churn(1 page, window " << window << ", new world per frame): evictions "
            << dynamic.residency().evictions() << ", fallbacks " << dynamic.atlas_fallbacks() << " in "
            << fallback_frames << " frames, resident " << dynamic.residency().entries() << " ("
            << dynamic.residency().used_bytes() / 1024 << " KiB)\n";
}
// One World that persists, whose appearances keep changing: residency must survive generations and evict across frames.
void persistent_churn(World& world, Rig& rig, Atlas& one_page, const std::vector<StatusArt::Label>& texts,
                      Totals& totals, std::uint64_t window) {
  std::mt19937 rng(31337);
  Scene dynamic, surface;
  dynamic.set_status_art(&rig.status);
  surface.set_status_art(&rig.status);
  surface.set_dynamic_atlas(false);
  dynamic.set_atlas_protect_frames(window);
  std::int32_t time = 5000;
  std::vector<std::int32_t> ids;
  std::uint64_t reentered{};
  for (int step = 0; step < 40; ++step) {
    time += 90;
    // Rotate a slice of the population: remove some, add new ones with fresh dyes and names, keep the rest (re-entry
    // via cache hits).
    std::vector<std::int32_t> kept;
    for (const auto id : ids)
      if (rng() % 3 == 0)
        world.remove(id);
      else
        kept.push_back(id);
    ids = std::move(kept);
    populate(world, rig, rng, 14, 20, 12, 6, 500, &ids);
    world.particles().update(world, time);
    const auto a = shoot(dynamic, true, world, rig, one_page, texts, {47, 45}, step * .17, 1.5219, time);
    const auto b = shoot(surface, false, world, rig, one_page, texts, {47, 45}, step * .17, 1.5219, time);
    compare(a, b, totals, "persistent frame " + std::to_string(step));
    reentered += dynamic.residency().entries();
  }
  check(dynamic.residency().evictions() > 0 && dynamic.atlas_fallbacks() > 0,
        "persistent churn evicts and counts fallbacks");
  std::cout << "persistent churn(1 page, window " << window << "): evictions " << dynamic.residency().evictions()
            << ", fallbacks " << dynamic.atlas_fallbacks() << ", resident " << dynamic.residency().entries()
            << ", average resident " << reentered / 40 << "\n";
}
// Starts at one page with a cap of four and a churning population: the atlas must grow when the cache holds a recent
// working set, the static layers must come through every resize byte for byte, every frame must still match the oracle,
// and a map reset must shrink it back.
void growth_run(Rig& rig, Atlas& atlas, const std::vector<StatusArt::Label>& texts, Totals& totals) {
  std::mt19937 rng(2468);
  Scene dynamic, surface;
  dynamic.set_status_art(&rig.status);
  surface.set_status_art(&rig.status);
  surface.set_dynamic_atlas(false);
  dynamic.set_dynamic_atlas_pages(1, 4, 6);
  World world(rig.catalog);
  world.initialize(96, 96);
  const SpriteRegion statics[]{{0, 0, 0, 512, 512}, {atlas.dynamic_first() - 1, 0, 0, 512, 512}};
  std::vector<unsigned char> baseline[2];
  for (int i = 0; i < 2; ++i) baseline[i] = read_region(atlas, statics[i]);
  std::int32_t time = 8000;
  std::vector<std::int32_t> ids;
  int previous = atlas.dynamic_pages(), reset_at = 34;
  std::string sequence = std::to_string(previous);
  std::uint64_t static_checks{};
  for (int step = 0; step < 44; ++step) {
    time += 90;
    if (step == reset_at) {
      world.initialize(96, 96);
      ids.clear();
    }
    std::vector<std::int32_t> kept;
    for (const auto id : ids)
      if (rng() % 3 == 0)
        world.remove(id);
      else
        kept.push_back(id);
    ids = std::move(kept);
    if (step < reset_at)
      populate(world, rig, rng, 14, 20, 12, 6, 500, &ids);
    else
      populate(world, rig, rng, 4, 4, 3, 1, 100, &ids);
    world.particles().update(world, time);
    const auto a = shoot(dynamic, true, world, rig, atlas, texts, {47, 45}, step * .17, 1.5219, time);
    const auto b = shoot(surface, false, world, rig, atlas, texts, {47, 45}, step * .17, 1.5219, time);
    compare(a, b, totals, "growth frame " + std::to_string(step));
    if (atlas.dynamic_pages() != previous) {
      sequence += " -> " + std::to_string(atlas.dynamic_pages()) + " (frame " + std::to_string(step) + ")";
      previous = atlas.dynamic_pages();
    }
    check(dynamic.residency().pages() == atlas.dynamic_pages() && atlas.dynamic_pages() >= 1 &&
              atlas.dynamic_pages() <= 4,
          "residency and atlas agree on the page count, within the cap");
    for (int i = 0; i < 2; ++i) {
      check(read_region(atlas, statics[i]) == baseline[i], "static layers are byte-identical across resizes");
      ++static_checks;
    }
    if (step == reset_at)
      check(atlas.dynamic_pages() == 1 && dynamic.residency().pages() == 1,
            "a map reset shrinks the atlas back to its initial pages");
  }
  check(atlas.resizes() >= 2, "the atlas grew and shrank");
  std::cout << "growth(1..4 pages): " << sequence << ", resizes " << atlas.resizes() << ", static layer checks "
            << static_checks << ", evictions " << dynamic.residency().evictions() << ", fallbacks "
            << dynamic.atlas_fallbacks() << ", copy path "
            << (Atlas::hardware_copy() ? "glCopyImageSubData" : "framebuffer blit") << "\n";
}
}
int main() {
  try {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) throw std::runtime_error(SDL_GetError());
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    auto* window = SDL_CreateWindow("dynamic atlas", 0, 0, width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window) throw std::runtime_error(SDL_GetError());
    auto context = SDL_GL_CreateContext(window);
    if (!context) throw std::runtime_error(SDL_GetError());
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) throw std::runtime_error("GLEW");
    while (glGetError() != GL_NO_ERROR) {
    }
    Totals roomy_totals, unit_totals, churn_totals[2], persistent_totals[2], growth_totals;
    {
      Catalog catalog(flash::source_root);
      AnimationLibrary animations(flash::source_root);
      Appearances appearances(flash::source_root, animations);
      FontBook fonts;
      fonts.set_pixel_scale(1);
      StatusArt status(fonts, &appearances);
      Renderer renderer(16384);
      Rig rig{catalog, animations, appearances, status, renderer, make_pools(catalog)};
      Atlas roomy_atlas(flash::source_root, animations.images(), animations.images().size(), 2);
      check(roomy_atlas.dynamic_pages() == 2 && roomy_atlas.dynamic_first() + 2 == roomy_atlas.pages() &&
                roomy_atlas.dynamic_bytes() == 2u * 2048 * 2048 * 4,
            "atlas reports its dynamic layers");
      std::vector<StatusArt::Label> texts;
      for (const char* t : {"-12", "-340", "+25 EXP", "Level Up!", "Dazed", "Weak", "-7", "+1 Fame", "Berserk",
                            "Stunned", "Healing", "-99999"})
        texts.push_back(status.status_text(t, t[0] == '-' ? 0xff0000U : 0x00ff00U));
      World world(catalog);
      world.initialize(96, 96);
      roomy(world, rig, roomy_atlas, texts, roomy_totals, unit_totals);
      check(roomy_totals.standalone + unit_totals.standalone == 0,
            "roomy atlas: no standalone surface quad is left in the stream");
      check(roomy_totals.calls_dynamic + unit_totals.calls_dynamic <
                (roomy_totals.calls_surface + unit_totals.calls_surface) / 10,
            "the dynamic atlas needs a tenth of the draw calls");
      Atlas one_page(flash::source_root, animations.images(), animations.images().size(), 1);
      for (std::uint64_t frames = 1; frames <= 2; ++frames)
        churn(rig, one_page, texts, churn_totals[frames - 1], frames);
      for (std::uint64_t frames = 1; frames <= 2; ++frames) {
        World persistent(catalog);
        persistent.initialize(96, 96);
        persistent_churn(persistent, rig, one_page, texts, persistent_totals[frames - 1], frames);
      }
      Atlas growing(flash::source_root, animations.images(), animations.images().size(), 1);
      growth_run(rig, growing, texts, growth_totals);
      // The same growth through the framebuffer-blit copy (drivers without GL 4.3 / ARB_copy_image).
      Atlas::force_blit_copy = true;
      Atlas blitting(flash::source_root, animations.images(), animations.images().size(), 1);
      growth_run(rig, blitting, texts, growth_totals);
      Atlas::force_blit_copy = false;
      // No dynamic pages (or the oracle switch): the old path, byte for byte, with no residency at all.
      Atlas plain(flash::source_root, animations.images(), animations.images().size());
      Scene legacy;
      legacy.set_status_art(&status);
      std::mt19937 rng(77);
      populate(world, rig, rng, 4, 4, 4, 2, 200);
      std::vector<Quad> quads;
      Camera camera;
      camera.configure({47, 45}, 0, width, height, 1);
      legacy.build(world, camera, plain, animations, nullptr, nullptr, &appearances, {47, 45}, 0, 1, width, height,
                   9000, quads);
      check(legacy.residency().entries() == 0 && legacy.atlas_fallbacks() == 0 && !quads.empty(),
            "an atlas without dynamic pages uses no residency");
      bool standalone = false;
      for (const auto& q : quads) standalone |= q.texture != 0;
      check(standalone, "...and keeps the SurfaceTexture quads");
    }
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    const auto report = [](const char* name, const Totals& t) {
      std::cout << name << ": " << t.frames << " frames, " << t.differing_frames << " differ, " << t.differing
                << " pixels differ (worst channel delta " << t.worst << ", by >16: " << t.large << ", edge-explained "
                << t.explained << ", tie-explained " << t.tied << ", unexplained " << t.unexplained << "), draw calls "
                << t.calls_dynamic << " dynamic vs " << t.calls_surface << " surface\n";
    };
    report("roomy 1:1", unit_totals);
    report("roomy scaled", roomy_totals);
    report("churn window 1", churn_totals[0]);
    report("churn window 2", churn_totals[1]);
    report("persistent churn window 1", persistent_totals[0]);
    report("persistent churn window 2", persistent_totals[1]);
    report("growth", growth_totals);
    std::cout << "residency content: " << content_checks << " regions (" << texel_checks << " texels) read back equal, "
              << quad_checks << " dynamic-layer quads inside live regions\n";
    std::cout << "Dynamic atlas checks: " << checks << " passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Dynamic atlas test failed: " << error.what() << "\n";
    return 1;
  }
}
