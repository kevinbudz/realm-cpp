#include "realm/ui/GraphicsUtil.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace realm {
std::vector<Vec2> cut_edge_rect(double x, double y, double width, double height, double cut,
                                std::array<bool, 4> corners) {
  std::vector<Vec2> path;
  if (corners[0]) {
    path.push_back({x, y + cut});
    path.push_back({x + cut, y});
  } else {
    path.push_back({x, y});
  }
  if (corners[1]) {
    path.push_back({x + width - cut, y});
    path.push_back({x + width, y + cut});
  } else {
    path.push_back({x + width, y});
  }
  if (corners[2]) {
    path.push_back({x + width, y + height - cut});
    path.push_back({x + width - cut, y + height});
  } else {
    path.push_back({x + width, y + height});
  }
  if (corners[3]) {
    path.push_back({x + cut, y + height});
    path.push_back({x, y + height - cut});
  } else {
    path.push_back({x, y + height});
  }
  return path;
}

void ui_path(Image& image, std::span<const Vec2> path, std::optional<std::uint32_t> fill,
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
  if (x1 <= x0 || y1 <= y0) {
    return;
  }
  // Past `reach` from every edge, both coverages are saturated: the fill is 1 inside and 0 outside
  // (its ramp spans 1/scale around the edge) and the stroke is 0 (its ramp ends at radius + 1/2
  // px). Only pixels within `reach` of an edge need the exact distance, so the band is marked first
  // and the rest take a saturated distance. The margin keeps rounding of the marking test from
  // deciding a pixel the exact test would not.
  const double reach = radius + .5 / scale;
  const double reach_squared = reach * reach * (1 + 1e-9);
  const auto width = static_cast<std::size_t>(x1 - x0);
  std::vector<unsigned char> band(width * static_cast<std::size_t>(y1 - y0), 0);
  for (std::size_t i = 0, j = path.size() - 1; i < path.size(); j = i++) {
    const auto start = path[i];
    const auto end = path[j];
    const double dx = end.x - start.x;
    const double dy = end.y - start.y;
    const double length = dx * dx + dy * dy;
    const int bx0 =
        std::max(x0, static_cast<int>(std::floor((std::min(start.x, end.x) - reach) * scale)) - 1);
    const int bx1 =
        std::min(x1, static_cast<int>(std::ceil((std::max(start.x, end.x) + reach) * scale)) + 1);
    const int by0 =
        std::max(y0, static_cast<int>(std::floor((std::min(start.y, end.y) - reach) * scale)) - 1);
    const int by1 =
        std::min(y1, static_cast<int>(std::ceil((std::max(start.y, end.y) + reach) * scale)) + 1);
    for (int py = by0; py < by1; ++py) {
      const double y = (py + .5) / scale;
      for (int px = bx0; px < bx1; ++px) {
        const double x = (px + .5) / scale;
        const double fraction =
            length > 0 ? std::clamp(((x - start.x) * dx + (y - start.y) * dy) / length, 0.0, 1.0)
                       : 0;
        const double qx = x - start.x - fraction * dx, qy = y - start.y - fraction * dy;
        if (qx * qx + qy * qy <= reach_squared) {
          band[static_cast<std::size_t>(py - y0) * width + static_cast<std::size_t>(px - x0)] = 1;
        }
      }
    }
  }
  // Crossings are the even-odd edge tests of one row: the row's y and every edge's x at that y are
  // the same expressions the per-pixel test used, so the parity is unchanged.
  std::vector<double> crossings;
  crossings.reserve(path.size());
  for (int py = y0; py < y1; ++py) {
    const double y = (py + .5) / scale;
    crossings.clear();
    for (std::size_t i = 0, j = path.size() - 1; i < path.size(); j = i++) {
      const auto start = path[i];
      const auto end = path[j];
      if ((start.y > y) != (end.y > y)) {
        crossings.push_back((end.x - start.x) * (y - start.y) / (end.y - start.y) + start.x);
      }
    }
    for (int px = x0; px < x1; ++px) {
      const Vec2 p{(px + .5) / scale, y};
      if (p.x > clip_right) {
        continue;
      }
      bool inside = false;
      for (const double crossing : crossings) {
        if (p.x < crossing) {
          inside = !inside;
        }
      }
      double distance = 2 * reach;
      if (band[static_cast<std::size_t>(py - y0) * width + static_cast<std::size_t>(px - x0)]) {
        distance = 1e9;
        for (std::size_t i = 0, j = path.size() - 1; i < path.size(); j = i++) {
          const auto start = path[i];
          const auto end = path[j];
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

void ui_menu_backdrop(Image& image, double logical_width) {
  const double scale = image.pixel_scale;
  for (int py = 0; py < image.height; ++py) {
    const double y = (py + .5) / scale;
    const double ratio = (y - 508) * 255 / 80;
    double alpha = 0;
    if (ratio >= 20 && ratio < 103) {
      alpha = .73 * (ratio - 20) / 83;
    } else if (ratio >= 103 && ratio < 173) {
      alpha = .73;
    } else if (ratio >= 173 && ratio < 237) {
      alpha = .73 * (237 - ratio) / 64;
    }
    const double combined = alpha + .8 * (1 - alpha);
    const auto value = static_cast<unsigned char>(std::lround(43 * .8 * (1 - alpha)));
    for (int px = 0; px < image.width; ++px) {
      const auto at = (static_cast<std::size_t>(py) * static_cast<std::size_t>(image.width) +
                       static_cast<std::size_t>(px)) *
                      4;
      image.pixels[at] = image.pixels[at + 1] = image.pixels[at + 2] = value;
      image.pixels[at + 3] = static_cast<unsigned char>(std::lround(combined * 255));
    }
  }
  ui_rectangle(image, 0, 524, logical_width, 52, 0x545454, true);
}
}
