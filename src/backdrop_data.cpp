#include "realm/backdrop.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <random>
#include <stdexcept>

namespace realm {
namespace {
// Flash perlinNoise gradients are not exactly unit-scaled like this lattice: measured over ten
// NexusBackground fields (tests/postfx_reference_tests.py) the blue channel has std 20.8, this field 17.6 at gain 1.
constexpr double noise_gain = 1.18;
}
std::size_t stars_sheet_index() {
    for (std::size_t i = 0; i < flash::sheets.size(); ++i) if (flash::sheets[i].name == "stars") return i;
    throw std::runtime_error("The stars image set is missing");
}
namespace {
std::uint32_t hash(std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d) {
    std::uint32_t h = a * 0x9E3779B1U ^ (b + 0x7F4A7C15U) * 0x85EBCA6BU ^ (c + 0x165667B1U) * 0xC2B2AE35U ^ (d + 0x27D4EB2FU) * 0x9E3779B1U;
    h ^= h >> 15; h *= 0x2C1B3C6DU; h ^= h >> 12; h *= 0x297A2D39U; h ^= h >> 15;
    return h;
}
// One gradient-noise octave whose lattice wraps every `cells`, so the field tiles (perlinNoise stitch).
double octave(double x, double y, int cells, std::uint32_t seed, int level) {
    const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
    const double fx = x - x0, fy = y - y0;
    const auto corner = [&](int ix, int iy, double dx, double dy) {
        const double angle = hash(seed, static_cast<std::uint32_t>(level), static_cast<std::uint32_t>(ix % cells), static_cast<std::uint32_t>(iy % cells)) * (2 * std::numbers::pi / 4294967296.0);
        return std::cos(angle) * dx + std::sin(angle) * dy;
    };
    const auto fade = [](double t) { return t * t * (3 - 2 * t); };
    const double u = fade(fx), v = fade(fy);
    const double top = corner(x0, y0, fx, fy) + u * (corner(x0 + 1, y0, fx - 1, fy) - corner(x0, y0, fx, fy));
    const double bottom = corner(x0, y0 + 1, fx, fy - 1) + u * (corner(x0 + 1, y0 + 1, fx - 1, fy - 1) - corner(x0, y0 + 1, fx, fy - 1));
    return top + v * (bottom - top);
}
}
std::vector<Star> star_field(std::uint32_t seed) {
    std::mt19937 random(seed);
    std::uniform_real_distribution<double> unit(0, 1);
    const int images = flash::sheets[stars_sheet_index()].count;
    std::vector<Star> stars;
    for (int attempt = 0; attempt < 100; ++attempt) {
        Star next{unit(random) * 1000 - 500, unit(random) * 1000 - 500, 4 * (0.5 + 0.5 * unit(random)), 0};
        next.image = std::min(images - 1, static_cast<int>(images * unit(random)));
        const bool crowded = std::ranges::any_of(stars, [&](const Star& s) { return std::hypot(next.x - s.x, next.y - s.y) < 3; });
        if (!crowded) stars.push_back(next);
    }
    return stars;
}
Image nexus_water(std::uint32_t seed) {
    constexpr int size = Backdrop::water_size, octaves = 8;
    Image image{size, size, std::vector<unsigned char>(static_cast<std::size_t>(size) * size * 4, 255)};
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            double sum{};
            for (int level = 0; level < octaves; ++level) {
                const int cells = 1 << level; // lattice cell = 1024 / 2^level pixels, amplitude 1 / 2^level
                sum += octave(static_cast<double>(x) * cells / size, static_cast<double>(y) * cells / size, cells, seed, level) / cells;
            }
            const auto blue = static_cast<unsigned char>(std::clamp((sum * noise_gain * 0.5 + 0.5) * 255.0 + 0.5, 0.0, 255.0));
            auto* pixel = &image.pixels[(static_cast<std::size_t>(y) * size + x) * 4];
            pixel[0] = 0; pixel[1] = 0; pixel[2] = blue;
        }
    return image;
}
}
