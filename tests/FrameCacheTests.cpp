#include "realm/minimap/MinimapGpu.hpp"
#include "realm/map/Scene.hpp"
#include "realm/platform/PlatformSdl.hpp"
#include "realm/hud/Hud.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* message) {
  if (!value) {
    throw std::runtime_error(message);
  }
}

void compare(const realm::Image& a, const realm::Image& b, double allowed, const char* message) {
  std::size_t different{};
  for (std::size_t i = 0; i < a.pixels.size(); i += 4) {
    bool differs = false;
    for (std::size_t c = 0; c < 3; ++c) {
      differs |= std::abs(int(a.pixels[i + c]) - int(b.pixels[i + c])) > 3;
    }
    different += differs;
  }
  const auto fraction =
      static_cast<double>(different) / (static_cast<double>(a.pixels.size()) / 4.0);
  std::cout << message << ": changed pixels=" << different << " fraction=" << fraction << '\n';
  check(fraction <= allowed, message);
}
}

int main() {
  try {
    using namespace realm;
    platform::WindowSession window;
    window.open("frame caches", 800, 600, true, 0);
    Catalog catalog(flash::source_root);
    AnimationLibrary animations(flash::source_root);
    World world(catalog);
    world.load_jm(std::filesystem::path(flash::source_root).parent_path() /
                  "realm-server/Resources/Worlds/Nexus/0.jm");
    const auto first = animations.images().size();
    Terrain terrain(world, flash::source_root, first, 0, true);
    auto images = animations.images();
    images.insert(images.end(), terrain.images().begin(), terrain.images().end());
    Atlas atlas(flash::source_root, images, first);
    Renderer renderer(32, true, 0), orphan(32, false);
    Scene reference, cached;
    cached.set_cached_terrain(true);
    Camera camera;
    auto scene_image = [&](Scene& scene, Vec2 position, double angle, double zoom, int time,
                           Terrain* alternate = nullptr) {
      auto& source = alternate ? *alternate : terrain;
      camera.configure(position, angle, 800, 600, zoom, true);
      std::vector<Quad> quads;
      scene.build(world, camera, atlas, animations, nullptr, &source, nullptr, position, angle,
                  zoom, 800, 600, time, quads);
      for (const auto slot : source.changes()) {
        atlas.update_additional(first + slot, source.images()[slot]);
      }
      renderer.draw(atlas, quads, 800, 600, 800);
      return renderer.capture(800, 600);
    };
    auto position = world.spawn_position();
    for (int i = 0; i < 3; ++i) {
      terrain.begin_frame();
      const double angle = i * .31, zoom = i == 2 ? 1.8 : 1;
      position.x += .13;
      position.y += .09;
      compare(scene_image(reference, position, angle, zoom, 1000 + i * 271),
              scene_image(cached, position, angle, zoom, 1000 + i * 271), .002,
              "terrain pan/rotate/zoom parity");
    }
    terrain.begin_frame();
    (void)scene_image(cached, position, .62, 1.8, 1900);
    const auto rebuilds = cached.terrain_rebuilds();
    terrain.begin_frame();
    position.x += .01;
    (void)scene_image(cached, position, .63, 1.8, 1901);
    check(cached.terrain_rebuilds() == rebuilds, "camera motion rebuilt terrain chunks");
    net::Update update;
    const int x = static_cast<int>(position.x), y = static_cast<int>(position.y);
    update.tiles.push_back({static_cast<std::int16_t>(x), static_cast<std::int16_t>(y),
                            catalog.ground("Grass")->type});
    world.apply(update);
    terrain.begin_frame();
    compare(scene_image(reference, position, .63, 1.8, 1902),
            scene_image(cached, position, .63, 1.8, 1902), .002, "terrain tile edit parity");
    check(cached.terrain_rebuilds() > rebuilds, "tile edit failed to dirty terrain");
    const auto neighbor_revision = world.terrain_chunk_revision(8, 8);
    update.tiles = {{7, 7, catalog.ground("Grass")->type}};
    world.apply(update);
    check(world.terrain_chunk_revision(8, 8) > neighbor_revision,
          "chunk-border edit missed neighbouring compositor");
    // Exercise both ground scrolling modes across a map transition, without rebuilding geometry for
    // animation time.
    for (int animation = 1; animation <= 2; ++animation) {
      const GroundDef* animated{};
      for (const auto& ground : catalog.grounds()) {
        if (ground.animate == animation && ground.texture.sheet >= 0) {
          animated = &ground;
          break;
        }
      }
      check(animated != nullptr, "animated ground fixture missing");
      world.initialize(32, 32);
      net::Update fill;
      for (int row = 0; row < 32; ++row) {
        for (int col = 0; col < 32; ++col) {
          fill.tiles.push_back(
              {static_cast<std::int16_t>(col), static_cast<std::int16_t>(row), animated->type});
        }
      }
      world.apply(fill);
      Terrain source(world, flash::source_root, first, 0, true);
      source.begin_frame();
      compare(scene_image(reference, {16.3, 16.2}, .23, 1, 1234, &source),
              scene_image(cached, {16.3, 16.2}, .23, 1, 1234, &source), .002,
              "animated ground/map transition parity");
      const auto built = cached.terrain_rebuilds();
      source.begin_frame();
      compare(scene_image(reference, {16.3, 16.2}, .23, 1, 1987, &source),
              scene_image(cached, {16.3, 16.2}, .23, 1, 1987, &source), .002,
              "animated ground shader clock parity");
      check(cached.terrain_rebuilds() == built, "animation time rebuilt terrain chunks");
    }

    // Grow, wrap and reuse the stream slots; every frame must match the GL 3.3 orphan path.
    for (int frame = 0; frame < 10; ++frame) {
      std::vector<Quad> quads;
      for (int i = 0; i < 128 + frame * 7; ++i) {
        Quad q;
        q.kind = 2;
        q.origin = {float((i * 17 + frame) % 790), float((i * 29) % 590)};
        q.axis_x = {10, 0};
        q.axis_y = {0, 10};
        q.multiply = {.2F, .8F, .4F, 1};
        quads.push_back(q);
      }
      renderer.draw(atlas, quads, 800, 600, 800);
      const auto a = renderer.capture(800, 600);
      orphan.draw(atlas, quads, 800, 600, 800);
      compare(a, orphan.capture(800, 600), 0, "stream growth/ring parity");
    }
    std::array<Quad, 1> flight;
    flight[0].kind = 2;
    flight[0].axis_x = {800, 0};
    flight[0].axis_y = {0, 600};
    for (int frame = 0; frame < 24; ++frame) {
      flight[0].multiply = {static_cast<float>(frame) / 24.F, .3F, .7F, 1};
      renderer.draw(atlas, flight, 800, 600, 800);
    }
    const auto in_flight = renderer.capture(800, 600);
    orphan.draw(atlas, flight, 800, 600, 800);
    compare(in_flight, orphan.capture(800, 600), 0,
            "stream reuse without readback synchronization");

    World mini_world(catalog);
    mini_world.initialize(100, 100);
    net::Update tiles;
    for (int row = 0; row < 100; ++row) {
      for (int col = 0; col < 100; ++col) {
        tiles.tiles.push_back({static_cast<std::int16_t>(col), static_cast<std::int16_t>(row),
                               catalog.ground("Grass")->type});
      }
    }
    mini_world.apply(tiles);
    const auto wizard = catalog.object("Wizard")->type;
    mini_world.add(-2, wizard, {50.5, 50.5});
    mini_world.set_player_id(-2);
    mini_world.add(3, wizard, {52.13, 48.19});
    Minimap cpu(catalog, animations, flash::source_root),
        gpu(catalog, animations, flash::source_root);
    MinimapGpu gpu_view;
    SurfaceTexture bitmap;
    auto minimap_image = [&](bool native, double angle, double scale) {
      const auto& player = *mini_world.entity(-2);
      std::array<Quad, 1> overlay;
      if (native) {
        cpu.set_pixel_scale(scale);
        cpu.update(mini_world, player, angle);
        bitmap.update(cpu.image());
        overlay[0] = bitmap.quad({8, 8}, {192 * scale, 0}, {0, 192 * scale});
      } else {
        overlay[0] = gpu_view.build(gpu, mini_world, player, angle, {8, 8}, scale);
      }
      renderer.draw(atlas, {}, 800, 600, 800, overlay);
      return renderer.capture(800, 600);
    };
    for (int i = 0; i < 3; ++i) {
      mini_world.goto_entity(-2, {50.5 + i * .17, 50.5 + i * .21});
      const double scale = i == 2 ? 1.8 : 1;
      compare(minimap_image(true, i * .41, scale), minimap_image(false, i * .41, scale), .001,
              "GPU minimap pan/rotate/HiDPI parity");
    }
    check(gpu.redraws() == 0, "GPU minimap rasterized CPU view");
    const auto colored = gpu.colored_rows();
    mini_world.goto_entity(-2, {1, 1});
    (void)minimap_image(false, 0, 1);
    check(gpu.colored_rows() == colored, "minimap panning recolored fog memory");
    cpu.zoom_out();
    gpu.zoom_out();
    compare(minimap_image(true, 0, 1), minimap_image(false, 0, 1), .001,
            "GPU minimap edge/zoom parity");
    mini_world.initialize(100, 100);
    mini_world.add(-2, wizard, {50, 50});
    mini_world.set_player_id(-2);
    compare(minimap_image(true, 0, 1), minimap_image(false, 0, 1), .001,
            "GPU minimap map reset parity");
    HudStateCache hud;
    auto& focus = *mini_world.entity(-2);
    (void)hud.player(mini_world, focus, nullptr, 100);
    const auto count = hud.rebuilds();
    (void)hud.player(mini_world, focus, nullptr, 101);
    check(hud.rebuilds() == count, "unchanged HUD state rebuilt each frame");
    focus.combat.hp = 42;
    mini_world.invalidate_hud();
    check(hud.player(mini_world, focus, nullptr, 102).bars[1].value == 42,
          "HUD mutation missed immediate invalidation");
    focus.combat.hp = 41;
    check(hud.player(mini_world, focus, nullptr, 110).bars[1].value == 41,
          "HUD fallback missed direct entity mutation");
    check_graphics_error();
    std::cout << "Frame caches passed (persistent=" << renderer.persistent_streaming() << ")\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
