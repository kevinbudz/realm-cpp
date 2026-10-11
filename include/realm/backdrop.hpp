#pragma once
#include "realm/atlas.hpp"
#include "realm/camera.hpp"
#include "realm/image.hpp"
#include "realm/renderer.hpp"
#include <cstdint>
#include <memory>
#include <vector>

namespace realm {
// Background.as: the MAPINFO background id picks the layer drawn behind the tiles.
enum class BackdropKind : int { none = 0, stars = 1, nexus = 2 };

// StarBackground.as: up to 100 stars at random points of a 1000x1000 square around the camera,
// rejecting any closer than 3 to an earlier one. They rotate with the camera but do not scroll.
struct Star { double x{}, y{}, scale{}; int image{}; };
// Index of the "stars" image set in flash::sheets (ImageSet 5x5, ten images).
[[nodiscard]] std::size_t stars_sheet_index();
[[nodiscard]] std::vector<Star> star_field(std::uint32_t seed);
// NexusBackground.as: a 1024x1024 fractal perlin field (8 octaves, stitched) in the blue channel.
[[nodiscard]] Image nexus_water(std::uint32_t seed);

class Backdrop {
public:
    static constexpr int water_size = 1024;
    static constexpr double water_drift = 0.01; // NexusBackground.MOVEMENT, pixels per millisecond on both axes
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
