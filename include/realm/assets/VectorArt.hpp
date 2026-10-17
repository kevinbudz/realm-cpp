#pragma once
#include "realm/assets/Image.hpp"
#include "realm/map/Camera.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Resolution-independent UI art: a strict SVG subset, rasterized at drawable density with the
// flash_draw coverage rasterizer. A drawing may stretch like a Flash scale-9 grid: the root
// <svg> carries data-stretch-x="A B" and/or data-stretch-y="A B" (natural units), and drawing it
// larger than its natural size widens only the band between A and B. Geometry is split at A and B
// and moved, never resampled, so edges stay sharp and outlines keep their width at any size.
//
// Supported: svg, g, path (all commands, arcs included), rect (rx/ry), circle, ellipse, line,
// polyline, polygon; linearGradient (userSpaceOnUse or objectBoundingBox, gradientTransform,
// href-inherited stops); fill, stroke, *-opacity, opacity, fill-rule, stroke-width,
// stroke-linejoin, stroke-linecap, stroke-miterlimit, transform; presentation attributes or
// style="". title, desc, metadata, defs and editor namedview elements are ignored. Anything else
// (text, image, use, filters, masks, clip paths, radial gradients) is rejected, so an asset never
// silently draws differently from its source.
namespace realm::vector_art {
struct Stop {
  double offset{};
  std::uint32_t color{};
  double opacity{1};
};

struct Paint {
  enum class Kind { none, solid, linear };
  Kind kind{Kind::none};
  std::uint32_t color{};
  double opacity{1};
  // Linear gradients: t = 0 at `from`, 1 at `to`, after `inverse` maps a natural-unit point into
  // gradient space. Padded at both ends.
  Vec2 from{};
  Vec2 to{1, 0};
  std::array<double, 6> inverse{1, 0, 0, 1, 0, 0};
  std::vector<Stop> stops;
};

struct Shape {
  // Flattened contours in natural units; `closed` per contour (fills always close).
  std::vector<std::vector<Vec2>> contours;
  std::vector<bool> closed;
  bool even_odd{};
  Paint fill;
  Paint stroke;
  double stroke_width{1};  // natural units
  enum class Join { miter, round, bevel } join{Join::miter};
  enum class Cap { butt, round, square } cap{Cap::butt};
  double miter_limit{4};
};

struct Node {
  std::optional<Shape> shape;
  std::vector<Node> children;
  double opacity{1};
};

struct Stretch {
  double start{};
  double end{};
};

struct Drawing {
  double width{};  // natural size, in design units
  double height{};
  std::optional<Stretch> stretch_x;
  std::optional<Stretch> stretch_y;
  Node root;
};

// Throws std::runtime_error naming the first unsupported or malformed construct.
[[nodiscard]] Drawing parse(std::string_view svg);

// The Deca UI art file ui/deca/FILE (for example "generic_green_button.svg"), embedded in the
// library at build time, or null when no such file exists. The headless HUD and the RmlUi host both
// draw from this copy.
[[nodiscard]] const std::string* embedded_deca_svg(std::string_view file);

struct Options {
  bool flip_x{};
  bool flip_y{};
  // Scales the art about the box center (SliceScalingButton's 0.9 press), redrawn, not resampled.
  double content_scale{1};
  // Tint.add: color * amount + pixel * (1 - amount).
  std::optional<std::pair<std::uint32_t, double>> tint;
  // GreyScale.setGreyScale's luminance matrix.
  bool grey{};
};

// Maps a natural-unit coordinate on one axis to a box `size` design units long. Wider than
// natural, only the stretch band grows; narrower than the fixed parts, they shrink uniformly and
// the band collapses. Without a band the axis scales uniformly.
[[nodiscard]] double stretch(double value, double natural, double size,
                             const std::optional<Stretch>& band);

// A premultiplied image of ceil(width * scale) x ceil(height * scale) pixels: the drawing laid
// out in a width x height design-unit box at `scale` pixels per unit.
[[nodiscard]] Image render(const Drawing& drawing, double width, double height, double scale,
                           const Options& options = {});
}
