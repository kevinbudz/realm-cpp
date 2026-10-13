#pragma once
#include "realm/stage3D/SpriteAtlas.hpp"
#include "realm/map/Camera.hpp"
#include "realm/background/BackdropData.hpp"
#include "realm/stage3D/Renderer.hpp"
#include <cstdint>
#include <memory>
#include <vector>

namespace realm {

class Backdrop {
 public:
  static constexpr int water_size = nexus_water_size;
  static constexpr double water_drift = 0.01;  // NexusBackground.MOVEMENT, pixels per millisecond on both axes
  void reset(int kind, std::uint32_t seed);
  [[nodiscard]] BackdropKind kind() const { return kind_; }
  // Reference hooks: draw an explicit star list or water bitmap instead of the generated ones.
  void set_stars(std::vector<Star> stars) { stars_ = std::move(stars); }
  void set_water(Image image);
  // Appends the layer first in the painter-ordered stream, before any terrain.
  void build(const Camera& camera, const Atlas& atlas, std::int32_t time, std::vector<Quad>& out);

 private:
  BackdropKind kind_{};
  std::uint32_t seed_{};
  std::vector<Star> stars_;
  std::unique_ptr<SurfaceTexture> water_;
  std::unique_ptr<Image> water_image_;
};
}
