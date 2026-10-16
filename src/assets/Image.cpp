#include "realm/assets/Image.hpp"
#include <png.h>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <vector>

namespace realm {
void ui_rounded_rectangle(Image& dst, double x, double y, double width, double height,
                          double radius, std::uint32_t color, double opacity, bool premultiplied) {
  auto shape = ui_image(width, height, dst.pixel_scale);
  const double scale = shape.pixel_scale;
  const double corner_radius = std::min({radius, width / 2, height / 2});
  for (int py = 0; py < shape.height; ++py) {
    for (int px = 0; px < shape.width; ++px) {
      const double dx =
          std::max(std::abs((px + .5) / scale - width / 2) - (width / 2 - corner_radius), 0.0);
      const double dy =
          std::max(std::abs((py + .5) / scale - height / 2) - (height / 2 - corner_radius), 0.0);
      const auto pixel =
          (static_cast<std::size_t>(py) * shape.width + static_cast<std::size_t>(px)) * 4;
      shape.pixels[pixel + 3] = static_cast<unsigned char>(std::lround(
          std::clamp(.5 + (corner_radius - std::hypot(dx, dy)) * scale, 0.0, 1.0) * opacity * 255));
      for (unsigned c = 0; c < 3; ++c) {
        shape.pixels[pixel + c] = static_cast<unsigned char>((color >> (16 - c * 8)) & 255U);
      }
    }
  }
  ui_blit(dst, shape, x, y, false, premultiplied);
}

void ui_shadow(Image& image, double radius, double width, double height) {
  const int w = std::min(image.width, static_cast<int>(std::ceil(width * image.pixel_scale)));
  const int h = std::min(image.height, static_cast<int>(std::ceil(height * image.pixel_scale)));
  const int radius_pixels = std::max(1, static_cast<int>(std::round(radius * image.pixel_scale)));
  const unsigned taps = static_cast<unsigned>(radius_pixels * 2 + 1);
  std::vector<unsigned> horizontal(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));

  for (int y = 0; y < h; ++y) {
    unsigned sum{};
    for (int x = -radius_pixels; x < w; ++x) {
      if (x + radius_pixels < w) {
        sum += image.pixels[(static_cast<std::size_t>(y) * image.width +
                             static_cast<std::size_t>(x + radius_pixels)) *
                                4 +
                            3];
      }
      if (x - radius_pixels - 1 >= 0) {
        sum -= image.pixels[(static_cast<std::size_t>(y) * image.width +
                             static_cast<std::size_t>(x - radius_pixels - 1)) *
                                4 +
                            3];
      }
      if (x >= 0) {
        horizontal[static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)] = sum / taps;
      }
    }
  }

  for (int x = 0; x < w; ++x) {
    unsigned sum{};
    for (int y = -radius_pixels; y < h; ++y) {
      if (y + radius_pixels < h) {
        sum += horizontal[static_cast<std::size_t>(y + radius_pixels) * w +
                          static_cast<std::size_t>(x)];
      }
      if (y - radius_pixels - 1 >= 0) {
        sum -= horizontal[static_cast<std::size_t>(y - radius_pixels - 1) * w +
                          static_cast<std::size_t>(x)];
      }
      if (y >= 0) {
        auto& a =
            image.pixels[(static_cast<std::size_t>(y) * image.width + static_cast<std::size_t>(x)) *
                             4 +
                         3];
        a = static_cast<unsigned char>(a + (sum / taps) * (255U - a) / 255U);
      }
    }
  }

  for (std::size_t i = 0; i < image.pixels.size(); i += 4) {
    if (const unsigned alpha = image.pixels[i + 3]; alpha && alpha != 255) {
      for (unsigned c = 0; c < 3; ++c) {
        image.pixels[i + c] =
            static_cast<unsigned char>(std::min(255U, image.pixels[i + c] * 255U / alpha));
      }
    }
  }
}

void ui_outline_alpha(Image& image, double radius) {
  const int w = image.width, h = image.height;
  // The outline is a grayscale dilation of coverage by a disc of `radius` device-scaled pixels,
  // with a one-pixel antialiased rim so curves and diagonals stay smooth.
  const double r = std::max(1.0, radius * image.pixel_scale);
  const int reach = static_cast<int>(std::ceil(r + 1));
  struct Tap {
    int dx, dy;
    unsigned weight;
  };
  std::vector<Tap> taps;
  for (int dy = -reach; dy <= reach; ++dy) {
    for (int dx = -reach; dx <= reach; ++dx) {
      const double weight = std::clamp(r + 1 - std::hypot(dx, dy), 0.0, 1.0);
      if (weight > 0 && (dx || dy)) {
        taps.push_back({dx, dy, static_cast<unsigned>(std::lround(weight * 256))});
      }
    }
  }
  std::vector<unsigned char> source(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
  for (std::size_t i = 0; i < source.size(); ++i) {
    source[i] = image.pixels[i * 4 + 3];
  }
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const auto at = static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                      static_cast<std::size_t>(x);
      const unsigned alpha = source[at];
      if (alpha == 255) {
        continue;
      }
      unsigned outline{};
      for (const auto& tap : taps) {
        const int sx = x + tap.dx, sy = y + tap.dy;
        if (sx < 0 || sy < 0 || sx >= w || sy >= h) {
          continue;
        }
        outline = std::max(outline, source[static_cast<std::size_t>(sy) * w + sx] * tap.weight);
      }
      outline = std::min(255U, (outline + 128U) >> 8U);
      image.pixels[at * 4 + 3] =
          static_cast<unsigned char>(alpha + (outline * (255U - alpha) + 127U) / 255U);
    }
  }
}

Image ui_image(double width, double height, double scale) {
  if (!std::isfinite(scale) || scale <= 0 || !std::isfinite(width) || !std::isfinite(height) ||
      width <= 0 || height <= 0 || width * scale > 8192 || height * scale > 8192) {
    throw std::invalid_argument("Invalid UI surface dimensions");
  }
  const int w = static_cast<int>(std::ceil(width * scale));
  const int h = static_cast<int>(std::ceil(height * scale));
  return {w, h,
          std::vector<unsigned char>(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4),
          scale};
}

OpaqueBounds opaque_bounds(const Image& source) {
  OpaqueBounds bounds{source.width, source.height, 0, 0};
  for (int y = 0; y < source.height; ++y) {
    for (int x = 0; x < source.width; ++x) {
      if (source.pixels[static_cast<std::size_t>((y * source.width + x) * 4 + 3)] >= 128) {
        bounds.left = std::min(bounds.left, x);
        bounds.top = std::min(bounds.top, y);
        bounds.right = std::max(bounds.right, x + 1);
        bounds.bottom = std::max(bounds.bottom, y + 1);
      }
    }
  }
  if (bounds.right <= bounds.left) {
    return {};
  }
  return bounds;
}

namespace {
// std::lround for a non-negative value without the libm call: truncate, then round the fraction
// half up (exact, as v - r is exact).
inline long round_up(double value) noexcept {
  const auto integer = static_cast<long>(value);
  return value - static_cast<double>(integer) >= .5 ? integer + 1 : integer;
}

bool near_integer(double value) {
  return std::abs(value - std::round(value)) < 1e-9;
}

// The source texels one destination pixel overlaps along an axis: texels first .. first + count -
// 1, with their area weights (summing to one) stored from `offset`. Texels outside the source are
// transparent and left out.
struct TexelSpan {
  int first{}, count{};
  std::size_t offset{};
};

void texel_spans(int begin, int end, int origin, double ratio, int size,
                 std::vector<TexelSpan>& spans, std::vector<double>& weights) {
  spans.resize(static_cast<std::size_t>(end - begin));
  weights.clear();
  for (int p = begin; p < end; ++p) {
    // Destination pixel p covers the source interval [a, b) along this axis.
    const double a = (p - origin) * ratio, b = a + ratio;
    auto& span = spans[static_cast<std::size_t>(p - begin)];
    span = {static_cast<int>(std::floor(a)), 0, weights.size()};
    for (int t = span.first; t < b && t < size; ++t) {
      weights.push_back((std::min(b, t + 1.0) - std::max(a, static_cast<double>(t))) / ratio);
      ++span.count;
    }
  }
}
}

void ui_blit(Image& dst, const Image& src, double x, double y, bool src_pm, bool dst_pm,
             double opacity, double cx, double cy, double cw, double ch) {
  const double scale = dst.pixel_scale;
  const double ratio = src.pixel_scale / scale;
  const int ox = static_cast<int>(std::round(x * scale));
  const int oy = static_cast<int>(std::round(y * scale));
  const int left = std::max({0, ox, static_cast<int>(std::floor(cx * scale))});
  const int top = std::max({0, oy, static_cast<int>(std::floor(cy * scale))});
  const int right = static_cast<int>(std::ceil(
      std::min({static_cast<double>(dst.width), ox + src.width / ratio, (cx + cw) * scale})));
  const int bottom = static_cast<int>(std::ceil(
      std::min({static_cast<double>(dst.height), oy + src.height / ratio, (cy + ch) * scale})));
  if (top >= bottom || left >= right) {
    return;
  }
  // Every blended term is non-negative, so round_up equals std::lround here.
  const auto blend = [&](std::size_t to, const double value[3], double alpha) {
    const double background = dst.pixels[to + 3] / 255.0;
    const double combined = alpha + background * (1 - alpha);
    for (unsigned c = 0; c < 3; ++c) {
      const double old = dst.pixels[to + c] * (dst_pm ? 1 : background);
      dst.pixels[to + c] = static_cast<unsigned char>(
          std::clamp(round_up((value[c] + old * (1 - alpha)) / (dst_pm ? 1 : combined)), 0L, 255L));
    }
    dst.pixels[to + 3] = static_cast<unsigned char>(round_up(combined * 255));
  };
  // Integer magnification or reduction keeps nearest sampling: each destination pixel takes one
  // texel, so a block of texels stays exact. Other ratios take the area each destination pixel
  // covers. Every texel then keeps a footprint of the same width, with at most one partly covered
  // destination pixel at each edge, instead of widths that alternate between floor and ceil.
  if (near_integer(ratio) || near_integer(1 / ratio)) {
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
        const double value[3] = {
            src_pm ? src.pixels[from] * opacity : src.pixels[from] * a,
            src_pm ? src.pixels[from + 1] * opacity : src.pixels[from + 1] * a,
            src_pm ? src.pixels[from + 2] * opacity : src.pixels[from + 2] * a};
        blend(to, value, a);
      }
    }
    return;
  }
  // Premultiplied color and alpha are averaged over the overlap weights; straight colors are
  // premultiplied first so that partly covered texels do not bring their color into fully
  // transparent neighbours.
  static thread_local std::vector<TexelSpan> columns, rows;
  static thread_local std::vector<double> column_weights, row_weights;
  texel_spans(left, right, ox, ratio, src.width, columns, column_weights);
  texel_spans(top, bottom, oy, ratio, src.height, rows, row_weights);
  for (int py = top; py < bottom; ++py) {
    const auto& row = rows[static_cast<std::size_t>(py - top)];
    for (int px = left; px < right; ++px) {
      const auto& column = columns[static_cast<std::size_t>(px - left)];
      double premultiplied[3]{}, alpha_sum{};
      for (int i = 0; i < row.count; ++i) {
        const double wy = row_weights[row.offset + static_cast<std::size_t>(i)];
        for (int j = 0; j < column.count; ++j) {
          const double w = wy * column_weights[column.offset + static_cast<std::size_t>(j)];
          const auto from =
              (static_cast<std::size_t>(row.first + i) * static_cast<std::size_t>(src.width) +
               static_cast<std::size_t>(column.first + j)) *
              4;
          const double alpha = src.pixels[from + 3] * (1.0 / 255.0);
          alpha_sum += w * alpha;
          for (unsigned c = 0; c < 3; ++c) {
            premultiplied[c] += w * (src_pm ? src.pixels[from + c] : src.pixels[from + c] * alpha);
          }
        }
      }
      const double a = alpha_sum * opacity;
      if (a <= 0) {
        continue;
      }
      const double value[3] = {premultiplied[0] * opacity, premultiplied[1] * opacity,
                               premultiplied[2] * opacity};
      const auto to = (static_cast<std::size_t>(py) * static_cast<std::size_t>(dst.width) +
                       static_cast<std::size_t>(px)) *
                      4;
      blend(to, value, a);
    }
  }
}

void ui_rectangle(Image& dst, double x, double y, double width, double height, std::uint32_t color,
                  bool dst_pm) {
  const double scale = dst.pixel_scale;
  x *= scale;
  y *= scale;
  width *= scale;
  height *= scale;
  const int top = std::max(0, static_cast<int>(std::floor(y))),
            bottom = std::min(dst.height, static_cast<int>(std::ceil(y + height)));
  const int left = std::max(0, static_cast<int>(std::floor(x))),
            right = std::min(dst.width, static_cast<int>(std::ceil(x + width)));
  if (top >= bottom || left >= right) {
    return;
  }
  // Coverage is separable: one value per column and per row instead of recomputing both for every
  // pixel. A fully covered pixel is exactly the source color and alpha 255 (the general formula
  // reduces to that), so it skips the blend.
  const unsigned char solid[4] = {static_cast<unsigned char>(color >> 16U),
                                  static_cast<unsigned char>(color >> 8U),
                                  static_cast<unsigned char>(color), 255};
  static thread_local std::vector<double> columns;
  columns.resize(static_cast<std::size_t>(right - left));
  for (int px = left; px < right; ++px) {
    columns[static_cast<std::size_t>(px - left)] =
        std::clamp(std::min(x + width, px + 1.0) - std::max(x, static_cast<double>(px)), 0.0, 1.0);
  }
  for (int py = top; py < bottom; ++py) {
    const double row =
        std::clamp(std::min(y + height, py + 1.0) - std::max(y, static_cast<double>(py)), 0.0, 1.0);
    for (int px = left; px < right; ++px) {
      const double alpha = columns[static_cast<std::size_t>(px - left)] * row;
      const auto to = (static_cast<std::size_t>(py) * static_cast<std::size_t>(dst.width) +
                       static_cast<std::size_t>(px)) *
                      4;
      if (alpha == 1.0) {
        std::copy_n(solid, 4, dst.pixels.data() + to);
        continue;
      }
      const double background = dst.pixels[to + 3] / 255.0;
      const double combined = alpha + background * (1 - alpha);
      if (combined <= 0) {
        continue;
      }
      for (unsigned c = 0; c < 3; ++c) {
        dst.pixels[to + c] = static_cast<unsigned char>(
            std::min(round_up((((color >> (16 - c * 8)) & 255U) * alpha +
                               dst.pixels[to + c] * (dst_pm ? 1 : background) * (1 - alpha)) /
                              (dst_pm ? 1 : combined)),
                     255L));
      }
      dst.pixels[to + 3] = static_cast<unsigned char>(round_up(combined * 255));
    }
  }
}

void normalize_flash_png(Image& image) {
  for (std::size_t i = 0; i < image.pixels.size(); i += 4) {
    const unsigned alpha = image.pixels[i + 3];
    if (alpha == 255) {
      continue;
    }
    for (unsigned c = 0; c < 3; ++c) {
      image.pixels[i + c] = static_cast<unsigned char>(
          alpha ? ((static_cast<unsigned>(image.pixels[i + c]) * alpha >> 8U) << 8U) / alpha : 0);
    }
  }
}

void premultiply_flash_bitmap(Image& image) {
  for (std::size_t i = 0; i < image.pixels.size(); i += 4) {
    for (unsigned c = 0; c < 3; ++c) {
      if (image.pixels[i + 3] != 255) {
        image.pixels[i + c] = static_cast<unsigned char>(
            (static_cast<unsigned>(image.pixels[i + c]) * image.pixels[i + 3] + 255U) >> 8U);
      }
    }
  }
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
  if (!png_image_begin_read_from_file(&png, path.string().c_str())) {
    throw std::runtime_error("Cannot read PNG " + path.string() + ": " + png.message);
  }
  return finish_png(png);
}

Image load_png(std::span<const unsigned char> bytes) {
  png_image png{};
  png.version = PNG_IMAGE_VERSION;
  if (!png_image_begin_read_from_memory(&png, bytes.data(), bytes.size())) {
    throw std::runtime_error(std::string("Cannot read bundled PNG: ") + png.message);
  }
  return finish_png(png);
}

void save_png(const std::filesystem::path& path, const Image& image) {
  if (image.width <= 0 || image.height <= 0 ||
      image.pixels.size() !=
          static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4) {
    throw std::invalid_argument("Invalid PNG image data");
  }
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path());
  }
  png_image png{};
  png.version = PNG_IMAGE_VERSION;
  png.width = static_cast<png_uint_32>(image.width);
  png.height = static_cast<png_uint_32>(image.height);
  png.format = PNG_FORMAT_RGBA;
  if (!png_image_write_to_file(&png, path.string().c_str(), 0, image.pixels.data(), 0, nullptr)) {
    throw std::runtime_error("Cannot write PNG " + path.string() + ": " + png.message);
  }
}

}
