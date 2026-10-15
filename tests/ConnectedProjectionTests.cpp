#include "realm/engine3d/ConnectedObjects.hpp"
#include "realm/map/World.hpp"
#include "realm/stage3D/WallGeometry.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
void check(bool value, const char* reason) {
  if (!value) {
    throw std::runtime_error(reason);
  }
}

using namespace realm;

// One triangle in screen space: corner positions, UVs, shade, and the solid and repeat flags of its
// group.
struct WallTriangle {
  std::array<Vec2, 3> screen{};
  std::array<double, 3> u{}, v{}, shade{};
  bool solid{}, repeat{};
};

struct WallTotals {
  double position{}, uv{}, shade{},
      planarity{};  // largest errors over matched triangles and checked faces
  std::size_t triangles{}, solid{}, faces{}, degenerate{};
  double degenerate_determinant{};  // largest |determinant| of an excluded zero-area triangle
  std::vector<std::string> failures;
};

struct CornerErrors {
  double position{}, uv{}, shade{};
};

// The GPU path on the CPU: build_wall_gpu_vertices output mapped by the camera's affine projection
// (as model_quad maps model vertices), culled per triangle by the screen determinant of
// project_object_faces, with solid taken from the neighbor mask bit of the slot each triangle
// names.
std::vector<WallTriangle> emulate_wall(const std::vector<WallGpuVertex>& vertices,
                                       const Camera& camera, Vec2 position, bool connected,
                                       std::uint32_t black_mask) {
  const auto origin = camera.world_to_screen(position);
  const auto step = [&](Vec2 p, double z) {
    const auto q = camera.world_to_screen(p, z);
    return Vec2{q.x - origin.x, q.y - origin.y};
  };
  const auto ex = step({position.x + 1, position.y}, 0), ey = step({position.x, position.y + 1}, 0),
             ez = step(position, 1);
  std::vector<WallTriangle> result;
  for (std::size_t i = 0; i + 3 <= vertices.size(); i += 3) {
    WallTriangle t;
    for (std::size_t k = 0; k < 3; ++k) {
      const auto& w = vertices[i + k];
      t.screen[k] = {origin.x + ex.x * w.x + ey.x * w.y + ez.x * w.z,
                     origin.y + ex.y * w.x + ey.y * w.y + ez.y * w.z};
      t.u[k] = w.u;
      t.v[k] = w.v;
      t.shade[k] = w.shade;
    }
    const auto& a = t.screen[0];
    const auto& b = t.screen[1];
    const auto& c = t.screen[2];
    if ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x) <= 1e-12) {
      continue;
    }
    const auto flags = static_cast<int>(vertices[i].flags);
    const int slot = flags >> 1;
    t.repeat = (flags & 1) != 0;
    t.solid = connected && slot != 4 && ((black_mask >> slot) & 1U) != 0;
    result.push_back(t);
  }
  return result;
}

std::vector<WallTriangle> triangles_of(const MeshDraw& mesh) {
  std::vector<WallTriangle> result;
  for (const auto& group : mesh.groups) {
    for (std::uint32_t i = group.first; i + 3 <= group.first + group.count; i += 3) {
      WallTriangle t;
      for (std::size_t k = 0; k < 3; ++k) {
        const auto& w = mesh.vertices[i + k];
        t.screen[k] = {w.x, w.y};
        t.u[k] = w.u;
        t.v[k] = w.v;
        t.shade[k] = w.shade;
      }
      t.solid = group.solid;
      t.repeat = group.repeat;
      result.push_back(t);
    }
  }
  return result;
}

double screen_determinant(const WallTriangle& t) {
  const auto& a = t.screen[0];
  const auto& b = t.screen[1];
  const auto& c = t.screen[2];
  return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

constexpr std::array<std::array<std::size_t, 3>, 6> corner_orders{
    {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}}};

CornerErrors corner_errors(const WallTriangle& a, const WallTriangle& b,
                           const std::array<std::size_t, 3>& order) {
  CornerErrors e;
  for (std::size_t k = 0; k < 3; ++k) {
    const auto m = order[k];
    e.position = std::max(e.position,
                          std::hypot(a.screen[k].x - b.screen[m].x, a.screen[k].y - b.screen[m].y));
    e.uv = std::max({e.uv, std::abs(a.u[k] - b.u[m]), std::abs(a.v[k] - b.v[m])});
    e.shade = std::max(e.shade, std::abs(a.shade[k] - b.shade[m]));
  }
  return e;
}

// Matches each expected triangle with a distinct actual triangle, corners in any order, within the
// tolerances (1e-3 px, 1e-4 uv, 1e-5 shade). Returns how many expected triangles stay unmatched.
std::size_t match_triangles(const std::vector<WallTriangle>& expected,
                            const std::vector<WallTriangle>& actual, WallTotals& totals) {
  std::vector<bool> used(actual.size());
  std::size_t unmatched = 0;
  for (const auto& e : expected) {
    std::size_t best_index = actual.size();
    double best_score = std::numeric_limits<double>::infinity();
    CornerErrors best;
    for (std::size_t j = 0; j < actual.size(); ++j) {
      if (used[j] || actual[j].solid != e.solid || actual[j].repeat != e.repeat) {
        continue;
      }
      for (const auto& order : corner_orders) {
        const auto errors = corner_errors(e, actual[j], order);
        const double score =
            std::max({errors.position / 1e-3, errors.uv / 1e-4, errors.shade / 1e-5});
        if (score < best_score) {
          best_score = score;
          best_index = j;
          best = errors;
        }
      }
    }
    if (best_index == actual.size() || best_score > 1) {
      ++unmatched;
      continue;
    }
    used[best_index] = true;
    totals.position = std::max(totals.position, best.position);
    totals.uv = std::max(totals.uv, best.uv);
    totals.shade = std::max(totals.shade, best.shade);
    if (e.solid) {
      ++totals.solid;
    }
  }
  return unmatched;
}

// Bit per neighbor slot (dy + 1) * 3 + (dx + 1), set where the boundary test makes the face black,
// as project_object_faces decides it.
std::uint32_t wall_black_mask(const World& world, int tile_x, int tile_y) {
  std::uint32_t mask{};
  for (int slot = 0; slot < 9; ++slot) {
    if (slot == 4) {
      continue;
    }
    const auto* neighbor = world.tile(tile_x + slot % 3 - 1, tile_y + slot / 3 - 1);
    if (!neighbor || neighbor->type == 255 || !world.catalog().ground(neighbor->type)) {
      mask |= 1U << slot;
    }
  }
  return mask;
}

// Every non-split face is planar: its corners lie within 1e-9 of the plane through its first,
// second and last corners. Each rotation is applied as the builder applies it, although rotation
// about z cannot change planarity.
void check_planar(const std::vector<ObjectFaceGeometry>& faces, const std::string& where,
                  WallTotals& totals) {
  for (const auto& face : faces) {
    const auto n = face.vertices.size();
    if (n < 3 || (face.split_quad && n == 4)) {
      continue;
    }
    ++totals.faces;
    for (const double degrees : {0.0, 90.0, 180.0, 270.0}) {
      const double radians = degrees * std::numbers::pi / 180, c = std::cos(radians),
                   s = std::sin(radians);
      std::vector<std::array<double, 3>> p;
      for (const auto& v : face.vertices) {
        p.push_back({v.x * c - v.y * s, v.x * s + v.y * c, v.z});
      }
      const auto& a = p[0];
      const auto& b = p[1];
      const auto& d = p[n - 1];
      std::array<double, 3> normal{(b[1] - a[1]) * (d[2] - a[2]) - (b[2] - a[2]) * (d[1] - a[1]),
                                   (b[2] - a[2]) * (d[0] - a[0]) - (b[0] - a[0]) * (d[2] - a[2]),
                                   (b[0] - a[0]) * (d[1] - a[1]) - (b[1] - a[1]) * (d[0] - a[0])};
      const double length = std::hypot(std::hypot(normal[0], normal[1]), normal[2]);
      if (length <= 1e-12) {
        totals.failures.push_back(where + ": degenerate face");
        continue;
      }
      for (auto& component : normal) {
        component /= length;
      }
      double deviation = 0;
      for (const auto& q : p) {
        deviation =
            std::max(deviation, std::abs(normal[0] * (q[0] - a[0]) + normal[1] * (q[1] - a[1]) +
                                         normal[2] * (q[2] - a[2])));
      }
      totals.planarity = std::max(totals.planarity, deviation);
      if (deviation > 1e-9) {
        totals.failures.push_back(where + ": non-planar face, deviation " +
                                  std::to_string(deviation));
      }
    }
  }
}

// Compares the software projection (both materials) with the emulated GPU path for one geometry
// over five camera angles, two zooms and four rotations. Triangle counts must agree exactly, which
// also checks culling.
void compare_geometry(const std::vector<ObjectFaceGeometry>& faces, const World& world,
                      bool connected, int tile_x, int tile_y, const std::string& label,
                      WallTotals& totals) {
  const Vec2 position{tile_x + .37, tile_y + .61};
  const SpriteRegion sprite{0, 0, 0, 8, 8};
  Camera camera;
  for (const double angle : {0.0, .3, 1.1, 2.5, 4.0}) {
    for (const double zoom : {1.0, 2.5}) {
      for (const double rotation : {0.0, 90.0, 180.0, 270.0}) {
        camera.configure(position, angle, 800, 600, zoom);
        const std::string where = label + " angle " + std::to_string(angle) + " zoom " +
                                  std::to_string(zoom) + " rotation " + std::to_string(rotation);
        for (const std::uint8_t material : {std::uint8_t{0}, std::uint8_t{1}}) {
          const auto mesh =
              project_object_faces(faces, material, sprite, camera, position, 0, rotation,
                                   connected ? &world : nullptr, tile_x, tile_y, connected);
          std::vector<WallGpuVertex> gpu;
          build_wall_gpu_vertices(faces, material, rotation, gpu);
          const auto emulated =
              emulate_wall(gpu, camera, position, connected,
                           connected ? wall_black_mask(world, tile_x, tile_y) : 0U);
          auto expected = mesh ? triangles_of(*mesh) : std::vector<WallTriangle>{};
          // A triangle with a repeated corner has zero area from every view. The software path
          // emits it (it culls whole polygons, not triangles); the GPU path never uploads it. These
          // are counted and excluded.
          std::erase_if(expected, [&](const WallTriangle& t) {
            const double determinant = std::abs(screen_determinant(t));
            if (determinant > 1e-12) {
              return false;
            }
            ++totals.degenerate;
            totals.degenerate_determinant = std::max(totals.degenerate_determinant, determinant);
            return true;
          });
          const auto tag = where + " material " + std::to_string(material);
          if (expected.size() != emulated.size()) {
            totals.failures.push_back(tag + ": triangle count " + std::to_string(expected.size()) +
                                      " vs " + std::to_string(emulated.size()));
            continue;
          }
          totals.triangles += expected.size();
          const auto unmatched = match_triangles(expected, emulated, totals);
          if (unmatched) {
            totals.failures.push_back(tag + ": " + std::to_string(unmatched) +
                                      " triangles unmatched");
          }
        }
      }
    }
  }
}
}  // namespace

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
    check(connected_object_connections(world, wall_type, 5, 5) == 0x01010202U,
          "offline neighboring walls form elbow");
    world.remove(12);
    check(connected_object_connections(world, wall_type, 5, 5) == 0x02010202U,
          "offline removals update wall connections");
    ObjectFaceGeometry top;
    top.vertices = {{0, 0, 1, 0, 0}, {1, 0, 1, 1, 0}, {1, 1, 1, 0, 0}, {0, 1, 1, 0, 1}};
    const auto mesh =
        project_object_faces({top}, 0, sprite, camera, {5.5, 5.5}, 0, 0, nullptr, 0, 0, false);
    check(mesh && mesh->vertices.size() == 6, "quad projects to two triangles");
    const auto corner = camera.world_to_screen({6.5, 6.5}, 1);
    bool mapped{};
    for (const auto& vertex : mesh->vertices) {
      if (std::abs(vertex.x - corner.x) < 1e-5 && std::abs(vertex.y - corner.y) < 1e-5) {
        check(std::abs(vertex.u - 1) < 1e-5 && std::abs(vertex.v - 1) < 1e-5,
              "intermediate placeholder UV is derived from first/second/last screen-space affine "
              "mapping");
        mapped = true;
      }
    }
    check(mapped, "mapped corner exists");
    top.repeat = false;
    top.vertices[0].u = -.5;
    top.vertices[1].u = 1.5;
    const auto clamp =
        project_object_faces({top}, 0, sprite, camera, {5.5, 5.5}, 0, 0, nullptr, 0, 0, false);
    check(clamp && !clamp->groups[0].repeat &&
              std::any_of(clamp->vertices.begin(), clamp->vertices.end(),
                          [](const auto& v) { return v.u < 0; }),
          "nonrepeating faces preserve affine UVs and clamp in fragment sampling");
    top.repeat = true;
    top.split_quad = true;
    top.split_diagonal = true;
    top.vertices[2].z = .6;
    const auto split =
        project_object_faces({top}, 0, sprite, camera, {5.5, 5.5}, 0, 0, nullptr, 0, 0, false);
    check(split && split->vertices.size() == 6 &&
              std::abs(split->vertices[0].shade - split->vertices[3].shade) > 1e-4,
          "cave quad split triangles compute their own lighting");
    ObjectFaceGeometry side;
    side.vertices = {{0, 0, 1, 0, 0}, {1, 0, 1, 1, 0}, {0, 0, 0, 0, 1}};
    const auto black =
        project_object_faces({side}, 0, sprite, camera, {5.5, 5.5}, 0, 0, &world, 5, 5, true);
    check(black && black->groups[0].solid && black->color[0] == 0,
          "missing adjacent ground makes boundary face black");
    world.tile(5, 6)->type = catalog.ground("Grass")->type;
    const auto textured =
        project_object_faces({side}, 0, sprite, camera, {5.5, 5.5}, 0, 0, &world, 5, 5, true);
    check(textured && !textured->groups[0].solid, "known adjacent ground restores face texture");
    std::reverse(side.vertices.begin(), side.vertices.end());
    check(!project_object_faces({side}, 0, sprite, camera, {5.5, 5.5}, 0, 0, nullptr, 0, 0, false),
          "back faces are culled");
    std::cout
        << "Connected face affine UV, lighting, boundary, repetition and culling checks passed\n";
    // The pooled variant must write exactly what the allocating one returns, through one MeshDraw
    // reused across angles, materials and emptiness, and must keep its storage.
    const auto wall = connected_object_geometry(
        *catalog.object("Tomb Wall"), connected_object_connections(world, wall_type, 5, 5), 5, 5,
        0x9e3779b9U);
    const double rotation = wall.rotation * 90.0;
    const auto same_group = [](const MeshGroup& a, const MeshGroup& b) {
      return a.solid == b.solid && a.first == b.first && a.count == b.count && a.repeat == b.repeat;
    };
    MeshDraw pooled;
    std::size_t compared[2]{};
    for (const double angle : {0.0, .31, 1.2, 2.9}) {
      camera.configure({5.5, 5.5}, angle, 800, 600);
      for (const std::uint8_t material : {std::uint8_t{0}, std::uint8_t{1}}) {
        const auto expected = project_object_faces(wall.faces, material, sprite, camera, {5.5, 5.5},
                                                   0, rotation, &world, 5, 5, true);
        const bool emitted = project_object_faces_into(pooled, wall.faces, material, sprite, camera,
                                                       {5.5, 5.5}, 0, rotation, &world, 5, 5, true);
        check(emitted == static_cast<bool>(expected),
              "pooled projection emits exactly when the allocating one does");
        if (!expected) {
          continue;
        }
        ++compared[material];
        check(pooled.vertices.size() == expected->vertices.size() &&
                  pooled.groups.size() == expected->groups.size() &&
                  pooled.bounds == expected->bounds && pooled.page == expected->page &&
                  pooled.color == expected->color,
              "pooled mesh header matches the allocating projection");
        check(std::memcmp(pooled.vertices.data(), expected->vertices.data(),
                          pooled.vertices.size() * sizeof(MeshVertex)) == 0,
              "pooled vertices are byte-identical to the allocating projection");
        check(std::equal(pooled.groups.begin(), pooled.groups.end(), expected->groups.begin(),
                         same_group),
              "pooled groups match the allocating projection");
      }
    }
    check(compared[0] > 0 && compared[1] > 0, "wall geometry emits both body and cap meshes");
    camera.configure({5.5, 5.5}, 0, 800, 600);
    check(project_object_faces_into(pooled, wall.faces, 0, sprite, camera, {5.5, 5.5}, 0, rotation,
                                    &world, 5, 5, true),
          "wall projects a body");
    const auto* storage = pooled.vertices.data();
    const auto capacity = pooled.vertices.capacity();
    check(project_object_faces_into(pooled, wall.faces, 0, sprite, camera, {5.5, 5.5}, 0, rotation,
                                    &world, 5, 5, true),
          "wall projects a body on reuse");
    check(pooled.vertices.data() == storage && pooled.vertices.capacity() == capacity,
          "reusing a mesh keeps its vertex storage");
    check(!project_object_faces_into(pooled, {side}, 0, sprite, camera, {5.5, 5.5}, 0, 0, nullptr,
                                     0, 0, false) &&
              pooled.vertices.empty(),
          "culled faces leave the reused mesh empty");
    std::cout << "Pooled projection matches the allocating projection\n";
    // GPU wall geometry: planar faces, and the emulated GPU path matches the software projection
    // for every wall shape, rotation, material, camera and boundary neighbor pattern. The field
    // mixes void (black), ground-less and grass tiles so boundary tests run, including at the world
    // edge.
    WallTotals totals;
    World field(catalog);
    field.initialize(16, 16);
    const auto grass = catalog.ground("Grass")->type;
    const auto no_ground = catalog.object("Tomb Wall")->type;
    check(!catalog.ground(no_ground),
          "an object type has no ground, so boundary tests see missing ground");
    const std::uint16_t void_tile = 255;
    for (int y = 0; y < 16; ++y) {
      for (int x = 0; x < 16; ++x) {
        const int pick = (x * 7 + y * 3) % 6;
        field.tile(x, y)->type = pick == 0 ? void_tile : pick == 1 ? no_ground : grass;
      }
    }
    std::array<bool, 7> shapes{};  // indexed by ConnectedShape
    const auto& tomb = *catalog.object("Tomb Wall");
    const auto& cave = *catalog.object("Cave Wall");
    for (std::uint32_t pattern = 0; pattern < 16; ++pattern) {
      // Side bytes N, E, S, W from most to least significant: 1 when connected, 2 when not.
      std::uint32_t stat = 0;
      for (int bit = 0; bit < 4; ++bit) {
        stat = (stat << 8U) | (((pattern >> bit) & 1U) ? 1U : 2U);
      }
      for (const auto [tx, ty] : {std::pair{5, 5}, std::pair{15, 0}}) {
        const auto geometry = connected_object_geometry(tomb, stat, tx, ty, 0x51f3U + pattern);
        shapes[static_cast<std::size_t>(geometry.shape)] = true;
        const auto where = "ConnectedWall pattern " + std::to_string(pattern) + " tile " +
                           std::to_string(tx) + "," + std::to_string(ty);
        check_planar(geometry.faces, where, totals);
        compare_geometry(geometry.faces, field, true, tx, ty, where, totals);
      }
      for (const std::uint32_t seed : {3U, 77U, 9001U}) {
        for (const auto [tx, ty] : {std::pair{5, 5}, std::pair{0, 11}, std::pair{15, 15}}) {
          const auto geometry = connected_object_geometry(cave, stat, tx, ty, seed);
          const auto where = "CaveWall pattern " + std::to_string(pattern) + " seed " +
                             std::to_string(seed) + " tile " + std::to_string(tx) + "," +
                             std::to_string(ty);
          check_planar(geometry.faces, where, totals);
          compare_geometry(geometry.faces, field, true, tx, ty, where, totals);
        }
      }
    }
    for (std::size_t shape = 1; shape < shapes.size(); ++shape) {
      check(shapes[shape], "the wall patterns cover all six connected shapes");
    }
    for (const std::uint32_t seed : {1U, 42U, 0x9e3779b9U, 123456U, 7U, 99U, 2024U, 5U}) {
      const auto faces = stalagmite_geometry(seed);
      const auto where = "Stalagmite seed " + std::to_string(seed);
      check_planar(faces, where, totals);
      compare_geometry(faces, field, false, 7, 9, where, totals);
    }
    // A triangulation may list a triangle clockwise relative to its polygon's first, second and
    // last corners. The software path still emits it; the GPU path must emit it with the polygon's
    // winding, not drop it.
    ObjectFaceGeometry clockwise;
    clockwise.vertices = {{0, 0, 0, 0, 0}, {1, 0, 0, 1, 0}, {1, 1, 0, 1, 1}, {0, 1, 0, 0, 1}};
    clockwise.triangles = {{0, 2, 1}, {0, 2, 3}};
    check_planar({clockwise}, "clockwise triangle", totals);
    compare_geometry({clockwise}, field, false, 7, 9, "clockwise triangle", totals);
    check(totals.triangles > 0 && totals.solid > 0,
          "the comparison covers solid and textured triangles");
    std::cout << "GPU wall triangles: compared=" << totals.triangles << " solid=" << totals.solid
              << " zero-area excluded=" << totals.degenerate << " (max |det| "
              << totals.degenerate_determinant << ")"
              << " planar faces=" << totals.faces << " max error: position=" << totals.position
              << "px uv=" << totals.uv << " shade=" << totals.shade
              << " planarity=" << totals.planarity << '\n';
    for (std::size_t i = 0; i < std::min<std::size_t>(totals.failures.size(), 10); ++i) {
      std::cerr << totals.failures[i] << '\n';
    }
    check(totals.failures.empty(), "GPU wall geometry matches the software projection");
    std::cout << "GPU wall geometry matches the software projection\n";
    // The geometry store: appends are contiguous within a generation, reset starts a new one, a
    // full store refuses, and an instance quad carries its range and mask.
    WallGeometryStore store;
    const auto first_generation = store.generation();
    const std::vector<WallGpuVertex> three(3);
    const auto first_range = store.append(three), second_range = store.append(three);
    check(first_range && second_range && first_range->first == 0 && first_range->count == 3 &&
              second_range->first == 3 && second_range->count == 3 &&
              first_range->generation == first_generation,
          "store appends contiguous ranges of its generation");
    store.reset();
    check(store.vertices().empty() && store.generation() != first_generation,
          "reset empties the store and starts a new generation");
    const WallGeometryStore other;
    check(other.generation() != store.generation() && other.generation() != first_generation,
          "generations are unique across stores and resets");
    const std::vector<WallGpuVertex> full(WallGeometryStore::capacity);
    check(store.append(full) && !store.append(three), "a full store refuses more corners");
    const auto instance = wall_quad(std::make_shared<WallGeometryStore>(), WallRange{4, 9, 1},
                                    camera, {5.5, 5.5}, sprite, 0x12U);
    check(instance.kind == 17 && instance.multiply[0] == 4 && instance.multiply[1] == 9 &&
              instance.multiply[2] == 0x12 && instance.owner,
          "instance quad carries its range and neighbor mask");
    std::cout << "Geometry store and instance quad checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
