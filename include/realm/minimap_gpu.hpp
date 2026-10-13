#pragma once
#include "realm/minimap.hpp"
#include "realm/renderer.hpp"

namespace realm {
class MinimapGpu {
 public:
  [[nodiscard]] Quad build(Minimap& minimap, const World& world, const Entity& focus, double angle, Vec2 origin,
                           double scale);

 private:
  SurfaceTexture map_, arrow_, out_, in_;
  std::shared_ptr<QuadLayer> layer_ = std::make_shared<QuadLayer>();
  const Minimap* source_{};
  std::uint64_t generation_{};
  int width_{}, height_{};
};
}
