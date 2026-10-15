// ui_rectangle against the original per-pixel implementation (kept here verbatim as the oracle):
// identical bytes for random geometry, scales, colors, premultiplied and straight destinations,
// partially transparent backgrounds.
#include "realm/assets/Image.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <random>
#include <utility>
#include <stdexcept>

namespace {
void reference(realm::Image& dst, double x, double y, double w, double h, std::uint32_t color,
               bool pm) {
  const double s = dst.pixel_scale;
  x *= s;
  y *= s;
  w *= s;
  h *= s;
  for (int py = std::max(0, static_cast<int>(std::floor(y)));
       py < std::min(dst.height, static_cast<int>(std::ceil(y + h))); ++py) {
    for (int px = std::max(0, static_cast<int>(std::floor(x)));
         px < std::min(dst.width, static_cast<int>(std::ceil(x + w))); ++px) {
      const double a =
          std::clamp(std::min(x + w, px + 1.0) - std::max(x, static_cast<double>(px)), 0.0, 1.0) *
          std::clamp(std::min(y + h, py + 1.0) - std::max(y, static_cast<double>(py)), 0.0, 1.0);
      const auto to = (static_cast<std::size_t>(py) * static_cast<std::size_t>(dst.width) +
                       static_cast<std::size_t>(px)) *
                      4;
      const double da = dst.pixels[to + 3] / 255.0, combined = a + da * (1 - a);
      if (combined <= 0) {
        continue;
      }
      for (unsigned c = 0; c < 3; ++c) {
        dst.pixels[to + c] = static_cast<unsigned char>(
            std::clamp(std::lround((((color >> (16 - c * 8)) & 255U) * a +
                                    dst.pixels[to + c] * (pm ? 1 : da) * (1 - a)) /
                                   (pm ? 1 : combined)),
                       0L, 255L));
      }
      dst.pixels[to + 3] = static_cast<unsigned char>(std::lround(combined * 255));
    }
  }
}
}

// ui_blit as it was before area-weighted sampling, kept verbatim as the oracle for integer ratios.
void reference_blit(realm::Image& dst, const realm::Image& src, double x, double y, bool src_pm,
                    bool dst_pm, double opacity) {
  const double s = dst.pixel_scale, ratio = src.pixel_scale / s;
  const int ox = static_cast<int>(std::round(x * s)), oy = static_cast<int>(std::round(y * s));
  const int left = std::max(0, ox), top = std::max(0, oy);
  const int right =
      static_cast<int>(std::ceil(std::min(static_cast<double>(dst.width), ox + src.width / ratio)));
  const int bottom = static_cast<int>(
      std::ceil(std::min(static_cast<double>(dst.height), oy + src.height / ratio)));
  for (int py = top; py < bottom; ++py) {
    for (int px = left; px < right; ++px) {
      const int sx = std::clamp(static_cast<int>((px - ox + .5) * ratio), 0, src.width - 1),
                sy = std::clamp(static_cast<int>((py - oy + .5) * ratio), 0, src.height - 1);
      const auto from = (static_cast<std::size_t>(sy) * static_cast<std::size_t>(src.width) +
                         static_cast<std::size_t>(sx)) *
                        4;
      const auto to = (static_cast<std::size_t>(py) * static_cast<std::size_t>(dst.width) +
                       static_cast<std::size_t>(px)) *
                      4;
      const double a = src.pixels[from + 3] / 255.0 * opacity;
      if (a <= 0) {
        continue;
      }
      const double da = dst.pixels[to + 3] / 255.0, combined = a + da * (1 - a);
      for (unsigned c = 0; c < 3; ++c) {
        const double value = src_pm ? src.pixels[from + c] * opacity : src.pixels[from + c] * a;
        const double old = dst.pixels[to + c] * (dst_pm ? 1 : da);
        dst.pixels[to + c] = static_cast<unsigned char>(
            std::clamp(std::lround((value + old * (1 - a)) / (dst_pm ? 1 : combined)), 0L, 255L));
      }
      dst.pixels[to + 3] = static_cast<unsigned char>(std::lround(combined * 255));
    }
  }
}

// Integer ratios must equal the nearest oracle byte for byte. Fractional ratios use area coverage:
// a one-texel line covers the same total width on every row (1.5 device pixels at 1.5x), at most
// two pixels are partly covered per texel, and premultiplied sampling keeps the source color at
// partial coverage.
long blit_checks() {
  std::mt19937 rng(11);
  std::uniform_real_distribution<double> unit(0, 1);
  long checks{};
  // {destination scale, source scale}: ratios 1, 2, 1/2, 1/3 and 2 are integer, so they take the
  // nearest path.
  const std::pair<double, double> integer_pairs[] = {{1, 1}, {2, 1},    {3, 1},  {1, 2},
                                                     {2, 2}, {1.5, .5}, {1, .5}, {4, 2}};
  for (int round = 0; round < 400; ++round) {
    for (const auto& [dst_scale, src_scale] : integer_pairs) {
      const int sw = 1 + static_cast<int>(rng() % 12), sh = 1 + static_cast<int>(rng() % 12);
      realm::Image src{sw, sh, std::vector<unsigned char>(static_cast<std::size_t>(sw * sh * 4))};
      src.pixel_scale = src_scale;
      for (auto& v : src.pixels) {
        v = static_cast<unsigned char>(rng());
      }
      const bool src_pm = rng() % 2 == 0, dst_pm = rng() % 2 == 0;
      realm::Image a{20, 20, std::vector<unsigned char>(20 * 20 * 4)};
      a.pixel_scale = dst_scale;
      for (auto& v : a.pixels) {
        v = static_cast<unsigned char>(rng());
      }
      auto b = a;
      const double x = unit(rng) * 24 - 4, y = unit(rng) * 24 - 4,
                   opacity = rng() % 2 ? 1 : unit(rng);
      realm::ui_blit(a, src, x, y, src_pm, dst_pm, opacity);
      reference_blit(b, src, x, y, src_pm, dst_pm, opacity);
      ++checks;
      if (a.pixels != b.pixels) {
        throw std::runtime_error("integer-ratio ui_blit differs from nearest sampling");
      }
    }
  }

  // One opaque texel column on a transparent source, at 1.5x and several sub-pixel origins: every
  // row carries the same total coverage, one texel's width, with at most two partly covered device
  // pixels.
  for (int texel = 0; texel < 8; ++texel) {
    for (const double x : {0., .3, .7}) {
      realm::Image line{8, 2, std::vector<unsigned char>(8 * 2 * 4, 0)};
      line.pixel_scale = 1;
      for (int row = 0; row < 2; ++row) {
        line.pixels[static_cast<std::size_t>((row * 8 + texel) * 4 + 3)] = 255;
      }
      realm::Image dst = realm::ui_image(40, 2, 1.5);
      realm::ui_blit(dst, line, x, 0);
      for (int row = 0; row < dst.height; ++row) {
        int total = 0, partial = 0;
        for (int column = 0; column < dst.width; ++column) {
          const int alpha =
              dst.pixels[static_cast<std::size_t>((row * dst.width + column) * 4 + 3)];
          total += alpha;
          partial += alpha > 0 && alpha < 255;
        }
        if (std::abs(total - 1.5 * 255) > 2 || partial > 2) {
          throw std::runtime_error("fractional-ratio ui_blit line width is not uniform");
        }
        ++checks;
      }
    }
  }

  // Straight red texel beside transparency at 1.8x: partly covered pixels keep the red color, not a
  // darkened blend.
  realm::Image red{2, 1, std::vector<unsigned char>{255, 0, 0, 255, 0, 0, 0, 0}};
  red.pixel_scale = 1;
  realm::Image dst = realm::ui_image(10, 1, 1.8);
  realm::ui_blit(dst, red, 0, 0);
  for (int column = 0; column < dst.width; ++column) {
    const auto at = static_cast<std::size_t>(column) * 4;
    if (dst.pixels[at + 3] == 0) {
      continue;
    }
    if (dst.pixels[at] < 254 || dst.pixels[at + 1] > 1 || dst.pixels[at + 2] > 1) {
      throw std::runtime_error("partial coverage darkened the straight-alpha color");
    }
    ++checks;
  }
  return checks;
}

int main(int argc, char**) {
  try {
    std::mt19937 rng(5);
    std::uniform_real_distribution<double> unit(0, 1);
    long checks{};
    for (int round = 0; round < 4000; ++round) {
      const int size = 8 + static_cast<int>(rng() % 120);
      const double scale = (rng() % 3 == 0) ? 1 : .5 + unit(rng) * 3;
      realm::Image a{size, size,
                     std::vector<unsigned char>(static_cast<std::size_t>(size * size * 4))};
      a.pixel_scale = scale;
      for (auto& v : a.pixels) {
        v = static_cast<unsigned char>(rng());
      }
      if (rng() % 2) {
        for (std::size_t i = 3; i < a.pixels.size(); i += 4) {
          a.pixels[i] = 255;
        }
      }
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
        if (a.pixels != b.pixels) {
          throw std::runtime_error("ui_rectangle differs from the original implementation");
        }
      }
    }
    if (argc > 1) {  // --profile: 6000 minimap markers at 1080p density
      realm::Image image{192 * 2, 192 * 2, std::vector<unsigned char>(192 * 2 * 192 * 2 * 4, 255)};
      image.pixel_scale = 1.8;
      for (int variant = 0; variant < 2; ++variant) {
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 6000; ++i) {
          const double x = 94 + unit(rng) * 4, y = 94 + unit(rng) * 4;
          if (variant) {
            reference(image, x, y, 4, 4, 0xffff00, false);
          } else {
            realm::ui_rectangle(image, x, y, 4, 4, 0xffff00);
          }
        }
        std::cout << (variant ? "original" : "current") << " 6000 markers: "
                  << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                               t0)
                         .count()
                  << " ms\n";
      }
      // HUD-sized: 2000 sidebar-tile blits (40x40 logical) onto the 200x600 sidebar at 1.8x.
      realm::Image tile{40, 40, std::vector<unsigned char>(40 * 40 * 4, 180)};
      realm::Image sidebar = realm::ui_image(200, 600, 1.8);
      for (int variant = 0; variant < 2; ++variant) {
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 2000; ++i) {
          const double x = 14 + (i % 4) * 44, y = 300 + (i / 4 % 8) * 44;
          if (variant) {
            reference_blit(sidebar, tile, x, y, true, false, 1);
          } else {
            realm::ui_blit(sidebar, tile, x, y, true, false);
          }
        }
        std::cout << (variant ? "nearest" : "area") << " 2000 tile blits: "
                  << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                               t0)
                         .count()
                  << " ms\n";
      }
    }
    std::cout << checks << " ui_rectangle differential checks passed\n";
    std::cout << blit_checks() << " ui_blit area and nearest checks passed\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
