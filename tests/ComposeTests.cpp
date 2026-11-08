// Appearances::compose against compose_reference (the original per-pixel implementation): identical
// bytes and density for every animation frame with its mask, random sizes/dyes/padding, and random
// synthetic art including odd and very large sizes.
#include "realm/assets/Appearances.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <initializer_list>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

namespace {
using namespace realm;

// The alpha of the outline on each side of the fully opaque block, the first four pixels outward
// from its edge (left, right, top, bottom).
std::array<std::array<unsigned char, 4>, 4> outline_profiles(const Image& image) {
  const auto alpha = [&](int x, int y) -> unsigned char {
    if (x < 0 || y < 0 || x >= image.width || y >= image.height) {
      return 0;
    }
    return image.pixels[static_cast<std::size_t>((y * image.width + x) * 4 + 3)];
  };
  int left = image.width, right = -1, top = image.height, bottom = -1;
  for (int y = 0; y < image.height; ++y) {
    for (int x = 0; x < image.width; ++x) {
      if (alpha(x, y) == 255) {
        left = std::min(left, x);
        right = std::max(right, x);
        top = std::min(top, y);
        bottom = std::max(bottom, y);
      }
    }
  }
  if (right < 0) {
    throw std::runtime_error("solid square has no opaque pixels");
  }
  const int mid_x = (left + right) / 2;
  const int mid_y = (top + bottom) / 2;
  std::array<std::array<unsigned char, 4>, 4> profile{};
  for (int i = 0; i < 4; ++i) {
    profile[0][static_cast<std::size_t>(i)] = alpha(left - 1 - i, mid_y);
    profile[1][static_cast<std::size_t>(i)] = alpha(right + 1 + i, mid_y);
    profile[2][static_cast<std::size_t>(i)] = alpha(mid_x, top - 1 - i);
    profile[3][static_cast<std::size_t>(i)] = alpha(mid_x, bottom + 1 + i);
  }
  return profile;
}

// The outline of a solid square at zoom z: floor(z) full rings (.8 * 255), then one ring at
// .8 * frac * 255 where frac = z - floor(z), the same on all four sides. Zoom 1 and 2 are the 3x3
// and 5x5 outlines. The default (HUD) outline stays one full ring whatever the device scale.
void outline_checks(const Appearances& appearances, long& checks) {
  const auto solid = static_cast<unsigned char>(std::lround(.8 * 255));
  for (const double zoom : {1.0, 1.1, 1.3, 1.4, 1.5, 1.7, 2.0}) {
    const int full = static_cast<int>(std::floor(zoom));
    const double fraction = zoom - full;
    const auto partial = static_cast<unsigned char>(std::lround(.8 * fraction * 255));
    for (const int texels : {1, 4, 8}) {
      for (const bool pad : {false, true}) {
        const Image source{
            texels, texels,
            std::vector<unsigned char>(static_cast<std::size_t>(texels * texels) * 4, 255)};
        const auto image = appearances.compose_zoomed(source, nullptr, 100, pad, 0, 0, zoom, false);
        const auto profile = outline_profiles(image);
        for (std::size_t side = 0; side < 4; ++side) {
          for (std::size_t ring = 0; ring < 4; ++ring) {
            const auto expected = static_cast<int>(ring) < full    ? solid
                                  : static_cast<int>(ring) == full ? partial
                                                                   : static_cast<unsigned char>(0);
            if (profile[side][ring] != expected) {
              throw std::runtime_error("outline at zoom " + std::to_string(zoom) + " side " +
                                       std::to_string(side) + " ring " + std::to_string(ring) +
                                       " is " + std::to_string(profile[side][ring]) +
                                       ", expected " + std::to_string(expected));
            }
          }
        }
        checks += 4;
      }
    }
  }
  // The HUD outline is unchanged: one full ring at device scale 2.
  const Image source{4, 4, std::vector<unsigned char>(4 * 4 * 4, 255)};
  const auto hud = appearances.compose(source, nullptr, 100, false, 0, 0, false, 2.0, true);
  for (const auto& side : outline_profiles(hud)) {
    if (side[0] != solid || side[1] != 0) {
      throw std::runtime_error("HUD outline at device 2 is not one 3x3 ring");
    }
  }
  ++checks;
}

// At zoom 1 the zoomed bake is the default bake, byte for byte, over real art with and without
// dyes.
void unit_zoom_identity(const Appearances& appearances, const AnimationLibrary& animations,
                        long& checks) {
  const auto& images = animations.images();
  const std::uint32_t dyes[]{0, 0x1ff0000, 0x100ff00};
  for (std::size_t i = 0; i < images.size(); i += std::max<std::size_t>(1, images.size() / 25)) {
    Image source = images[i];
    normalize_flash_png(source);
    const Image* mask = i + 1 < images.size() ? &images[i + 1] : nullptr;
    for (const int size : {50, 100, 200}) {
      for (const bool pad : {false, true}) {
        const auto plain = appearances.compose(source, mask, size, pad, dyes[1], dyes[2]);
        const auto zoomed =
            appearances.compose_zoomed(source, mask, size, pad, dyes[1], dyes[2], 1.0, true);
        ++checks;
        if (plain.width != zoomed.width || plain.height != zoomed.height ||
            plain.pixel_scale != zoomed.pixel_scale || plain.pixels != zoomed.pixels) {
          throw std::runtime_error("compose_zoomed at zoom 1 differs from compose");
        }
      }
    }
  }
}

// The zoomed bitmap is the scale-10 art resampled nearest-neighbour: wherever both the zoomed pixel
// and its scale-10 texel are opaque art, the colours are equal, so the art is what GL_NEAREST drew.
// The density is the scale-10 density times the zoom.
void nearest_art_checks(const Appearances& appearances, long& checks) {
  std::mt19937 rng(43);
  for (const double zoom : {0.8, 1.1, 1.4, 1.5, 2.0}) {
    for (const bool pad : {false, true}) {
      const int sw = 3 + static_cast<int>(rng() % 5);
      const int sh = 2 + static_cast<int>(rng() % 6);
      Image source{sw, sh, std::vector<unsigned char>(static_cast<std::size_t>(sw * sh) * 4, 255)};
      for (std::size_t p = 0; p < source.pixels.size(); p += 4) {
        for (std::size_t k = 0; k < 3; ++k) {
          source.pixels[p + k] = static_cast<unsigned char>(rng());
        }
      }
      // Glow off: an opaque texel's colour is unchanged by the outline, so this is the scale-10
      // art.
      const auto base = appearances.compose(source, nullptr, 100, pad, 0, 0, false);
      const auto zoomed = appearances.compose_zoomed(source, nullptr, 100, pad, 0, 0, zoom, false);
      const int width = zoomed.width;
      const int height = static_cast<int>(std::lround(base.height * zoom));
      if (width != static_cast<int>(std::lround(base.width * zoom)) || zoomed.height < height) {
        throw std::runtime_error("zoomed canvas size is not the scale-10 canvas times the zoom");
      }
      if (zoomed.pixel_scale != base.pixel_scale * zoom) {
        throw std::runtime_error("zoomed density is not the scale-10 density times the zoom");
      }
      std::size_t compared = 0;
      for (int y = 0; y < height; ++y) {
        const int sy = std::min(base.height - 1,
                                static_cast<int>(std::floor((y + .5) * base.height / height)));
        for (int x = 0; x < width; ++x) {
          const int sx =
              std::min(base.width - 1, static_cast<int>(std::floor((x + .5) * base.width / width)));
          const auto to = static_cast<std::size_t>((y * width + x) * 4);
          const auto from = static_cast<std::size_t>((sy * base.width + sx) * 4);
          if (zoomed.pixels[to + 3] != 255 || base.pixels[from + 3] != 255) {
            continue;
          }
          ++compared;
          for (std::size_t k = 0; k < 3; ++k) {
            if (zoomed.pixels[to + k] != base.pixels[from + k]) {
              throw std::runtime_error("zoomed art at zoom " + std::to_string(zoom) +
                                       " is not the nearest scale-10 texel");
            }
          }
        }
      }
      if (compared == 0) {
        throw std::runtime_error("no opaque art was compared");
      }
      checks += static_cast<long>(compared);
    }
  }
}

// A zoomed bitmap never exceeds an atlas page: a huge sprite is shrunk, and its density carries the
// shrink, so the drawn size is the same as the scale-10 bitmap's at the zoom.
void page_cap_checks(const Appearances& appearances, long& checks) {
  const Image source{60, 60, std::vector<unsigned char>(60 * 60 * 4, 255)};
  const auto base = appearances.compose(source, nullptr, 400, false, 0, 0, false);
  const auto zoomed = appearances.compose_zoomed(source, nullptr, 400, false, 0, 0, 2.0, false);
  if (base.width * 2 <= Appearances::maximum_zoomed_side) {
    throw std::runtime_error("the page cap test sprite is too small");
  }
  if (zoomed.width > Appearances::maximum_zoomed_side ||
      zoomed.height > Appearances::maximum_zoomed_side ||
      std::max(zoomed.width, zoomed.height) < Appearances::maximum_zoomed_side - 4) {
    throw std::runtime_error("a zoomed bitmap is not capped at the page side");
  }
  const double drawn_w = zoomed.width * 2.0 / zoomed.pixel_scale;
  const double expected_w = base.width * 2.0 / base.pixel_scale;
  if (std::abs(drawn_w - expected_w) > 1.0) {
    throw std::runtime_error("the capped zoomed bitmap changes the drawn width");
  }
  ++checks;
}

// purge_zoom drops exactly the zoomed variants of other levels, with their bytes and LRU entries.
// Scale 10 and the kept level stay, and a purge is not an LRU eviction.
void purge_checks(const AnimationLibrary& animations, long& checks) {
  Appearances cache(flash::assets_root, animations);
  const auto key = [](std::int32_t size, std::uint8_t scale) {
    return Appearances::Key{7, -1, 0, 0, size, 0, 0, 1, 0, scale};
  };
  const auto expect = [&checks](bool ok, const std::string& why) {
    ++checks;
    if (!ok) {
      throw std::runtime_error(why);
    }
  };
  const Appearances::Key base{};
  expect(
      base.scale == 10 && cache.animated_key(AnimationFrame{7, -1}, 100, false, 0, 0).scale == 10,
      "keys default to scale 10");
  const auto a = cache.image(key(100, 10));
  const auto b = cache.image(key(100, 15));
  const auto c = cache.image(key(200, 15));
  const auto d = cache.image(key(100, 13));
  const auto e = cache.image(key(100, 20));
  const auto bytes = [](std::initializer_list<const Image*> images) {
    std::size_t total = 0;
    for (const auto* image : images) {
      total += image->pixels.size();
    }
    return total;
  };
  expect(
      cache.entries() == 5 && cache.bytes() == bytes({a.get(), b.get(), c.get(), d.get(), e.get()}),
      "five variants are cached with their bytes");
  expect(cache.purge_zoom(15) == 2 && cache.entries() == 3 &&
             cache.bytes() == bytes({a.get(), b.get(), c.get()}) && cache.evictions() == 0,
         "purge_zoom(15) drops the 13 and 20 variants, bytes and entries follow, no eviction");
  expect(!cache.find(key(100, 13)) && !cache.find(key(100, 20)) &&
             cache.find(key(100, 15)).get() == b.get() &&
             cache.find(key(100, 10)).get() == a.get() && cache.find(key(200, 15)).get() == c.get(),
         "the kept variants are still found, the purged ones are not");
  expect(cache.purge_zoom(15) == 0 && cache.entries() == 3,
         "a second purge at the same level is a no-op");
  expect(cache.purge_zoom(10) == 2 && cache.entries() == 1 && cache.bytes() == bytes({a.get()}) &&
             !cache.find(key(100, 15)) && cache.find(key(100, 10)).get() == a.get(),
         "purge_zoom(10) drops every zoomed variant and keeps scale 10");
  // The LRU is still consistent: refilled entries evict oldest first under a byte budget.
  const auto f = cache.image(key(100, 15));
  const auto g = cache.image(key(100, 13));
  expect(cache.entries() == 3 && cache.bytes() == bytes({a.get(), f.get(), g.get()}),
         "refilled variants count their bytes");
  cache.set_budget(1, Appearances::maximum_bytes);
  expect(cache.entries() == 1 && cache.bytes() == bytes({g.get()}) && cache.evictions() == 2 &&
             cache.find(key(100, 13)).get() == g.get(),
         "after a purge the LRU evicts the oldest entries, keeping the newest");
  cache.clear();
  expect(cache.entries() == 0 && cache.bytes() == 0, "clear empties the cache");
}
}  // namespace

int main(int argc, char**) {
  try {
    using namespace realm;
    AnimationLibrary animations(flash::assets_root);
    Appearances appearances(flash::assets_root, animations);
    std::mt19937 rng(41);
    long checks{};
    const std::uint32_t dyes[]{0,         0x1ff0000, 0x100ff00, 0x1ffffff, 0x4000003,
                               0x5000008, 0x9000011, 0xa000007, 0x1000001};
    auto same = [&](const Image& source, const Image* mask, int size, bool pad, std::uint32_t t1,
                    std::uint32_t t2) {
      const auto fast = appearances.compose(source, mask, size, pad, t1, t2),
                 slow = appearances.compose_reference(source, mask, size, pad, t1, t2);
      ++checks;
      if (fast.width != slow.width || fast.height != slow.height ||
          fast.pixel_scale != slow.pixel_scale || fast.pixels != slow.pixels) {
        throw std::runtime_error("compose differs from compose_reference");
      }
    };
    const auto& images = animations.images();
    // Every animation image (with and without its mask) at a few sizes: the real art.
    for (std::size_t i = 0; i < images.size(); i += std::max<std::size_t>(1, images.size() / 150)) {
      Image source = images[i];
      normalize_flash_png(source);
      const Image* mask = i + 1 < images.size() ? &images[i + 1] : nullptr;
      for (const int size : {50, 100, 137, 200, 350}) {
        same(source, mask, size, rng() % 2, dyes[rng() % 9], dyes[rng() % 9]);
      }
    }
    // Synthetic art: random alpha, masks with both channels, odd shapes, sizes up to the 1024 px
    // raster bound.
    for (int round = 0; round < 600; ++round) {
      const int sw = 1 + static_cast<int>(rng() % 40), sh = 1 + static_cast<int>(rng() % 40);
      Image source{sw, sh, std::vector<unsigned char>(static_cast<std::size_t>(sw * sh * 4))},
          mask = source;
      for (std::size_t p = 0; p < source.pixels.size(); p += 4) {
        const bool opaque = rng() % 3;
        for (int k = 0; k < 3; ++k) {
          source.pixels[p + static_cast<std::size_t>(k)] = static_cast<unsigned char>(rng());
        }
        source.pixels[p + 3] = opaque ? static_cast<unsigned char>(rng()) : 0;
        mask.pixels[p] = rng() % 4 == 0 ? static_cast<unsigned char>(rng()) : 0;
        mask.pixels[p + 1] = rng() % 4 == 0 ? static_cast<unsigned char>(rng()) : 0;
        mask.pixels[p + 2] = 0;
        mask.pixels[p + 3] = rng() % 2 ? 255 : 0;
      }
      const int size = (rng() % 40 == 0) ? 400 + static_cast<int>(rng() % 10000)
                                         : 1 + static_cast<int>(rng() % 300);
      same(source, rng() % 2 ? &mask : nullptr, size, rng() % 2, dyes[rng() % 9], dyes[rng() % 9]);
    }
    if (argc > 1) {  // --profile
      const Image source = images[images.size() / 2];
      const Image* mask = &images[images.size() / 2 + 1];
      for (const bool reference : {true, false}) {
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 300; ++i) {
          (void)(reference ? appearances.compose_reference(source, mask, 100 + i % 3 * 25, false,
                                                           dyes[i % 9], dyes[(i / 3) % 9])
                           : appearances.compose(source, mask, 100 + i % 3 * 25, false, dyes[i % 9],
                                                 dyes[(i / 3) % 9]));
        }
        std::cout << (reference ? "reference" : "current") << " 300 composes: "
                  << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                               t0)
                         .count()
                  << " ms\n";
      }
    }
    outline_checks(appearances, checks);
    unit_zoom_identity(appearances, animations, checks);
    nearest_art_checks(appearances, checks);
    page_cap_checks(appearances, checks);
    purge_checks(animations, checks);
    std::cout << checks << " compose differential checks passed\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
