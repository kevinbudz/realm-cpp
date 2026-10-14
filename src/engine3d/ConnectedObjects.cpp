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
  double x{}, y{}, z{};
};

class Random {
 public:
  explicit Random(std::uint32_t seed) : state_(seed ? seed : 0x6d2b79f5U) {}
  double unit() {
    // Small, stable substitute for Flash Math.random(): static geometry must not jitter between rendered frames.
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    return static_cast<double>(state_ >> 8) / 16777216.0;
  }

 private:
  std::uint32_t state_;
};

Point p(double x, double y, double z = 0) { return {x, y, z}; }
ObjectVertex vertex(Point point, double u, double v) { return {point.x, point.y, point.z, u, v}; }

std::uint32_t low_nibbles(std::uint32_t value) { return value & 0x0f0f0f0fU; }

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
  const double b, t, h;

  explicit Builder(const ObjectDef& d, ConnectedShape shape, int rotation)
      : def(d), b(d.wall_base_indent), t(d.wall_top_indent), h(d.wall_height) {
    result.shape = shape;
    result.rotation = rotation;
  }

  void quad(Point a, Point b0, Point c, Point d0, bool left_indent, bool right_indent) {
    const double delta = b - t;
    const double denominator = 1 - delta - (right_indent ? delta : 0);
    const double n = left_indent && std::abs(denominator) > 1e-12 ? -delta / denominator : 0;
    ObjectFaceGeometry face;
    face.material = 0;
    face.repeat = def.wall_texture_repeat;
    face.vertices = {vertex(a, 0, 0), vertex(b0, 1, 0), vertex(c, 1, 1), vertex(d0, n, 1)};
    result.faces.push_back(std::move(face));
  }

  void bit(Point a, Point b0, Point c, Point d0, std::array<double, 3> normal, bool indent) {
    ObjectFaceGeometry face;
    face.material = 0;
    face.repeat = def.wall_texture_repeat;
    face.has_normal_local = true;
    face.normal_local = normal;
    if (indent)
      face.vertices = {vertex(a, -0.5 + t, 0), vertex(b0, 0, 0), vertex(c, 0, 0), vertex(d0, -0.5 + b, 1)};
    else
      face.vertices = {vertex(a, 1, 0), vertex(b0, 1.5 - t, 0), vertex(c, 0, 0), vertex(d0, 1, 1)};
    result.faces.push_back(std::move(face));
  }

  void top(std::initializer_list<Point> points, std::array<double, 6> uv) {
    ObjectFaceGeometry face;
    face.material = 1;
    face.repeat = def.top_texture_repeat;
    std::size_t i = 0;
    for (const auto point : points) {
      const auto tex = i == 0                   ? std::pair{uv[0], uv[1]}
                       : i == 1                 ? std::pair{uv[2], uv[3]}
                       : i + 1 == points.size() ? std::pair{uv[4], uv[5]}
                                                : std::pair{0.0, 0.0};
      face.vertices.push_back(vertex(point, tex.first, tex.second));
      ++i;
    }
    result.faces.push_back(std::move(face));
  }

  void dot() {
    const Point v0 = p(-b, -b), v1 = p(b, -b), v2 = p(b, b), v3 = p(-b, b);
    const Point v4 = p(-t, -t, h), v5 = p(t, -t, h), v6 = p(t, t, h), v7 = p(-t, t, h);
    quad(v5, v4, v0, v1, true, true);
    quad(v6, v5, v1, v2, true, true);
    quad(v4, v7, v3, v0, true, true);
    quad(v7, v6, v2, v3, true, true);
    top({v4, v5, v6, v7}, {0.25, 0.25, 0.75, 0.25, 0.25, 0.75});
  }

  void short_line() {
    const Point v0 = p(-b, -0.5), v1 = p(b, -0.5), v2 = p(b, b), v3 = p(-b, b);
    const Point v4 = p(-t, -0.5, h), v5 = p(t, -0.5, h), v6 = p(t, t, h), v7 = p(-t, t, h);
    quad(v6, v5, v1, v2, true, false);
    quad(v4, v7, v3, v0, false, true);
    quad(v7, v6, v2, v3, true, true);
    top({v4, v5, v6, v7}, {0.25, 0, 0.75, 0, 0.25, 0.75});
  }

  void elbow() {
    const Point v0 = p(-b, -0.5), v1 = p(b, -0.5), v2 = p(b, -b), v3 = p(0.5, -b), v4 = p(0.5, b), v5 = p(-b, b);
    const Point v6 = p(-t, -0.5, h), v7 = p(t, -0.5, h), v8 = p(t, -t, h), v9 = p(0.5, -t, h);
    const Point va = p(0.5, t, h), vb = p(-t, t, h);
    bit(v8, v7, v1, v2, {1, -1, 0}, true);
    bit(v9, v8, v2, v3, {1, -1, 0}, false);
    quad(vb, va, v4, v5, true, false);
    quad(v6, vb, v5, v0, false, true);
    top({v6, v7, v8, v9, va, vb}, {0.25, 0, 0.75, 0, 0.25, 0.75});
  }

  void line() {
    const Point v0 = p(-b, -0.5), v1 = p(b, -0.5), v2 = p(b, 0.5), v3 = p(-b, 0.5);
    const Point v4 = p(-t, -0.5, h), v5 = p(t, -0.5, h), v6 = p(t, 0.5, h), v7 = p(-t, 0.5, h);
    quad(v6, v5, v1, v2, false, false);
    quad(v4, v7, v3, v0, false, false);
    top({v4, v5, v6, v7}, {0.25, 0, 0.75, 0, 0.25, 1});
  }

  void tee() {
    const Point v0 = p(-b, -0.5), v1 = p(b, -0.5), v2 = p(b, -b), v3 = p(0.5, -b), v4 = p(0.5, b);
    const Point v5 = p(-0.5, b), v6 = p(-0.5, -b), v7 = p(-b, -b);
    const Point v8 = p(-t, -0.5, h), v9 = p(t, -0.5, h), va = p(t, -t, h), vb = p(0.5, -t, h);
    const Point vc = p(0.5, t, h), vd = p(-0.5, t, h), ve = p(-0.5, -t, h), vf = p(-t, -t, h);
    bit(va, v9, v1, v2, {1, -1, 0}, true);
    bit(vb, va, v2, v3, {1, -1, 0}, false);
    quad(vd, vc, v4, v5, false, false);
    bit(vf, ve, v6, v7, {-1, -1, 0}, true);
    bit(v8, vf, v7, v0, {-1, -1, 0}, false);
    top({v8, v9, va, vb, vc, vd, ve, vf}, {0.25, 0, 0.75, 0, 0.25, 0.25});
  }

  void cross() {
    const Point vb0 = p(-b, -0.5), vb1 = p(b, -0.5), vb2 = p(b, -b), vb3 = p(0.5, -b), vb4 = p(0.5, b);
    const Point vb5 = p(b, b), vb6 = p(b, 0.5), vb7 = p(-b, 0.5), vb8 = p(-b, b), vb9 = p(-0.5, b);
    const Point vba = p(-0.5, -b), vbb = p(-b, -b);
    const Point vt0 = p(-t, -0.5, h), vt1 = p(t, -0.5, h), vt2 = p(t, -t, h), vt3 = p(0.5, -t, h);
    const Point vt4 = p(0.5, t, h), vt5 = p(t, t, h), vt6 = p(t, 0.5, h), vt7 = p(-t, 0.5, h);
    const Point vt8 = p(-t, t, h), vt9 = p(-0.5, t, h), vta = p(-0.5, -t, h), vtb = p(-t, -t, h);
    bit(vt2, vt1, vb1, vb2, {1, -1, 0}, true);
    bit(vt3, vt2, vb2, vb3, {1, -1, 0}, false);
    bit(vt5, vt4, vb4, vb5, {1, 1, 0}, true);
    bit(vt6, vt5, vb5, vb6, {1, 1, 0}, false);
    bit(vt8, vt7, vb7, vb8, {-1, 1, 0}, true);
    bit(vt9, vt8, vb8, vb9, {-1, 1, 0}, false);
    bit(vtb, vta, vba, vbb, {-1, -1, 0}, true);
    bit(vt0, vtb, vbb, vb0, {-1, -1, 0}, false);
    top({vt0, vt1, vt2, vt3, vt4, vt5, vt6, vt7, vt8, vt9, vta, vtb}, {0.25, 0, 0.75, 0, 0.25, 0.25});
  }
};

Point cave_vertex(int rotation, int tile_x, int tile_y, int side, int id) {
  int x = tile_x, y = tile_y;
  const int rside = (side + rotation) % 4;
  if (rside == 1)
    ++x;
  else if (rside == 2)
    ++y;
  const auto rem = [](int a, std::uint32_t ax, int b, std::uint32_t by) {
    // Flash's int products wrap to 32 bits before signed remainder. Keep the same behavior without C++ signed overflow.
    const auto wrapped =
        std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(a) * ax ^ static_cast<std::uint32_t>(b) * by);
    return wrapped % 35;
  };
  const int r = id == 0 || id == 3 ? 15 + rem(x, 1259U, y, 2957U) : 3 + rem(x, 2179U, y, 1237U);
  double v{}, h{};
  switch (id) {
    case 0:
      v = -r / 100.0;
      h = 0;
      break;
    case 1:
      v = -r / 100.0;
      h = 1;
      break;
    case 2:
      v = r / 100.0;
      h = 1;
      break;
    case 3:
      v = r / 100.0;
      h = 0;
      break;
    default:
      break;
  }
  switch (side) {
    case 0:
      return p(v, -0.5, h);
    case 1:
      return p(0.5, v, h);
    case 2:
      return p(v, 0.5, h);
    default:
      return p(-0.5, v, h);
  }
}

void cave_face(std::vector<ObjectFaceGeometry>& faces, std::uint32_t& rng, const std::array<double, 3>* normal,
               std::initializer_list<Point> points) {
  ObjectFaceGeometry face;
  face.material = 0;
  if (normal) {
    face.has_normal_local = true;
    face.normal_local = *normal;
  }
  for (const auto point : points) face.vertices.push_back(vertex(point, 0, 0));
  const auto n = face.vertices.size();
  if (n == 3) {
    face.vertices[0].u = 0;
    face.vertices[0].v = 0;
    face.vertices[1].u = 0;
    face.vertices[1].v = 1;
    face.vertices[2].u = 1;
    face.vertices[2].v = 1;
  } else if (n == 4) {
    face.vertices[0].u = 0;
    face.vertices[0].v = 0;
    face.vertices[1].u = 1;
    face.vertices[1].v = 0;
    face.vertices[2].u = 1;
    face.vertices[2].v = 1;
    face.vertices[3].u = 0;
    face.vertices[3].v = 1;
    face.split_quad = true;
    // Keep the branch identical to faceHelper's Math.random() < .5, but stable for this static map object.
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    face.split_diagonal = (rng & 0x80000000U) != 0;
  } else if (n == 5) {
    const std::array<std::pair<double, double>, 5> uv{{{.2, 0}, {.8, 0}, {1, .2}, {1, .8}, {0, .8}}};
    for (std::size_t i = 0; i < n; ++i) {
      face.vertices[i].u = uv[i].first;
      face.vertices[i].v = uv[i].second;
    }
  } else if (n == 6) {
    const std::array<std::pair<double, double>, 6> uv{{{0, 0}, {.2, 0}, {1, .2}, {1, .8}, {0, .8}, {0, .2}}};
    for (std::size_t i = 0; i < n; ++i) {
      face.vertices[i].u = uv[i].first;
      face.vertices[i].v = uv[i].second;
    }
  } else if (n == 8) {
    const std::array<std::pair<double, double>, 8> uv{
        {{0, 0}, {.2, 0}, {1, .2}, {1, .8}, {.8, 1}, {.2, 1}, {0, .8}, {0, .2}}};
    for (std::size_t i = 0; i < n; ++i) {
      face.vertices[i].u = uv[i].first;
      face.vertices[i].v = uv[i].second;
    }
  }
  faces.push_back(std::move(face));
}

std::vector<ObjectFaceGeometry> cave_geometry(ConnectedShape shape, int rotation, int x, int y, std::uint32_t seed) {
  std::vector<ObjectFaceGeometry> faces;
  Random random(seed);
  const auto rv = [&](double lo, double range) { return lo + random.unit() * range; };
  const std::array<double, 3> n0{-1, -1, 0}, n2{1, -1, 0}, n3{1, 0, 0}, n4{1, 1, 0}, n6{-1, 1, 0}, n7{-1, 0, 0};
  const auto gv = [&](int side, int id) { return cave_vertex(rotation, x, y, side, id); };
  switch (shape) {
    case ConnectedShape::dot: {
      const Point v0 = p(-rv(.25, .25), -rv(.25, .25)), v1 = p(rv(.25, .25), -rv(.25, .25));
      const Point v2 = p(rv(.25, .25), rv(.25, .25)), v3 = p(-rv(.25, .25), rv(.25, .25));
      const Point v4 = p(rv(-.25, .5), rv(-.25, .5), 1);
      cave_face(faces, seed, nullptr, {v4, v0, v1});
      cave_face(faces, seed, nullptr, {v4, v1, v2});
      cave_face(faces, seed, nullptr, {v4, v2, v3});
      cave_face(faces, seed, nullptr, {v4, v3, v0});
      break;
    }
    case ConnectedShape::short_line: {
      const Point v0 = gv(0, 0), v1 = gv(0, 3), v2 = p(rv(.25, .25), rv(.25, .25)), v3 = p(-rv(.25, .25), rv(.25, .25));
      const Point v4 = gv(0, 1), v5 = gv(0, 2), v6 = p(rv(0, .25), rv(0, .25), .5), v7 = p(-rv(0, .25), rv(0, .25), .5);
      cave_face(faces, seed, nullptr, {v4, v7, v3, v0});
      cave_face(faces, seed, nullptr, {v7, v6, v2, v3});
      cave_face(faces, seed, nullptr, {v6, v5, v1, v2});
      cave_face(faces, seed, nullptr, {v4, v5, v6, v7});
      break;
    }
    case ConnectedShape::elbow: {
      const Point v0 = gv(0, 0), v1 = gv(0, 3), v2 = gv(1, 0), v3 = gv(1, 3), v4 = p(-rv(0, .25), rv(0, .25));
      const Point v5 = gv(0, 1), v6 = gv(0, 2), v7 = gv(1, 1), v8 = gv(1, 2), v9 = p(rv(0, .25), -rv(0, .25), 1);
      cave_face(faces, seed, nullptr, {v5, v9, v4, v0});
      cave_face(faces, seed, nullptr, {v9, v8, v3, v4});
      cave_face(faces, seed, &n2, {v7, v6, v1, v2});
      cave_face(faces, seed, nullptr, {v5, v6, v7, v8, v9});
      break;
    }
    case ConnectedShape::line: {
      const Point v0 = gv(0, 0), v1 = gv(0, 3), v2 = gv(2, 3), v3 = gv(2, 0), v4 = gv(0, 1), v5 = gv(0, 2);
      const Point v6 = gv(2, 2), v7 = gv(2, 1);
      cave_face(faces, seed, &n7, {v4, v7, v3, v0});
      cave_face(faces, seed, &n3, {v6, v5, v1, v2});
      cave_face(faces, seed, nullptr, {v4, v5, v6, v7});
      break;
    }
    case ConnectedShape::tee: {
      const Point v0 = gv(0, 0), v1 = gv(0, 3), v2 = gv(1, 0), v3 = gv(1, 3), v4 = gv(3, 3), v5 = gv(3, 0);
      const Point v6 = gv(0, 1), v7 = gv(0, 2), v8 = gv(1, 1), v9 = gv(1, 2), va = gv(3, 2), vb = gv(3, 1);
      cave_face(faces, seed, &n2, {v8, v7, v1, v2});
      cave_face(faces, seed, nullptr, {va, v9, v3, v4});
      cave_face(faces, seed, &n0, {v6, vb, v5, v0});
      cave_face(faces, seed, nullptr, {v6, v7, v8, v9, va, vb});
      break;
    }
    case ConnectedShape::cross: {
      const Point v0 = gv(0, 0), v1 = gv(0, 3), v2 = gv(1, 0), v3 = gv(1, 3), v4 = gv(2, 3), v5 = gv(2, 0);
      const Point v6 = gv(3, 3), v7 = gv(3, 0), v8 = gv(0, 1), v9 = gv(0, 2), va = gv(1, 1), vb = gv(1, 2);
      const Point vc = gv(2, 2), vd = gv(2, 1), ve = gv(3, 2), vf = gv(3, 1);
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
  const auto pat = low_nibbles(connect_stat);
  for (const auto& candidate : patterns) {
    auto value = candidate.bits;
    for (int turn = 0; turn < 4; ++turn) {
      if (pat == value) {
        if (rotation) *rotation = turn;
        return candidate.shape;
      }
      value = (value >> 8U) | ((value & 0xffU) << 24U);
    }
  }
  if (rotation) *rotation = 0;
  return ConnectedShape::none;
}

ConnectedObjectGeometry connected_object_geometry(const ObjectDef& definition, std::uint32_t connect_stat, int tile_x,
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
  constexpr double bs = 3.14159265358979323846 / 6, cs = 3.14159265358979323846 / 3;
  Random random(seed);
  const double a1 = bs + cs * random.unit(), a2 = 2 * cs + bs + cs * random.unit(),
               a3 = 4 * cs + bs + cs * random.unit();
  const Point v0 = p(std::cos(a1) * .3, std::sin(a1) * .3), v1 = p(std::cos(a2) * .3, std::sin(a2) * .3),
              v2 = p(std::cos(a3) * .3, std::sin(a3) * .3), v3 = p(0, 0, .6 + .6 * random.unit());
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
  const std::size_t n = face.vertices.size();
  std::vector<Triangle> result;
  if (n < 3) return result;
  if (n == 3) {
    result.push_back({0, 1, 2});
    return result;
  }
  if (n == 4 && face.split_quad) {
    if (face.split_diagonal)
      result = {{{0, 1, 3}}, {{1, 2, 3}}};
    else
      result = {{{0, 2, 3}}, {{0, 1, 2}}};
    return result;
  }
  double area{};
  for (std::size_t i = 0; i < n; ++i) {
    const auto& a = face.vertices[i];
    const auto& b = face.vertices[(i + 1) % n];
    area += a.x * b.y - b.x * a.y;
  }
  const double orientation = area >= 0 ? 1 : -1;
  std::vector<std::size_t> live(n);
  for (std::size_t i = 0; i < n; ++i) live[i] = i;
  const auto inside = [&](const ObjectVertex& p, const ObjectVertex& a, const ObjectVertex& b, const ObjectVertex& c) {
    constexpr double epsilon = 1e-10;
    return cross2(a, b, p) * orientation >= -epsilon && cross2(b, c, p) * orientation >= -epsilon &&
           cross2(c, a, p) * orientation >= -epsilon;
  };
  while (live.size() > 3) {
    bool clipped = false;
    for (std::size_t i = 0; i < live.size(); ++i) {
      const auto prev = live[(i + live.size() - 1) % live.size()], curr = live[i], next = live[(i + 1) % live.size()];
      const auto& a = face.vertices[prev];
      const auto& b = face.vertices[curr];
      const auto& c = face.vertices[next];
      if (cross2(a, b, c) * orientation <= 1e-10) continue;
      bool contains = false;
      for (const auto point : live)
        if (point != prev && point != curr && point != next && inside(face.vertices[point], a, b, c)) {
          contains = true;
          break;
        }
      if (contains) continue;
      result.push_back({prev, curr, next});
      live.erase(live.begin() + static_cast<std::ptrdiff_t>(i));
      clipped = true;
      break;
    }
    if (!clipped) {
      result.clear();
      for (std::size_t i = 1; i + 1 < n; ++i) result.push_back({0, i, i + 1});
      return result;
    }
  }
  result.push_back({live[0], live[1], live[2]});
  return result;
}
// Divide by sqrt(14) rather than multiply by its reciprocal: the division keeps shading bit-identical.
const double sqrt14 = std::sqrt(14.0);
std::vector<std::array<std::uint8_t, 3>> compact_triangulation(const ObjectFaceGeometry& face) {
  std::vector<std::array<std::uint8_t, 3>> result;
  for (const auto& tri : triangulate_object_face(face))
    result.push_back({static_cast<std::uint8_t>(tri[0]), static_cast<std::uint8_t>(tri[1]),
                      static_cast<std::uint8_t>(tri[2])});
  return result;
}
void finalize_object_faces(std::vector<ObjectFaceGeometry>& faces) {
  // Connected faces have at most 12 vertices, so the uint8 indices never truncate.
  for (auto& face : faces) face.triangles = compact_triangulation(face);
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

std::shared_ptr<const MeshDraw> project_object_faces(const std::vector<ObjectFaceGeometry>& faces,
                                                     std::uint8_t material, SpriteRegion sprite, const Camera& camera,
                                                     Vec2 position, double z, double rotation_degrees,
                                                     const World* world, int tile_x, int tile_y, bool connected) {
  const double radians = rotation_degrees * std::numbers::pi / 180, cosine = std::cos(radians),
               sine = std::sin(radians);
  auto draw = std::make_shared<MeshDraw>();
  constexpr float inv_page = 1.0F / sprite_page_size;
  draw->bounds = {static_cast<float>(sprite.x) * inv_page, static_cast<float>(sprite.y) * inv_page,
                  static_cast<float>(sprite.width) * inv_page, static_cast<float>(sprite.height) * inv_page};
  draw->page = static_cast<float>(sprite.page);
  draw->color = {0, 0, 0};
  // An n-gon triangulates into n - 2 triangles, so the output size is known before any projection.
  std::size_t vertex_count = 0;
  for (const auto& face : faces)
    if (face.material == material && face.vertices.size() >= 3) vertex_count += 3 * (face.vertices.size() - 2);
  draw->vertices.reserve(vertex_count);
  // Connected faces have at most 12 vertices (a cross cap), so projected corners live on the stack.
  constexpr std::size_t stack_vertices = 16;
  std::array<Vec2, stack_vertices> screen_stack;
  std::array<std::array<double, 3>, stack_vertices> points_stack;
  std::vector<Vec2> screen_heap;
  std::vector<std::array<double, 3>> points_heap;
  // ObjectFace3D.draw: a polygon is culled, lit and boundary-tested from its first (f), second (s) and last (l)
  // vertices. Each triangle indexes the polygon's vertices and is textured from those three.
  const auto draw_polygon = [&](const ObjectFaceGeometry& face, const Vec2* screen, const std::array<double, 3>* points,
                                std::size_t f, std::size_t s, std::size_t l,
                                std::span<const std::array<std::uint8_t, 3>> triangles) {
    const auto& a = screen[f];
    const auto& b = screen[s];
    const auto& c = screen[l];
    const double ux = b.x - a.x, uy = b.y - a.y, vx = c.x - a.x, vy = c.y - a.y;
    const double determinant = ux * vy - uy * vx;
    // ObjectFace3D.draw rejects this face when its first/second/last projected points wind backwards.
    if (determinant <= 1e-12) return;
    const auto& pa = points[f];
    const auto& pb = points[s];
    const auto& pc = points[l];
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
    double neighbor_nx = nx, neighbor_ny = ny;
    if (face.has_normal_local) {
      neighbor_nx = face.normal_local[0] * cosine - face.normal_local[1] * sine;
      neighbor_ny = face.normal_local[0] * sine + face.normal_local[1] * cosine;
    }
    const int dx = neighbor_nx > .4 ? 1 : neighbor_nx < -.4 ? -1 : 0;
    const int dy = neighbor_ny > .4 ? 1 : neighbor_ny < -.4 ? -1 : 0;
    bool black_boundary = false;
    if (connected && (dx || dy) && world) {
      const auto* neighbor = world->tile(tile_x + dx, tile_y + dy);
      black_boundary = !neighbor || neighbor->type == 255 || !world->catalog().ground(neighbor->type);
    }
    const auto& uv0 = face.vertices[f];
    const auto& uv1 = face.vertices[s];
    const auto& uv2 = face.vertices[l];
    for (const auto& tri : triangles) {
      const auto first = static_cast<std::uint32_t>(draw->vertices.size());
      for (const auto index : tri) {
        // Software ObjectFace3D.tToS maps the entire polygon affinely from its first, second and last
        // projected vertices. Intermediate UVs in ConnectedWall caps are intentionally placeholders.
        const double pixel_dx = screen[index].x - a.x, pixel_dy = screen[index].y - a.y;
        const double alpha = (pixel_dx * vy - pixel_dy * vx) / determinant;
        const double beta = (ux * pixel_dy - uy * pixel_dx) / determinant;
        const double u = uv0.u + alpha * (uv1.u - uv0.u) + beta * (uv2.u - uv0.u);
        const double v = uv0.v + alpha * (uv1.v - uv0.v) + beta * (uv2.v - uv0.v);
        draw->vertices.push_back({static_cast<float>(screen[index].x), static_cast<float>(screen[index].y),
                                  static_cast<float>(u), static_cast<float>(v), static_cast<float>(shade)});
      }
      const bool solid = black_boundary;
      if (!draw->groups.empty() && draw->groups.back().solid == solid && draw->groups.back().repeat == face.repeat)
        draw->groups.back().count += 3;
      else
        draw->groups.push_back({solid, first, 3, face.repeat});
    }
  };
  for (const auto& face : faces) {
    const auto n = face.vertices.size();
    if (face.material != material || n < 3) continue;
    if (n > stack_vertices) {
      screen_heap.resize(n);
      points_heap.resize(n);
    }
    Vec2* screen = n > stack_vertices ? screen_heap.data() : screen_stack.data();
    std::array<double, 3>* points = n > stack_vertices ? points_heap.data() : points_stack.data();
    for (std::size_t i = 0; i < n; ++i) {
      const auto& v = face.vertices[i];
      const double x = v.x * cosine - v.y * sine, y = v.x * sine + v.y * cosine;
      points[i] = {position.x + x, position.y + y, z + v.z};
      screen[i] = camera.world_to_screen({position.x + x, position.y + y}, z + v.z);
    }
    // Hand-built faces carry no precomputed triangulation and triangulate here.
    std::vector<std::array<std::uint8_t, 3>> computed;
    if (face.triangles.empty()) computed = compact_triangulation(face);
    const auto& triangles = face.triangles.empty() ? computed : face.triangles;
    if (face.split_quad && n == 4) {
      // CaveWall.faceHelper creates independent ObjectFace3D triangles, each with its own culling and light. Each is
      // drawn in place from its three indices rather than copying the quad.
      for (const auto& tri : triangles)
        draw_polygon(face, screen, points, tri[0], tri[1], tri[2], std::span(&tri, 1));
    } else {
      draw_polygon(face, screen, points, 0, 1, n - 1, triangles);
    }
  }
  return draw->vertices.empty() ? nullptr : draw;
}

}
