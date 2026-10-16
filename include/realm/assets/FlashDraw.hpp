#pragma once
#include "realm/map/Camera.hpp"
#include "realm/assets/Image.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <utility>
#include <vector>

// A small subset of display list rendering, enough to draw the original UI decorations natively:
// Graphics fills and strokes, BitmapData.draw of bitmaps, ColorTransform/ColorMatrixFilter and the
// BlurFilter-based glow and drop shadow filters. Canvases are premultiplied RGBA at density 1.
namespace realm::flash_draw {
struct Path {
  std::vector<std::vector<Vec2>> contours;
  void move(double x, double y);
  void line(double x, double y);
  void curve(double cx, double cy, double x, double y);
  void close();
  // GraphicsUtil.drawCutEdgeRect; corners flagged top-left, top-right, bottom-right, bottom-left.
  void cut_edge_rect(double x, double y, double width, double height, double cut,
                     std::array<bool, 4> corners = {true, true, true, true});
  void rect(double x, double y, double width, double height);
  // Graphics.drawRoundRect: ellipse_width is the corner diameter.
  void round_rect(double x, double y, double width, double height, double ellipse_width);
  void circle(double x, double y, double radius);
  // GraphicsUtil.drawDiamond: a square rotated 45 degrees with the given half diagonal.
  void diamond(double x, double y, double radius);
};

[[nodiscard]] Image canvas(int width, int height);
// Nonzero fill with exact area coverage, composited source-over.
void fill(Image& canvas, const Path& path, std::uint32_t color, double alpha = 1, Vec2 offset = {});
// The straight 0xRRGGBB color and alpha of a paint at a pixel center (x + .5, y + .5).
using Shade = std::function<std::pair<std::uint32_t, double>(double x, double y)>;
// Fill with exact area coverage and a color per pixel, nonzero or even-odd, composited source-over.
void fill(Image& canvas, const Path& path, const Shade& shade, bool even_odd);
// A centered stroke with round joins and no caps.
void stroke(Image& canvas, const Path& path, double width, std::uint32_t color, double alpha = 1,
            Vec2 offset = {});
// Draws a premultiplied bitmap translated (and scaled, nearest neighbour) source-over.
void draw(Image& canvas, const Image& source, double x, double y, double scale_x = 1,
          double scale_y = 1, double alpha = 1);

struct ColorTransform {
  double red{1};
  double green{1};
  double blue{1};
  double alpha{1};
  double red_offset{};
  double green_offset{};
  double blue_offset{};
  double alpha_offset{};
};

void color_transform(Image& image, const ColorTransform& transform);
// A ColorMatrixFilter's 4x5 matrix applied to unpremultiplied colors.
void color_matrix(Image& image, const std::array<double, 20>& matrix);
// BlurFilter: `quality` passes of a box exactly blur_x by blur_y pixels wide, on every channel.
void blur(Image& image, double blur_x, double blur_y, int quality = 1);
// GlowFilter / DropShadowFilter (outer, not knocked out), drawn behind the image.
void glow(Image& image, std::uint32_t color, double alpha, double blur_x, double blur_y,
          double strength, int quality = 1);
void drop_shadow(Image& image, double distance, double angle_degrees, std::uint32_t color,
                 double alpha, double blur_x, double blur_y, double strength = 1, int quality = 1);
// Grows the canvas by `margin` pixels on every side, so filters have room to spread.
[[nodiscard]] Image padded(const Image& image, int margin);

// The first DefineShape in an embedded vector SWF (com/company/rotmg/graphics), in twips converted
// to pixels.
struct Shape {
  struct Fill {
    std::uint32_t color{};
    double alpha{1};
    Path path;
  };

  struct Line {
    double width{};
    std::uint32_t color{};
    double alpha{1};
    Path path;
  };

  std::vector<Fill> fills;
  std::vector<Line> lines;
  // Paint order: (is_line, index into fills or lines). Each shape's fills come before its lines.
  std::vector<std::pair<bool, std::size_t>> order;
  double left{}, top{}, right{}, bottom{};

  [[nodiscard]] double width() const { return right - left; }

  [[nodiscard]] double height() const { return bottom - top; }
};

[[nodiscard]] Shape load_swf_shape(const std::filesystem::path& path);
// Renders a shape with its origin at (x, y), scaled.
void draw_shape(Image& canvas, const Shape& shape, double x, double y, double scale = 1,
                const ColorTransform& transform = {});
}
