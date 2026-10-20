#include "DrawPlan.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace realm::draw_plan {
namespace {
// Mesh groups become whole triangles; a run's single draw relies on every span starting on a
// triangle boundary.
void collect_meshes(std::span<const Quad> list, MeshBatch& meshes) {
  for (const auto& quad : list) {
    if (quad.kind == 14) {
      if (const auto* layer = static_cast<const QuadLayer*>(quad.owner.get())) {
        collect_meshes(layer->quads, meshes);
      }
      continue;
    }
    if (quad.kind != 9 || !quad.owner) {
      continue;
    }
    const auto& mesh = *static_cast<const MeshDraw*>(quad.owner.get());
    const auto first = meshes.vertices.size();
    for (const auto& group : mesh.groups) {
      const std::size_t begin = group.first;
      const std::size_t end = begin + (group.count - group.count % 3);
      if (end > mesh.vertices.size()) {
        throw std::logic_error("Invalid mesh group");
      }
      const float textured = group.solid ? 0.F : 1.F;
      const float repeat = group.repeat ? 1.F : 0.F;
      for (std::size_t i = begin; i < end; ++i) {
        const auto& v = mesh.vertices[i];
        meshes.vertices.push_back(
            {v.x, v.y, v.u, v.v, v.shade, mesh.bounds, mesh.page, mesh.color, textured, repeat});
      }
    }
    meshes.ranges.push_back({first, meshes.vertices.size() - first});
  }
}

// The one wall store a submission draws from, also inside layers. Invariant: one store per
// submission. Scene builds a frame's walls into one store and replaces it only at the start of a
// build, so a second store is a caller error (the texture buffer mirrors one store).
void find_walls(std::span<const Quad> list, const WallGeometryStore*& store) {
  for (const auto& quad : list) {
    if (quad.kind == 14) {
      if (const auto* layer = static_cast<const QuadLayer*>(quad.owner.get())) {
        find_walls(layer->quads, store);
      }
      continue;
    }
    if (quad.kind != 17) {
      continue;
    }
    const auto* found = static_cast<const WallGeometryStore*>(quad.owner.get());
    if (!found) {
      throw std::logic_error("Wall instance without its geometry store");
    }
    if (store && store != found) {
      throw std::logic_error("A submission draws walls from two geometry stores");
    }
    store = found;
  }
}
}  // namespace

void pack_quads(std::span<const Quad> list, std::byte* instances, QuadMeta* meta,
                const WallGeometryStore*& walls, bool collect, MeshBatch& meshes) {
  for (std::size_t i = 0; i < list.size(); ++i) {
    const Quad& quad = list[i];
    std::memcpy(instances + i * quad_instance_bytes, static_cast<const void*>(&quad),
                quad_instance_bytes);
    meta[i] = {quad.owner.get(), quad.texture, quad.kind, quad.offset[2], quad.multiply[1]};
    if (!collect) {
      continue;
    }
    if (quad.kind == 14) {
      if (const auto* layer = static_cast<const QuadLayer*>(quad.owner.get())) {
        collect_meshes(layer->quads, meshes);
        find_walls(layer->quads, walls);
      }
    } else if (quad.kind == 9 && quad.owner) {
      collect_meshes(list.subspan(i, 1), meshes);
    } else if (quad.kind == 17) {
      find_walls(list.subspan(i, 1), walls);
    }
  }
}

WallStep plan_walls(WallSync& sync, const WallGeometryStore& store, std::size_t limit) {
  const auto count = store.vertices().size();
  const auto generation = store.generation();
  if (count > limit) {
    throw std::runtime_error("Wall geometry exceeds the GPU texture buffer limit");
  }
  if (generation != sync.generation || count < sync.uploaded) {
    sync.uploaded = 0;
  }
  WallStep step;
  if (generation == sync.generation && count == sync.uploaded) {
    step.none = true;
    return step;
  }
  if (count > sync.capacity) {
    sync.capacity = std::min(limit, std::max({count, 2 * sync.capacity, std::size_t{4096}}));
    step.reallocate = true;
    sync.uploaded = 0;
  }
  step.capacity = sync.capacity;
  step.first = sync.uploaded;
  step.end = count;
  sync.generation = generation;
  sync.uploaded = count;
  return step;
}

std::size_t model_block_end(std::span<const QuadMeta> meta, std::size_t first) {
  std::size_t end = first;
  while (end < meta.size() && meta[end].kind == 9 && !meta[end].owner) {
    ++end;
  }
  return end;
}

std::size_t model_run_end(std::span<const QuadMeta> meta, std::size_t run,
                          std::size_t block_end) {
  const float model = meta[run].model;
  std::size_t next = run + 1;
  while (next < block_end && meta[next].model == model) {
    ++next;
  }
  return next;
}

std::size_t mesh_block_end(std::span<const QuadMeta> meta, std::size_t first) {
  std::size_t end = first;
  while (end < meta.size() && meta[end].kind == 9 && meta[end].owner) {
    ++end;
  }
  return end;
}

SpriteRun sprite_run(std::span<const QuadMeta> meta, std::size_t first) {
  const auto texture = meta[first].texture;
  SpriteRun run;
  // One pass over the run: each quad is counted as its kind is read, so the walls are not
  // scanned a second time.
  const auto count_walls = [&run, &meta](std::size_t i) {
    if (meta[i].kind == 17) {
      const auto count = static_cast<std::int32_t>(meta[i].walls);
      ++run.walls;
      run.max_vertices = std::max(run.max_vertices, count);
      run.wall_triangles += static_cast<std::uint64_t>(count) / 3;
    }
  };
  count_walls(first);
  run.end = first + 1;
  while (run.end < meta.size() && meta[run.end].texture == texture && meta[run.end].kind != 9 &&
         meta[run.end].kind != 10 && meta[run.end].kind != 14) {
    count_walls(run.end);
    ++run.end;
  }
  return run;
}

std::span<const MeshBatchRange> take_mesh_ranges(MeshBatch& meshes, std::size_t count) {
  if (meshes.ranges.size() - meshes.cursor < count) {
    throw std::logic_error("Mesh batch out of sync");
  }
  const auto run = std::span<const MeshBatchRange>(meshes.ranges).subspan(meshes.cursor, count);
  meshes.cursor += count;
  return run;
}

MeshTotals mesh_totals(std::span<const MeshBatchRange> ranges) {
  MeshTotals totals;
  for (const auto& range : ranges) {
    totals.vertices += range.count;
    totals.models += range.count != 0;
  }
  return totals;
}
}  // namespace realm::draw_plan
