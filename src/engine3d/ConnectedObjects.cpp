#include "realm/engine3d/ConnectedObjects.hpp"
#include "realm/map/World.hpp"
#include <algorithm>
#include <numbers>
#include <span>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <utility>

namespace realm {
namespace {
struct Point {
  double x{};
  double y{};
  double z{};
};

class Random {
 public:
  explicit Random(std::uint32_t seed) : state_(seed ? seed : 0x6d2b79f5U) {}

  double unit() {
    // Small, stable substitute for Flash Math.random(): static geometry must not jitter between
    // rendered frames.
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    return static_cast<double>(state_ >> 8) / 16777216.0;
  }

 private:
  std::uint32_t state_;
};

Point point(double x, double y, double z = 0) {
  return {x, y, z};
}

ObjectVertex vertex(Point position, double u, double v) {
  return {position.x, position.y, position.z, u, v};
}

std::uint32_t low_nibbles(std::uint32_t value) {
  return value & 0x0f0f0f0fU;
}

struct Pattern {
  std::uint32_t bits;
  ConnectedShape shape;
};

constexpr std::array<Pattern, 6> patterns{{
    {0x02020202U, ConnectedShape::dot},
    {0x01020202U, ConnectedShape::short_line},
    {0x01010202U, ConnectedShape::elbow},
    {0x01020102U, ConnectedShape::line},
    {0x01010201U, ConnectedShape::tee},
    {0x01010101U, ConnectedShape::cross},
}};

struct Builder {
  const ObjectDef& def;
  ConnectedObjectGeometry result;
  const double base;
  const double cap;
  const double height;

  explicit Builder(const ObjectDef& d, ConnectedShape shape, int rotation)
      : def(d), base(d.wall_base_indent), cap(d.wall_top_indent), height(d.wall_height) {
    result.shape = shape;
    result.rotation = rotation;
  }

  void quad(Point a, Point b0, Point c, Point d0, bool left_indent, bool right_indent) {
    const double delta = base - cap;
    const double denominator = 1 - delta - (right_indent ? delta : 0);
    const double offset = left_indent && std::abs(denominator) > 1e-12 ? -delta / denominator : 0;
    ObjectFaceGeometry face;
    face.material = 0;
    face.repeat = def.wall_texture_repeat;
    face.vertices = {vertex(a, 0, 0), vertex(b0, 1, 0), vertex(c, 1, 1), vertex(d0, offset, 1)};
    result.faces.push_back(std::move(face));
  }

  void bit(Point a, Point b0, Point c, Point d0, std::array<double, 3> normal, bool indent) {
    ObjectFaceGeometry face;
    face.material = 0;
    face.repeat = def.wall_texture_repeat;
    face.has_normal_local = true;
    face.normal_local = normal;
    if (indent) {
      face.vertices = {vertex(a, -0.5 + cap, 0), vertex(b0, 0, 0), vertex(c, 0, 0),
                       vertex(d0, -0.5 + base, 1)};
    } else {
      face.vertices = {vertex(a, 1, 0), vertex(b0, 1.5 - cap, 0), vertex(c, 0, 0),
                       vertex(d0, 1, 1)};
    }
    result.faces.push_back(std::move(face));
  }

  void top(std::initializer_list<Point> points, std::array<double, 6> uv) {
    ObjectFaceGeometry face;
    face.material = 1;
    face.repeat = def.top_texture_repeat;
    std::size_t i = 0;
    for (const auto corner : points) {
      const auto coords = i == 0                   ? std::pair{uv[0], uv[1]}
                          : i == 1                 ? std::pair{uv[2], uv[3]}
                          : i + 1 == points.size() ? std::pair{uv[4], uv[5]}
                                                   : std::pair{0.0, 0.0};
      face.vertices.push_back(vertex(corner, coords.first, coords.second));
      ++i;
    }
    result.faces.push_back(std::move(face));
  }

  void dot() {
    const Point v0 = point(-base, -base);
    const Point v1 = point(base, -base);
    const Point v2 = point(base, base);
    const Point v3 = point(-base, base);
    const Point v4 = point(-cap, -cap, height);
    const Point v5 = point(cap, -cap, height);
    const Point v6 = point(cap, cap, height);
    const Point v7 = point(-cap, cap, height);
    quad(v5, v4, v0, v1, true, true);
    quad(v6, v5, v1, v2, true, true);
    quad(v4, v7, v3, v0, true, true);
    quad(v7, v6, v2, v3, true, true);
    top({v4, v5, v6, v7}, {0.25, 0.25, 0.75, 0.25, 0.25, 0.75});
  }

  void short_line() {
    const Point v0 = point(-base, -0.5);
    const Point v1 = point(base, -0.5);
    const Point v2 = point(base, base);
    const Point v3 = point(-base, base);
    const Point v4 = point(-cap, -0.5, height);
    const Point v5 = point(cap, -0.5, height);
    const Point v6 = point(cap, cap, height);
    const Point v7 = point(-cap, cap, height);
    quad(v6, v5, v1, v2, true, false);
    quad(v4, v7, v3, v0, false, true);
    quad(v7, v6, v2, v3, true, true);
    top({v4, v5, v6, v7}, {0.25, 0, 0.75, 0, 0.25, 0.75});
  }

  void elbow() {
    const Point v0 = point(-base, -0.5);
    const Point v1 = point(base, -0.5);
    const Point v2 = point(base, -base);
    const Point v3 = point(0.5, -base);
    const Point v4 = point(0.5, base);
    const Point v5 = point(-base, base);
    const Point v6 = point(-cap, -0.5, height);
    const Point v7 = point(cap, -0.5, height);
    const Point v8 = point(cap, -cap, height);
    const Point v9 = point(0.5, -cap, height);
    const Point va = point(0.5, cap, height);
    const Point vb = point(-cap, cap, height);
    bit(v8, v7, v1, v2, {1, -1, 0}, true);
    bit(v9, v8, v2, v3, {1, -1, 0}, false);
    quad(vb, va, v4, v5, true, false);
    quad(v6, vb, v5, v0, false, true);
    top({v6, v7, v8, v9, va, vb}, {0.25, 0, 0.75, 0, 0.25, 0.75});
  }

  void line() {
    const Point v0 = point(-base, -0.5);
    const Point v1 = point(base, -0.5);
    const Point v2 = point(base, 0.5);
    const Point v3 = point(-base, 0.5);
    const Point v4 = point(-cap, -0.5, height);
    const Point v5 = point(cap, -0.5, height);
    const Point v6 = point(cap, 0.5, height);
    const Point v7 = point(-cap, 0.5, height);
    quad(v6, v5, v1, v2, false, false);
    quad(v4, v7, v3, v0, false, false);
    top({v4, v5, v6, v7}, {0.25, 0, 0.75, 0, 0.25, 1});
  }

  void tee() {
    const Point v0 = point(-base, -0.5);
    const Point v1 = point(base, -0.5);
    const Point v2 = point(base, -base);
    const Point v3 = point(0.5, -base);
    const Point v4 = point(0.5, base);
    const Point v5 = point(-0.5, base);
    const Point v6 = point(-0.5, -base);
    const Point v7 = point(-base, -base);
    const Point v8 = point(-cap, -0.5, height);
    const Point v9 = point(cap, -0.5, height);
    const Point va = point(cap, -cap, height);
    const Point vb = point(0.5, -cap, height);
    const Point vc = point(0.5, cap, height);
    const Point vd = point(-0.5, cap, height);
    const Point ve = point(-0.5, -cap, height);
    const Point vf = point(-cap, -cap, height);
    bit(va, v9, v1, v2, {1, -1, 0}, true);
    bit(vb, va, v2, v3, {1, -1, 0}, false);
    quad(vd, vc, v4, v5, false, false);
    bit(vf, ve, v6, v7, {-1, -1, 0}, true);
    bit(v8, vf, v7, v0, {-1, -1, 0}, false);
    top({v8, v9, va, vb, vc, vd, ve, vf}, {0.25, 0, 0.75, 0, 0.25, 0.25});
  }

  void cross() {
    const Point vb0 = point(-base, -0.5);
    const Point vb1 = point(base, -0.5);
    const Point vb2 = point(base, -base);
    const Point vb3 = point(0.5, -base);
    const Point vb4 = point(0.5, base);
    const Point vb5 = point(base, base);
    const Point vb6 = point(base, 0.5);
    const Point vb7 = point(-base, 0.5);
    const Point vb8 = point(-base, base);
    const Point vb9 = point(-0.5, base);
    const Point vba = point(-0.5, -base);
    const Point vbb = point(-base, -base);
    const Point vt0 = point(-cap, -0.5, height);
    const Point vt1 = point(cap, -0.5, height);
    const Point vt2 = point(cap, -cap, height);
    const Point vt3 = point(0.5, -cap, height);
    const Point vt4 = point(0.5, cap, height);
    const Point vt5 = point(cap, cap, height);
    const Point vt6 = point(cap, 0.5, height);
    const Point vt7 = point(-cap, 0.5, height);
    const Point vt8 = point(-cap, cap, height);
    const Point vt9 = point(-0.5, cap, height);
    const Point vta = point(-0.5, -cap, height);
    const Point vtb = point(-cap, -cap, height);
    bit(vt2, vt1, vb1, vb2, {1, -1, 0}, true);
    bit(vt3, vt2, vb2, vb3, {1, -1, 0}, false);
    bit(vt5, vt4, vb4, vb5, {1, 1, 0}, true);
    bit(vt6, vt5, vb5, vb6, {1, 1, 0}, false);
    bit(vt8, vt7, vb7, vb8, {-1, 1, 0}, true);
    bit(vt9, vt8, vb8, vb9, {-1, 1, 0}, false);
    bit(vtb, vta, vba, vbb, {-1, -1, 0}, true);
    bit(vt0, vtb, vbb, vb0, {-1, -1, 0}, false);
    top({vt0, vt1, vt2, vt3, vt4, vt5, vt6, vt7, vt8, vt9, vta, vtb},
        {0.25, 0, 0.75, 0, 0.25, 0.25});
  }
};

Point cave_vertex(int rotation, int tile_x, int tile_y, int side, int corner) {
  int x = tile_x;
  int y = tile_y;
  const int rotated = (side + rotation) % 4;
  if (rotated == 1) {
    ++x;
  } else if (rotated == 2) {
    ++y;
  }
  const auto remainder = [](int px, std::uint32_t x_factor, int py, std::uint32_t y_factor) {
    // Flash's int products wrap to 32 bits before signed remainder. Keep the same behavior without
    // C++ signed overflow.
    const auto wrapped = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(px) * x_factor ^
                                                     static_cast<std::uint32_t>(py) * y_factor);
    return wrapped % 35;
  };
  const int radius = corner == 0 || corner == 3 ? 15 + remainder(x, 1259U, y, 2957U)
                                                : 3 + remainder(x, 2179U, y, 1237U);
  double offset{};
  double height{};
  switch (corner) {
    case 0:
      offset = -radius / 100.0;
      height = 0;
      break;
    case 1:
      offset = -radius / 100.0;
      height = 1;
      break;
    case 2:
      offset = radius / 100.0;
      height = 1;
      break;
    case 3:
      offset = radius / 100.0;
      height = 0;
      break;
    default:
      break;
  }
  switch (side) {
    case 0:
      return point(offset, -0.5, height);
    case 1:
      return point(0.5, offset, height);
    case 2:
      return point(offset, 0.5, height);
    default:
      return point(-0.5, offset, height);
  }
}

void cave_face(std::vector<ObjectFaceGeometry>& faces, std::uint32_t& rng,
               const std::array<double, 3>* normal, std::initializer_list<Point> points) {
  ObjectFaceGeometry face;
  face.material = 0;
  if (normal) {
    face.has_normal_local = true;
    face.normal_local = *normal;
  }
  for (const auto corner : points) {
    face.vertices.push_back(vertex(corner, 0, 0));
  }
  const auto count = face.vertices.size();
  if (count == 3) {
    face.vertices[0].u = 0;
    face.vertices[0].v = 0;
    face.vertices[1].u = 0;
    face.vertices[1].v = 1;
    face.vertices[2].u = 1;
    face.vertices[2].v = 1;
  } else if (count == 4) {
    face.vertices[0].u = 0;
    face.vertices[0].v = 0;
    face.vertices[1].u = 1;
    face.vertices[1].v = 0;
    face.vertices[2].u = 1;
    face.vertices[2].v = 1;
    face.vertices[3].u = 0;
    face.vertices[3].v = 1;
    face.split_quad = true;
    // Keep the branch identical to faceHelper's Math.random() < .5, but stable for this static map
    // object.
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    face.split_diagonal = (rng & 0x80000000U) != 0;
  } else if (count == 5) {
    const std::array<std::pair<double, double>, 5> uv{
        {{.2, 0}, {.8, 0}, {1, .2}, {1, .8}, {0, .8}}};
    for (std::size_t i = 0; i < count; ++i) {
      face.vertices[i].u = uv[i].first;
      face.vertices[i].v = uv[i].second;
    }
  } else if (count == 6) {
    const std::array<std::pair<double, double>, 6> uv{
        {{0, 0}, {.2, 0}, {1, .2}, {1, .8}, {0, .8}, {0, .2}}};
    for (std::size_t i = 0; i < count; ++i) {
      face.vertices[i].u = uv[i].first;
      face.vertices[i].v = uv[i].second;
    }
  } else if (count == 8) {
    const std::array<std::pair<double, double>, 8> uv{
        {{0, 0}, {.2, 0}, {1, .2}, {1, .8}, {.8, 1}, {.2, 1}, {0, .8}, {0, .2}}};
    for (std::size_t i = 0; i < count; ++i) {
      face.vertices[i].u = uv[i].first;
      face.vertices[i].v = uv[i].second;
    }
  }
  faces.push_back(std::move(face));
}

std::vector<ObjectFaceGeometry> cave_geometry(ConnectedShape shape, int rotation, int x, int y,
                                              std::uint32_t seed) {
  std::vector<ObjectFaceGeometry> faces;
  Random random(seed);
  const auto sample = [&](double lo, double range) { return lo + random.unit() * range; };
  const std::array<double, 3> n0{-1, -1, 0};
  const std::array<double, 3> n2{1, -1, 0};
  const std::array<double, 3> n3{1, 0, 0};
  const std::array<double, 3> n4{1, 1, 0};
  const std::array<double, 3> n6{-1, 1, 0};
  const std::array<double, 3> n7{-1, 0, 0};
  const auto corner = [&](int side, int index) { return cave_vertex(rotation, x, y, side, index); };
  switch (shape) {
    case ConnectedShape::dot: {
      const Point v0 = point(-sample(.25, .25), -sample(.25, .25));
      const Point v1 = point(sample(.25, .25), -sample(.25, .25));
      const Point v2 = point(sample(.25, .25), sample(.25, .25));
      const Point v3 = point(-sample(.25, .25), sample(.25, .25));
      const Point v4 = point(sample(-.25, .5), sample(-.25, .5), 1);
      cave_face(faces, seed, nullptr, {v4, v0, v1});
      cave_face(faces, seed, nullptr, {v4, v1, v2});
      cave_face(faces, seed, nullptr, {v4, v2, v3});
      cave_face(faces, seed, nullptr, {v4, v3, v0});
      break;
    }
    case ConnectedShape::short_line: {
      const Point v0 = corner(0, 0);
      const Point v1 = corner(0, 3);
      const Point v2 = point(sample(.25, .25), sample(.25, .25));
      const Point v3 = point(-sample(.25, .25), sample(.25, .25));
      const Point v4 = corner(0, 1);
      const Point v5 = corner(0, 2);
      const Point v6 = point(sample(0, .25), sample(0, .25), .5);
      const Point v7 = point(-sample(0, .25), sample(0, .25), .5);
      cave_face(faces, seed, nullptr, {v4, v7, v3, v0});
      cave_face(faces, seed, nullptr, {v7, v6, v2, v3});
      cave_face(faces, seed, nullptr, {v6, v5, v1, v2});
      cave_face(faces, seed, nullptr, {v4, v5, v6, v7});
      break;
    }
    case ConnectedShape::elbow: {
      const Point v0 = corner(0, 0);
      const Point v1 = corner(0, 3);
      const Point v2 = corner(1, 0);
      const Point v3 = corner(1, 3);
      const Point v4 = point(-sample(0, .25), sample(0, .25));
      const Point v5 = corner(0, 1);
      const Point v6 = corner(0, 2);
      const Point v7 = corner(1, 1);
      const Point v8 = corner(1, 2);
      const Point v9 = point(sample(0, .25), -sample(0, .25), 1);
      cave_face(faces, seed, nullptr, {v5, v9, v4, v0});
      cave_face(faces, seed, nullptr, {v9, v8, v3, v4});
      cave_face(faces, seed, &n2, {v7, v6, v1, v2});
      cave_face(faces, seed, nullptr, {v5, v6, v7, v8, v9});
      break;
    }
    case ConnectedShape::line: {
      const Point v0 = corner(0, 0);
      const Point v1 = corner(0, 3);
      const Point v2 = corner(2, 3);
      const Point v3 = corner(2, 0);
      const Point v4 = corner(0, 1);
      const Point v5 = corner(0, 2);
      const Point v6 = corner(2, 2);
      const Point v7 = corner(2, 1);
      cave_face(faces, seed, &n7, {v4, v7, v3, v0});
      cave_face(faces, seed, &n3, {v6, v5, v1, v2});
      cave_face(faces, seed, nullptr, {v4, v5, v6, v7});
      break;
    }
    case ConnectedShape::tee: {
      const Point v0 = corner(0, 0);
      const Point v1 = corner(0, 3);
      const Point v2 = corner(1, 0);
      const Point v3 = corner(1, 3);
      const Point v4 = corner(3, 3);
      const Point v5 = corner(3, 0);
      const Point v6 = corner(0, 1);
      const Point v7 = corner(0, 2);
      const Point v8 = corner(1, 1);
      const Point v9 = corner(1, 2);
      const Point va = corner(3, 2);
      const Point vb = corner(3, 1);
      cave_face(faces, seed, &n2, {v8, v7, v1, v2});
      cave_face(faces, seed, nullptr, {va, v9, v3, v4});
      cave_face(faces, seed, &n0, {v6, vb, v5, v0});
      cave_face(faces, seed, nullptr, {v6, v7, v8, v9, va, vb});
      break;
    }
    case ConnectedShape::cross: {
      const Point v0 = corner(0, 0);
      const Point v1 = corner(0, 3);
      const Point v2 = corner(1, 0);
      const Point v3 = corner(1, 3);
      const Point v4 = corner(2, 3);
      const Point v5 = corner(2, 0);
      const Point v6 = corner(3, 3);
      const Point v7 = corner(3, 0);
      const Point v8 = corner(0, 1);
      const Point v9 = corner(0, 2);
      const Point va = corner(1, 1);
      const Point vb = corner(1, 2);
      const Point vc = corner(2, 2);
      const Point vd = corner(2, 1);
      const Point ve = corner(3, 2);
      const Point vf = corner(3, 1);
      cave_face(faces, seed, &n2, {va, v9, v1, v2});
      cave_face(faces, seed, &n4, {vc, vb, v3, v4});
      cave_face(faces, seed, &n6, {ve, vd, v5, v6});
      cave_face(faces, seed, &n0, {v8, vf, v7, v0});
      cave_face(faces, seed, nullptr, {v8, v9, va, vb, vc, vd, ve, vf});
      break;
    }
    case ConnectedShape::none:
      break;
  }
  return faces;
}

void finalize_object_faces(std::vector<ObjectFaceGeometry>& faces);
}

ConnectedShape connected_shape(std::uint32_t connect_stat, int* rotation) {
  const auto pattern = low_nibbles(connect_stat);
  for (const auto& candidate : patterns) {
    auto bits = candidate.bits;
    for (int turn = 0; turn < 4; ++turn) {
      if (pattern == bits) {
        if (rotation) {
          *rotation = turn;
        }
        return candidate.shape;
      }
      bits = (bits >> 8U) | ((bits & 0xffU) << 24U);
    }
  }
  if (rotation) {
    *rotation = 0;
  }
  return ConnectedShape::none;
}

ConnectedObjectGeometry connected_object_geometry(const ObjectDef& definition,
                                                  std::uint32_t connect_stat, int tile_x,
                                                  int tile_y, std::uint32_t seed) {
  int rotation{};
  const auto shape = connected_shape(connect_stat, &rotation);
  if (definition.class_name == "CaveWall") {
    ConnectedObjectGeometry result;
    result.shape = shape;
    result.rotation = rotation;
    result.faces = cave_geometry(shape, rotation, tile_x, tile_y, seed);
    finalize_object_faces(result.faces);
    return result;
  }
  Builder builder(definition, shape, rotation);
  switch (shape) {
    case ConnectedShape::dot:
      builder.dot();
      break;
    case ConnectedShape::short_line:
      builder.short_line();
      break;
    case ConnectedShape::elbow:
      builder.elbow();
      break;
    case ConnectedShape::line:
      builder.line();
      break;
    case ConnectedShape::tee:
      builder.tee();
      break;
    case ConnectedShape::cross:
      builder.cross();
      break;
    case ConnectedShape::none:
      break;
  }
  builder.result.faces.shrink_to_fit();
  finalize_object_faces(builder.result.faces);
  return std::move(builder.result);
}

std::vector<ObjectFaceGeometry> stalagmite_geometry(std::uint32_t seed) {
  constexpr double base = 3.14159265358979323846 / 6;
  constexpr double step = 3.14159265358979323846 / 3;
  Random random(seed);
  const double first = base + step * random.unit();
  const double second = 2 * step + base + step * random.unit();
  const double third = 4 * step + base + step * random.unit();
  const Point v0 = point(std::cos(first) * .3, std::sin(first) * .3);
  const Point v1 = point(std::cos(second) * .3, std::sin(second) * .3);
  const Point v2 = point(std::cos(third) * .3, std::sin(third) * .3);
  const Point v3 = point(0, 0, .6 + .6 * random.unit());
  std::vector<ObjectFaceGeometry> faces(3);
  faces[0].vertices = {vertex(v0, 0, 1), vertex(v1, .5, 1), vertex(v3, .5, 0)};
  faces[1].vertices = {vertex(v1, .5, 1), vertex(v2, 1, 1), vertex(v3, .5, 0)};
  faces[2].vertices = {vertex(v2, 1, 1), vertex(v0, 0, 1), vertex(v3, .5, 0)};
  finalize_object_faces(faces);
  return faces;
}
}

namespace realm {
namespace {
using Triangle = std::array<std::size_t, 3>;

double cross2(const ObjectVertex& a, const ObjectVertex& b, const ObjectVertex& c) {
  return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

std::vector<Triangle> triangulate_object_face(const ObjectFaceGeometry& face) {
  const std::size_t count = face.vertices.size();
  std::vector<Triangle> result;
  if (count < 3) {
    return result;
  }
  if (count == 3) {
    result.push_back({0, 1, 2});
    return result;
  }
  if (count == 4 && face.split_quad) {
    if (face.split_diagonal) {
      result = {{{0, 1, 3}}, {{1, 2, 3}}};
    } else {
      result = {{{0, 2, 3}}, {{0, 1, 2}}};
    }
    return result;
  }
  double area{};
  for (std::size_t i = 0; i < count; ++i) {
    const auto& a = face.vertices[i];
    const auto& b = face.vertices[(i + 1) % count];
    area += a.x * b.y - b.x * a.y;
  }
  const double orientation = area >= 0 ? 1 : -1;
  std::vector<std::size_t> live(count);
  for (std::size_t i = 0; i < count; ++i) {
    live[i] = i;
  }
  const auto inside = [&](const ObjectVertex& p, const ObjectVertex& a, const ObjectVertex& b,
                          const ObjectVertex& c) {
    constexpr double epsilon = 1e-10;
    return cross2(a, b, p) * orientation >= -epsilon && cross2(b, c, p) * orientation >= -epsilon &&
           cross2(c, a, p) * orientation >= -epsilon;
  };
  while (live.size() > 3) {
    bool clipped = false;
    for (std::size_t i = 0; i < live.size(); ++i) {
      const auto prev = live[(i + live.size() - 1) % live.size()];
      const auto curr = live[i];
      const auto next = live[(i + 1) % live.size()];
      const auto& a = face.vertices[prev];
      const auto& b = face.vertices[curr];
      const auto& c = face.vertices[next];
      if (cross2(a, b, c) * orientation <= 1e-10) {
        continue;
      }
      bool contains = false;
      for (const auto point : live) {
        if (point != prev && point != curr && point != next &&
            inside(face.vertices[point], a, b, c)) {
          contains = true;
          break;
        }
      }
      if (contains) {
        continue;
      }
      result.push_back({prev, curr, next});
      live.erase(live.begin() + static_cast<std::ptrdiff_t>(i));
      clipped = true;
      break;
    }
    if (!clipped) {
      result.clear();
      for (std::size_t i = 1; i + 1 < count; ++i) {
        result.push_back({0, i, i + 1});
      }
      return result;
    }
  }
  result.push_back({live[0], live[1], live[2]});
  return result;
}

// Divide by sqrt(14) rather than multiply by its reciprocal: the division keeps shading
// bit-identical.
const double sqrt14 = std::sqrt(14.0);

std::vector<std::array<std::uint8_t, 3>> compact_triangulation(const ObjectFaceGeometry& face) {
  std::vector<std::array<std::uint8_t, 3>> result;
  for (const auto& tri : triangulate_object_face(face)) {
    result.push_back({static_cast<std::uint8_t>(tri[0]), static_cast<std::uint8_t>(tri[1]),
                      static_cast<std::uint8_t>(tri[2])});
  }
  return result;
}

void finalize_object_faces(std::vector<ObjectFaceGeometry>& faces) {
  // Connected faces have at most 12 vertices, so the uint8 indices never truncate.
  for (auto& face : faces) {
    face.triangles = compact_triangulation(face);
  }
}

}  // namespace

std::uint32_t connected_object_connections(const World& world, std::uint16_t type, int x, int y) {
  constexpr std::array<Vec2, 4> offsets{{{0, -1}, {1, 0}, {0, 1}, {-1, 0}}};
  std::uint32_t connections{};
  for (const auto offset : offsets) {
    const auto* tile = world.tile(x + static_cast<int>(offset.x), y + static_cast<int>(offset.y));
    const auto* neighbor = tile ? world.entity(tile->object_id) : nullptr;
    connections = (connections << 8U) | (neighbor && neighbor->type == type ? 1U : 2U);
  }
  return connections;
}

bool project_object_faces_into(MeshDraw& draw, const std::vector<ObjectFaceGeometry>& faces,
                               std::uint8_t material, SpriteRegion sprite, const Camera& camera,
                               Vec2 position, double z, double rotation_degrees, const World* world,
                               int tile_x, int tile_y, bool connected) {
  const double radians = rotation_degrees * std::numbers::pi / 180;
  const double cosine = std::cos(radians);
  const double sine = std::sin(radians);
  constexpr float inv_page = 1.0F / sprite_page_size;
  draw.bounds = {static_cast<float>(sprite.x) * inv_page, static_cast<float>(sprite.y) * inv_page,
                 static_cast<float>(sprite.width) * inv_page,
                 static_cast<float>(sprite.height) * inv_page};
  draw.page = static_cast<float>(sprite.page);
  draw.color = {0, 0, 0};
  draw.vertices.clear();
  draw.groups.clear();
  // An n-gon triangulates into n - 2 triangles, so the output size is known before any projection.
  std::size_t vertex_count = 0;
  for (const auto& face : faces) {
    if (face.material == material && face.vertices.size() >= 3) {
      vertex_count += 3 * (face.vertices.size() - 2);
    }
  }
  draw.vertices.reserve(vertex_count);
  // Connected faces have at most 12 vertices (a cross cap), so projected corners live on the stack.
  constexpr std::size_t stack_vertices = 16;
  std::array<Vec2, stack_vertices> screen_stack;
  std::array<std::array<double, 3>, stack_vertices> points_stack;
  std::vector<Vec2> screen_heap;
  std::vector<std::array<double, 3>> points_heap;
  // A neighbor's boundary state depends only on its (dx, dy) offset within this call: each of nine
  // is looked up once.
  enum class Edge : std::uint8_t { unknown, black, textured };
  std::array<Edge, 9> edges{};
  // ObjectFace3D.draw: a polygon is culled, lit and boundary-tested from its first, second and last
  // vertices. Each triangle indexes the polygon's vertices and is textured from those three.
  const auto draw_polygon = [&](const ObjectFaceGeometry& face, const Vec2* screen,
                                const std::array<double, 3>* points, std::size_t first,
                                std::size_t second, std::size_t last,
                                std::span<const std::array<std::uint8_t, 3>> triangles) {
    const auto& a = screen[first];
    const auto& b = screen[second];
    const auto& c = screen[last];
    const double ux = b.x - a.x;
    const double uy = b.y - a.y;
    const double vx = c.x - a.x;
    const double vy = c.y - a.y;
    const double determinant = ux * vy - uy * vx;
    // ObjectFace3D.draw rejects this face when its first/second/last projected points wind
    // backwards.
    if (determinant <= 1e-12) {
      return;
    }
    const auto& pa = points[first];
    const auto& pb = points[second];
    const auto& pc = points[last];
    double nx = (pb[1] - pa[1]) * (pc[2] - pa[2]) - (pb[2] - pa[2]) * (pc[1] - pa[1]);
    double ny = (pb[2] - pa[2]) * (pc[0] - pa[0]) - (pb[0] - pa[0]) * (pc[2] - pa[2]);
    double nz = (pb[0] - pa[0]) * (pc[1] - pa[1]) - (pb[1] - pa[1]) * (pc[0] - pa[0]);
    const double length = std::hypot(std::hypot(nx, ny), nz);
    if (length > 1e-12) {
      nx /= length;
      ny /= length;
      nz /= length;
    }
    const double shade = .75 + .25 * std::max(0.0, (nx + 3 * ny + 2 * nz) / sqrt14);
    double neighbor_nx = nx;
    double neighbor_ny = ny;
    if (face.has_normal_local) {
      neighbor_nx = face.normal_local[0] * cosine - face.normal_local[1] * sine;
      neighbor_ny = face.normal_local[0] * sine + face.normal_local[1] * cosine;
    }
    const int dx = neighbor_nx > .4 ? 1 : neighbor_nx < -.4 ? -1 : 0;
    const int dy = neighbor_ny > .4 ? 1 : neighbor_ny < -.4 ? -1 : 0;
    bool black_boundary = false;
    if (connected && (dx || dy) && world) {
      auto& edge = edges[static_cast<std::size_t>((dy + 1) * 3 + dx + 1)];
      if (edge == Edge::unknown) {
        edge = connected_black_neighbor(*world, tile_x + dx, tile_y + dy) ? Edge::black
                                                                          : Edge::textured;
      }
      black_boundary = edge == Edge::black;
    }
    const auto& uv0 = face.vertices[first];
    const auto& uv1 = face.vertices[second];
    const auto& uv2 = face.vertices[last];
    for (const auto& tri : triangles) {
      const auto start = static_cast<std::uint32_t>(draw.vertices.size());
      for (const auto index : tri) {
        // Software ObjectFace3D.tToS maps the entire polygon affinely from its first, second and
        // last projected vertices. Intermediate UVs in ConnectedWall caps are intentionally
        // placeholders.
        const double pixel_dx = screen[index].x - a.x;
        const double pixel_dy = screen[index].y - a.y;
        const double alpha = (pixel_dx * vy - pixel_dy * vx) / determinant;
        const double beta = (ux * pixel_dy - uy * pixel_dx) / determinant;
        const double u = uv0.u + alpha * (uv1.u - uv0.u) + beta * (uv2.u - uv0.u);
        const double v = uv0.v + alpha * (uv1.v - uv0.v) + beta * (uv2.v - uv0.v);
        draw.vertices.push_back({static_cast<float>(screen[index].x),
                                 static_cast<float>(screen[index].y), static_cast<float>(u),
                                 static_cast<float>(v), static_cast<float>(shade)});
      }
      const bool solid = black_boundary;
      if (!draw.groups.empty() && draw.groups.back().solid == solid &&
          draw.groups.back().repeat == face.repeat) {
        draw.groups.back().count += 3;
      } else {
        draw.groups.push_back({solid, start, 3, face.repeat});
      }
    }
  };
  for (const auto& face : faces) {
    const auto count = face.vertices.size();
    if (face.material != material || count < 3) {
      continue;
    }
    if (count > stack_vertices) {
      screen_heap.resize(count);
      points_heap.resize(count);
    }
    Vec2* screen = count > stack_vertices ? screen_heap.data() : screen_stack.data();
    std::array<double, 3>* points =
        count > stack_vertices ? points_heap.data() : points_stack.data();
    for (std::size_t i = 0; i < count; ++i) {
      const auto& v = face.vertices[i];
      const double x = v.x * cosine - v.y * sine;
      const double y = v.x * sine + v.y * cosine;
      points[i] = {position.x + x, position.y + y, z + v.z};
      screen[i] = camera.world_to_screen({position.x + x, position.y + y}, z + v.z);
    }
    // Hand-built faces carry no precomputed triangulation and triangulate here.
    std::vector<std::array<std::uint8_t, 3>> computed;
    if (face.triangles.empty()) {
      computed = compact_triangulation(face);
    }
    const auto& triangles = face.triangles.empty() ? computed : face.triangles;
    if (face.split_quad && count == 4) {
      // CaveWall.faceHelper creates independent ObjectFace3D triangles, each with its own culling
      // and light. Each is drawn in place from its three indices rather than copying the quad.
      for (const auto& tri : triangles) {
        draw_polygon(face, screen, points, tri[0], tri[1], tri[2], std::span(&tri, 1));
      }
    } else {
      draw_polygon(face, screen, points, 0, 1, count - 1, triangles);
    }
  }
  return !draw.vertices.empty();
}

bool connected_black_neighbor(const World& world, int x, int y) {
  const auto* neighbor = world.tile(x, y);
  return !neighbor || neighbor->type == 255 || !world.catalog().ground(neighbor->type);
}

std::uint32_t connected_black_mask(const World& world, int tile_x, int tile_y) {
  std::uint32_t mask{};
  for (int slot = 0; slot < 9; ++slot) {
    if (slot != 4 &&
        connected_black_neighbor(world, tile_x + slot % 3 - 1, tile_y + slot / 3 - 1)) {
      mask |= 1U << slot;
    }
  }
  return mask;
}

std::shared_ptr<const MeshDraw> project_object_faces(const std::vector<ObjectFaceGeometry>& faces,
                                                     std::uint8_t material, SpriteRegion sprite,
                                                     const Camera& camera, Vec2 position, double z,
                                                     double rotation_degrees, const World* world,
                                                     int tile_x, int tile_y, bool connected) {
  auto draw = std::make_shared<MeshDraw>();
  if (!project_object_faces_into(*draw, faces, material, sprite, camera, position, z,
                                 rotation_degrees, world, tile_x, tile_y, connected)) {
    return nullptr;
  }
  return draw;
}

void build_wall_gpu_vertices(const std::vector<ObjectFaceGeometry>& faces, std::uint8_t material,
                             double rotation_degrees, std::vector<WallGpuVertex>& out) {
  const double radians = rotation_degrees * std::numbers::pi / 180;
  const double cosine = std::cos(radians);
  const double sine = std::sin(radians);

  struct Point {
    double x, y, z;
  };

  std::vector<Point> points;
  std::vector<std::array<double, 2>> plane;
  std::vector<std::array<double, 2>>
      uvs;  // a polygon's corners in its own plane, and their affine UVs
  for (const auto& face : faces) {
    const auto count = face.vertices.size();
    if (face.material != material || count < 3) {
      continue;
    }
    points.clear();
    for (const auto& v : face.vertices) {
      points.push_back({v.x * cosine - v.y * sine, v.x * sine + v.y * cosine, v.z});
    }
    std::vector<std::array<std::uint8_t, 3>> computed;
    if (face.triangles.empty()) {
      computed = compact_triangulation(face);
    }
    const auto& triangles = face.triangles.empty() ? computed : face.triangles;
    // ObjectFace3D.draw for one polygon with its first, second and last corners, without a camera.
    // Screen projection is affine, so barycentric weights taken in the polygon's plane equal those
    // taken on screen.
    const auto polygon = [&](std::size_t first, std::size_t second, std::size_t last,
                             std::span<const std::array<std::uint8_t, 3>> polygon_triangles) {
      const auto& pa = points[first];
      const auto& pb = points[second];
      const auto& pc = points[last];
      double nx = (pb.y - pa.y) * (pc.z - pa.z) - (pb.z - pa.z) * (pc.y - pa.y);
      double ny = (pb.z - pa.z) * (pc.x - pa.x) - (pb.x - pa.x) * (pc.z - pa.z);
      double nz = (pb.x - pa.x) * (pc.y - pa.y) - (pb.y - pa.y) * (pc.x - pa.x);
      const double length = std::hypot(std::hypot(nx, ny), nz);
      if (length <= 1e-12) {
        return;
      }
      nx /= length;
      ny /= length;
      nz /= length;
      const double shade = .75 + .25 * std::max(0.0, (nx + 3 * ny + 2 * nz) / sqrt14);
      double neighbor_nx = nx;
      double neighbor_ny = ny;
      if (face.has_normal_local) {
        neighbor_nx = face.normal_local[0] * cosine - face.normal_local[1] * sine;
        neighbor_ny = face.normal_local[0] * sine + face.normal_local[1] * cosine;
      }
      const int dx = neighbor_nx > .4 ? 1 : neighbor_nx < -.4 ? -1 : 0;
      const int dy = neighbor_ny > .4 ? 1 : neighbor_ny < -.4 ? -1 : 0;
      const int slot = (dy + 1) * 3 + (dx + 1);
      const float flags = (face.repeat ? 1.F : 0.F) + 2.F * static_cast<float>(slot);
      // Plane basis: e1 follows the first-to-second edge and e2 = n x e1, so (e1, e2, n) is
      // right-handed.
      const double edge = std::hypot(std::hypot(pb.x - pa.x, pb.y - pa.y), pb.z - pa.z);
      const std::array<double, 3> e1{(pb.x - pa.x) / edge, (pb.y - pa.y) / edge,
                                     (pb.z - pa.z) / edge};
      const std::array<double, 3> e2{ny * e1[2] - nz * e1[1], nz * e1[0] - nx * e1[2],
                                     nx * e1[1] - ny * e1[0]};
      const auto to_plane = [&](const Point& corner) -> std::array<double, 2> {
        const double x = corner.x - pa.x;
        const double y = corner.y - pa.y;
        const double z = corner.z - pa.z;
        return {x * e1[0] + y * e1[1] + z * e1[2], x * e2[0] + y * e2[1] + z * e2[2]};
      };
      plane.resize(count);
      uvs.resize(count);
      for (std::size_t k = 0; k < count; ++k) {
        plane[k] = to_plane(points[k]);
      }
      // Plane coordinates of the first corner are exactly zero, and the first, second, last
      // orientation is positive by construction.
      const double ux = plane[second][0] - plane[first][0];
      const double uy = plane[second][1] - plane[first][1];
      const double vx = plane[last][0] - plane[first][0];
      const double vy = plane[last][1] - plane[first][1];
      const double determinant = ux * vy - uy * vx;
      const auto& uv0 = face.vertices[first];
      const auto& uv1 = face.vertices[second];
      const auto& uv2 = face.vertices[last];
      for (std::size_t k = 0; k < count; ++k) {
        const double pdx = plane[k][0] - plane[first][0];
        const double pdy = plane[k][1] - plane[first][1];
        const double alpha = (pdx * vy - pdy * vx) / determinant;
        const double beta = (ux * pdy - uy * pdx) / determinant;
        uvs[k] = {uv0.u + alpha * (uv1.u - uv0.u) + beta * (uv2.u - uv0.u),
                  uv0.v + alpha * (uv1.v - uv0.v) + beta * (uv2.v - uv0.v)};
      }
      for (const auto& tri : polygon_triangles) {
        std::size_t a = tri[0];
        std::size_t b = tri[1];
        std::size_t c = tri[2];
        const double orientation = (plane[b][0] - plane[a][0]) * (plane[c][1] - plane[a][1]) -
                                   (plane[b][1] - plane[a][1]) * (plane[c][0] - plane[a][0]);
        // A zero-area triangle (a repeated corner) covers no pixels from any camera, so it is not
        // uploaded.
        if (std::abs(orientation) <= 1e-12) {
          continue;
        }
        // Every triangle takes the polygon's orientation, so one screen-space winding test culls
        // the whole polygon.
        if (orientation * determinant < 0) {
          std::swap(b, c);
        }
        for (const auto index : {a, b, c}) {
          const auto& vertex = points[index];
          out.push_back({static_cast<float>(vertex.x), static_cast<float>(vertex.y),
                         static_cast<float>(vertex.z), static_cast<float>(uvs[index][0]),
                         static_cast<float>(uvs[index][1]), static_cast<float>(shade), flags, 0.F});
        }
      }
    };
    if (face.split_quad && count == 4) {
      // Each CaveWall triangle is its own polygon, as in project_object_faces.
      for (const auto& tri : triangles) {
        polygon(tri[0], tri[1], tri[2], std::span(&tri, 1));
      }
    } else {
      polygon(0, 1, count - 1, triangles);
    }
  }
}

}
