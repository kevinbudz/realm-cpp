#include "realm/flash_draw.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <string>
#include <unordered_map>
#include <numbers>
#include <span>
#include <stdexcept>
#include <zlib.h>
namespace realm::flash_draw {
namespace {
constexpr double curve_steps = 8;
unsigned char byte(double value) { return static_cast<unsigned char>(std::clamp(std::lround(value), 0L, 255L)); }
std::size_t at(const Image& image, int x, int y) {
  return (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x)) * 4;
}
// Signed area accumulation (as in font-rs): each edge adds the area it encloses to the right in every row it crosses;
// a running sum along the row then gives the winding coverage of each pixel.
class Coverage {
 public:
  Coverage(int width, int height)
      : width_(width + 2),
        height_(height),
        cells_(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height) + 1) {}
  void line(Vec2 a, Vec2 b) {
    if (a.y == b.y) return;
    const double direction = a.y < b.y ? 1 : -1;
    if (a.y > b.y) std::swap(a, b);
    const auto clamp_x = [&](double x) { return std::clamp(x, 0.0, static_cast<double>(width_ - 2)); };
    const double slope = (b.x - a.x) / (b.y - a.y);
    double x = a.x;
    const double top = std::max(0.0, a.y), bottom = std::min(static_cast<double>(height_), b.y);
    if (a.y < 0) x -= a.y * slope;
    for (int row = static_cast<int>(std::floor(top)); row < bottom; ++row) {
      const double y0 = std::max<double>(row, top), y1 = std::min<double>(row + 1, bottom);
      if (y1 <= y0) continue;
      const double next = x + slope * (y1 - y0), d = (y1 - y0) * direction;
      double x0 = clamp_x(x), x1 = clamp_x(next);
      if (x0 > x1) std::swap(x0, x1);
      auto* cells = &cells_[static_cast<std::size_t>(row) * static_cast<std::size_t>(width_)];
      const double floor0 = std::floor(x0);
      const int i0 = static_cast<int>(floor0), i1 = static_cast<int>(std::ceil(x1));
      if (i1 <= i0 + 1) {
        const double middle = .5 * (x0 + x1) - floor0;
        cells[i0] += d - d * middle;
        cells[i0 + 1] += d * middle;
      } else {
        const double s = 1 / (x1 - x0), f0 = x0 - floor0, a0 = .5 * s * (1 - f0) * (1 - f0), f1 = x1 - i1 + 1,
                     am = .5 * s * f1 * f1;
        cells[i0] += d * a0;
        if (i1 == i0 + 2)
          cells[i0 + 1] += d * (1 - a0 - am);
        else {
          const double a1 = s * (1.5 - f0);
          cells[i0 + 1] += d * (a1 - a0);
          for (int i = i0 + 2; i < i1 - 1; ++i) cells[i] += d * s;
          const double a2 = a1 + (i1 - i0 - 3) * s;
          cells[i1 - 1] += d * (1 - a2 - am);
        }
        cells[i1] += d * am;
      }
      x = next;
    }
  }
  template <class Paint>
  void paint(Paint&& paint) const {
    for (int y = 0; y < height_; ++y) {
      double sum{};
      for (int x = 0; x < width_ - 2; ++x) {
        sum += cells_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)];
        const double coverage = std::min(1.0, std::abs(sum));
        if (coverage > 1e-4) paint(x, y, coverage);
      }
    }
  }

 private:
  int width_, height_;
  std::vector<double> cells_;
};
void add_polygon(Coverage& coverage, const std::vector<Vec2>& points, bool closed, Vec2 offset) {
  for (std::size_t i = 0; i + 1 < points.size() + (closed ? 1 : 0); ++i) {
    const auto a = points[i], b = points[(i + 1) % points.size()];
    coverage.line({a.x + offset.x, a.y + offset.y}, {b.x + offset.x, b.y + offset.y});
  }
}
// Adds a polygon with positive orientation, so that overlapping stroke pieces accumulate instead of cancelling.
void add_oriented(Coverage& coverage, std::vector<Vec2> points, Vec2 offset) {
  double area{};
  for (std::size_t i = 0; i < points.size(); ++i) {
    const auto a = points[i], b = points[(i + 1) % points.size()];
    area += a.x * b.y - b.x * a.y;
  }
  if (area < 0) std::reverse(points.begin(), points.end());
  add_polygon(coverage, points, true, offset);
}
void composite(Image& image, int x, int y, std::uint32_t color, double alpha) {
  auto* pixel = &image.pixels[at(image, x, y)];
  for (unsigned c = 0; c < 3; ++c) pixel[c] = byte(((color >> (16 - 8 * c)) & 255U) * alpha + pixel[c] * (1 - alpha));
  pixel[3] = byte(255 * alpha + pixel[3] * (1 - alpha));
}
// One BlurFilter pass along rows (horizontal) or columns of a plane of 0-255 values, truncating each result.
void box(std::vector<double>& plane, int width, int height, double size, bool horizontal) {
  const double radius = size / 2;
  if (radius <= .5) return;
  const int reach = static_cast<int>(std::ceil(radius - .5));
  std::vector<double> weights;
  for (int k = -reach; k <= reach; ++k)
    weights.push_back(std::max(0.0, std::min(k + .5, radius) - std::max(k - .5, -radius)) / size);
  const int lines = horizontal ? height : width, length = horizontal ? width : height;
  std::vector<double> source(static_cast<std::size_t>(length));
  for (int line = 0; line < lines; ++line) {
    const auto index = [&](int i) {
      return horizontal
                 ? static_cast<std::size_t>(line) * static_cast<std::size_t>(width) + static_cast<std::size_t>(i)
                 : static_cast<std::size_t>(i) * static_cast<std::size_t>(width) + static_cast<std::size_t>(line);
    };
    for (int i = 0; i < length; ++i) source[static_cast<std::size_t>(i)] = plane[index(i)];
    for (int i = 0; i < length; ++i) {
      double sum{};
      for (int k = -reach; k <= reach; ++k)
        if (i + k >= 0 && i + k < length)
          sum += weights[static_cast<std::size_t>(k + reach)] * source[static_cast<std::size_t>(i + k)];
      plane[index(i)] = std::floor(sum + 1e-9);
    }
  }
}
std::vector<double> blurred_alpha(const Image& image, double blur_x, double blur_y, int quality) {
  std::vector<double> plane(static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height));
  for (std::size_t i = 0; i < plane.size(); ++i) plane[i] = image.pixels[i * 4 + 3];
  for (int pass = 0; pass < quality; ++pass) {
    box(plane, image.width, image.height, blur_x, true);
    box(plane, image.width, image.height, blur_y, false);
  }
  return plane;
}
void behind(Image& image, const std::vector<double>& plane, int dx, int dy, std::uint32_t color, double alpha,
            double strength) {
  for (int y = 0; y < image.height; ++y)
    for (int x = 0; x < image.width; ++x) {
      const int sx = x - dx, sy = y - dy;
      if (sx < 0 || sy < 0 || sx >= image.width || sy >= image.height) continue;
      const double shadow = std::min(255.0, plane[static_cast<std::size_t>(sy) * static_cast<std::size_t>(image.width) +
                                                  static_cast<std::size_t>(sx)] *
                                                strength) /
                            255 * alpha;
      if (shadow <= 0) continue;
      auto* pixel = &image.pixels[at(image, x, y)];
      const double remaining = 1 - pixel[3] / 255.0;
      for (unsigned c = 0; c < 3; ++c)
        pixel[c] = byte(pixel[c] + ((color >> (16 - 8 * c)) & 255U) * shadow * remaining);
      pixel[3] = byte(pixel[3] + 255 * shadow * remaining);
    }
}
// Bit reader for SWF records.
class Bits {
 public:
  explicit Bits(std::span<const unsigned char> data) : data_(data) {}
  void align() {
    if (bit_) {
      bit_ = 0;
      ++at_;
    }
  }
  std::uint32_t u8() {
    align();
    need(1);
    return data_[at_++];
  }
  std::uint32_t u16() {
    const auto low = u8();
    return low | (u8() << 8U);
  }
  std::uint32_t u32() {
    const auto low = u16();
    return low | (u16() << 16U);
  }
  std::uint32_t ub(unsigned count) {
    std::uint32_t value{};
    for (unsigned i = 0; i < count; ++i) {
      need(1);
      value = (value << 1U) | ((data_[at_] >> (7 - bit_)) & 1U);
      if (++bit_ == 8) {
        bit_ = 0;
        ++at_;
      }
    }
    return value;
  }
  std::int32_t sb(unsigned count) {
    if (!count) return 0;
    const auto value = ub(count);
    return (value & (1U << (count - 1))) ? static_cast<std::int32_t>(value | ~((1U << count) - 1))
                                         : static_cast<std::int32_t>(value);
  }
  void skip(std::size_t count) {
    align();
    need(count);
    at_ += count;
  }
  [[nodiscard]] std::size_t position() const { return at_; }
  void seek(std::size_t position) {
    bit_ = 0;
    at_ = position;
  }

 private:
  void need(std::size_t count) const {
    if (at_ + count > data_.size()) throw std::runtime_error("Truncated SWF shape");
  }
  std::span<const unsigned char> data_;
  std::size_t at_{};
  unsigned bit_{};
};
struct Style {
  std::uint32_t color{};
  double alpha{1};
};
std::vector<Style> fill_styles(Bits& bits, int version) {
  auto count = bits.u8();
  if (count == 0xff && version >= 2) count = bits.u16();
  std::vector<Style> styles;
  const auto matrix = [&] {
    bits.align();
    if (bits.ub(1)) {
      const auto n = bits.ub(5);
      bits.ub(n);
      bits.ub(n);
    }
    if (bits.ub(1)) {
      const auto n = bits.ub(5);
      bits.ub(n);
      bits.ub(n);
    }
    const auto n = bits.ub(5);
    bits.ub(n);
    bits.ub(n);
    bits.align();
  };
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto type = bits.u8();
    Style style;
    if (type == 0) {
      style.color = (bits.u8() << 16U) | (bits.u8() << 8U);
      style.color |= bits.u8();
      if (version >= 3) style.alpha = bits.u8() / 255.0;
    } else if (type == 0x10 || type == 0x12 || type == 0x13) {
      // Gradients are approximated by the average of their stops.
      matrix();
      const auto records = bits.u8() & 15U;
      double r{}, g{}, b{}, a{};
      for (std::uint32_t k = 0; k < records; ++k) {
        bits.u8();
        r += bits.u8();
        g += bits.u8();
        b += bits.u8();
        a += version >= 3 ? bits.u8() : 255;
      }
      if (type == 0x13) bits.u16();
      if (records)
        style = {(static_cast<std::uint32_t>(r / records) << 16U) | (static_cast<std::uint32_t>(g / records) << 8U) |
                     static_cast<std::uint32_t>(b / records),
                 a / records / 255};
    } else if (type >= 0x40 && type <= 0x43) {
      bits.u16();
      matrix();
      style.alpha = 0;
    } else
      throw std::runtime_error("Unsupported SWF fill style");
    styles.push_back(style);
  }
  return styles;
}
std::vector<std::pair<double, Style>> line_styles(Bits& bits, int version) {
  auto count = bits.u8();
  if (count == 0xff) count = bits.u16();
  std::vector<std::pair<double, Style>> styles;
  for (std::uint32_t i = 0; i < count; ++i) {
    const double width = bits.u16() / 20.0;
    Style style;
    if (version == 4) {
      bits.ub(2);
      const auto join = bits.ub(2);
      const auto has_fill = bits.ub(1);
      bits.ub(11);
      if (join == 2) bits.u16();
      if (has_fill) {
        const auto fills = fill_styles(bits, version);
        (void)fills;
      } else {
        style.color = (bits.u8() << 16U) | (bits.u8() << 8U);
        style.color |= bits.u8();
        style.alpha = bits.u8() / 255.0;
      }
    } else {
      style.color = (bits.u8() << 16U) | (bits.u8() << 8U);
      style.color |= bits.u8();
      if (version >= 3) style.alpha = bits.u8() / 255.0;
    }
    styles.push_back({width, style});
  }
  return styles;
}
}
void Path::move(double x, double y) { contours.push_back({{x, y}}); }
void Path::line(double x, double y) {
  if (contours.empty()) contours.push_back({{0, 0}});
  contours.back().push_back({x, y});
}
void Path::curve(double cx, double cy, double x, double y) {
  if (contours.empty()) contours.push_back({{0, 0}});
  const auto start = contours.back().back();
  for (int i = 1; i <= curve_steps; ++i) {
    const double t = i / curve_steps, u = 1 - t;
    contours.back().push_back(
        {u * u * start.x + 2 * u * t * cx + t * t * x, u * u * start.y + 2 * u * t * cy + t * t * y});
  }
}
void Path::close() {
  if (!contours.empty() && contours.back().size() > 1) contours.back().push_back(contours.back().front());
}
void Path::cut_edge_rect(double x, double y, double w, double h, double c, std::array<bool, 4> k) {
  if (k[0]) {
    move(x, y + c);
    line(x + c, y);
  } else
    move(x, y);
  if (k[1]) {
    line(x + w - c, y);
    line(x + w, y + c);
  } else
    line(x + w, y);
  if (k[2]) {
    line(x + w, y + h - c);
    line(x + w - c, y + h);
  } else
    line(x + w, y + h);
  if (k[3]) {
    line(x + c, y + h);
    line(x, y + h - c);
  } else
    line(x, y + h);
  close();
}
void Path::rect(double x, double y, double w, double h) {
  move(x, y);
  line(x + w, y);
  line(x + w, y + h);
  line(x, y + h);
  close();
}
void Path::round_rect(double x, double y, double w, double h, double ellipse) {
  const double r = std::min({ellipse / 2, w / 2, h / 2});
  if (r <= 0) {
    rect(x, y, w, h);
    return;
  }
  // Each corner is drawn as two quadratic curves through the 45 degree point.
  const double k = r * (1 - std::numbers::sqrt2 / 2), c = r * (1 - std::tan(std::numbers::pi / 8));
  move(x + r, y);
  line(x + w - r, y);
  curve(x + w - c, y, x + w - k, y + k);
  curve(x + w, y + c, x + w, y + r);
  line(x + w, y + h - r);
  curve(x + w, y + h - c, x + w - k, y + h - k);
  curve(x + w - c, y + h, x + w - r, y + h);
  line(x + r, y + h);
  curve(x + c, y + h, x + k, y + h - k);
  curve(x, y + h - c, x, y + h - r);
  line(x, y + r);
  curve(x, y + c, x + k, y + k);
  curve(x + c, y, x + r, y);
  close();
}
void Path::circle(double x, double y, double r) {
  move(x + r, y);
  for (int i = 1; i <= 64; ++i) {
    const double a = i * std::numbers::pi / 32;
    line(x + r * std::cos(a), y + r * std::sin(a));
  }
  close();
}
void Path::diamond(double x, double y, double r) {
  move(x - r, y);
  line(x, y - r);
  line(x + r, y);
  line(x, y + r);
  close();
}
Image canvas(int width, int height) {
  if (width < 0 || height < 0 || width > 8192 || height > 8192) throw std::invalid_argument("Invalid canvas size");
  return {width, height,
          std::vector<unsigned char>(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4)};
}
void fill(Image& image, const Path& path, std::uint32_t color, double alpha, Vec2 offset) {
  if (image.width == 0 || image.height == 0) return;
  Coverage coverage(image.width, image.height);
  for (const auto& contour : path.contours)
    if (contour.size() > 1) add_polygon(coverage, contour, true, offset);
  coverage.paint([&](int x, int y, double c) { composite(image, x, y, color, c * alpha); });
}
void stroke(Image& image, const Path& path, double width, std::uint32_t color, double alpha, Vec2 offset) {
  if (image.width == 0 || image.height == 0) return;
  Coverage coverage(image.width, image.height);
  const double half = std::max(width, 1.0) / 2;
  // Odd whole-pixel strokes cover the pixels below and to the right of their path, so they stay crisp.
  if (const auto whole = std::lround(std::max(width, 1.0));
      std::abs(std::max(width, 1.0) - static_cast<double>(whole)) < 1e-9 && whole % 2)
    offset = {offset.x + .5, offset.y + .5};
  for (const auto& contour : path.contours) {
    for (std::size_t i = 0; i + 1 < contour.size(); ++i) {
      const auto a = contour[i], b = contour[i + 1];
      const double dx = b.x - a.x, dy = b.y - a.y, length = std::hypot(dx, dy);
      if (length <= 0) continue;
      const double nx = -dy / length * half, ny = dx / length * half;
      add_oriented(coverage, {{a.x + nx, a.y + ny}, {b.x + nx, b.y + ny}, {b.x - nx, b.y - ny}, {a.x - nx, a.y - ny}},
                   offset);
    }
    // Round joins at interior vertices (and where a closed contour meets its start).
    const bool closed =
        contour.size() > 2 && contour.front().x == contour.back().x && contour.front().y == contour.back().y;
    for (std::size_t i = closed ? 0 : 1; i + 1 < contour.size(); ++i) {
      std::vector<Vec2> disc;
      for (int k = 0; k < 16; ++k) {
        const double angle = k * std::numbers::pi / 8;
        disc.push_back({contour[i].x + half * std::cos(angle), contour[i].y + half * std::sin(angle)});
      }
      add_oriented(coverage, disc, offset);
    }
  }
  coverage.paint([&](int x, int y, double c) { composite(image, x, y, color, c * alpha); });
}
void draw(Image& image, const Image& source, double x, double y, double sx, double sy, double alpha) {
  if (sx <= 0 || sy <= 0 || source.width == 0 || source.height == 0) return;
  const int x0 = std::max(0, static_cast<int>(std::floor(x))), y0 = std::max(0, static_cast<int>(std::floor(y)));
  const int x1 = std::min(image.width, static_cast<int>(std::ceil(x + source.width * sx))),
            y1 = std::min(image.height, static_cast<int>(std::ceil(y + source.height * sy)));
  for (int py = y0; py < y1; ++py) {
    const int v = static_cast<int>(std::floor((py + .5 - y) / sy));
    if (v < 0 || v >= source.height) continue;
    for (int px = x0; px < x1; ++px) {
      const int u = static_cast<int>(std::floor((px + .5 - x) / sx));
      if (u < 0 || u >= source.width) continue;
      const auto* s = &source.pixels[at(source, u, v)];
      if (!s[3]) continue;
      auto* d = &image.pixels[at(image, px, py)];
      const double remaining = 1 - s[3] * alpha / 255;
      for (unsigned c = 0; c < 4; ++c) d[c] = byte(s[c] * alpha + d[c] * remaining);
    }
  }
}
void color_transform(Image& image, const ColorTransform& t) {
  for (std::size_t i = 0; i < image.pixels.size(); i += 4) {
    auto* p = &image.pixels[i];
    const double a = p[3];
    const double na = std::clamp(a * t.alpha + t.alpha_offset, 0.0, 255.0);
    if (na <= 0) {
      std::fill_n(p, 4, 0);
      continue;
    }
    const double m[3]{t.red, t.green, t.blue}, o[3]{t.red_offset, t.green_offset, t.blue_offset};
    for (unsigned c = 0; c < 3; ++c) {
      const double straight = a > 0 ? p[c] * 255 / a : 0;
      p[c] = byte(std::clamp(straight * m[c] + o[c], 0.0, 255.0) * na / 255);
    }
    p[3] = byte(na);
  }
}
void color_matrix(Image& image, const std::array<double, 20>& m) {
  for (std::size_t i = 0; i < image.pixels.size(); i += 4) {
    auto* p = &image.pixels[i];
    const double a = p[3];
    const double s[4]{a > 0 ? p[0] * 255 / a : 0, a > 0 ? p[1] * 255 / a : 0, a > 0 ? p[2] * 255 / a : 0, a};
    double r[4];
    for (unsigned row = 0; row < 4; ++row)
      r[row] = std::clamp(
          m[row * 5] * s[0] + m[row * 5 + 1] * s[1] + m[row * 5 + 2] * s[2] + m[row * 5 + 3] * s[3] + m[row * 5 + 4],
          0.0, 255.0);
    for (unsigned c = 0; c < 3; ++c) p[c] = byte(r[c] * r[3] / 255);
    p[3] = byte(r[3]);
  }
}
void blur(Image& image, double bx, double by, int quality) {
  const auto count = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
  std::vector<double> plane(count);
  for (unsigned c = 0; c < 4; ++c) {
    for (std::size_t i = 0; i < count; ++i) plane[i] = image.pixels[i * 4 + c];
    for (int pass = 0; pass < quality; ++pass) {
      box(plane, image.width, image.height, bx, true);
      box(plane, image.width, image.height, by, false);
    }
    for (std::size_t i = 0; i < count; ++i) image.pixels[i * 4 + c] = byte(plane[i]);
  }
}
void glow(Image& image, std::uint32_t color, double alpha, double bx, double by, double strength, int quality) {
  behind(image, blurred_alpha(image, bx, by, quality), 0, 0, color, alpha, strength);
}
void drop_shadow(Image& image, double distance, double angle, std::uint32_t color, double alpha, double bx, double by,
                 double strength, int quality) {
  const double radians = angle * std::numbers::pi / 180;
  behind(image, blurred_alpha(image, bx, by, quality), static_cast<int>(std::lround(distance * std::cos(radians))),
         static_cast<int>(std::lround(distance * std::sin(radians))), color, alpha, strength);
}
Image padded(const Image& image, int margin) {
  auto result = canvas(image.width + 2 * margin, image.height + 2 * margin);
  for (int y = 0; y < image.height; ++y)
    std::copy_n(&image.pixels[at(image, 0, y)], static_cast<std::size_t>(image.width) * 4,
                &result.pixels[at(result, margin, y + margin)]);
  return result;
}
namespace {
std::array<double, 4> rect(Bits& bits) {
  bits.align();
  const auto n = bits.ub(5);
  std::array<double, 4> r{};
  for (auto& v : r) v = bits.sb(n) / 20.0;
  bits.align();
  return r;
}
// x' = a x + c y + tx, y' = b x + d y + ty.
struct Matrix {
  double a{1}, b{}, c{}, d{1}, tx{}, ty{};
};
Matrix matrix(Bits& bits) {
  bits.align();
  Matrix m;
  if (bits.ub(1)) {
    const auto n = bits.ub(5);
    m.a = bits.sb(n) / 65536.0;
    m.d = bits.sb(n) / 65536.0;
  }
  if (bits.ub(1)) {
    const auto n = bits.ub(5);
    m.b = bits.sb(n) / 65536.0;
    m.c = bits.sb(n) / 65536.0;
  }
  const auto n = bits.ub(5);
  m.tx = bits.sb(n) / 20.0;
  m.ty = bits.sb(n) / 20.0;
  bits.align();
  return m;
}
Matrix concat(const Matrix& outer, const Matrix& inner) {
  return {outer.a * inner.a + outer.c * inner.b,
          outer.b * inner.a + outer.d * inner.b,
          outer.a * inner.c + outer.c * inner.d,
          outer.b * inner.c + outer.d * inner.d,
          outer.a * inner.tx + outer.c * inner.ty + outer.tx,
          outer.b * inner.tx + outer.d * inner.ty + outer.ty};
}
ColorTransform cxform(Bits& bits) {
  bits.align();
  ColorTransform t;
  const auto add = bits.ub(1), multiply = bits.ub(1);
  const auto n = bits.ub(4);
  if (multiply) {
    t.red = bits.sb(n) / 256.0;
    t.green = bits.sb(n) / 256.0;
    t.blue = bits.sb(n) / 256.0;
    t.alpha = bits.sb(n) / 256.0;
  }
  if (add) {
    t.red_offset = bits.sb(n);
    t.green_offset = bits.sb(n);
    t.blue_offset = bits.sb(n);
    t.alpha_offset = bits.sb(n);
  }
  bits.align();
  return t;
}
ColorTransform concat(const ColorTransform& outer, const ColorTransform& inner) {
  return {outer.red * inner.red,
          outer.green * inner.green,
          outer.blue * inner.blue,
          outer.alpha * inner.alpha,
          outer.red * inner.red_offset + outer.red_offset,
          outer.green * inner.green_offset + outer.green_offset,
          outer.blue * inner.blue_offset + outer.blue_offset,
          outer.alpha * inner.alpha_offset + outer.alpha_offset};
}
Shape parse_shape(Bits& bits, int version) {
  Shape shape;
  const auto bounds = rect(bits);
  shape.left = bounds[0];
  shape.right = bounds[1];
  shape.top = bounds[2];
  shape.bottom = bounds[3];
  if (version == 4) {
    rect(bits);
    bits.u8();
  }
  auto fills = fill_styles(bits, version);
  auto lines = line_styles(bits, version);
  auto fill_bits = bits.ub(4), line_bits = bits.ub(4);
  // Edges are collected per style: fill-1 edges as drawn, fill-0 edges reversed, so every region winds the same way.
  std::vector<std::vector<std::array<Vec2, 2>>> fill_edges(fills.size());
  std::vector<Path> line_paths(lines.size());
  std::size_t base_fill{}, base_line{};
  std::uint32_t fill0{}, fill1{}, line{};
  Vec2 pen{};
  bool line_open{};
  const auto add = [&](Vec2 a, Vec2 b) {
    if (fill0) fill_edges[base_fill + fill0 - 1].push_back({b, a});
    if (fill1) fill_edges[base_fill + fill1 - 1].push_back({a, b});
    if (line) {
      auto& p = line_paths[base_line + line - 1];
      if (!line_open) {
        p.move(a.x, a.y);
        line_open = true;
      }
      p.line(b.x, b.y);
    }
  };
  while (true) {
    if (bits.ub(1)) {
      if (bits.ub(1)) {
        const auto n = bits.ub(4) + 2;
        Vec2 delta{};
        if (bits.ub(1)) {
          delta.x = bits.sb(n) / 20.0;
          delta.y = bits.sb(n) / 20.0;
        } else if (bits.ub(1))
          delta.y = bits.sb(n) / 20.0;
        else
          delta.x = bits.sb(n) / 20.0;
        const Vec2 next{pen.x + delta.x, pen.y + delta.y};
        add(pen, next);
        pen = next;
      } else {
        const auto n = bits.ub(4) + 2;
        const Vec2 control{pen.x + bits.sb(n) / 20.0, pen.y + bits.sb(n) / 20.0};
        const Vec2 anchor{control.x + bits.sb(n) / 20.0, control.y + bits.sb(n) / 20.0};
        Vec2 previous = pen;
        for (int i = 1; i <= curve_steps; ++i) {
          const double t = i / curve_steps, u = 1 - t;
          const Vec2 point{u * u * pen.x + 2 * u * t * control.x + t * t * anchor.x,
                           u * u * pen.y + 2 * u * t * control.y + t * t * anchor.y};
          add(previous, point);
          previous = point;
        }
        pen = anchor;
      }
      continue;
    }
    const auto new_styles = bits.ub(1), line_style = bits.ub(1), style1 = bits.ub(1), style0 = bits.ub(1),
               move = bits.ub(1);
    if (!new_styles && !line_style && !style1 && !style0 && !move) break;
    line_open = false;
    if (move) {
      const auto n = bits.ub(5);
      pen.x = bits.sb(n) / 20.0;
      pen.y = bits.sb(n) / 20.0;
    }
    if (style0) fill0 = bits.ub(fill_bits);
    if (style1) fill1 = bits.ub(fill_bits);
    if (line_style) line = bits.ub(line_bits);
    if (new_styles) {
      base_fill = fills.size();
      base_line = lines.size();
      auto more_fills = fill_styles(bits, version);
      auto more_lines = line_styles(bits, version);
      fills.insert(fills.end(), more_fills.begin(), more_fills.end());
      lines.insert(lines.end(), more_lines.begin(), more_lines.end());
      fill_edges.resize(fills.size());
      line_paths.resize(lines.size());
      fill_bits = bits.ub(4);
      line_bits = bits.ub(4);
    }
  }
  for (std::size_t i = 0; i < fills.size(); ++i) {
    Shape::Fill fill{fills[i].color, fills[i].alpha, {}};
    for (const auto& edge : fill_edges[i]) fill.path.contours.push_back({edge[0], edge[1]});
    if (!fill.path.contours.empty() && fill.alpha > 0) {
      shape.order.push_back({false, shape.fills.size()});
      shape.fills.push_back(std::move(fill));
    }
  }
  for (std::size_t i = 0; i < lines.size(); ++i)
    if (!line_paths[i].contours.empty()) {
      shape.order.push_back({true, shape.lines.size()});
      shape.lines.push_back({lines[i].first, lines[i].second.color, lines[i].second.alpha, std::move(line_paths[i])});
    }
  return shape;
}
struct Placement {
  int depth{}, character{};
  Matrix matrix;
  ColorTransform color;
};
struct Library {
  std::unordered_map<int, Shape> shapes;
  std::unordered_map<int, std::vector<Placement>> sprites;
  std::unordered_map<std::string, int> exports;
  int first_shape{-1};
};
// Reads tags until End, recording definitions and the first frame's placements of this timeline.
void read_tags(Bits& bits, Library& library, std::vector<Placement>* timeline) {
  bool frame_done = false;
  while (true) {
    const auto header = bits.u16();
    const auto code = header >> 6U;
    std::size_t length = header & 63U;
    if (length == 63) length = bits.u32();
    const auto end = bits.position() + length;
    if (code == 0) break;
    const int version = code == 2 ? 1 : code == 22 ? 2 : code == 32 ? 3 : code == 83 ? 4 : 0;
    if (version) {
      const auto id = static_cast<int>(bits.u16());
      library.shapes.emplace(id, parse_shape(bits, version));
      if (library.first_shape < 0) library.first_shape = id;
    } else if (code == 39) {
      const auto id = static_cast<int>(bits.u16());
      bits.u16();
      std::vector<Placement> placements;
      read_tags(bits, library, &placements);
      library.sprites.emplace(id, std::move(placements));
    } else if (code == 1)
      frame_done = true;
    else if ((code == 26 || code == 70) && timeline && !frame_done) {
      const auto flags = bits.u8();
      const auto flags2 = code == 70 ? bits.u8() : 0;
      Placement placement;
      placement.depth = static_cast<int>(bits.u16());
      if (code == 70 && ((flags2 & 8U) || ((flags2 & 16U) && (flags & 2U))))
        while (bits.u8()) {
        }
      if (flags & 2U) {
        placement.character = static_cast<int>(bits.u16());
        if (flags & 4U) placement.matrix = matrix(bits);
        if (flags & 8U) placement.color = cxform(bits);
        timeline->push_back(placement);
      }
    } else if (code == 56 || code == 76) {
      const auto count = bits.u16();
      for (std::uint32_t i = 0; i < count; ++i) {
        const auto id = static_cast<int>(bits.u16());
        std::string name;
        for (char c; (c = static_cast<char>(bits.u8()));) name += c;
        library.exports.emplace(name, id);
      }
    }
    bits.seek(end);
  }
}
void flatten(const Library& library, int character, const Matrix& m, const ColorTransform& t, Shape& out, int depth) {
  if (depth > 16) throw std::runtime_error("SWF symbol nesting too deep");
  if (const auto shape = library.shapes.find(character); shape != library.shapes.end()) {
    const auto map = [&](const Path& path) {
      Path result;
      for (const auto& contour : path.contours) {
        auto& c = result.contours.emplace_back();
        for (const auto p : contour) c.push_back({m.a * p.x + m.c * p.y + m.tx, m.b * p.x + m.d * p.y + m.ty});
      }
      return result;
    };
    const auto tint = [&](std::uint32_t color, double alpha, double& out_alpha) {
      std::uint32_t result{};
      const double mul[3]{t.red, t.green, t.blue}, add[3]{t.red_offset, t.green_offset, t.blue_offset};
      for (unsigned c = 0; c < 3; ++c)
        result |= static_cast<std::uint32_t>(byte(((color >> (16 - 8 * c)) & 255U) * mul[c] + add[c])) << (16 - 8 * c);
      out_alpha = std::clamp(alpha * t.alpha + t.alpha_offset / 255, 0.0, 1.0);
      return result;
    };
    const double scale = std::sqrt(std::abs(m.a * m.d - m.b * m.c));
    for (const auto [is_line, index] : shape->second.order) {
      if (is_line) {
        const auto& l = shape->second.lines[index];
        Shape::Line line;
        line.width = l.width * scale;
        line.color = tint(l.color, l.alpha, line.alpha);
        line.path = map(l.path);
        out.order.push_back({true, out.lines.size()});
        out.lines.push_back(std::move(line));
      } else {
        const auto& f = shape->second.fills[index];
        Shape::Fill fill;
        fill.color = tint(f.color, f.alpha, fill.alpha);
        fill.path = map(f.path);
        out.order.push_back({false, out.fills.size()});
        out.fills.push_back(std::move(fill));
      }
    }
    const auto& s = shape->second;
    for (const auto corner :
         {Vec2{s.left, s.top}, Vec2{s.right, s.top}, Vec2{s.left, s.bottom}, Vec2{s.right, s.bottom}}) {
      const double x = m.a * corner.x + m.c * corner.y + m.tx, y = m.b * corner.x + m.d * corner.y + m.ty;
      out.left = std::min(out.left, x);
      out.right = std::max(out.right, x);
      out.top = std::min(out.top, y);
      out.bottom = std::max(out.bottom, y);
    }
  } else if (const auto sprite = library.sprites.find(character); sprite != library.sprites.end()) {
    auto placements = sprite->second;
    std::stable_sort(placements.begin(), placements.end(),
                     [](const Placement& a, const Placement& b) { return a.depth < b.depth; });
    for (const auto& p : placements)
      flatten(library, p.character, concat(m, p.matrix), concat(t, p.color), out, depth + 1);
  }
}
}
Shape load_swf_shape(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  std::vector<unsigned char> file((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  if (file.size() < 8 || (file[0] != 'F' && file[0] != 'C') || file[1] != 'W' || file[2] != 'S')
    throw std::runtime_error("Unsupported SWF: " + path.string());
  std::vector<unsigned char> data;
  if (file[0] == 'C') {
    uLongf length = file[4] | (file[5] << 8U) | (file[6] << 16U) | (static_cast<uLongf>(file[7]) << 24U);
    if (length < 8 || length > 16 * 1024 * 1024) throw std::runtime_error("Invalid SWF length: " + path.string());
    data.resize(length - 8);
    uLongf actual = data.size();
    if (uncompress(data.data(), &actual, file.data() + 8, file.size() - 8) != Z_OK)
      throw std::runtime_error("Cannot inflate SWF: " + path.string());
    data.resize(actual);
  } else
    data.assign(file.begin() + 8, file.end());
  Bits bits(data);
  rect(bits);
  bits.u16();
  bits.u16();
  Library library;
  read_tags(bits, library, nullptr);
  // The embedded class: the exported symbol named after the file, else the only shape.
  int character = library.first_shape;
  for (const auto& [name, id] : library.exports)
    if (name.ends_with("." + path.stem().string()) || name == path.stem().string()) character = id;
  if (character < 0) throw std::runtime_error("SWF has no shape: " + path.string());
  Shape result;
  result.left = result.top = 1e9;
  result.right = result.bottom = -1e9;
  flatten(library, character, {}, {}, result, 0);
  if (result.fills.empty() && result.lines.empty()) throw std::runtime_error("SWF symbol is empty: " + path.string());
  return result;
}
void draw_shape(Image& image, const Shape& shape, double x, double y, double scale, const ColorTransform& t) {
  const auto transformed = [&](const Path& path) {
    Path result;
    for (const auto& contour : path.contours) {
      auto& out = result.contours.emplace_back();
      for (const auto p : contour) out.push_back({x + p.x * scale, y + p.y * scale});
    }
    return result;
  };
  const auto color = [&](std::uint32_t value, double alpha, double& out_alpha) {
    std::uint32_t result{};
    const double m[3]{t.red, t.green, t.blue}, o[3]{t.red_offset, t.green_offset, t.blue_offset};
    for (unsigned c = 0; c < 3; ++c)
      result |= static_cast<std::uint32_t>(byte(std::clamp(((value >> (16 - 8 * c)) & 255U) * m[c] + o[c], 0.0, 255.0)))
                << (16 - 8 * c);
    out_alpha = std::clamp(alpha * t.alpha + t.alpha_offset / 255, 0.0, 1.0);
    return result;
  };
  for (const auto [is_line, index] : shape.order) {
    if (is_line) {
      const auto& l = shape.lines[index];
      double alpha{};
      const auto c = color(l.color, l.alpha, alpha);
      stroke(image, transformed(l.path), l.width * scale, c, alpha);
      continue;
    }
    // Edge soups: each contour is a single edge, so fill them without closing segments.
    const auto& f = shape.fills[index];
    Coverage coverage(image.width, image.height);
    for (const auto& contour : transformed(f.path).contours) add_polygon(coverage, contour, false, {});
    double alpha{};
    const auto c = color(f.color, f.alpha, alpha);
    coverage.paint([&](int px, int py, double cover) { composite(image, px, py, c, cover * alpha); });
  }
}
}
