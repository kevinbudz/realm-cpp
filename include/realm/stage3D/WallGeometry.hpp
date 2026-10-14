#pragma once
#include "realm/map/Camera.hpp"
#include "realm/stage3D/DrawTypes.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace realm {
// One corner of a wall triangle as the GPU reads it (two RGBA32F texels). x, y, z are local to the object position and
// already rotated by its rotation. flags = repeat + 2 * slot, where slot = (dy + 1) * 3 + (dx + 1) names the boundary
// neighbor, and 4 means there is no neighbor test.
struct WallGpuVertex {
  float x, y, z, u, v, shade, flags, pad;
};
// A run of consecutive corners in a store (three per triangle), and the generation it was appended in.
struct WallRange {
  std::uint32_t first{}, count{};
  std::uint64_t generation{};
};
// Wall triangles resident in one arena. Appends never move earlier corners. reset() empties the arena and starts a new
// generation. Generations are unique across all stores and resets, so a GPU copy can tell that its contents went stale.
class WallGeometryStore {
 public:
  // Corners per store: 262144 corners, 87381 triangles.
  static constexpr std::size_t capacity = 262144;
  // `limit` is the corner capacity of this store. Scene always uses `capacity`; a smaller limit is a test oracle that
  // forces the software-mesh fallback once a frame outgrows it.
  explicit WallGeometryStore(std::size_t limit = capacity);
  // Appends the corners and returns their range, or nullopt when they would exceed the limit.
  [[nodiscard]] std::optional<WallRange> append(std::span<const WallGpuVertex> vertices);
  void reset();
  [[nodiscard]] const std::vector<WallGpuVertex>& vertices() const { return vertices_; }
  [[nodiscard]] std::uint64_t generation() const { return generation_; }
  [[nodiscard]] std::size_t limit() const { return limit_; }

 private:
  std::vector<WallGpuVertex> vertices_;
  std::uint64_t generation_{};
  std::size_t limit_{capacity};
};
// The instance quad (kind 17) of a range at an object's position. origin, axis_x, axis_y and uv_offset (the z axis) are
// the affine screen map of the object's local axes, as model_quad computes them without rotation. uv and page are the
// sprite as project_object_faces sets it. multiply = {first, count, black_mask, 0} as exact floats, where black_mask
// has bit `slot` set for each boundary neighbor whose faces are black (0 for objects without boundary tests). owner is
// the store that holds the range.
[[nodiscard]] Quad wall_quad(const std::shared_ptr<WallGeometryStore>& store, WallRange range, const Camera& camera,
                             Vec2 position, SpriteRegion sprite, std::uint32_t black_mask);
}
