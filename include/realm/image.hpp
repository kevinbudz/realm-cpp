#pragma once
#include <filesystem>
#include <vector>
#include <span>
namespace realm {
struct Image { int width{}, height{}; std::vector<unsigned char> pixels; };
[[nodiscard]] Image load_png(const std::filesystem::path& path);
[[nodiscard]] Image load_png(std::span<const unsigned char> bytes);
// Flash stores BitmapData colors premultiplied. Its PNG loader and encoder
// quantize translucent channels differently from a plain straight-alpha PNG.
void normalize_flash_png(Image& image);
void premultiply_flash_bitmap(Image& image);
void save_png(const std::filesystem::path& path, const Image& image);
}
