#include "realm/util/Image.hpp"
#include <png.h>
#include <stdexcept>
#include <algorithm>
#include <cmath>
namespace realm {
void ui_rounded_rectangle(Image& dst, double x, double y, double w, double h, double radius, std::uint32_t color,
                          double opacity, bool pm) {
  auto shape = ui_image(w, h, dst.pixel_scale);
  const double s = shape.pixel_scale, r = std::min({radius, w / 2, h / 2});
  for (int py = 0; py < shape.height; ++py)
    for (int px = 0; px < shape.width; ++px) {
      const double dx = std::max(std::abs((px + .5) / s - w / 2) - (w / 2 - r), 0.0),
                   dy = std::max(std::abs((py + .5) / s - h / 2) - (h / 2 - r), 0.0);
      const auto at = (static_cast<std::size_t>(py) * shape.width + static_cast<std::size_t>(px)) * 4;
      shape.pixels[at + 3] = static_cast<unsigned char>(
          std::lround(std::clamp(.5 + (r - std::hypot(dx, dy)) * s, 0.0, 1.0) * opacity * 255));
      for (unsigned c = 0; c < 3; ++c)
        shape.pixels[at + c] = static_cast<unsigned char>((color >> (16 - c * 8)) & 255U);
    }
  ui_blit(dst, shape, x, y, false, pm);
}
void ui_shadow(Image& image, double radius, double width, double height) {
  const int w = std::min(image.width, static_cast<int>(std::ceil(width * image.pixel_scale))),
            h = std::min(image.height, static_cast<int>(std::ceil(height * image.pixel_scale)));
  const int r = std::max(1, static_cast<int>(std::round(radius * image.pixel_scale)));
  const unsigned taps = static_cast<unsigned>(r * 2 + 1);
  std::vector<unsigned> horizontal(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
  for (int y = 0; y < h; ++y) {
    unsigned sum{};
    for (int x = -r; x < w; ++x) {
      if (x + r < w)
        sum += image.pixels[(static_cast<std::size_t>(y) * image.width + static_cast<std::size_t>(x + r)) * 4 + 3];
      if (x - r - 1 >= 0)
        sum -= image.pixels[(static_cast<std::size_t>(y) * image.width + static_cast<std::size_t>(x - r - 1)) * 4 + 3];
      if (x >= 0) horizontal[static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)] = sum / taps;
    }
  }
  for (int x = 0; x < w; ++x) {
    unsigned sum{};
    for (int y = -r; y < h; ++y) {
      if (y + r < h) sum += horizontal[static_cast<std::size_t>(y + r) * w + static_cast<std::size_t>(x)];
      if (y - r - 1 >= 0) sum -= horizontal[static_cast<std::size_t>(y - r - 1) * w + static_cast<std::size_t>(x)];
      if (y >= 0) {
        auto& a = image.pixels[(static_cast<std::size_t>(y) * image.width + static_cast<std::size_t>(x)) * 4 + 3];
        a = static_cast<unsigned char>(a + (sum / taps) * (255U - a) / 255U);
      }
    }
  }
  for (std::size_t i = 0; i < image.pixels.size(); i += 4)
    if (const unsigned a = image.pixels[i + 3]; a && a != 255)
      for (unsigned c = 0; c < 3; ++c)
        image.pixels[i + c] = static_cast<unsigned char>(std::min(255U, image.pixels[i + c] * 255U / a));
}
Image ui_image(double width, double height, double scale) {
  if (!std::isfinite(scale) || scale <= 0 || !std::isfinite(width) || !std::isfinite(height) || width <= 0 ||
      height <= 0 || width * scale > 8192 || height * scale > 8192)
    throw std::invalid_argument("Invalid UI surface dimensions");
  const int w = static_cast<int>(std::ceil(width * scale)), h = static_cast<int>(std::ceil(height * scale));
  return {w, h, std::vector<unsigned char>(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4), scale};
}
void ui_blit(Image& dst, const Image& src, double x, double y, bool src_pm, bool dst_pm, double opacity, double cx,
             double cy, double cw, double ch) {
  const double s = dst.pixel_scale, ratio = src.pixel_scale / s;
  const int ox = static_cast<int>(std::round(x * s)), oy = static_cast<int>(std::round(y * s));
  const int left = std::max({0, ox, static_cast<int>(std::floor(cx * s))});
  const int top = std::max({0, oy, static_cast<int>(std::floor(cy * s))});
  const int right =
      static_cast<int>(std::ceil(std::min({static_cast<double>(dst.width), ox + src.width / ratio, (cx + cw) * s})));
  const int bottom =
      static_cast<int>(std::ceil(std::min({static_cast<double>(dst.height), oy + src.height / ratio, (cy + ch) * s})));
  for (int py = top; py < bottom; ++py)
    for (int px = left; px < right; ++px) {
      const int sx = std::clamp(static_cast<int>((px - ox + .5) * ratio), 0, src.width - 1),
                sy = std::clamp(static_cast<int>((py - oy + .5) * ratio), 0, src.height - 1);
      const auto from =
          (static_cast<std::size_t>(sy) * static_cast<std::size_t>(src.width) + static_cast<std::size_t>(sx)) * 4;
      const auto to =
          (static_cast<std::size_t>(py) * static_cast<std::size_t>(dst.width) + static_cast<std::size_t>(px)) * 4;
      const double a = src.pixels[from + 3] / 255.0 * opacity;
      if (a <= 0) continue;
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
namespace {
// std::lround for a non-negative value without the libm call: truncate, then round the fraction half up (exact, as v -
// r is exact).
inline long round_up(double v) noexcept {
  const auto r = static_cast<long>(v);
  return v - static_cast<double>(r) >= .5 ? r + 1 : r;
}
}
void ui_rectangle(Image& dst, double x, double y, double w, double h, std::uint32_t color, bool pm) {
  const double s = dst.pixel_scale;
  x *= s;
  y *= s;
  w *= s;
  h *= s;
  const int top = std::max(0, static_cast<int>(std::floor(y))),
            bottom = std::min(dst.height, static_cast<int>(std::ceil(y + h)));
  const int left = std::max(0, static_cast<int>(std::floor(x))),
            right = std::min(dst.width, static_cast<int>(std::ceil(x + w)));
  if (top >= bottom || left >= right) return;
  // Coverage is separable: one value per column and per row instead of recomputing both for every pixel. A fully
  // covered pixel is exactly the source color and alpha 255 (the general formula reduces to that), so it skips the
  // blend.
  const unsigned char solid[4] = {static_cast<unsigned char>(color >> 16U), static_cast<unsigned char>(color >> 8U),
                                  static_cast<unsigned char>(color), 255};
  static thread_local std::vector<double> columns;
  columns.resize(static_cast<std::size_t>(right - left));
  for (int px = left; px < right; ++px)
    columns[static_cast<std::size_t>(px - left)] =
        std::clamp(std::min(x + w, px + 1.0) - std::max(x, static_cast<double>(px)), 0.0, 1.0);
  for (int py = top; py < bottom; ++py) {
    const double row = std::clamp(std::min(y + h, py + 1.0) - std::max(y, static_cast<double>(py)), 0.0, 1.0);
    for (int px = left; px < right; ++px) {
      const double a = columns[static_cast<std::size_t>(px - left)] * row;
      const auto to =
          (static_cast<std::size_t>(py) * static_cast<std::size_t>(dst.width) + static_cast<std::size_t>(px)) * 4;
      if (a == 1.0) {
        std::copy_n(solid, 4, dst.pixels.data() + to);
        continue;
      }
      const double da = dst.pixels[to + 3] / 255.0, combined = a + da * (1 - a);
      if (combined <= 0) continue;
      for (unsigned c = 0; c < 3; ++c)
        dst.pixels[to + c] = static_cast<unsigned char>(
            std::min(round_up((((color >> (16 - c * 8)) & 255U) * a + dst.pixels[to + c] * (pm ? 1 : da) * (1 - a)) /
                              (pm ? 1 : combined)),
                     255L));
      dst.pixels[to + 3] = static_cast<unsigned char>(round_up(combined * 255));
    }
  }
}
void normalize_flash_png(Image& image) {
  for (std::size_t i = 0; i < image.pixels.size(); i += 4) {
    const unsigned alpha = image.pixels[i + 3];
    if (alpha == 255) continue;
    for (unsigned c = 0; c < 3; ++c)
      image.pixels[i + c] = static_cast<unsigned char>(
          alpha ? ((static_cast<unsigned>(image.pixels[i + c]) * alpha >> 8U) << 8U) / alpha : 0);
  }
}
void premultiply_flash_bitmap(Image& image) {
  for (std::size_t i = 0; i < image.pixels.size(); i += 4)
    for (unsigned c = 0; c < 3; ++c)
      if (image.pixels[i + 3] != 255)
        image.pixels[i + c] =
            static_cast<unsigned char>((static_cast<unsigned>(image.pixels[i + c]) * image.pixels[i + 3] + 255U) >> 8U);
}
namespace {
Image finish_png(png_image& png) {
  png.format = PNG_FORMAT_RGBA;
  if (png.width > 8192 || png.height > 8192) {
    png_image_free(&png);
    throw std::runtime_error("PNG exceeds asset size limit");
  }
  Image result{static_cast<int>(png.width), static_cast<int>(png.height),
               std::vector<unsigned char>(PNG_IMAGE_SIZE(png))};
  if (!png_image_finish_read(&png, nullptr, result.pixels.data(), 0, nullptr)) {
    const std::string error = png.message;
    png_image_free(&png);
    throw std::runtime_error(error);
  }
  png_image_free(&png);
  return result;
}
}
Image load_png(const std::filesystem::path& path) {
  png_image png{};
  png.version = PNG_IMAGE_VERSION;
  if (!png_image_begin_read_from_file(&png, path.string().c_str()))
    throw std::runtime_error("Cannot read PNG " + path.string() + ": " + png.message);
  return finish_png(png);
}
Image load_png(std::span<const unsigned char> bytes) {
  png_image png{};
  png.version = PNG_IMAGE_VERSION;
  if (!png_image_begin_read_from_memory(&png, bytes.data(), bytes.size()))
    throw std::runtime_error(std::string("Cannot read bundled PNG: ") + png.message);
  return finish_png(png);
}
void save_png(const std::filesystem::path& path, const Image& image) {
  if (image.width <= 0 || image.height <= 0 ||
      image.pixels.size() != static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4)
    throw std::invalid_argument("Invalid PNG image data");
  if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
  png_image png{};
  png.version = PNG_IMAGE_VERSION;
  png.width = static_cast<png_uint_32>(image.width);
  png.height = static_cast<png_uint_32>(image.height);
  png.format = PNG_FORMAT_RGBA;
  if (!png_image_write_to_file(&png, path.string().c_str(), 0, image.pixels.data(), 0, nullptr))
    throw std::runtime_error("Cannot write PNG " + path.string() + ": " + png.message);
}

}
