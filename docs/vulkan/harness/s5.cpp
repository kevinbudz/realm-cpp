// Scratch harness (not part of the repo): one synthetic submission with wall instances (kind 17),
// prepared meshes (kind 9 with owner) and sprites, drawn by GL or Vulkan; writes raw RGBA pixels.
// Usage: s5 <gl|vulkan> <flash-root> <out.raw>
#include "realm/platform/PlatformSdl.hpp"
#include "realm/stage3D/Atlas.hpp"
#include "realm/stage3D/DrawTypes.hpp"
#include "realm/stage3D/GraphicsApi.hpp"
#include "realm/stage3D/Renderer.hpp"
#include "realm/stage3D/WallGeometry.hpp"
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
realm::gfx::WindowSurface window_surface(realm::platform::WindowSession& session) {
  return {[&session] { return session.vulkan_instance_proc(); },
          [&session] { return session.vulkan_instance_extensions(); },
          [&session](void* instance) { return session.vulkan_surface(instance); }};
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) {
    std::cerr << "usage: s5 <gl|vulkan> <flash-root> <out.raw>\n";
    return 2;
  }
  const std::string api_name = argv[1];
  const auto api = api_name == "vulkan" ? realm::gfx::Api::vulkan : realm::gfx::Api::opengl;
  constexpr int width = 480, height = 360;
  realm::platform::WindowSession session;
  session.open(api, "s5", width, height, true, 0);
  realm::gfx::select_api(api);
  realm::initialise_graphics(window_surface(session));

  {
  realm::Atlas atlas{std::filesystem::path(argv[2])};
  const auto sprite = atlas.image(0);
  // A wall store: triangles of a fan, some facing away (culled by the determinant test).
  auto store = std::make_shared<realm::WallGeometryStore>();
  std::vector<realm::WallGpuVertex> corners;
  for (int i = 0; i < 6; ++i) {
    const float a = static_cast<float>(i) * 1.0471976f;
    const float ca = std::cos(a), sa = std::sin(a);
    // Three corners in local units (x, y, z; u, v; shade; flags = repeat + 2 * slot; slot 4 = no neighbor).
    corners.push_back({0.0f, 0.0f, 0.0f, 0.1f, 0.1f, 1.0f, 8.0f, 0.0f});
    corners.push_back({ca * 3.0f, sa * 3.0f, 0.0f, 0.9f, 0.1f, 0.7f, 8.0f, 0.0f});
    corners.push_back({ca * 3.0f, sa * 3.0f, 2.0f + i * 0.3f, 0.9f, 0.9f, 0.5f, 9.0f, 0.0f});
  }
  const auto range = store->append(corners);
  if (!range) {
    std::cerr << "wall store full\n";
    return 1;
  }
  std::vector<realm::Quad> quads;
  // Sprites first, so the painter order mixes runs.
  quads.push_back(realm::sprite_quad(sprite, {40, 40}, {60, 0}, {0, 60}));
  // Wall instance: origin, axes, multiply = (first corner, corner count, black mask, unused).
  realm::Quad wall;
  wall.kind = 17;
  wall.origin = {200, 160};
  wall.axis_x = {12, 0};
  wall.axis_y = {0, 12};
  wall.uv_offset = {4, -2};
  wall.uv = {sprite.x / 2048.0f, sprite.y / 2048.0f, sprite.width / 2048.0f, sprite.height / 2048.0f};
  wall.page = static_cast<float>(sprite.page);
  wall.multiply = {static_cast<float>(range->first), static_cast<float>(range->count), 0, 0};
  wall.owner = store;
  quads.push_back(wall);
  quads.push_back(realm::sprite_quad(sprite, {300, 90}, {40, 0}, {0, 40}));
  // Prepared mesh: a triangle with a sprite on it and a solid group.
  auto mesh = std::make_shared<realm::MeshDraw>();
  mesh->vertices = {{300, 180, 0.0f, 0.0f, 1.0f}, {380, 200, 1.0f, 0.0f, 0.6f}, {330, 280, 0.0f, 1.0f, 0.8f},
                    {60, 300, 0.2f, 0.2f, 0.9f},  {120, 310, 0.8f, 0.2f, 0.7f}, {90, 340, 0.5f, 0.9f, 0.6f}};
  mesh->groups = {{false, 0, 3, true}, {true, 3, 3, false}};
  mesh->bounds = {sprite.x / 2048.0f, sprite.y / 2048.0f, sprite.width / 2048.0f, sprite.height / 2048.0f};
  mesh->page = static_cast<float>(sprite.page);
  mesh->color = {0.9f, 0.3f, 0.2f};
  quads.push_back(realm::mesh_quad(mesh));
  quads.push_back(realm::sprite_quad(sprite, {400, 300}, {30, 0}, {0, 30}));

  realm::Renderer renderer(64);
  renderer.clear_color({0.1f, 0.2f, 0.3f, 1.0f});
  renderer.draw(atlas, quads, width, height, width);
  const auto image = renderer.capture(width, height);
  renderer.finish_frame();
  std::ofstream out(argv[3], std::ios::binary);
  out.write(reinterpret_cast<const char*>(image.pixels.data()), static_cast<std::streamsize>(image.pixels.size()));
  std::cout << api_name << " wrote " << image.pixels.size() << " bytes, walls " << range->count << " corners\n";
  }
  realm::shutdown_graphics();
  return 0;
}
