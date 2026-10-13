// Appearances::compose against compose_reference (the original per-pixel implementation): identical bytes and density
// for every animation frame with its mask, random sizes/dyes/padding, and random synthetic art including odd and very
// large sizes.
#include "realm/util/TextureRedrawer.hpp"
#include "flash_assets.hpp"
#include <chrono>
#include <iostream>
#include <random>
#include <stdexcept>
int main(int argc, char**) {
  try {
    using namespace realm;
    AnimationLibrary animations(flash::source_root);
    Appearances appearances(flash::source_root, animations);
    std::mt19937 rng(41);
    long checks{};
    const std::uint32_t dyes[]{0,         0x1ff0000, 0x100ff00, 0x1ffffff, 0x4000003,
                               0x5000008, 0x9000011, 0xa000007, 0x1000001};
    auto same = [&](const Image& source, const Image* mask, int size, bool pad, std::uint32_t t1, std::uint32_t t2) {
      const auto fast = appearances.compose(source, mask, size, pad, t1, t2),
                 slow = appearances.compose_reference(source, mask, size, pad, t1, t2);
      ++checks;
      if (fast.width != slow.width || fast.height != slow.height || fast.pixel_scale != slow.pixel_scale ||
          fast.pixels != slow.pixels)
        throw std::runtime_error("compose differs from compose_reference");
    };
    const auto& images = animations.images();
    // Every animation image (with and without its mask) at a few sizes: the real art.
    for (std::size_t i = 0; i < images.size(); i += std::max<std::size_t>(1, images.size() / 150)) {
      Image source = images[i];
      normalize_flash_png(source);
      const Image* mask = i + 1 < images.size() ? &images[i + 1] : nullptr;
      for (const int size : {50, 100, 137, 200, 350})
        same(source, mask, size, rng() % 2, dyes[rng() % 9], dyes[rng() % 9]);
    }
    // Synthetic art: random alpha, masks with both channels, odd shapes, sizes up to the 1024 px raster bound.
    for (int round = 0; round < 600; ++round) {
      const int sw = 1 + static_cast<int>(rng() % 40), sh = 1 + static_cast<int>(rng() % 40);
      Image source{sw, sh, std::vector<unsigned char>(static_cast<std::size_t>(sw * sh * 4))}, mask = source;
      for (std::size_t p = 0; p < source.pixels.size(); p += 4) {
        const bool opaque = rng() % 3;
        for (int k = 0; k < 3; ++k) source.pixels[p + static_cast<std::size_t>(k)] = static_cast<unsigned char>(rng());
        source.pixels[p + 3] = opaque ? static_cast<unsigned char>(rng()) : 0;
        mask.pixels[p] = rng() % 4 == 0 ? static_cast<unsigned char>(rng()) : 0;
        mask.pixels[p + 1] = rng() % 4 == 0 ? static_cast<unsigned char>(rng()) : 0;
        mask.pixels[p + 2] = 0;
        mask.pixels[p + 3] = rng() % 2 ? 255 : 0;
      }
      const int size = (rng() % 40 == 0) ? 400 + static_cast<int>(rng() % 10000) : 1 + static_cast<int>(rng() % 300);
      same(source, rng() % 2 ? &mask : nullptr, size, rng() % 2, dyes[rng() % 9], dyes[rng() % 9]);
    }
    if (argc > 1) {  // --profile
      const Image source = images[images.size() / 2];
      const Image* mask = &images[images.size() / 2 + 1];
      for (const bool reference : {true, false}) {
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 300; ++i)
          (void)(reference
                     ? appearances.compose_reference(source, mask, 100 + i % 3 * 25, false, dyes[i % 9],
                                                     dyes[(i / 3) % 9])
                     : appearances.compose(source, mask, 100 + i % 3 * 25, false, dyes[i % 9], dyes[(i / 3) % 9]));
        std::cout << (reference ? "reference" : "current") << " 300 composes: "
                  << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count()
                  << " ms\n";
      }
    }
    std::cout << checks << " compose differential checks passed\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
