#include "realm/objects/Catalog.hpp"
#include "realm/engine3d/ConnectedObjects.hpp"
#include "realm/engine3d/Model3D.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
int checks{};

void check(bool ok, const char* why) {
  ++checks;
  if (!ok) {
    throw std::runtime_error(why);
  }
}
}

using namespace realm;

namespace {
Camera camera_at(double angle, double zoom = 1) {
  Camera c;
  c.configure({5.5, 5.5}, angle, 800, 600, zoom);
  return c;
}

const SpriteRegion sprite{0, 16, 32, 8, 8};

std::vector<float> shades(const MeshDraw& mesh) {
  std::vector<float> r;
  for (std::size_t i = 0; i < mesh.vertices.size(); i += 3) {
    r.push_back(mesh.vertices[i].shade);
  }
  std::sort(r.begin(), r.end());
  return r;
}
}

int main() {
  try {
    Catalog catalog(flash::source_root);
    // CONNECT_STAT's four bytes are N,E,S,W; ConnectedObject masks each byte to its low nibble and
    // takes the first registered rotation of each canonical pattern.
    constexpr std::array<std::pair<std::uint32_t, ConnectedShape>, 6> connected_patterns{
        {{0x02020202U, ConnectedShape::dot},
         {0x01020202U, ConnectedShape::short_line},
         {0x01010202U, ConnectedShape::elbow},
         {0x01020102U, ConnectedShape::line},
         {0x01010201U, ConnectedShape::tee},
         {0x01010101U, ConnectedShape::cross}}};
    const auto rotate_bytes = [](std::uint32_t value) {
      return (value >> 8U) | ((value & 0xffU) << 24U);
    };
    for (const auto& [pattern, shape] : connected_patterns) {
      auto value = pattern;
      for (int rotation = 0; rotation < 4; ++rotation) {
        auto first_value = pattern;
        int first_rotation = 0;
        while (first_value != value && first_rotation < 4) {
          first_value = rotate_bytes(first_value);
          ++first_rotation;
        }
        int actual_rotation = -1;
        check(connected_shape(value | 0xf0f0f0f0U, &actual_rotation) == shape &&
                  actual_rotation == first_rotation,
              "CONNECT_STAT low-nibble pattern and rotation");
        value = rotate_bytes(value);
      }
    }
    check(connected_shape(0) == ConnectedShape::none, "unsupported connection pattern is ignored");
    const auto* tomb_wall = catalog.object("Tomb Wall");
    check(tomb_wall && tomb_wall->class_name == "ConnectedWall" && !tomb_wall->wall_texture_repeat,
          "Catalog reads Tomb Wall repeat mode");
    if (tomb_wall) {
      for (std::size_t i = 0; i < tomb_wall->connected_textures.size(); ++i) {
        check(tomb_wall->connected_textures[i].sheet >= 0 &&
                  tomb_wall->connected_textures[i].index == 0x30 + static_cast<int>(i),
              "Catalog parses direct connected-cap File/Index nodes");
      }
    }
    const auto* bench = catalog.object("White Bench");
    check(bench && std::abs(bench->wall_base_indent - .25) < 1e-8 &&
              std::abs(bench->wall_top_indent - .25) < 1e-8 &&
              std::abs(bench->wall_height - .5) < 1e-8,
          "Catalog reads connected-wall extrusion dimensions");
    ObjectDef connected_def;
    connected_def.class_name = "ConnectedWall";
    ObjectDef cave_def;
    cave_def.class_name = "CaveWall";
    constexpr std::array<std::size_t, 6> connected_faces{5, 4, 5, 3, 6, 9};
    constexpr std::array<std::size_t, 6> cave_faces{4, 4, 4, 3, 4, 5};
    for (const auto& [pattern, shape] : connected_patterns) {
      const auto connected = connected_object_geometry(connected_def, pattern, 12, 9, 1234);
      check(connected.shape == shape &&
                connected.faces.size() == connected_faces[static_cast<std::size_t>(shape) - 1],
            "ConnectedWall builds the six Flash face layouts");
      check(connected.faces.back().material == 1, "ConnectedWall cap uses its pattern material");
      for (const auto& face : connected.faces) {
        for (const auto& vertex : face.vertices) {
          check(std::isfinite(vertex.x) && std::isfinite(vertex.y) && std::isfinite(vertex.z) &&
                    std::isfinite(vertex.u) && std::isfinite(vertex.v),
                "ConnectedWall geometry and UVs are finite");
        }
      }
      const auto cave = connected_object_geometry(cave_def, pattern, 12, 9, 1234);
      const auto cave_again = connected_object_geometry(cave_def, pattern, 12, 9, 1234);
      check(cave.shape == shape &&
                cave.faces.size() == cave_faces[static_cast<std::size_t>(shape) - 1],
            "CaveWall builds the six Flash face layouts");
      for (std::size_t i = 0; i < cave.faces.size(); ++i) {
        check(cave.faces[i].vertices.size() == cave_again.faces[i].vertices.size() &&
                  cave.faces[i].split_diagonal == cave_again.faces[i].split_diagonal,
              "CaveWall variation is stable for an object seed");
        if (cave.faces[i].split_quad) {
          check(cave.faces[i].vertices.size() == 4, "CaveWall quad keeps its random diagonal");
        }
      }
    }
    const auto stalagmite = stalagmite_geometry(99), stalagmite_again = stalagmite_geometry(99);
    check(stalagmite.size() == 3 && stalagmite[0].vertices.size() == 3 &&
              stalagmite[1].vertices.size() == 3 && stalagmite[2].vertices.size() == 3,
          "Stalagmite has its three textured triangular faces");
    check(stalagmite[0].vertices[0].x == stalagmite_again[0].vertices[0].x &&
              stalagmite[0].vertices[2].z >= .6 && stalagmite[0].vertices[2].z <= 1.2,
          "Stalagmite geometry is stable with the Flash height range");
    // Every <Model> in the registered object XML resolves to a registered OBJ.
    std::set<std::string> used;
    for (const auto& object : catalog.objects()) {
      if (!object.model.empty()) {
        check(has_model(object.model), ("unregistered model " + object.model).c_str());
        used.insert(object.model);
      }
    }
    check(used.size() >= 20, "object XML references the model registry");
    check(model_count() == 42, "all EmbeddedAssets.models_ entries compiled");
    for (std::size_t i = 0; i < model_count(); ++i) {
      check(model_triangle_count(model_name(i)) > 0, "model has triangles");
    }
    check(!has_model("Nonexistent") && project_model("Nonexistent", camera_at(0), {5.5, 5.5}, 0,
                                                     0xffffff, sprite) == nullptr,
          "unknown model");
    // Cube: 0..0.5 tall, one unit footprint. Orthographic gameplay camera shows the top and south
    // faces only.
    const auto camera = camera_at(0);
    auto cube = project_model("Cube", camera, {5.5, 5.5}, 0, 0xffffff, sprite);
    check(cube && cube->vertices.size() == 12 && cube->groups.size() == 1 && !cube->groups[0].solid,
          "cube shows two textured faces");
    const float top = static_cast<float>(.75 + .25 * 2 / std::sqrt(14.0)),
                south = static_cast<float>(.75 + .25 * 3 / std::sqrt(14.0));
    auto s = shades(*cube);
    check(std::abs(s.front() - top) < 1e-5 && std::abs(s.back() - south) < 1e-5,
          "cube flat shade values");
    float minx = 1e9F, maxx = -1e9F, miny = 1e9F, maxy = -1e9F;
    for (const auto& v : cube->vertices) {
      minx = std::min(minx, v.x);
      maxx = std::max(maxx, v.x);
      miny = std::min(miny, v.y);
      maxy = std::max(maxy, v.y);
    }
    check(std::abs((maxx - minx) - 50) < 1e-3 && std::abs((maxy - miny) - 75) < 1e-3,
          "cube screen extent is 50 x 75 pixels");
    const auto center = camera.world_to_screen({5.5, 5.5});
    check(std::abs((minx + maxx) / 2 - center.x) < 1e-3 && std::abs(maxy - (center.y + 25)) < 1e-3,
          "cube is centered on its tile");
    // Sprite-less objects and Solid* material groups use the flat color.
    auto bare = project_model("Cube", camera, {5.5, 5.5}, 0, 0x336699, std::nullopt);
    check(bare && bare->groups[0].solid && std::abs(bare->color[2] - 0x99 / 255.0F) < 1e-6,
          "sprite-less model is solid object color");
    bool mixed = false;
    for (std::size_t i = 0; i < model_count() && !mixed; ++i) {
      if (auto m = project_model(model_name(i), camera, {5.5, 5.5}, 0, 0xffffff, sprite)) {
        const bool first = m->groups[0].solid;
        for (const auto& g : m->groups) {
          if (g.solid != first) {
            mixed = true;
          }
        }
      }
    }
    check(mixed, "textured and Solid* groups coexist in at least one model");
    // Zoom scales every projected coordinate about the camera origin.
    auto zoomed = project_model("Cube", camera_at(0, 2), {5.5, 5.5}, 0, 0xffffff, sprite);
    check(zoomed && zoomed->vertices.size() == cube->vertices.size() &&
              std::abs(zoomed->vertices[0].x - cube->vertices[0].x) > 1,
          "zoom changes projection");
    // Rotating the object by theta is the same silhouette as rotating the camera by -theta.
    for (std::size_t i = 0; i < model_count(); ++i) {
      const auto name = model_name(i);
      for (const double theta : {30.0, 90.0, 215.0}) {
        auto turned = project_model(name, camera_at(0), {5.5, 5.5}, theta, 0xffffff, sprite);
        auto orbit = project_model(name, camera_at(-theta * 3.14159265358979323846 / 180),
                                   {5.5, 5.5}, 0, 0xffffff, sprite);
        check((turned == nullptr) == (orbit == nullptr), "rotation equivalence visibility");
        if (!turned) {
          continue;
        }
        check(turned->vertices.size() == orbit->vertices.size(),
              "rotation equivalence triangle count");
        for (std::size_t v = 0; v < turned->vertices.size(); ++v) {
          check(std::abs(turned->vertices[v].x - orbit->vertices[v].x) < .05F &&
                    std::abs(turned->vertices[v].y - orbit->vertices[v].y) < .05F,
                "rotation equivalence vertices");
        }
      }
    }
    // Sweep: every model, every camera angle stays finite, lit within the ambient range, and
    // bounded.
    for (std::size_t i = 0; i < model_count(); ++i) {
      for (int a = 0; a < 24; ++a) {
        auto m = project_model(model_name(i), camera_at(a * 3.14159265358979323846 / 12),
                               {5.5, 5.5}, a * 15.0, 0xffffff, sprite);
        if (!m) {
          continue;
        }
        check(m->vertices.size() % 3 == 0 && !m->groups.empty(), "whole triangles");
        std::uint32_t covered{};
        for (const auto& g : m->groups) {
          check(g.first == covered && g.count % 3 == 0 && g.count > 0, "contiguous groups");
          covered += g.count;
        }
        check(covered == m->vertices.size(), "groups cover vertices");
        for (const auto& v : m->vertices) {
          check(std::isfinite(v.x) && std::isfinite(v.y) && v.shade >= .75F - 1e-6F &&
                    v.shade <= 1 + 1e-6F,
                "finite lit vertices");
        }
      }
    }
    std::cout << "Model checks: " << checks << " passed, " << used.size()
              << " models in object XML\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Model test failed: " << e.what() << "\n";
    return 1;
  }
}
