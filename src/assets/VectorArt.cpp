#include "realm/assets/VectorArt.hpp"
#include "realm/assets/FlashDraw.hpp"
#include "realm/util/Xml.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <map>
#include <numbers>
#include <stdexcept>
#include <string>

namespace realm::vector_art {
namespace {
// Curves are flattened in natural units to this distance, fine enough for 8x density.
constexpr double flatness = 0.0025;

[[noreturn]] void fail(const std::string& message) {
  throw std::runtime_error("SVG: " + message);
}

// x' = a x + c y + e, y' = b x + d y + f, as SVG's matrix(a b c d e f).
struct Affine {
  double a{1}, b{0}, c{0}, d{1}, e{0}, f{0};

  [[nodiscard]] Vec2 apply(Vec2 p) const { return {a * p.x + c * p.y + e, b * p.x + d * p.y + f}; }

  [[nodiscard]] Affine then(const Affine& inner) const {
    return {a * inner.a + c * inner.b,     b * inner.a + d * inner.b,
            a * inner.c + c * inner.d,     b * inner.c + d * inner.d,
            a * inner.e + c * inner.f + e, b * inner.e + d * inner.f + f};
  }

  [[nodiscard]] double determinant() const { return a * d - b * c; }

  [[nodiscard]] Affine inverse() const {
    const double det = determinant();
    if (std::abs(det) < 1e-12) {
      fail("singular transform");
    }
    return {d / det, -b / det, -c / det, a / det, (c * f - d * e) / det, (b * e - a * f) / det};
  }
};

// Numbers in path data and attribute lists: "1.5.5" is two numbers, "-1-2" too.
class Scanner {
 public:
  explicit Scanner(std::string_view text) : text_(text) {}

  void skip() {
    while (at_ < text_.size() &&
           (std::isspace(static_cast<unsigned char>(text_[at_])) || text_[at_] == ',')) {
      ++at_;
    }
  }

  [[nodiscard]] bool done() {
    skip();
    return at_ >= text_.size();
  }

  [[nodiscard]] char peek() {
    skip();
    return at_ < text_.size() ? text_[at_] : '\0';
  }

  char take() { return text_[at_++]; }

  [[nodiscard]] bool number_next() {
    const char c = peek();
    return std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+' || c == '.';
  }

  double number() {
    skip();
    const std::size_t start = at_;
    if (at_ < text_.size() && (text_[at_] == '-' || text_[at_] == '+')) {
      ++at_;
    }
    bool digits = false;
    while (at_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[at_]))) {
      ++at_;
      digits = true;
    }
    if (at_ < text_.size() && text_[at_] == '.') {
      ++at_;
      while (at_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[at_]))) {
        ++at_;
        digits = true;
      }
    }
    if (!digits) {
      fail("expected a number in '" + std::string(text_) + "'");
    }
    if (at_ < text_.size() && (text_[at_] == 'e' || text_[at_] == 'E')) {
      std::size_t exponent = at_ + 1;
      if (exponent < text_.size() && (text_[exponent] == '-' || text_[exponent] == '+')) {
        ++exponent;
      }
      if (exponent < text_.size() && std::isdigit(static_cast<unsigned char>(text_[exponent]))) {
        at_ = exponent;
        while (at_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[at_]))) {
          ++at_;
        }
      }
    }
    const std::string token(text_.substr(start, at_ - start));
    return std::strtod(token.c_str(), nullptr);
  }

  // Arc flags may be written without separators ("a5 5 0 01 10 10").
  bool flag() {
    skip();
    if (at_ < text_.size() && (text_[at_] == '0' || text_[at_] == '1')) {
      return text_[at_++] == '1';
    }
    fail("expected an arc flag in '" + std::string(text_) + "'");
  }

 private:
  std::string_view text_;
  std::size_t at_{};
};

std::string trim(std::string_view text) {
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) {
    return {};
  }
  const auto last = text.find_last_not_of(" \t\r\n");
  return std::string(text.substr(first, last - first + 1));
}

// A length with an optional "px" or "%" (of `percent_of`).
double length(std::string_view text, double percent_of = 1) {
  const std::string value = trim(text);
  if (value.empty()) {
    fail("empty length");
  }
  char* end = nullptr;
  const double number = std::strtod(value.c_str(), &end);
  const std::string unit = trim(end);
  if (end == value.c_str() || !std::isfinite(number)) {
    fail("bad length '" + value + "'");
  }
  if (unit.empty() || unit == "px") {
    return number;
  }
  if (unit == "%") {
    return number / 100 * percent_of;
  }
  fail("unsupported unit in '" + value + "'");
}

Affine parse_transform(std::string_view text) {
  Affine result;
  std::size_t at = 0;
  while (true) {
    while (at < text.size() &&
           (std::isspace(static_cast<unsigned char>(text[at])) || text[at] == ',')) {
      ++at;
    }
    if (at >= text.size()) {
      break;
    }
    const auto open = text.find('(', at);
    const auto close = text.find(')', at);
    if (open == std::string_view::npos || close == std::string_view::npos || close < open) {
      fail("bad transform '" + std::string(text) + "'");
    }
    const std::string name = trim(text.substr(at, open - at));
    Scanner scanner(text.substr(open + 1, close - open - 1));
    std::vector<double> v;
    while (!scanner.done()) {
      v.push_back(scanner.number());
    }
    Affine step;
    const auto count = v.size();
    if (name == "matrix" && count == 6) {
      step = {v[0], v[1], v[2], v[3], v[4], v[5]};
    } else if (name == "translate" && (count == 1 || count == 2)) {
      step = {1, 0, 0, 1, v[0], count == 2 ? v[1] : 0};
    } else if (name == "scale" && (count == 1 || count == 2)) {
      step = {v[0], 0, 0, count == 2 ? v[1] : v[0], 0, 0};
    } else if (name == "rotate" && (count == 1 || count == 3)) {
      const double angle = v[0] * std::numbers::pi / 180;
      const double cos = std::cos(angle), sin = std::sin(angle);
      step = {cos, sin, -sin, cos, 0, 0};
      if (count == 3) {
        step = Affine{1, 0, 0, 1, v[1], v[2]}.then(step).then({1, 0, 0, 1, -v[1], -v[2]});
      }
    } else if (name == "skewX" && count == 1) {
      step = {1, 0, std::tan(v[0] * std::numbers::pi / 180), 1, 0, 0};
    } else if (name == "skewY" && count == 1) {
      step = {1, std::tan(v[0] * std::numbers::pi / 180), 0, 1, 0, 0};
    } else {
      fail("unsupported transform '" + name + "'");
    }
    result = result.then(step);
    at = close + 1;
  }
  return result;
}

struct Color {
  std::uint32_t rgb{};
  double alpha{1};
};

std::optional<Color> parse_color(const std::string& text) {
  static const std::map<std::string, std::uint32_t, std::less<>> named{
      {"black", 0x000000}, {"white", 0xffffff},   {"red", 0xff0000},     {"lime", 0x00ff00},
      {"green", 0x008000}, {"blue", 0x0000ff},    {"yellow", 0xffff00},  {"cyan", 0x00ffff},
      {"aqua", 0x00ffff},  {"magenta", 0xff00ff}, {"fuchsia", 0xff00ff}, {"gray", 0x808080},
      {"grey", 0x808080},  {"orange", 0xffa500},  {"purple", 0x800080},  {"silver", 0xc0c0c0}};
  if (text == "none") {
    return std::nullopt;
  }
  if (text == "transparent") {
    return Color{0, 0};
  }
  if (!text.empty() && text[0] == '#') {
    const auto hex = text.substr(1);
    const bool valid =
        (hex.size() == 3 || hex.size() == 6) &&
        std::all_of(hex.begin(), hex.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
    if (!valid) {
      fail("bad color '" + text + "'");
    }
    auto value = static_cast<std::uint32_t>(std::stoul(hex, nullptr, 16));
    if (hex.size() == 3) {
      value = ((value & 0xf00U) << 12) | ((value & 0xf00U) << 8) | ((value & 0x0f0U) << 8) |
              ((value & 0x0f0U) << 4) | ((value & 0x00fU) << 4) | (value & 0x00fU);
    }
    return Color{value};
  }
  if (text.starts_with("rgb(") && text.ends_with(")")) {
    std::uint32_t value = 0;
    std::string body = text.substr(4, text.size() - 5);
    std::size_t at = 0;
    for (int channel = 0; channel < 3; ++channel) {
      const auto next = body.find(',', at);
      const std::string part =
          trim(body.substr(at, next == std::string::npos ? std::string::npos : next - at));
      const double component = std::clamp(length(part, 255), 0.0, 255.0);
      value = (value << 8) | static_cast<std::uint32_t>(std::lround(component));
      if (next == std::string::npos && channel < 2) {
        fail("bad color '" + text + "'");
      }
      at = next + 1;
    }
    return Color{value};
  }
  if (const auto found = named.find(text); found != named.end()) {
    return Color{found->second};
  }
  fail("unsupported color '" + text + "'");
}

double unit_number(const std::string& text) {
  return std::clamp(length(text, 1), 0.0, 1.0);
}

// Properties from presentation attributes, overridden by style="".
std::map<std::string, std::string, std::less<>> properties(const Xml& element) {
  static constexpr std::string_view names[]{
      "fill",           "fill-opacity", "fill-rule",       "stroke",         "stroke-width",
      "stroke-opacity", "opacity",      "stroke-linejoin", "stroke-linecap", "stroke-miterlimit",
      "stop-color",     "stop-opacity", "display",         "visibility",     "stroke-dasharray",
      "filter",         "mask",         "clip-path"};
  std::map<std::string, std::string, std::less<>> result;
  for (const auto name : names) {
    if (auto value = element.attribute(name); !value.empty()) {
      result[std::string(name)] = trim(value);
    }
  }
  const std::string style = element.attribute("style");
  std::size_t at = 0;
  while (at < style.size()) {
    const auto end = std::min(style.find(';', at), style.size());
    const auto declaration = std::string_view(style).substr(at, end - at);
    if (const auto colon = declaration.find(':'); colon != std::string_view::npos) {
      result[trim(declaration.substr(0, colon))] = trim(declaration.substr(colon + 1));
    }
    at = end + 1;
  }
  return result;
}

struct Style {
  std::string fill{"#000000"};
  double fill_opacity{1};
  bool even_odd{};
  std::string stroke{"none"};
  double stroke_width{1};
  double stroke_opacity{1};
  Shape::Join join{Shape::Join::miter};
  Shape::Cap cap{Shape::Cap::butt};
  double miter_limit{4};
};

struct RawGradient {
  std::map<std::string, std::string, std::less<>> attributes;
  std::vector<Stop> stops;
};

class Parser {
 public:
  explicit Parser(const Xml& root) {
    collect_gradients(root);
    if (root.name() != "svg") {
      fail("the root element must be <svg>");
    }
    const std::string box = root.attribute("viewBox");
    Vec2 origin{};
    if (!box.empty()) {
      Scanner scanner(box);
      origin = {scanner.number(), scanner.number()};
      view_ = {scanner.number(), scanner.number()};
    }
    const std::string width = root.attribute("width"), height = root.attribute("height");
    drawing_.width = width.empty() ? view_.x : length(width);
    drawing_.height = height.empty() ? view_.y : length(height);
    if (box.empty()) {
      view_ = {drawing_.width, drawing_.height};
    }
    if (!(drawing_.width > 0 && drawing_.height > 0 && view_.x > 0 && view_.y > 0)) {
      fail("the <svg> needs a positive width and height or viewBox");
    }
    const Affine view{drawing_.width / view_.x,
                      0,
                      0,
                      drawing_.height / view_.y,
                      -origin.x * drawing_.width / view_.x,
                      -origin.y * drawing_.height / view_.y};
    drawing_.stretch_x = band(root.attribute("data-stretch-x"), drawing_.width);
    drawing_.stretch_y = band(root.attribute("data-stretch-y"), drawing_.height);
    drawing_.root = group(root, view, Style{});
  }

  Drawing take() { return std::move(drawing_); }

 private:
  static std::optional<Stretch> band(const std::string& text, double natural) {
    if (text.empty()) {
      return std::nullopt;
    }
    Scanner scanner(text);
    Stretch result{scanner.number(), scanner.number()};
    if (!scanner.done() || result.start < 0 || result.end < result.start || result.end > natural) {
      fail("bad stretch band '" + text + "'");
    }
    return result;
  }

  void collect_gradients(const Xml& element) {
    for (const auto& child : element.children()) {
      if (child.name() == "linearGradient") {
        RawGradient raw;
        for (const auto* name : {"x1", "y1", "x2", "y2", "gradientUnits", "gradientTransform",
                                 "href", "spreadMethod"}) {
          if (auto value = child.attribute(name); !value.empty()) {
            raw.attributes[name] = trim(value);
          }
        }
        for (const auto& stop : child.children("stop")) {
          auto values = properties(stop);
          Stop parsed;
          parsed.offset = unit_number(stop.attribute("offset", "0"));
          if (!raw.stops.empty()) {
            parsed.offset = std::max(parsed.offset, raw.stops.back().offset);
          }
          const auto color =
              parse_color(values.contains("stop-color") ? values["stop-color"] : "#000000");
          parsed.color = color ? color->rgb : 0;
          parsed.opacity =
              (color ? color->alpha : 0) *
              (values.contains("stop-opacity") ? unit_number(values["stop-opacity"]) : 1);
          raw.stops.push_back(parsed);
        }
        gradients_[child.attribute("id")] = std::move(raw);
      } else if (child.name() == "radialGradient") {
        fail("radialGradient is not supported");
      }
      collect_gradients(child);
    }
  }

  // A gradient's own attribute, or the one it inherits through href.
  std::optional<std::string> gradient_attribute(const std::string& id, std::string_view name,
                                                int depth = 0) const {
    const auto found = gradients_.find(id);
    if (found == gradients_.end() || depth > 16) {
      return std::nullopt;
    }
    if (const auto value = found->second.attributes.find(name);
        value != found->second.attributes.end()) {
      return value->second;
    }
    if (const auto href = found->second.attributes.find("href");
        href != found->second.attributes.end() && href->second.starts_with("#")) {
      return gradient_attribute(href->second.substr(1), name, depth + 1);
    }
    return std::nullopt;
  }

  const std::vector<Stop>* gradient_stops(const std::string& id, int depth = 0) const {
    const auto found = gradients_.find(id);
    if (found == gradients_.end() || depth > 16) {
      return nullptr;
    }
    if (!found->second.stops.empty()) {
      return &found->second.stops;
    }
    if (const auto href = found->second.attributes.find("href");
        href != found->second.attributes.end() && href->second.starts_with("#")) {
      return gradient_stops(href->second.substr(1), depth + 1);
    }
    return nullptr;
  }

  static Style inherit(const Style& parent,
                       const std::map<std::string, std::string, std::less<>>& values) {
    Style style = parent;
    for (const auto& [name, value] : values) {
      if (value == "inherit") {
        continue;
      }
      if (name == "fill") {
        style.fill = value;
      } else if (name == "fill-opacity") {
        style.fill_opacity = unit_number(value);
      } else if (name == "fill-rule") {
        style.even_odd = value == "evenodd";
      } else if (name == "stroke") {
        style.stroke = value;
      } else if (name == "stroke-width") {
        style.stroke_width = length(value);
      } else if (name == "stroke-opacity") {
        style.stroke_opacity = unit_number(value);
      } else if (name == "stroke-linejoin") {
        style.join = value == "round"   ? Shape::Join::round
                     : value == "bevel" ? Shape::Join::bevel
                                        : Shape::Join::miter;
      } else if (name == "stroke-linecap") {
        style.cap = value == "round"    ? Shape::Cap::round
                    : value == "square" ? Shape::Cap::square
                                        : Shape::Cap::butt;
      } else if (name == "stroke-miterlimit") {
        style.miter_limit = std::max(1.0, length(value));
      } else if (name == "stroke-dasharray" && value != "none") {
        fail("stroke-dasharray is not supported");
      } else if ((name == "filter" || name == "mask" || name == "clip-path") && value != "none") {
        fail(name + " is not supported");
      }
    }
    return style;
  }

  static bool hidden(const std::map<std::string, std::string, std::less<>>& values) {
    const auto display = values.find("display");
    const auto visibility = values.find("visibility");
    return (display != values.end() && display->second == "none") ||
           (visibility != values.end() && visibility->second == "hidden");
  }

  Node group(const Xml& element, const Affine& parent, const Style& parent_style) {
    Node node;
    for (const auto& child : element.children()) {
      const std::string name = child.name();
      if (name == "title" || name == "desc" || name == "metadata" || name == "defs" ||
          name == "namedview" || name == "linearGradient") {
        continue;
      }
      const auto values = properties(child);
      if (hidden(values)) {
        continue;
      }
      const Affine transform = parent.then(parse_transform(child.attribute("transform")));
      const Style style = inherit(parent_style, values);
      Node item;
      if (name == "g") {
        item = group(child, transform, style);
      } else {
        item.shape = shape(child, name, transform, style);
      }
      if (const auto opacity = values.find("opacity"); opacity != values.end()) {
        item.opacity = unit_number(opacity->second);
      }
      node.children.push_back(std::move(item));
    }
    return node;
  }

  // Path building: control points are transformed into natural units and curves flattened there.
  class Builder {
   public:
    explicit Builder(const Affine& transform) : transform_(transform) {}

    void move(Vec2 p) {
      finish();
      start_ = current_ = p;
      contour_.push_back(transform_.apply(p));
    }

    void line(Vec2 p) {
      ensure();
      current_ = p;
      contour_.push_back(transform_.apply(p));
    }

    void cubic(Vec2 c1, Vec2 c2, Vec2 p) {
      ensure();
      subdivide(transform_.apply(current_), transform_.apply(c1), transform_.apply(c2),
                transform_.apply(p), 0);
      current_ = p;
    }

    void quadratic(Vec2 c, Vec2 p) {
      cubic({current_.x + 2.0 / 3 * (c.x - current_.x), current_.y + 2.0 / 3 * (c.y - current_.y)},
            {p.x + 2.0 / 3 * (c.x - p.x), p.y + 2.0 / 3 * (c.y - p.y)}, p);
    }

    // SVG endpoint arc, converted to cubics (SVG 1.1 implementation notes, F.6).
    void arc(double rx, double ry, double rotation, bool large, bool sweep, Vec2 p) {
      ensure();
      const Vec2 from = current_;
      rx = std::abs(rx);
      ry = std::abs(ry);
      if ((from.x == p.x && from.y == p.y)) {
        return;
      }
      if (rx == 0 || ry == 0) {
        line(p);
        return;
      }
      const double phi = rotation * std::numbers::pi / 180;
      const double cos = std::cos(phi), sin = std::sin(phi);
      const double dx = (from.x - p.x) / 2, dy = (from.y - p.y) / 2;
      const double x1 = cos * dx + sin * dy, y1 = -sin * dx + cos * dy;
      const double lambda = x1 * x1 / (rx * rx) + y1 * y1 / (ry * ry);
      if (lambda > 1) {
        rx *= std::sqrt(lambda);
        ry *= std::sqrt(lambda);
      }
      const double numerator = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
      const double denominator = rx * rx * y1 * y1 + ry * ry * x1 * x1;
      double root = denominator > 0 ? std::sqrt(std::max(0.0, numerator / denominator)) : 0;
      if (large == sweep) {
        root = -root;
      }
      const double cx1 = root * rx * y1 / ry, cy1 = -root * ry * x1 / rx;
      const double cx = cos * cx1 - sin * cy1 + (from.x + p.x) / 2;
      const double cy = sin * cx1 + cos * cy1 + (from.y + p.y) / 2;
      const auto angle = [](double ux, double uy, double vx, double vy) {
        return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
      };
      const double theta = angle(1, 0, (x1 - cx1) / rx, (y1 - cy1) / ry);
      double delta = angle((x1 - cx1) / rx, (y1 - cy1) / ry, (-x1 - cx1) / rx, (-y1 - cy1) / ry);
      if (!sweep && delta > 0) {
        delta -= 2 * std::numbers::pi;
      } else if (sweep && delta < 0) {
        delta += 2 * std::numbers::pi;
      }
      const int pieces = static_cast<int>(std::ceil(std::abs(delta) / (std::numbers::pi / 2)));
      const double step = delta / pieces;
      const double k = 4.0 / 3 * std::tan(step / 4);
      const auto point = [&](double t) {
        return Vec2{cx + rx * std::cos(t) * cos - ry * std::sin(t) * sin,
                    cy + rx * std::cos(t) * sin + ry * std::sin(t) * cos};
      };
      const auto tangent = [&](double t) {
        return Vec2{-rx * std::sin(t) * cos - ry * std::cos(t) * sin,
                    -rx * std::sin(t) * sin + ry * std::cos(t) * cos};
      };
      for (int i = 0; i < pieces; ++i) {
        const double t0 = theta + step * i, t1 = t0 + step;
        const Vec2 p0 = point(t0), p1 = i + 1 == pieces ? p : point(t1);
        const Vec2 d0 = tangent(t0), d1 = tangent(t1);
        cubic({p0.x + k * d0.x, p0.y + k * d0.y}, {p1.x - k * d1.x, p1.y - k * d1.y}, p1);
      }
    }

    void close() {
      if (!contour_.empty()) {
        closed_ = true;
        finish();
        current_ = start_;
      }
    }

    [[nodiscard]] Vec2 current() const { return current_; }

    Shape take() {
      finish();
      return std::move(shape_);
    }

   private:
    void ensure() {
      if (contour_.empty()) {
        contour_.push_back(transform_.apply(current_));
        start_ = current_;
      }
    }

    void finish() {
      if (contour_.size() > 1) {
        shape_.contours.push_back(std::move(contour_));
        shape_.closed.push_back(closed_);
      }
      contour_.clear();
      closed_ = false;
    }

    void subdivide(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, int depth) {
      const auto distance = [&](Vec2 q) {
        const double dx = p3.x - p0.x, dy = p3.y - p0.y, span = std::hypot(dx, dy);
        return span < 1e-12 ? std::hypot(q.x - p0.x, q.y - p0.y)
                            : std::abs((q.x - p0.x) * dy - (q.y - p0.y) * dx) / span;
      };
      if (depth >= 12 || std::max(distance(p1), distance(p2)) <= flatness) {
        contour_.push_back(p3);
        return;
      }
      const auto mid = [](Vec2 a, Vec2 b) { return Vec2{(a.x + b.x) / 2, (a.y + b.y) / 2}; };
      const Vec2 a = mid(p0, p1), b = mid(p1, p2), c = mid(p2, p3);
      const Vec2 ab = mid(a, b), bc = mid(b, c), center = mid(ab, bc);
      subdivide(p0, a, ab, center, depth + 1);
      subdivide(center, bc, c, p3, depth + 1);
    }

    Affine transform_;
    Vec2 start_{}, current_{};
    std::vector<Vec2> contour_;
    bool closed_{};
    Shape shape_;
  };

  static void path_data(Builder& builder, std::string_view data) {
    Scanner scanner(data);
    char command = '\0';
    Vec2 last_control{};
    char last_command = '\0';
    while (!scanner.done()) {
      if (std::isalpha(static_cast<unsigned char>(scanner.peek()))) {
        command = scanner.take();
      } else if (command == '\0') {
        fail("path data must start with a command");
      } else if (command == 'M') {
        command = 'L';
      } else if (command == 'm') {
        command = 'l';
      }
      const bool relative = std::islower(static_cast<unsigned char>(command)) != 0;
      const Vec2 origin = relative ? builder.current() : Vec2{};
      const auto point = [&] {
        const double x = scanner.number();
        const double y = scanner.number();
        return Vec2{origin.x + x, origin.y + y};
      };
      const auto reflected = [&](const char* kinds) {
        const Vec2 current = builder.current();
        if (std::string_view(kinds).find(static_cast<char>(std::toupper(last_command))) ==
            std::string_view::npos) {
          return current;
        }
        return Vec2{2 * current.x - last_control.x, 2 * current.y - last_control.y};
      };
      switch (std::toupper(static_cast<unsigned char>(command))) {
        case 'M':
          builder.move(point());
          break;
        case 'L':
          builder.line(point());
          break;
        case 'H': {
          const double x = scanner.number();
          builder.line({relative ? builder.current().x + x : x, builder.current().y});
          break;
        }
        case 'V': {
          const double y = scanner.number();
          builder.line({builder.current().x, relative ? builder.current().y + y : y});
          break;
        }
        case 'C': {
          const Vec2 c1 = point(), c2 = point(), p = point();
          builder.cubic(c1, c2, p);
          last_control = c2;
          break;
        }
        case 'S': {
          const Vec2 c1 = reflected("CS"), c2 = point(), p = point();
          builder.cubic(c1, c2, p);
          last_control = c2;
          break;
        }
        case 'Q': {
          const Vec2 c = point(), p = point();
          builder.quadratic(c, p);
          last_control = c;
          break;
        }
        case 'T': {
          const Vec2 c = reflected("QT"), p = point();
          builder.quadratic(c, p);
          last_control = c;
          break;
        }
        case 'A': {
          const double rx = scanner.number(), ry = scanner.number(), rotation = scanner.number();
          const bool large = scanner.flag(), sweep = scanner.flag();
          builder.arc(rx, ry, rotation, large, sweep, point());
          break;
        }
        case 'Z':
          builder.close();
          break;
        default:
          fail(std::string("unknown path command '") + command + "'");
      }
      last_command = command;
      if (std::toupper(static_cast<unsigned char>(command)) == 'Z' && scanner.number_next()) {
        fail("numbers after a closepath");
      }
    }
  }

  static std::vector<Vec2> points(const std::string& text) {
    Scanner scanner(text);
    std::vector<Vec2> result;
    while (!scanner.done()) {
      const double x = scanner.number();
      const double y = scanner.number();
      result.push_back({x, y});
    }
    return result;
  }

  Shape shape(const Xml& element, const std::string& name, const Affine& transform,
              const Style& style) {
    Builder builder(transform);
    const auto number = [&](const char* attribute, double percent_of) {
      const std::string value = element.attribute(attribute);
      return value.empty() ? 0.0 : length(value, percent_of);
    };
    if (name == "path") {
      path_data(builder, element.attribute("d"));
    } else if (name == "rect") {
      const double x = number("x", view_.x), y = number("y", view_.y);
      const double w = number("width", view_.x), h = number("height", view_.y);
      double rx = number("rx", view_.x), ry = number("ry", view_.y);
      if (element.attribute("rx").empty()) {
        rx = ry;
      }
      if (element.attribute("ry").empty()) {
        ry = rx;
      }
      rx = std::min(rx, w / 2);
      ry = std::min(ry, h / 2);
      if (w > 0 && h > 0) {
        builder.move({x + rx, y});
        builder.line({x + w - rx, y});
        if (rx > 0 && ry > 0) {
          builder.arc(rx, ry, 0, false, true, {x + w, y + ry});
        }
        builder.line({x + w, y + h - ry});
        if (rx > 0 && ry > 0) {
          builder.arc(rx, ry, 0, false, true, {x + w - rx, y + h});
        }
        builder.line({x + rx, y + h});
        if (rx > 0 && ry > 0) {
          builder.arc(rx, ry, 0, false, true, {x, y + h - ry});
        }
        builder.line({x, y + ry});
        if (rx > 0 && ry > 0) {
          builder.arc(rx, ry, 0, false, true, {x + rx, y});
        }
        builder.close();
      }
    } else if (name == "circle" || name == "ellipse") {
      const double cx = number("cx", view_.x), cy = number("cy", view_.y);
      const double rx = name == "circle" ? number("r", view_.x) : number("rx", view_.x);
      const double ry = name == "circle" ? rx : number("ry", view_.y);
      if (rx > 0 && ry > 0) {
        builder.move({cx + rx, cy});
        builder.arc(rx, ry, 0, false, true, {cx - rx, cy});
        builder.arc(rx, ry, 0, false, true, {cx + rx, cy});
        builder.close();
      }
    } else if (name == "line") {
      builder.move({number("x1", view_.x), number("y1", view_.y)});
      builder.line({number("x2", view_.x), number("y2", view_.y)});
    } else if (name == "polyline" || name == "polygon") {
      const auto list = points(element.attribute("points"));
      for (std::size_t i = 0; i < list.size(); ++i) {
        if (i == 0) {
          builder.move(list[i]);
        } else {
          builder.line(list[i]);
        }
      }
      if (name == "polygon") {
        builder.close();
      }
    } else {
      fail("<" + name + "> is not supported");
    }
    Shape result = builder.take();
    result.even_odd = style.even_odd;
    result.join = style.join;
    result.cap = style.cap;
    result.miter_limit = style.miter_limit;
    result.stroke_width = style.stroke_width * std::sqrt(std::abs(transform.determinant()));
    result.fill = paint(style.fill, style.fill_opacity, result, transform);
    if (name == "line" || name == "polyline") {
      result.fill.kind = Paint::Kind::none;
    }
    result.stroke = paint(style.stroke, style.stroke_opacity, result, transform);
    return result;
  }

  Paint paint(const std::string& text, double opacity, const Shape& shape,
              const Affine& transform) const {
    Paint result;
    result.opacity = opacity;
    if (text.starts_with("url(")) {
      const auto close = text.find(')');
      std::string id =
          trim(text.substr(4, close == std::string::npos ? std::string::npos : close - 4));
      if (id.starts_with("#")) {
        id = id.substr(1);
      }
      if (!gradients_.contains(id)) {
        fail("unknown paint server '" + text + "'");
      }
      const auto* stops = gradient_stops(id);
      if (!stops || stops->empty()) {
        result.kind = Paint::Kind::none;
        return result;
      }
      const bool box =
          gradient_attribute(id, "gradientUnits").value_or("objectBoundingBox") != "userSpaceOnUse";
      const auto coordinate = [&](const char* name, const char* fallback, double percent_of) {
        return length(gradient_attribute(id, name).value_or(fallback), box ? 1 : percent_of);
      };
      if (const auto spread = gradient_attribute(id, "spreadMethod"); spread && *spread != "pad") {
        fail("only spreadMethod=pad is supported");
      }
      result.from = {coordinate("x1", "0%", view_.x), coordinate("y1", "0%", view_.y)};
      result.to = {coordinate("x2", "100%", view_.x), coordinate("y2", "0%", view_.y)};
      Affine space = transform;
      if (box) {
        // The bounding box in user space, before the element's transform.
        const Affine inverse = transform.inverse();
        Vec2 low{1e300, 1e300}, high{-1e300, -1e300};
        for (const auto& contour : shape.contours) {
          for (const auto& point : contour) {
            const Vec2 user = inverse.apply(point);
            low = {std::min(low.x, user.x), std::min(low.y, user.y)};
            high = {std::max(high.x, user.x), std::max(high.y, user.y)};
          }
        }
        if (!(high.x > low.x && high.y > low.y)) {
          result.kind = Paint::Kind::none;
          return result;
        }
        space = space.then({high.x - low.x, 0, 0, high.y - low.y, low.x, low.y});
      }
      if (const auto extra = gradient_attribute(id, "gradientTransform")) {
        space = space.then(parse_transform(*extra));
      }
      const Affine inverse = space.inverse();
      result.inverse = {inverse.a, inverse.b, inverse.c, inverse.d, inverse.e, inverse.f};
      result.stops = *stops;
      result.kind = Paint::Kind::linear;
      if (result.stops.size() == 1) {
        result.kind = Paint::Kind::solid;
        result.color = result.stops[0].color;
        result.opacity *= result.stops[0].opacity;
      }
      return result;
    }
    const auto color = parse_color(text);
    if (!color) {
      return result;
    }
    result.kind = Paint::Kind::solid;
    result.color = color->rgb;
    result.opacity *= color->alpha;
    return result;
  }

  Drawing drawing_;
  Vec2 view_{};
  std::map<std::string, RawGradient, std::less<>> gradients_;
};

// --- Rendering ---

double unstretch(double value, double natural, double size, const std::optional<Stretch>& band) {
  if (!band || band->end <= band->start) {
    return size > 0 ? value * natural / size : value;
  }
  const double zone = band->end - band->start;
  const double fixed = natural - zone;
  if (size >= fixed) {
    const double grown = size - fixed;
    if (value <= band->start) {
      return value;
    }
    if (value >= band->start + grown) {
      return value - band->start - grown + band->end;
    }
    return grown > 0 ? band->start + (value - band->start) * zone / grown : band->start;
  }
  const double k = fixed > 0 ? size / fixed : 0;
  if (k <= 0) {
    return band->start;
  }
  const double unscaled = value / k;
  return unscaled <= band->start ? unscaled : unscaled + zone;
}

// Natural units to pixels and back, for one render.
class Layout {
 public:
  Layout(const Drawing& drawing, double width, double height, double scale, const Options& options)
      : drawing_(drawing), width_(width), height_(height), scale_(scale), options_(options) {}

  [[nodiscard]] Vec2 forward(Vec2 p) const {
    double x = stretch(p.x, drawing_.width, width_, drawing_.stretch_x);
    double y = stretch(p.y, drawing_.height, height_, drawing_.stretch_y);
    if (options_.flip_x) {
      x = width_ - x;
    }
    if (options_.flip_y) {
      y = height_ - y;
    }
    x = width_ / 2 + (x - width_ / 2) * options_.content_scale;
    y = height_ / 2 + (y - height_ / 2) * options_.content_scale;
    return {x * scale_, y * scale_};
  }

  [[nodiscard]] Vec2 backward(Vec2 pixel) const {
    double x = pixel.x / scale_, y = pixel.y / scale_;
    if (options_.content_scale > 0) {
      x = width_ / 2 + (x - width_ / 2) / options_.content_scale;
      y = height_ / 2 + (y - height_ / 2) / options_.content_scale;
    }
    if (options_.flip_x) {
      x = width_ - x;
    }
    if (options_.flip_y) {
      y = height_ - y;
    }
    return {unstretch(x, drawing_.width, width_, drawing_.stretch_x),
            unstretch(y, drawing_.height, height_, drawing_.stretch_y)};
  }

  // Splits each segment where it crosses a band edge, so the piecewise mapping is exact, then maps.
  [[nodiscard]] std::vector<Vec2> map(const std::vector<Vec2>& contour, bool closed) const {
    std::vector<double> xs, ys;
    if (drawing_.stretch_x) {
      xs = {drawing_.stretch_x->start, drawing_.stretch_x->end};
    }
    if (drawing_.stretch_y) {
      ys = {drawing_.stretch_y->start, drawing_.stretch_y->end};
    }
    std::vector<Vec2> result;
    const std::size_t count = contour.size() + (closed ? 1 : 0);
    for (std::size_t i = 0; i < count; ++i) {
      const Vec2 b = contour[i % contour.size()];
      if (i > 0) {
        const Vec2 a = contour[i - 1];
        std::vector<double> cuts;
        for (const double x : xs) {
          if ((a.x - x) * (b.x - x) < 0) {
            cuts.push_back((x - a.x) / (b.x - a.x));
          }
        }
        for (const double y : ys) {
          if ((a.y - y) * (b.y - y) < 0) {
            cuts.push_back((y - a.y) / (b.y - a.y));
          }
        }
        std::sort(cuts.begin(), cuts.end());
        for (const double t : cuts) {
          result.push_back(forward({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}));
        }
      }
      if (i < contour.size()) {
        result.push_back(forward(b));
      }
    }
    return result;
  }

  // Pixels per natural unit for widths (strokes do not stretch).
  [[nodiscard]] double width_scale() const { return scale_ * options_.content_scale; }

 private:
  const Drawing& drawing_;
  double width_, height_, scale_;
  const Options& options_;
};

std::pair<std::uint32_t, double> gradient_color(const Paint& paint, double t) {
  const auto& stops = paint.stops;
  if (t <= stops.front().offset) {
    return {stops.front().color, stops.front().opacity};
  }
  if (t >= stops.back().offset) {
    return {stops.back().color, stops.back().opacity};
  }
  std::size_t i = 1;
  while (i + 1 < stops.size() && stops[i].offset < t) {
    ++i;
  }
  const Stop& a = stops[i - 1];
  const Stop& b = stops[i];
  const double span = b.offset - a.offset;
  const double k = span > 0 ? (t - a.offset) / span : 1;
  std::uint32_t color = 0;
  for (int shift = 16; shift >= 0; shift -= 8) {
    const double from = (a.color >> shift) & 255U, to = (b.color >> shift) & 255U;
    color |= static_cast<std::uint32_t>(std::lround(from + (to - from) * k)) << shift;
  }
  return {color, a.opacity + (b.opacity - a.opacity) * k};
}

flash_draw::Shade shade(const Paint& paint, const Layout& layout, double opacity) {
  if (paint.kind == Paint::Kind::solid) {
    const double alpha = paint.opacity * opacity;
    const std::uint32_t color = paint.color;
    return [color, alpha](double, double) { return std::pair{color, alpha}; };
  }
  return [&paint, &layout, opacity](double x, double y) {
    const Vec2 natural = layout.backward({x, y});
    const auto& m = paint.inverse;
    const Vec2 g{m[0] * natural.x + m[2] * natural.y + m[4],
                 m[1] * natural.x + m[3] * natural.y + m[5]};
    const double dx = paint.to.x - paint.from.x, dy = paint.to.y - paint.from.y;
    const double length = dx * dx + dy * dy;
    const double t =
        length > 0 ? ((g.x - paint.from.x) * dx + (g.y - paint.from.y) * dy) / length : 0;
    auto [color, alpha] = gradient_color(paint, t);
    return std::pair{color, alpha * paint.opacity * opacity};
  };
}

// Pieces with positive orientation, so their overlaps accumulate under the nonzero rule.
void add_piece(flash_draw::Path& path, std::vector<Vec2> piece) {
  double area = 0;
  for (std::size_t i = 0; i < piece.size(); ++i) {
    const Vec2 a = piece[i], b = piece[(i + 1) % piece.size()];
    area += a.x * b.y - b.x * a.y;
  }
  if (std::abs(area) < 1e-12) {
    return;
  }
  if (area < 0) {
    std::reverse(piece.begin(), piece.end());
  }
  path.contours.push_back(std::move(piece));
}

void add_disc(flash_draw::Path& path, Vec2 center, double radius) {
  std::vector<Vec2> disc;
  const int steps = std::clamp(static_cast<int>(radius * 4), 12, 64);
  for (int k = 0; k < steps; ++k) {
    const double angle = 2 * std::numbers::pi * k / steps;
    disc.push_back({center.x + radius * std::cos(angle), center.y + radius * std::sin(angle)});
  }
  add_piece(path, std::move(disc));
}

// The stroke of a pixel-space polyline as a union of segment quads, joins and caps.
flash_draw::Path stroke_outline(std::vector<Vec2> points, bool closed, double width,
                                const Shape& shape) {
  flash_draw::Path outline;
  points.erase(std::unique(points.begin(), points.end(),
                           [](Vec2 a, Vec2 b) {
                             return std::abs(a.x - b.x) < 1e-9 && std::abs(a.y - b.y) < 1e-9;
                           }),
               points.end());
  if (closed && points.size() > 1 && std::abs(points.front().x - points.back().x) < 1e-9 &&
      std::abs(points.front().y - points.back().y) < 1e-9) {
    points.pop_back();
  }
  const double half = width / 2;
  if (points.size() < 2 || half <= 0) {
    return outline;
  }
  const std::size_t segments = closed ? points.size() : points.size() - 1;
  const auto direction = [&](std::size_t i) {
    const Vec2 a = points[i % points.size()], b = points[(i + 1) % points.size()];
    const double length = std::hypot(b.x - a.x, b.y - a.y);
    return Vec2{(b.x - a.x) / length, (b.y - a.y) / length};
  };
  for (std::size_t i = 0; i < segments; ++i) {
    const Vec2 a = points[i], b = points[(i + 1) % points.size()];
    const Vec2 d = direction(i);
    const Vec2 n{-d.y * half, d.x * half};
    add_piece(outline, {{a.x + n.x, a.y + n.y},
                        {b.x + n.x, b.y + n.y},
                        {b.x - n.x, b.y - n.y},
                        {a.x - n.x, a.y - n.y}});
  }
  for (std::size_t i = closed ? 0 : 1; i < (closed ? points.size() : points.size() - 1); ++i) {
    const Vec2 v = points[i];
    const Vec2 d0 = direction((i + points.size() - 1) % points.size()), d1 = direction(i);
    const double cross = d0.x * d1.y - d0.y * d1.x;
    const double dot = d0.x * d1.x + d0.y * d1.y;
    if (std::abs(cross) < 1e-9 && dot > 0) {
      continue;
    }
    if (shape.join == Shape::Join::round) {
      add_disc(outline, v, half);
      continue;
    }
    const double side = cross > 0 ? -1 : 1;
    const Vec2 n0{-d0.y, d0.x}, n1{-d1.y, d1.x};
    const Vec2 p0{v.x + side * half * n0.x, v.y + side * half * n0.y};
    const Vec2 p1{v.x + side * half * n1.x, v.y + side * half * n1.y};
    const double normals = 1 + n0.x * n1.x + n0.y * n1.y;
    const double ratio = normals > 1e-9 ? std::sqrt(2 / normals) : 1e9;
    if (shape.join == Shape::Join::miter && ratio <= shape.miter_limit) {
      const Vec2 tip{v.x + side * half * (n0.x + n1.x) / normals,
                     v.y + side * half * (n0.y + n1.y) / normals};
      add_piece(outline, {v, p0, tip, p1});
    } else {
      add_piece(outline, {v, p0, p1});
    }
  }
  if (!closed) {
    for (const bool start : {true, false}) {
      const Vec2 end = start ? points.front() : points.back();
      Vec2 d = direction(start ? 0 : segments - 1);
      if (start) {
        d = {-d.x, -d.y};
      }
      if (shape.cap == Shape::Cap::round) {
        add_disc(outline, end, half);
      } else if (shape.cap == Shape::Cap::square) {
        const Vec2 n{-d.y * half, d.x * half};
        const Vec2 tip{end.x + d.x * half, end.y + d.y * half};
        add_piece(outline, {{end.x + n.x, end.y + n.y},
                            {tip.x + n.x, tip.y + n.y},
                            {tip.x - n.x, tip.y - n.y},
                            {end.x - n.x, end.y - n.y}});
      }
    }
  }
  return outline;
}

void draw_shape(Image& image, const Shape& shape, const Layout& layout, double opacity) {
  if (shape.fill.kind != Paint::Kind::none) {
    flash_draw::Path path;
    for (const auto& contour : shape.contours) {
      path.contours.push_back(layout.map(contour, true));
    }
    flash_draw::fill(image, path, shade(shape.fill, layout, opacity), shape.even_odd);
  }
  if (shape.stroke.kind != Paint::Kind::none) {
    flash_draw::Path outline;
    const double width = shape.stroke_width * layout.width_scale();
    for (std::size_t i = 0; i < shape.contours.size(); ++i) {
      auto piece = stroke_outline(layout.map(shape.contours[i], shape.closed[i]), shape.closed[i],
                                  width, shape);
      for (auto& contour : piece.contours) {
        outline.contours.push_back(std::move(contour));
      }
    }
    flash_draw::fill(image, outline, shade(shape.stroke, layout, opacity), false);
  }
}

void draw_node(Image& image, const Node& node, const Layout& layout, double opacity) {
  const double alpha = opacity * node.opacity;
  if (alpha <= 0) {
    return;
  }
  const bool layered =
      node.opacity < 1 &&
      (!node.children.empty() || (node.shape && node.shape->fill.kind != Paint::Kind::none &&
                                  node.shape->stroke.kind != Paint::Kind::none));
  if (layered) {
    // Group opacity applies to the composited group, not to each child.
    Image layer = image;
    std::fill(layer.pixels.begin(), layer.pixels.end(), 0);
    Node opaque = node;
    opaque.opacity = 1;
    draw_node(layer, opaque, layout, 1);
    for (std::size_t i = 0; i < image.pixels.size(); i += 4) {
      const double source_alpha = layer.pixels[i + 3] * alpha / 255;
      for (std::size_t c = 0; c < 4; ++c) {
        const double value = layer.pixels[i + c] * alpha + image.pixels[i + c] * (1 - source_alpha);
        image.pixels[i + c] = static_cast<unsigned char>(std::clamp(std::lround(value), 0L, 255L));
      }
    }
    return;
  }
  if (node.shape) {
    draw_shape(image, *node.shape, layout, alpha);
  }
  for (const auto& child : node.children) {
    draw_node(image, child, layout, alpha);
  }
}
}

Drawing parse(std::string_view svg) {
  const XmlDocument document(svg);
  return Parser(document.root()).take();
}

double stretch(double value, double natural, double size, const std::optional<Stretch>& band) {
  if (!band || band->end <= band->start) {
    return natural > 0 ? value * size / natural : value;
  }
  const double zone = band->end - band->start;
  const double fixed = natural - zone;
  if (size >= fixed) {
    const double grown = size - fixed;
    if (value <= band->start) {
      return value;
    }
    if (value >= band->end) {
      return value - band->end + band->start + grown;
    }
    return band->start + (value - band->start) * grown / zone;
  }
  const double k = fixed > 0 ? size / fixed : 0;
  if (value <= band->start) {
    return value * k;
  }
  if (value >= band->end) {
    return (value - zone) * k;
  }
  return band->start * k;
}

Image render(const Drawing& drawing, double width, double height, double scale,
             const Options& options) {
  auto image = ui_image(width, height, scale);
  const Layout layout(drawing, width, height, scale, options);
  draw_node(image, drawing.root, layout, 1);
  if (options.tint) {
    const auto [color, amount] = *options.tint;
    flash_draw::color_transform(
        image, {1 - amount, 1 - amount, 1 - amount, 1, ((color >> 16) & 255U) * amount,
                ((color >> 8) & 255U) * amount, (color & 255U) * amount, 0});
  }
  if (options.grey) {
    constexpr double r = 0.2225, g = 0.7169, b = 0.0606;
    flash_draw::color_matrix(image, {r, g, b, 0, 0, r, g, b, 0, 0, r, g, b, 0, 0, 0, 0, 0, 1, 0});
  }
  return image;
}
}
