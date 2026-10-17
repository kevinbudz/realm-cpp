#pragma once
// Flash: com/company/assembleegameclient/util/AnimatedChars.as
#include "realm/assets/Image.hpp"
#include <array>
#include <memory>
#include <span>

namespace realm {
// Every flash::assets PNG, decoded once and shared by the atlas, terrain, minimap, appearances and
// animation library. `normalized[i]` is normalize_flash_png applied to the file (it is not
// idempotent, so it is computed from the file pixels exactly once). `raw[i]` keeps the file pixels
// as stored, for the animation sheets and their masks only; the other entries are empty.
struct FlashImages {
  explicit FlashImages(const std::filesystem::path& root);
  std::vector<Image> raw;
  std::vector<Image> normalized;
};

enum class Action : int { stand, walk, attack };

struct AnimationFrame {
  std::size_t image{};
  int mask{-1};
};

struct Character {
  std::array<std::array<std::vector<AnimationFrame>, 3>, 4> directions;
  [[nodiscard]] AnimationFrame frame(double facing, double camera_angle, Action action,
                                     double phase) const;
};

class AnimationLibrary {
 public:
  explicit AnimationLibrary(const std::filesystem::path& root);
  explicit AnimationLibrary(const FlashImages& pngs);
  AnimationLibrary() = default;
  // Exposed for deterministic fixture tests and asset conversion tools.
  Character build(const Image& strip, const Image* mask, int width, int height,
                  int first_direction);
  [[nodiscard]] const Character* character(int sheet, int index) const;

  [[nodiscard]] const std::vector<Image>& images() const { return images_; }

 private:
  std::vector<Image> images_;
  std::vector<std::vector<Character>> sheets_;
};

[[nodiscard]] Image crop(const Image& source, int x, int y, int width, int height);
[[nodiscard]] Image mirror(const Image& source, int width = 0);
}
