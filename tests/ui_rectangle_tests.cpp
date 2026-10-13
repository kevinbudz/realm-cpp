// ui_rectangle against the original per-pixel implementation (kept here verbatim as the oracle): identical bytes for
// random geometry, scales, colors, premultiplied and straight destinations, partially transparent backgrounds.
#include "realm/image.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>
namespace {
void reference(realm::Image& dst, double x, double y, double w, double h, std::uint32_t color, bool pm) {
  const double s = dst.pixel_scale;
  x *= s;
  y *= s;
  w *= s;
  h *= s;
  for (int py = std::max(0, static_cast<int>(std::floor(y)));
       py < std::min(dst.height, static_cast<int>(std::ceil(y + h))); ++py)
    for (int px = std::max(0, static_cast<int>(std::floor(x)));
         px < std::min(dst.width, static_cast<int>(std::ceil(x + w))); ++px) {
      const double a = std::clamp(std::min(x + w, px + 1.0) - std::max(x, static_cast<double>(px)), 0.0, 1.0) *
                       std::clamp(std::min(y + h, py + 1.0) - std::max(y, static_cast<double>(py)), 0.0, 1.0);
      const auto to =
          (static_cast<std::size_t>(py) * static_cast<std::size_t>(dst.width) + static_cast<std::size_t>(px)) * 4;
      const double da = dst.pixels[to + 3] / 255.0, combined = a + da * (1 - a);
      if (combined <= 0) continue;
      for (unsigned c = 0; c < 3; ++c)
        dst.pixels[to + c] = static_cast<unsigned char>(std::clamp(
            std::lround((((color >> (16 - c * 8)) & 255U) * a + dst.pixels[to + c] * (pm ? 1 : da) * (1 - a)) /
                        (pm ? 1 : combined)),
            0L, 255L));
      dst.pixels[to + 3] = static_cast<unsigned char>(std::lround(combined * 255));
    }
}
}
int main(int argc, char** argv) {
  try {
    std::mt19937 rng(5);
    std::uniform_real_distribution<double> unit(0, 1);
    long checks{};
    for (int round = 0; round < 4000; ++round) {
      const int size = 8 + static_cast<int>(rng() % 120);
      const double scale = (rng() % 3 == 0) ? 1 : .5 + unit(rng) * 3;
      realm::Image a{size, size, std::vector<unsigned char>(static_cast<std::size_t>(size * size * 4))};
      a.pixel_scale = scale;
      for (auto& v : a.pixels) v = static_cast<unsigned char>(rng());
      if (rng() % 2)
        for (std::size_t i = 3; i < a.pixels.size(); i += 4) a.pixels[i] = 255;
      auto b = a;
      for (int k = 0; k < 20; ++k) {
        const double x = unit(rng) * size / scale * 1.2 - 3, y = unit(rng) * size / scale * 1.2 - 3,
                     w = (rng() % 4 == 0 ? unit(rng) * .3 : unit(rng) * 12),
                     h = (rng() % 4 == 0 ? unit(rng) * .3 : unit(rng) * 12);
        const auto color = static_cast<std::uint32_t>(rng()) & 0xffffffU;
        const bool pm = rng() % 3 == 0;
        realm::ui_rectangle(a, x, y, w, h, color, pm);
        reference(b, x, y, w, h, color, pm);
        ++checks;
        if (a.pixels != b.pixels) throw std::runtime_error("ui_rectangle differs from the original implementation");
      }
    }
    if (argc > 1) {  // --profile: 6000 minimap markers at 1080p density
      realm::Image image{192 * 2, 192 * 2, std::vector<unsigned char>(192 * 2 * 192 * 2 * 4, 255)};
      image.pixel_scale = 1.8;
      for (int variant = 0; variant < 2; ++variant) {
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 6000; ++i) {
          const double x = 94 + unit(rng) * 4, y = 94 + unit(rng) * 4;
          if (variant)
            reference(image, x, y, 4, 4, 0xffff00, false);
          else
            realm::ui_rectangle(image, x, y, 4, 4, 0xffff00);
        }
        std::cout << (variant ? "original" : "current") << " 6000 markers: "
                  << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count()
                  << " ms\n";
      }
    }
    std::cout << checks << " ui_rectangle differential checks passed\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
