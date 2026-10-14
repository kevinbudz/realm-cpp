#pragma once
#include "realm/assets/Image.hpp"
#include <cstdint>
#include <vector>

namespace realm {
inline constexpr int nexus_water_size = 1024;
// Background.as: the MAPINFO background id picks the layer drawn behind the tiles.
enum class BackdropKind : int { none = 0, stars = 1, nexus = 2 };

// StarBackground.as: up to 100 stars at random points of a 1000x1000 square around the camera,
// rejecting any closer than 3 to an earlier one. They rotate with the camera but do not scroll.
struct Star {
  double x{}, y{}, scale{};
  int image{};
};
// Index of the "stars" image set in flash::sheets (ImageSet 5x5, ten images).
[[nodiscard]] std::size_t stars_sheet_index();
[[nodiscard]] std::vector<Star> star_field(std::uint32_t seed);
// NexusBackground.as: a 1024x1024 fractal perlin field (8 octaves, stitched) in the blue channel.
[[nodiscard]] Image nexus_water(std::uint32_t seed);

}
