#include "realm/ui/GraphicsUtil.hpp"
#include <algorithm>
#include <cmath>

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
            length > 0 ? std::clamp(((p.x - start.x) * dx + (p.y - start.y) * dy) / length, 0.0,
                                    1.0)
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
        over(*fill, std::clamp(.5 + (inside ? distance : -distance) * scale, 0.0, 1.0) * fill_alpha);
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
