#include "realm/engine3d/ConnectedObjects.hpp"
#include "realm/map/World.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
namespace {
void check(bool value, const char* reason) {
  if (!value) throw std::runtime_error(reason);
}
}
int main() {
  try {
    using namespace realm;
    Catalog catalog(flash::source_root);
    World world(catalog);
    world.initialize(12, 12);
    Camera camera;
    camera.configure({5.5, 5.5}, 0, 800, 600);
    const SpriteRegion sprite{0, 0, 0, 8, 8};
    const auto wall_type = catalog.object("Tomb Wall")->type;
    world.add(11, wall_type, {5.5, 5.5});
    check(connected_object_connections(world, wall_type, 5, 5) == 0x02020202U,
          "offline isolated wall uses dot pattern");
    world.add(12, wall_type, {5.5, 4.5});
    world.add(13, wall_type, {6.5, 5.5});
    check(connected_object_connections(world, wall_type, 5, 5) == 0x01010202U, "offline neighboring walls form elbow");
    world.remove(12);
    check(connected_object_connections(world, wall_type, 5, 5) == 0x02010202U,
          "offline removals update wall connections");
    ObjectFaceGeometry top;
    top.vertices = {{0, 0, 1, 0, 0}, {1, 0, 1, 1, 0}, {1, 1, 1, 0, 0}, {0, 1, 1, 0, 1}};
    const auto mesh = project_object_faces({top}, 0, sprite, camera, {5.5, 5.5}, 0, 0, nullptr, 0, 0, false);
    check(mesh && mesh->vertices.size() == 6, "quad projects to two triangles");
    const auto corner = camera.world_to_screen({6.5, 6.5}, 1);
    bool mapped{};
    for (const auto& vertex : mesh->vertices)
      if (std::abs(vertex.x - corner.x) < 1e-5 && std::abs(vertex.y - corner.y) < 1e-5) {
        check(std::abs(vertex.u - 1) < 1e-5 && std::abs(vertex.v - 1) < 1e-5,
              "intermediate placeholder UV is derived from first/second/last screen-space affine mapping");
        mapped = true;
      }
    check(mapped, "mapped corner exists");
    top.repeat = false;
    top.vertices[0].u = -.5;
    top.vertices[1].u = 1.5;
    const auto clamp = project_object_faces({top}, 0, sprite, camera, {5.5, 5.5}, 0, 0, nullptr, 0, 0, false);
    check(clamp && !clamp->groups[0].repeat &&
              std::any_of(clamp->vertices.begin(), clamp->vertices.end(), [](const auto& v) { return v.u < 0; }),
          "nonrepeating faces preserve affine UVs and clamp in fragment sampling");
    top.repeat = true;
    top.split_quad = true;
    top.split_diagonal = true;
    top.vertices[2].z = .6;
    const auto split = project_object_faces({top}, 0, sprite, camera, {5.5, 5.5}, 0, 0, nullptr, 0, 0, false);
    check(split && split->vertices.size() == 6 && std::abs(split->vertices[0].shade - split->vertices[3].shade) > 1e-4,
          "cave quad split triangles compute their own lighting");
    ObjectFaceGeometry side;
    side.vertices = {{0, 0, 1, 0, 0}, {1, 0, 1, 1, 0}, {0, 0, 0, 0, 1}};
    const auto black = project_object_faces({side}, 0, sprite, camera, {5.5, 5.5}, 0, 0, &world, 5, 5, true);
    check(black && black->groups[0].solid && black->color[0] == 0, "missing adjacent ground makes boundary face black");
    world.tile(5, 6)->type = catalog.ground("Grass")->type;
    const auto textured = project_object_faces({side}, 0, sprite, camera, {5.5, 5.5}, 0, 0, &world, 5, 5, true);
    check(textured && !textured->groups[0].solid, "known adjacent ground restores face texture");
    std::reverse(side.vertices.begin(), side.vertices.end());
    check(!project_object_faces({side}, 0, sprite, camera, {5.5, 5.5}, 0, 0, nullptr, 0, 0, false),
          "back faces are culled");
    std::cout << "Connected face affine UV, lighting, boundary, repetition and culling checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
