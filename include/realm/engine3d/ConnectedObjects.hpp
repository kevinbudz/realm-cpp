#pragma once
#include "realm/objects/ObjectProperties.hpp"
#include "realm/stage3D/DrawTypes.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace realm {
class World;

enum class ConnectedShape : std::uint8_t { none, dot, short_line, elbow, line, tee, cross };

struct ObjectVertex {
  double x{}, y{}, z{}, u{}, v{};
};

// One Flash ObjectFace3D polygon. `normal_local` overrides the geometric normal only for finding the neighboring
// square, as ConnectedWall.addBit does; lighting always uses the first, second and last vertices.
struct ObjectFaceGeometry {
  std::vector<ObjectVertex> vertices;
  std::uint8_t material{};  // 0: object's texture, 1: ConnectedWall's pattern-specific top texture
  bool repeat{true};
  bool split_quad{};  // CaveWall's random two-triangle diagonal
  bool split_diagonal{};
  bool has_normal_local{};
  std::array<double, 3> normal_local{};
  // Camera-independent triangulation of `vertices` (indices into them), filled when the geometry is built. Empty means
  // project_object_faces triangulates on the fly, as for hand-built faces.
  std::vector<std::array<std::uint8_t, 3>> triangles;
};

struct ConnectedObjectGeometry {
  ConnectedShape shape{ConnectedShape::none};
  int rotation{};
  std::vector<ObjectFaceGeometry> faces;
};

// CONNECT_STAT stores four side bytes (N,E,S,W from most to least significant). Flash masks each byte to its low
// nibble and registers only the first rotation of each of six canonical patterns.
[[nodiscard]] ConnectedObjectGeometry connected_object_geometry(const ObjectDef& definition, std::uint32_t connect_stat,
                                                                int tile_x, int tile_y, std::uint32_t seed);
[[nodiscard]] ConnectedShape connected_shape(std::uint32_t connect_stat, int* rotation = nullptr);
[[nodiscard]] std::vector<ObjectFaceGeometry> stalagmite_geometry(std::uint32_t seed);
// Offline maps have no server CONNECT_STAT; infer the same N/E/S/W bytes from neighboring static objects.
[[nodiscard]] std::uint32_t connected_object_connections(const World& world, std::uint16_t type, int x, int y);

// Flash software-face projection, including affine texture mapping, backface culling and boundary masking.
[[nodiscard]] std::shared_ptr<const MeshDraw> project_object_faces(const std::vector<ObjectFaceGeometry>& faces,
                                                                   std::uint8_t material, SpriteRegion sprite,
                                                                   const Camera& camera, Vec2 position, double z,
                                                                   double rotation_degrees, const World* world,
                                                                   int tile_x, int tile_y, bool connected);

}
