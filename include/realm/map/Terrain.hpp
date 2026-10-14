#pragma once
#include "realm/assets/Image.hpp"
#include "realm/map/World.hpp"
#include <memory>
#include <span>
namespace realm {
struct TileVisual {
  std::size_t image{};
  int overlay{-1}, sink{};
  Vec2 offset{};
};
class Terrain {
 public:
  Terrain(const World& world, const std::filesystem::path& root, std::size_t first_index, int mask_variant = -1,
          bool live = false);
  ~Terrain();
  void begin_frame();
  [[nodiscard]] std::span<const std::size_t> changes() const { return changes_; }
  [[nodiscard]] TileVisual tile(int x, int y) const;
  [[nodiscard]] const std::vector<Image>& images() const { return images_; }
  [[nodiscard]] std::uint64_t cache_evictions() const;
  struct SlotStamp {
    std::size_t image{};
    std::uint64_t revision{};
  };
  [[nodiscard]] SlotStamp stamp(std::size_t image) const;
  // Validate cached chunk images before pinning them against this frame's live-cache eviction.
  [[nodiscard]] bool retain(std::span<const SlotStamp> slots) const;

 private:
  int width_{};
  std::vector<TileVisual> tiles_;
  std::vector<Image> images_;
  std::vector<std::size_t> changes_;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
[[nodiscard]] std::uint32_t tile_hash(int x, int y);
[[nodiscard]] Image rotate(const Image& image, int turns);
}
