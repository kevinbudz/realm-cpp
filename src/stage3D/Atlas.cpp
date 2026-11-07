#include "realm/util/Telemetry.hpp"
#include "realm/stage3D/Atlas.hpp"
#include "GraphicsBackend.hpp"
#include "realm/assets/AnimationLibrary.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace realm {
bool Atlas::hardware_copy() {
  return !force_blit_copy && gfx::backend().copy_images_supported();
}

Atlas::Atlas(const std::filesystem::path& root, std::span<const Image> additional,
             std::size_t raw_prefix, int dynamic_pages)
    : Atlas(FlashImages(root), additional, raw_prefix, dynamic_pages) {}

Atlas::Atlas(const FlashImages& pngs, std::span<const Image> additional, std::size_t raw_prefix,
             int dynamic_pages) {
  if (raw_prefix > additional.size()) {
    throw std::invalid_argument("Invalid raw atlas prefix");
  }
  if (dynamic_pages < 0) {
    throw std::invalid_argument("Invalid dynamic atlas page count");
  }
  dynamic_pages_ = dynamic_pages;
  asset_count_ = flash::assets.size();
  std::vector<Image> images;
  images.reserve(asset_count_ + additional.size());
  for (std::size_t i = 0; i < flash::assets.size(); ++i) {
    const auto& asset = flash::assets[i];
    const auto& image = pngs.normalized.at(i);
    if (image.width != asset.width || image.height != asset.height) {
      throw std::runtime_error("Assets changed; rebuild manifest");
    }
    images.push_back(image);
  }
  images.insert(images.end(), additional.begin(), additional.end());
  for (std::size_t i = 0; i < raw_prefix; ++i) {
    normalize_flash_png(images[asset_count_ + i]);
  }
  for (auto& image : images) {
    premultiply_flash_bitmap(image);
  }

  struct FreeRect {
    int page, x, y, w, h;
  };

  std::vector<FreeRect> free;
  std::vector<std::size_t> order(images.size());
  std::iota(order.begin(), order.end(), 0);
  std::stable_sort(order.begin(), order.end(), [&](auto left, auto right) {
    return images[left].height > images[right].height;
  });
  regions_.resize(images.size());
  for (auto index : order) {
    const auto& image = images[index];
    const int width = image.width + 2;
    const int height = image.height + 2;
    if (width > page_size || height >= page_size) {
      throw std::runtime_error("Asset too large for atlas");
    }
    std::size_t best = free.size();
    int area = std::numeric_limits<int>::max();
    for (std::size_t i = 0; i < free.size(); ++i) {
      if (free[i].w >= width && free[i].h >= height && free[i].w * free[i].h < area) {
        best = i;
        area = free[i].w * free[i].h;
      }
    }
    // Reserve the last row as transparent texels (including the synthesized invisible set).
    if (best == free.size()) {
      free.push_back({pages_++, 0, 0, page_size, page_size - 1});
    }
    const auto rect = free[best];
    free.erase(free.begin() + static_cast<std::ptrdiff_t>(best));
    regions_[index] = {rect.page, rect.x + 1, rect.y + 1, image.width, image.height};
    if (rect.w > width) {
      free.push_back({rect.page, rect.x + width, rect.y, rect.w - width, height});
    }
    if (rect.h > height) {
      free.push_back({rect.page, rect.x, rect.y + height, rect.w, rect.h - height});
    }
  }
  const auto limits = gfx::backend().limits();
  if (limits.max_texture_size < page_size || pages_ + dynamic_pages_ > limits.max_array_layers) {
    throw std::runtime_error("GPU atlas limits insufficient");
  }

  // Allocate zero-filled pages once. No per-frame texture reconstruction or upload.
  std::vector<std::vector<unsigned char>> data(
      static_cast<std::size_t>(pages_),
      std::vector<unsigned char>(static_cast<std::size_t>(page_size) * page_size * 4));
  for (auto index : order) {
    const auto& region = regions_[index];
    const auto& image = images[index];
    if (image.width != region.width || image.height != region.height) {
      throw std::runtime_error("Assets changed since manifest generation; rebuild");
    }
    auto& page = data[static_cast<std::size_t>(region.page)];
    // One extruded border around each whole image. Sprite subrects clamp to
    // their own texel centers in the shader so neighboring cells never leak.
    for (int y = -1; y <= region.height; ++y) {
      for (int x = -1; x <= region.width; ++x) {
        const auto src =
            static_cast<std::size_t>((std::clamp(y, 0, region.height - 1) * region.width +
                                      std::clamp(x, 0, region.width - 1)) *
                                     4);
        const auto dst = static_cast<std::size_t>(((region.y + y) * page_size + region.x + x) * 4);
        std::copy_n(image.pixels.begin() + static_cast<std::ptrdiff_t>(src), 4,
                    page.begin() + static_cast<std::ptrdiff_t>(dst));
      }
    }
  }
  texture_ = gfx::backend().create_layers(page_size, pages_ + dynamic_pages_);
  for (int p = 0; p < pages_; ++p) {
    gfx::backend().upload_layer(
        texture_, p, 0, 0, page_size, page_size,
        reinterpret_cast<const std::byte*>(data[static_cast<std::size_t>(p)].data()));
  }
  if (dynamic_pages_) {
    // Zero-filled like the static pages, so a texel never written is transparent rather than
    // driver-defined.
    const std::vector<unsigned char> blank(static_cast<std::size_t>(page_size) * page_size * 4);
    for (int p = pages_; p < pages_ + dynamic_pages_; ++p) {
      gfx::backend().upload_layer(texture_, p, 0, 0, page_size, page_size,
                                  reinterpret_cast<const std::byte*>(blank.data()));
    }
  }
  gfx::backend().verify_layers(texture_);
}

Atlas::~Atlas() {
  if (texture_) {
    gfx::backend().release_texture(texture_);
  }
}

void Atlas::update_additional(std::size_t index, const Image& image) {
  const auto region = additional(index);
  if (image.width != region.width || image.height != region.height ||
      image.pixels.size() !=
          static_cast<std::size_t>(region.width) * static_cast<std::size_t>(region.height) * 4) {
    throw std::invalid_argument("Atlas update dimensions differ from reserved region");
  }

  // Shared scratch storage: the copy reuses its capacity across uploads.
  static thread_local Image bitmap;
  bitmap = image;
  premultiply_flash_bitmap(bitmap);
  std::vector<unsigned char> pixels(static_cast<std::size_t>(region.width + 2) *
                                    static_cast<std::size_t>(region.height + 2) * 4);
  for (int y = -1; y <= region.height; ++y) {
    for (int x = -1; x <= region.width; ++x) {
      const auto src =
          static_cast<std::size_t>((std::clamp(y, 0, region.height - 1) * region.width +
                                    std::clamp(x, 0, region.width - 1)) *
                                   4);
      const auto dst = static_cast<std::size_t>(((y + 1) * (region.width + 2) + x + 1) * 4);
      std::copy_n(bitmap.pixels.data() + src, 4, pixels.data() + dst);
    }
  }
  telemetry::add(telemetry::Counter::texture_upload_bytes, pixels.size());
  gfx::backend().upload_layer(texture_, region.page, region.x - 1, region.y - 1, region.width + 2,
                              region.height + 2, reinterpret_cast<const std::byte*>(pixels.data()));
}

void Atlas::upload_dynamic(int page, int x, int y, const Image& image) {
  REALM_ZONE("render.texture_upload");
  if (page < 0 || page >= dynamic_pages_ || x < 0 || y < 0 || image.width < 1 || image.height < 1 ||
      x + image.width > page_size || y + image.height > page_size ||
      image.pixels.size() !=
          static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4) {
    throw std::invalid_argument("Invalid dynamic atlas upload");
  }

  static thread_local Image bitmap;
  bitmap = image;
  premultiply_flash_bitmap(bitmap);
  telemetry::add(telemetry::Counter::texture_upload_bytes, bitmap.pixels.size());
  telemetry::add(telemetry::Counter::atlas_uploads);
  gfx::backend().upload_layer(texture_, pages_ + page, x, y, bitmap.width, bitmap.height,
                              reinterpret_cast<const std::byte*>(bitmap.pixels.data()));
}

void Atlas::resize_dynamic(int pages) {
  REALM_ZONE("render.texture_upload");
  if (pages < 0) {
    throw std::invalid_argument("Invalid dynamic atlas page count");
  }
  if (pages == dynamic_pages_) {
    return;
  }
  if (pages_ + pages > gfx::backend().limits().max_array_layers) {
    throw std::runtime_error("GPU atlas limits insufficient");
  }
  // Layers kept: every static layer and the first min(old, new) dynamic layers.
  const int kept = pages_ + std::min(dynamic_pages_, pages);
  const auto fresh =
      gfx::backend().resize_layers(texture_, page_size, pages_ + pages, kept, hardware_copy());
  const auto old_texture = texture_;
  texture_ = fresh;
  dynamic_pages_ = pages;
  ++resizes_;
  gfx::backend().release_texture(old_texture);
  telemetry::add(telemetry::Counter::atlas_resizes);
  telemetry::add(telemetry::Counter::texture_storage_bytes, bytes());
}

SpriteRegion Atlas::sprite(std::size_t sheet_index, int index) const {
  const auto& sheet = flash::sheets.at(sheet_index);
  if (index < 0 || index >= sheet.count) {
    throw std::out_of_range("Sprite index outside Flash image set");
  }
  if (sheet.asset < 0) {
    return {0, page_size - 1, page_size - 1, 1, 1};
  }
  const auto& region = regions_.at(static_cast<std::size_t>(sheet.asset));
  const int columns = region.width / sheet.width;
  return {region.page, region.x + index % columns * sheet.width,
          region.y + index / columns * sheet.height, sheet.width, sheet.height};
}

void Atlas::update_additional_region(std::size_t index, const Image& image, int x, int y, int width,
                                     int height) {
  const auto region = additional(index);
  if (image.width != region.width || image.height != region.height || x < 0 || y < 0 || width < 1 ||
      height < 1 || width > image.width - x || height > image.height - y ||
      image.pixels.size() !=
          static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4) {
    throw std::invalid_argument("Invalid partial atlas update");
  }
  const int left = x - (x == 0);
  const int top = y - (y == 0);
  const int right = x + width + (x + width == region.width);
  const int bottom = y + height + (y + height == region.height);
  Image pixels{right - left, bottom - top,
               std::vector<unsigned char>(static_cast<std::size_t>(right - left) *
                                          static_cast<std::size_t>(bottom - top) * 4)};
  for (int row = top; row < bottom; ++row) {
    for (int col = left; col < right; ++col) {
      const auto src = (static_cast<std::size_t>(std::clamp(row, 0, region.height - 1)) *
                            static_cast<std::size_t>(region.width) +
                        static_cast<std::size_t>(std::clamp(col, 0, region.width - 1))) *
                       4;
      const auto dst =
          (static_cast<std::size_t>(row - top) * static_cast<std::size_t>(pixels.width) +
           static_cast<std::size_t>(col - left)) *
          4;
      std::copy_n(image.pixels.data() + src, 4, pixels.pixels.data() + dst);
    }
  }
  premultiply_flash_bitmap(pixels);
  telemetry::add(telemetry::Counter::texture_upload_bytes, pixels.pixels.size());
  gfx::backend().upload_layer(texture_, region.page, region.x + left, region.y + top, pixels.width,
                              pixels.height,
                              reinterpret_cast<const std::byte*>(pixels.pixels.data()));
}
}
