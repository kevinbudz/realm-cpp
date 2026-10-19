#pragma once
// Backend-neutral frame preparation, shared by every renderer. A submission's instances and run
// data, its meshes and its walls are packed here, and the run boundaries a backend walks are
// computed here. Nothing in this file calls a graphics API: each backend issues its own draws in
// the order these functions give. The split keeps the GL renderer's values and call order as they
// were when the code lived inline in RendererGl.
#include "realm/stage3D/DrawTypes.hpp"
#include "realm/stage3D/Renderer.hpp"
#include "realm/stage3D/WallGeometry.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace realm::draw_plan {
// The instance attributes of a Quad in location order: component count and byte offset within the
// quad_instance_bytes instance.
inline constexpr std::array<int, 9> quad_sizes{2, 2, 2, 4, 1, 4, 4, 1, 2};
inline constexpr std::array<std::size_t, 9> quad_offsets{
    offsetof(Quad, origin), offsetof(Quad, axis_x), offsetof(Quad, axis_y),
    offsetof(Quad, uv),     offsetof(Quad, page),   offsetof(Quad, multiply),
    offsetof(Quad, offset), offsetof(Quad, kind),   offsetof(Quad, uv_offset)};

// A mesh vertex as the mesh program reads it: one per triangle corner, carrying its mesh's sprite
// and color and its group's material.
struct MeshBatchVertex {
  float x, y, u, v, shade;
  std::array<float, 4> bounds;
  float page;
  std::array<float, 3> color;
  float textured;
  float repeat;
};

struct MeshBatchRange {
  std::size_t first;
  std::size_t count;
};

// The meshes of one submission (quads, then overlay), in the order the draw visits them.
struct MeshBatch {
  std::vector<MeshBatchVertex> vertices;
  std::vector<MeshBatchRange> ranges;  // one per mesh quad
  std::size_t cursor{};                // the next range the draw takes
  void clear() {
    vertices.clear();
    ranges.clear();
    cursor = 0;
  }
};

// Sync state of a backend's GPU copy of a WallGeometryStore (two texels per corner).
struct WallSync {
  std::uint64_t generation{};
  std::size_t uploaded{};  // corners
  std::size_t capacity{};  // corners
};

struct WallStep {
  bool none{};        // the GPU copy is already current
  bool reallocate{};  // resize the GPU buffer to capacity corners first
  std::size_t capacity{};
  std::size_t first{};  // corners [first, end) are written
  std::size_t end{};
};

// Packs list's instances into `instances` and its run data into `meta` (both sized to list), one
// pass. With collect, the same pass gathers the meshes of list into `meshes` (a clipped layer's
// meshes at the layer's stream position) and its wall store into `walls`.
void pack_quads(std::span<const Quad> list, std::byte* instances, QuadMeta* meta,
                const WallGeometryStore*& walls, bool collect, MeshBatch& meshes);

// Plans the GPU copy of a store's corners. Throws "Wall geometry exceeds the GPU texture buffer
// limit" when the store has more than limit corners; the sync state is unchanged then.
[[nodiscard]] WallStep plan_walls(WallSync& sync, const WallGeometryStore& store,
                                  std::size_t limit);

// Run boundaries of draw_batch: each returns one past the last index of the run starting at first.
// meta is the list's run data, one entry per quad.
[[nodiscard]] std::size_t model_block_end(std::span<const QuadMeta> meta, std::size_t first);
[[nodiscard]] std::size_t model_run_end(std::span<const QuadMeta> meta, std::size_t run,
                                        std::size_t block_end);
[[nodiscard]] std::size_t mesh_block_end(std::span<const QuadMeta> meta, std::size_t first);

struct SpriteRun {
  std::size_t end{};
  std::size_t walls{};             // wall instances (kind 17) in the run
  std::int32_t max_vertices{};     // the largest corner count of those walls
  std::uint64_t wall_triangles{};  // their corners / 3, as submitted
};
// A run of one texture of sprite-like quads; it breaks at models, terrain and clipped layers.
[[nodiscard]] SpriteRun sprite_run(std::span<const QuadMeta> meta, std::size_t first);

// Takes the next count mesh ranges in visiting order. Throws "Mesh batch out of sync".
[[nodiscard]] std::span<const MeshBatchRange> take_mesh_ranges(MeshBatch& meshes,
                                                               std::size_t count);

struct MeshTotals {
  std::size_t vertices{};
  std::size_t models{};  // ranges with vertices
};
[[nodiscard]] MeshTotals mesh_totals(std::span<const MeshBatchRange> ranges);
}  // namespace realm::draw_plan
