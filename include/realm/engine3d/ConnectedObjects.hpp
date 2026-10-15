#pragma once
#include "realm/objects/ObjectProperties.hpp"
#include "realm/stage3D/DrawTypes.hpp"
#include "realm/stage3D/WallGeometry.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace realm {
class World;

enum class ConnectedShape : std::uint8_t { none, dot, short_line, elbow, line, tee, cross };

struct ObjectVertex {
  double x{};
  double y{};
  double z{};
  double u{};
  double v{};
};

// One Flash ObjectFace3D polygon. `normal_local` overrides the geometric normal only for finding
// the neighboring square, as ConnectedWall.addBit does; lighting always uses the first, second and
// last vertices.
struct ObjectFaceGeometry {
  std::vector<ObjectVertex> vertices;
  std::uint8_t material{};  // 0: object's texture, 1: ConnectedWall's pattern-specific top texture
  bool repeat{true};
  bool split_quad{};  // CaveWall's random two-triangle diagonal
  bool split_diagonal{};
  bool has_normal_local{};
  std::array<double, 3> normal_local{};
  // Camera-independent triangulation of `vertices` (indices into them), filled when the geometry is
  // built. Empty means project_object_faces triangulates on the fly, as for hand-built faces.
  std::vector<std::array<std::uint8_t, 3>> triangles;
};

struct ConnectedObjectGeometry {
  ConnectedShape shape{ConnectedShape::none};
  int rotation{};
  std::vector<ObjectFaceGeometry> faces;
};

// CONNECT_STAT stores four side bytes (N,E,S,W from most to least significant). Flash masks each
// byte to its low nibble and registers only the first rotation of each of six canonical patterns.
[[nodiscard]] ConnectedObjectGeometry connected_object_geometry(const ObjectDef& definition,
                                                                std::uint32_t connect_stat,
                                                                int tile_x, int tile_y,
                                                                std::uint32_t seed);
[[nodiscard]] ConnectedShape connected_shape(std::uint32_t connect_stat, int* rotation = nullptr);
[[nodiscard]] std::vector<ObjectFaceGeometry> stalagmite_geometry(std::uint32_t seed);
// Whether the boundary test makes faces next to tile (x, y) black: no tile, void (type 255), or a
// type without ground.
[[nodiscard]] bool connected_black_neighbor(const World& world, int x, int y);
// Bit `slot` = (dy + 1) * 3 + (dx + 1) is set where connected_black_neighbor holds for the tile at
// offset (dx, dy) from (tile_x, tile_y); slot 4 (the tile itself) is never set.
[[nodiscard]] std::uint32_t connected_black_mask(const World& world, int tile_x, int tile_y);
// Offline maps have no server CONNECT_STAT; infer the same N/E/S/W bytes from neighboring static
// objects.
[[nodiscard]] std::uint32_t connected_object_connections(const World& world, std::uint16_t type,
                                                         int x, int y);

// Flash software-face projection, including affine texture mapping, backface culling and boundary
// masking.
[[nodiscard]] std::shared_ptr<const MeshDraw> project_object_faces(
    const std::vector<ObjectFaceGeometry>& faces, std::uint8_t material, SpriteRegion sprite,
    const Camera& camera, Vec2 position, double z, double rotation_degrees, const World* world,
    int tile_x, int tile_y, bool connected);
// The same projection into a caller-owned mesh: its vectors are cleared and keep their capacity, so
// a reused mesh does not allocate. Returns false when nothing is emitted, leaving draw without
// vertices.
[[nodiscard]] bool project_object_faces_into(MeshDraw& draw,
                                             const std::vector<ObjectFaceGeometry>& faces,
                                             std::uint8_t material, SpriteRegion sprite,
                                             const Camera& camera, Vec2 position, double z,
                                             double rotation_degrees, const World* world,
                                             int tile_x, int tile_y, bool connected);

// Camera-independent wall triangles for GPU drawing (WallGpuVertex, WallGeometry.hpp): three
// corners per triangle. Appends the triangles of one material, with the same shading, neighbor
// slots, UVs and winding as project_object_faces: every triangle has the orientation of its
// polygon's first, second and last corners.
void build_wall_gpu_vertices(const std::vector<ObjectFaceGeometry>& faces, std::uint8_t material,
                             double rotation_degrees, std::vector<WallGpuVertex>& out);

}
