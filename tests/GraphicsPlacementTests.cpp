// Graphics helpers whose fast paths must match the original arithmetic bit for bit: ui_path's
// band-limited coverage (against the original per-pixel loop, kept here as the reference) and
// FontBook's place_text memo (a memo hit must equal a fresh render).
#include "realm/assets/FontBook.hpp"
#include "realm/ui/GraphicsUtil.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace {
int checks{};

void check(bool ok, const char* why) {
  ++checks;
  if (!ok) {
    throw std::runtime_error(why);
  }
}

// Deterministic generator: the same cases on every run.
std::uint64_t state = 0x9e3779b97f4a7c15ULL;

std::uint64_t next() {
  state ^= state << 13U;
  state ^= state >> 7U;
  state ^= state << 17U;
  return state;
}

double unit() {
  return static_cast<double>(next() >> 11U) / 9007199254740992.0;
}

double range(double low, double high) {
  return low + (high - low) * unit();
}

namespace reference {
using realm::Image;
using realm::Vec2;

void reference_ui_path(Image& image, std::span<const Vec2> path, std::optional<std::uint32_t> fill,
                       std::optional<std::uint32_t> stroke, double stroke_width, double fill_alpha,
                       double clip_right) {
  if (path.size() < 2) {
    return;
  }
  const double scale = image.pixel_scale;
  const double radius = stroke ? stroke_width / 2 : 0;
  double left = 1e9;
  double top = 1e9;
  double right = -1e9;
  double bottom = -1e9;
  for (const auto p : path) {
    left = std::min(left, p.x);
    right = std::max(right, p.x);
    top = std::min(top, p.y);
    bottom = std::max(bottom, p.y);
  }
  right = std::min(right, clip_right);
  const int x0 = std::max(0, static_cast<int>(std::floor((left - radius - 1) * scale)));
  const int x1 = std::min(image.width, static_cast<int>(std::ceil((right + radius + 1) * scale)));
  const int y0 = std::max(0, static_cast<int>(std::floor((top - radius - 1) * scale)));
  const int y1 = std::min(image.height, static_cast<int>(std::ceil((bottom + radius + 1) * scale)));
  for (int py = y0; py < y1; ++py) {
    for (int px = x0; px < x1; ++px) {
      const Vec2 p{(px + .5) / scale, (py + .5) / scale};
      if (p.x > clip_right) {
        continue;
      }
      bool inside = false;
      double distance = 1e9;
      for (std::size_t i = 0, j = path.size() - 1; i < path.size(); j = i++) {
        const auto start = path[i];
        const auto end = path[j];
        if ((start.y > p.y) != (end.y > p.y) &&
            p.x < (end.x - start.x) * (p.y - start.y) / (end.y - start.y) + start.x) {
          inside = !inside;
        }
        const double dx = end.x - start.x;
        const double dy = end.y - start.y;
        const double length = dx * dx + dy * dy;
        const double fraction =
            length > 0
                ? std::clamp(((p.x - start.x) * dx + (p.y - start.y) * dy) / length, 0.0, 1.0)
                : 0;
        distance = std::min(
            distance, std::hypot(p.x - start.x - fraction * dx, p.y - start.y - fraction * dy));
      }

      auto* pixel =
          &image.pixels[(static_cast<std::size_t>(py) * static_cast<std::size_t>(image.width) +
                         static_cast<std::size_t>(px)) *
                        4];
      const auto over = [&](std::uint32_t color, double alpha) {
        if (alpha <= 0) {
          return;
        }
        for (unsigned channel = 0; channel < 3; ++channel) {
          pixel[channel] = static_cast<unsigned char>(std::lround(
              ((color >> (16 - 8 * channel)) & 255U) * alpha + pixel[channel] * (1 - alpha)));
        }
        pixel[3] = static_cast<unsigned char>(std::lround(alpha * 255 + pixel[3] * (1 - alpha)));
      };
      if (fill) {
        over(*fill,
             std::clamp(.5 + (inside ? distance : -distance) * scale, 0.0, 1.0) * fill_alpha);
      }
      if (stroke) {
        over(*stroke, std::clamp((radius - distance) * scale + .5, 0.0, 1.0));
      }
    }
  }
}

}  // namespace reference

// Fills `image` with a deterministic, partly transparent background so blending has work to do.
void fill_background(realm::Image& image) {
  for (auto& byte : image.pixels) {
    byte = static_cast<unsigned char>(next());
  }
  // Premultiplied: channels never exceed alpha.
  for (std::size_t i = 0; i < image.pixels.size(); i += 4) {
    for (std::size_t c = 0; c < 3; ++c) {
      image.pixels[i + c] = std::min(image.pixels[i + c], image.pixels[i + 3]);
    }
  }
}

void ui_path_matches_reference() {
  const double scales[] = {1, 1024 / 600.0, 2.5, 0.75};
  for (int trial = 0; trial < 400; ++trial) {
    const double scale = scales[trial % 4];
    realm::Image expected = realm::ui_image(range(20, 340), range(20, 420), scale);
    fill_background(expected);
    realm::Image actual = expected;
    std::vector<realm::Vec2> path;
    if (trial % 3 == 0) {
      // Random polygon with a random even-odd shape.
      const int count = 3 + static_cast<int>(next() % 7);
      for (int i = 0; i < count; ++i) {
        path.push_back({range(-10, 330), range(-10, 430)});
      }
    } else {
      path =
          realm::cut_edge_rect(range(0, 30), range(0, 30), range(10, 300), range(10, 400),
                               range(0, 8), {unit() < .8, unit() < .8, unit() < .8, unit() < .8});
    }
    const bool fill = unit() < .85;
    const bool stroke = unit() < .7;
    const double stroke_width = unit() < .5 ? 1 : range(0.25, 3);
    const double fill_alpha = unit() < .5 ? 1 : range(0, 1);
    const double clip_right = unit() < .3 ? range(40, 300) : 1e9;
    const std::optional<std::uint32_t> fill_color =
        fill ? std::optional<std::uint32_t>(static_cast<std::uint32_t>(next() & 0xffffffU))
             : std::nullopt;
    const std::optional<std::uint32_t> stroke_color =
        stroke ? std::optional<std::uint32_t>(static_cast<std::uint32_t>(next() & 0xffffffU))
               : std::nullopt;
    if (!fill_color && !stroke_color) {
      continue;
    }
    reference::reference_ui_path(expected, path, fill_color, stroke_color, stroke_width, fill_alpha,
                                 clip_right);
    realm::ui_path(actual, path, fill_color, stroke_color, stroke_width, fill_alpha, clip_right);
    check(expected.pixels == actual.pixels, "ui_path differs from the reference");
  }
}

// A place_text memo hit returns the bytes a fresh render of the same text produces.
void place_text_hits_match_fresh_renders() {
  realm::FontBook fonts;
  const double scale = 1024 / 600.0;
  fonts.set_pixel_scale(scale);
  const auto first =
      fonts.rasterize("Crowded combat 123", 14, true, realm::TextEffect::outline, 0xffffff);
  // A second recipe for the same text is a separate memo key, so its renders are fresh.
  const auto second =
      fonts.rasterize("Crowded combat 123", 14, true, realm::TextEffect::outline, 0xffffff);
  fonts.set_pixel_scale(1);
  check(static_cast<bool>(first.redraw) && static_cast<bool>(second.redraw),
        "density text has a recipe");
  const double positions[][2] = {{3.25, 7.5}, {3.25, 7.5}, {10.0, 2.75}, {10.0, 2.75}, {0.1, 0.9}};
  for (const auto& position : positions) {
    realm::Image on_first = realm::ui_image(120, 40, scale);
    realm::Image on_second = realm::ui_image(120, 40, scale);
    realm::ui_blit_text(on_first, first, position[0], position[1]);
    realm::ui_blit_text(on_second, second, position[0], position[1]);
    check(on_first.pixels == on_second.pixels, "memoized placement differs from a fresh render");
    double x = position[0], y = position[1];
    double x2 = position[0], y2 = position[1];
    const auto placed = realm::place_text(first, scale, x, y);
    const auto fresh = realm::place_text(second, scale, x2, y2);
    check(placed.image.pixels == fresh.image.pixels && x == x2 && y == y2,
          "place_text result differs from a fresh render");
  }
}
}  // namespace

int main() {
  try {
    ui_path_matches_reference();
    place_text_hits_match_fresh_renders();
  } catch (const std::exception& error) {
    std::cerr << "graphics_placement: " << error.what() << '\n';
    return 1;
  }
  std::cout << "graphics placement checks passed: " << checks << '\n';
  return 0;
}
