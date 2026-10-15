#include "realm/stage3D/WallGeometry.hpp"
#include <atomic>

namespace realm {
namespace {
// Shared by every store and reset, so generations never repeat within a process.
std::atomic<std::uint64_t> last_generation{};

std::uint64_t next_generation() {
  return ++last_generation;
}
}  // namespace

WallGeometryStore::WallGeometryStore(std::size_t limit)
    : generation_(next_generation()), limit_(limit) {}

std::optional<WallRange> WallGeometryStore::append(std::span<const WallGpuVertex> vertices) {
  if (vertices_.size() + vertices.size() > limit_) {
    return std::nullopt;
  }
  const auto first = static_cast<std::uint32_t>(vertices_.size());
  vertices_.insert(vertices_.end(), vertices.begin(), vertices.end());
  return WallRange{first, static_cast<std::uint32_t>(vertices.size()), generation_};
}

void WallGeometryStore::reset() {
  vertices_.clear();
  generation_ = next_generation();
}

Quad wall_quad(const std::shared_ptr<WallGeometryStore>& store, WallRange range,
               const Camera& camera, Vec2 position, SpriteRegion sprite, std::uint32_t black_mask) {
  // world_to_screen is affine: its unit steps along world x, y and z give the object's screen axes
  // (model_quad).
  const auto origin = camera.world_to_screen(position);
  const auto step = [&](Vec2 p, double z) {
    const auto screen = camera.world_to_screen(p, z);
    return Vec2{screen.x - origin.x, screen.y - origin.y};
  };
  const auto ex = step({position.x + 1, position.y}, 0);
  const auto ey = step({position.x, position.y + 1}, 0);
  const auto ez = step(position, 1);
  const auto cast = [](double v) { return static_cast<float>(v); };
  // The same sprite bounds project_object_faces sets on its MeshDraw.
  constexpr float inv_page = 1.0F / sprite_page_size;
  Quad q;
  q.kind = 17;
  q.origin = {cast(origin.x), cast(origin.y)};
  q.axis_x = {cast(ex.x), cast(ex.y)};
  q.axis_y = {cast(ey.x), cast(ey.y)};
  q.uv_offset = {cast(ez.x), cast(ez.y)};
  q.uv = {static_cast<float>(sprite.x) * inv_page, static_cast<float>(sprite.y) * inv_page,
          static_cast<float>(sprite.width) * inv_page,
          static_cast<float>(sprite.height) * inv_page};
  q.page = static_cast<float>(sprite.page);
  q.multiply = {static_cast<float>(range.first), static_cast<float>(range.count),
                static_cast<float>(black_mask), 0};
  q.owner = store;
  return q;
}
}  // namespace realm
