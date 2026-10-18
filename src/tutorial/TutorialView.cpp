#include "realm/tutorial/TutorialView.hpp"
#include "realm/assets/FlashDraw.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace realm {
namespace {
constexpr int stage_width = 800;
constexpr int stage_height = 600;
constexpr TutorialRegion stage_region{0, 0, stage_width, stage_height};

bool empty(TutorialRegion region) {
  return region.width <= 0 || region.height <= 0;
}

std::size_t pixel_offset(int width, int x, int y) {
  return (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
          static_cast<std::size_t>(x)) *
         4;
}

TutorialRegion unite(TutorialRegion a, TutorialRegion b) {
  if (empty(a)) {
    return b;
  }
  if (empty(b)) {
    return a;
  }
  const int left = std::min(a.x, b.x), top = std::min(a.y, b.y);
  const int right = std::max(a.x + a.width, b.x + b.width),
            bottom = std::max(a.y + a.height, b.y + b.height);
  return {left, top, right - left, bottom - top};
}

void clear_region(Image& image, TutorialRegion region) {
  if (empty(region)) {
    return;
  }
  const auto row_bytes = static_cast<std::size_t>(region.width) * 4;
  for (int y = region.y; y < region.y + region.height; ++y) {
    std::fill_n(&image.pixels[pixel_offset(image.width, region.x, y)], row_bytes,
                static_cast<unsigned char>(0));
  }
}

// Copies a region of a stage-sized image into `destination`, which takes the region's size.
void copy_region(const Image& source, TutorialRegion region, Image& destination) {
  destination.width = region.width;
  destination.height = region.height;
  destination.pixel_scale = 1;
  const auto row_bytes = static_cast<std::size_t>(region.width) * 4;
  destination.pixels.resize(row_bytes * static_cast<std::size_t>(region.height));
  for (int row = 0; row < region.height; ++row) {
    std::copy_n(&source.pixels[pixel_offset(source.width, region.x, region.y + row)], row_bytes,
                &destination.pixels[static_cast<std::size_t>(row) * row_bytes]);
  }
}

// The stage rectangle that holds every pixel the strokes paint, grown by `reach` px for the blur.
// Strokes paint at most ceil(half width) + 2 px past their points (odd-width shift, antialiasing),
// so with reach = 0 this bounds the stroke canvases, and with the blur's reach it bounds every
// nonzero pixel of the full-stage render. Clamped to the stage.
TutorialRegion touched_region(std::span<const TutorialStroke> strokes, int reach) {
  double left{}, top{}, right{}, bottom{};
  double half{};
  bool any_point = false;
  for (const auto& stroke : strokes) {
    if (!std::isfinite(stroke.width)) {
      return stage_region;
    }
    // Matches flash_draw::stroke, which uses max(width, 1), for both the halo and the core.
    half = std::max(half, std::max({stroke.width * 1.2, stroke.width, 1.0}) / 2);
    for (const auto point : stroke.points) {
      if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
        return stage_region;
      }
      if (!any_point) {
        left = right = point.x;
        top = bottom = point.y;
        any_point = true;
      } else {
        left = std::min(left, point.x);
        right = std::max(right, point.x);
        top = std::min(top, point.y);
        bottom = std::max(bottom, point.y);
      }
    }
  }
  if (!any_point) {
    return {};
  }
  const double margin = std::ceil(half) + 3 + reach;
  const auto clamp_to_stage = [](double value, int limit) {
    return static_cast<int>(std::clamp(value, 0.0, static_cast<double>(limit)));
  };
  const int x0 = clamp_to_stage(std::floor(left - margin), stage_width);
  const int y0 = clamp_to_stage(std::floor(top - margin), stage_height);
  const int x1 = clamp_to_stage(std::ceil(right + margin), stage_width);
  const int y1 = clamp_to_stage(std::ceil(bottom + margin), stage_height);
  if (x1 <= x0 || y1 <= y0) {
    return {};
  }
  return {x0, y0, x1 - x0, y1 - y0};
}

// Strokes compare by exact bit patterns, so a cache hit means the rasterizer gets identical input.
bool same_coordinate(double a, double b) {
  return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

bool same_strokes(std::span<const TutorialStroke> strokes,
                  const std::vector<TutorialStroke>& cached) {
  if (strokes.size() != cached.size()) {
    return false;
  }
  for (std::size_t s = 0; s < strokes.size(); ++s) {
    const auto& a = strokes[s];
    const auto& b = cached[s];
    if (!same_coordinate(a.width, b.width) || a.color != b.color ||
        a.points.size() != b.points.size()) {
      return false;
    }
    for (std::size_t p = 0; p < a.points.size(); ++p) {
      if (!same_coordinate(a.points[p].x, b.points[p].x) ||
          !same_coordinate(a.points[p].y, b.points[p].y)) {
        return false;
      }
    }
  }
  return true;
}

struct Overlay {
  // Straight-alpha output. Zero outside `shown`.
  Image image = flash_draw::canvas(stage_width, stage_height);
  // Premultiplied stroke canvases at stage size, as the original full-stage render draws them.
  // Strokes stay on these canvases because flash_draw::stroke rasterizes in absolute coordinates,
  // so a translated copy would not be bit-identical. Only `stroked` can be nonzero.
  Image halo = flash_draw::canvas(stage_width, stage_height);
  Image core = flash_draw::canvas(stage_width, stage_height);
  // Region-sized scratch, reused between frames.
  Image halo_crop;
  Image core_crop;
  // The strokes last rasterized into halo and core, and the rectangle they may touch. The stroke
  // canvases are only redrawn when the strokes change, since the blur does not alter them.
  std::vector<TutorialStroke> stroked_strokes;
  TutorialRegion stroked{};
  // Output rectangle of the previous call, which may hold pixels in `image`.
  TutorialRegion shown{};
  bool started = false;
};

// Rendering runs on the UI thread only.
Overlay& overlay_state() {
  static Overlay state;
  return state;
}
}

TutorialRegion render_tutorial_overlay(std::span<const TutorialStroke> strokes, std::int32_t time) {
  auto& state = overlay_state();
  const TutorialRegion previous = state.shown;
  const double blur = 5 + std::abs(std::sin(time / 300.0)) * 5;
  const int reach = std::max(0, static_cast<int>(std::ceil(blur / 2 - .5)));

  if (!same_strokes(strokes, state.stroked_strokes)) {
    clear_region(state.halo, state.stroked);
    clear_region(state.core, state.stroked);
    state.stroked = touched_region(strokes, 0);
    state.stroked_strokes.assign(strokes.begin(), strokes.end());
    if (!empty(state.stroked)) {
      for (const auto& stroke : strokes) {
        flash_draw::Path path;
        for (std::size_t i = 0; i < stroke.points.size(); ++i) {
          const auto point = stroke.points[i];
          if (i == 0) {
            path.move(point.x, point.y);
          } else {
            path.line(point.x, point.y);
          }
        }
        flash_draw::stroke(state.halo, path, stroke.width * 1.2, stroke.color);
        flash_draw::stroke(state.core, path, stroke.width, stroke.color);
      }
    }
  }

  const TutorialRegion region = touched_region(strokes, reach);
  clear_region(state.image, previous);
  if (!empty(region)) {
    // The blur, composite and un-premultiply only read or write `region`. It contains every
    // nonzero stroke pixel and the blur's spread, so pixels outside it are zero in the full-stage
    // render and the result matches it exactly inside the region.
    copy_region(state.halo, region, state.halo_crop);
    copy_region(state.core, region, state.core_crop);
    flash_draw::blur(state.halo_crop, blur, blur);
    flash_draw::draw(state.halo_crop, state.core_crop, 0, 0);
    for (int row = 0; row < region.height; ++row) {
      const auto* input =
          &state.halo_crop
               .pixels[static_cast<std::size_t>(row) * static_cast<std::size_t>(region.width) * 4];
      auto* output = &state.image.pixels[pixel_offset(stage_width, region.x, region.y + row)];
      for (int column = 0; column < region.width; ++column, input += 4, output += 4) {
        const unsigned alpha = input[3];
        for (std::size_t c = 0; c < 3; ++c) {
          output[c] = static_cast<unsigned char>(
              alpha ? std::min(255U, static_cast<unsigned>(input[c]) * 255 / alpha) : 0);
        }
        output[3] = input[3];
      }
    }
  }

  state.shown = region;
  if (!state.started) {
    // The consumer's surface does not exist until it receives the whole stage once.
    state.started = true;
    return stage_region;
  }
  return unite(previous, region);
}

const Image& tutorial_overlay_image() {
  return overlay_state().image;
}

Image render_tutorial(std::span<const TutorialStroke> strokes, std::int32_t time) {
  static_cast<void>(render_tutorial_overlay(strokes, time));
  return overlay_state().image;
}
}
