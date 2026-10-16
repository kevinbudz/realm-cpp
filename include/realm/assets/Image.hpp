#pragma once
#include <filesystem>
#include <vector>
#include <span>
#include <cstdint>

namespace realm {
struct Image {
  int width{};
  int height{};
  std::vector<unsigned char> pixels;
  // UI images carry their density; coordinates in retained layouts stay logical.
  double pixel_scale{1};
};

[[nodiscard]] Image ui_image(double width, double height, double scale);
void ui_blit(Image& destination, const Image& source, double x, double y,
             bool source_premultiplied = false, bool destination_premultiplied = false,
             double opacity = 1, double clip_x = 0, double clip_y = 0, double clip_width = 1e9,
             double clip_height = 1e9);
void ui_rectangle(Image& destination, double x, double y, double width, double height,
                  std::uint32_t color, bool premultiplied = false);
// Adds a black shadow to premultiplied content, then converts to straight alpha.
void ui_shadow(Image& image, double radius, double width, double height);
// Adds a crisp black outline `radius` logical px wide beneath premultiplied content. Only alpha
// changes, so the content stays premultiplied.
void ui_outline_alpha(Image& image, double radius = 1);
void ui_rounded_rectangle(Image& destination, double x, double y, double width, double height,
                          double radius, std::uint32_t color, double opacity,
                          bool premultiplied = false);
[[nodiscard]] Image load_png(const std::filesystem::path& path);
[[nodiscard]] Image load_png(std::span<const unsigned char> bytes);
// BitmapData colors are stored premultiplied. The Flash PNG loader and encoder
// quantize translucent channels differently from a plain straight-alpha PNG.
void normalize_flash_png(Image& image);
void premultiply_flash_bitmap(Image& image);
void save_png(const std::filesystem::path& path, const Image& image);
}
