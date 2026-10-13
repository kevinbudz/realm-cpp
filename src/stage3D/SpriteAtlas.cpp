#include <GL/glew.h>
#include "realm/util/Telemetry.hpp"
#include "realm/stage3D/SpriteAtlas.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace realm {
bool Atlas::hardware_copy() { return !force_blit_copy && (GLEW_VERSION_4_3 || GLEW_ARB_copy_image); }
Atlas::Atlas(const std::filesystem::path& root, std::span<const Image> additional, std::size_t raw_prefix,
             int dynamic_pages) {
  if (raw_prefix > additional.size()) throw std::invalid_argument("Invalid raw atlas prefix");
  if (dynamic_pages < 0) throw std::invalid_argument("Invalid dynamic atlas page count");
  dynamic_pages_ = dynamic_pages;
  asset_count_ = flash::assets.size();
  std::vector<Image> images;
  images.reserve(asset_count_ + additional.size());
  for (const auto& asset : flash::assets) {
    auto image = load_png(root / asset.path);
    normalize_flash_png(image);
    if (image.width != asset.width || image.height != asset.height)
      throw std::runtime_error("Assets changed; rebuild manifest");
    images.push_back(std::move(image));
  }
  images.insert(images.end(), additional.begin(), additional.end());
  for (std::size_t i = 0; i < raw_prefix; ++i) normalize_flash_png(images[asset_count_ + i]);
  for (auto& image : images) premultiply_flash_bitmap(image);
  struct FreeRect {
    int page, x, y, w, h;
  };
  std::vector<FreeRect> free;
  std::vector<std::size_t> order(images.size());
  std::iota(order.begin(), order.end(), 0);
  std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) { return images[a].height > images[b].height; });
  regions_.resize(images.size());
  for (auto index : order) {
    const auto& a = images[index];
    const int w = a.width + 2, h = a.height + 2;
    if (w > page_size || h >= page_size) throw std::runtime_error("Asset too large for atlas");
    std::size_t best = free.size();
    int area = std::numeric_limits<int>::max();
    for (std::size_t i = 0; i < free.size(); ++i)
      if (free[i].w >= w && free[i].h >= h && free[i].w * free[i].h < area) {
        best = i;
        area = free[i].w * free[i].h;
      }
    // Reserve the last row as transparent texels (including the synthesized invisible set).
    if (best == free.size()) free.push_back({pages_++, 0, 0, page_size, page_size - 1});
    const auto r = free[best];
    free.erase(free.begin() + static_cast<std::ptrdiff_t>(best));
    regions_[index] = {r.page, r.x + 1, r.y + 1, a.width, a.height};
    if (r.w > w) free.push_back({r.page, r.x + w, r.y, r.w - w, h});
    if (r.h > h) free.push_back({r.page, r.x, r.y + h, r.w, r.h - h});
  }
  GLint max_size{}, max_layers{};
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_size);
  glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &max_layers);
  if (max_size < page_size || pages_ + dynamic_pages_ > max_layers)
    throw std::runtime_error("GPU atlas limits insufficient");
  // Allocate zero-filled pages once. No per-frame texture reconstruction or upload.
  std::vector<std::vector<unsigned char>> data(
      static_cast<std::size_t>(pages_),
      std::vector<unsigned char>(static_cast<std::size_t>(page_size) * page_size * 4));
  for (auto index : order) {
    const auto& r = regions_[index];
    const auto& image = images[index];
    if (image.width != r.width || image.height != r.height)
      throw std::runtime_error("Assets changed since manifest generation; rebuild");
    auto& page = data[static_cast<std::size_t>(r.page)];
    // One extruded border around each whole image. Sprite subrects clamp to
    // their own texel centers in the shader so neighboring cells never leak.
    for (int y = -1; y <= r.height; ++y)
      for (int x = -1; x <= r.width; ++x) {
        const auto src =
            static_cast<std::size_t>((std::clamp(y, 0, r.height - 1) * r.width + std::clamp(x, 0, r.width - 1)) * 4);
        const auto dst = static_cast<std::size_t>(((r.y + y) * page_size + r.x + x) * 4);
        std::copy_n(image.pixels.begin() + static_cast<std::ptrdiff_t>(src), 4,
                    page.begin() + static_cast<std::ptrdiff_t>(dst));
      }
  }
  glGenTextures(1, &texture_);
  glBindTexture(GL_TEXTURE_2D_ARRAY, texture_);
  glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, page_size, page_size, pages_ + dynamic_pages_, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  for (int p = 0; p < pages_; ++p)
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, p, page_size, page_size, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                    data[static_cast<std::size_t>(p)].data());
  if (dynamic_pages_) {
    // Zero-filled like the static pages, so a texel never written is transparent rather than driver-defined.
    const std::vector<unsigned char> blank(static_cast<std::size_t>(page_size) * page_size * 4);
    for (int p = pages_; p < pages_ + dynamic_pages_; ++p)
      glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, p, page_size, page_size, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                      blank.data());
  }
  if (glGetError() != GL_NO_ERROR) {
    glDeleteTextures(1, &texture_);
    texture_ = 0;
    throw std::runtime_error("GPU atlas upload failed");
  }
}
Atlas::~Atlas() {
  if (texture_) glDeleteTextures(1, &texture_);
}
void Atlas::update_additional(std::size_t index, const Image& image) {
  const auto r = additional(index);
  if (image.width != r.width || image.height != r.height ||
      image.pixels.size() != static_cast<std::size_t>(r.width) * static_cast<std::size_t>(r.height) * 4)
    throw std::invalid_argument("Atlas update dimensions differ from reserved region");
  auto bitmap = image;
  premultiply_flash_bitmap(bitmap);
  std::vector<unsigned char> pixels(static_cast<std::size_t>(r.width + 2) * static_cast<std::size_t>(r.height + 2) * 4);
  for (int y = -1; y <= r.height; ++y)
    for (int x = -1; x <= r.width; ++x) {
      const auto src =
          static_cast<std::size_t>((std::clamp(y, 0, r.height - 1) * r.width + std::clamp(x, 0, r.width - 1)) * 4);
      const auto dst = static_cast<std::size_t>(((y + 1) * (r.width + 2) + x + 1) * 4);
      std::copy_n(bitmap.pixels.data() + src, 4, pixels.data() + dst);
    }
  glBindTexture(GL_TEXTURE_2D_ARRAY, texture_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  telemetry::add(telemetry::Counter::texture_upload_bytes, pixels.size());
  glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, r.x - 1, r.y - 1, r.page, r.width + 2, r.height + 2, 1, GL_RGBA,
                  GL_UNSIGNED_BYTE, pixels.data());
}
void Atlas::upload_dynamic(int page, int x, int y, const Image& image) {
  REALM_ZONE("render.texture_upload");
  if (page < 0 || page >= dynamic_pages_ || x < 0 || y < 0 || image.width < 1 || image.height < 1 ||
      x + image.width > page_size || y + image.height > page_size ||
      image.pixels.size() != static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4)
    throw std::invalid_argument("Invalid dynamic atlas upload");
  auto bitmap = image;
  premultiply_flash_bitmap(bitmap);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D_ARRAY, texture_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  telemetry::add(telemetry::Counter::texture_upload_bytes, bitmap.pixels.size());
  telemetry::add(telemetry::Counter::atlas_uploads);
  glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, x, y, pages_ + page, bitmap.width, bitmap.height, 1, GL_RGBA,
                  GL_UNSIGNED_BYTE, bitmap.pixels.data());
}
void Atlas::resize_dynamic(int pages) {
  REALM_ZONE("render.texture_upload");
  if (pages < 0) throw std::invalid_argument("Invalid dynamic atlas page count");
  if (pages == dynamic_pages_) return;
  GLint max_layers{};
  glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &max_layers);
  if (pages_ + pages > max_layers) throw std::runtime_error("GPU atlas limits insufficient");
  GLint read_binding{}, draw_binding{}, unit{}, bound{};
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_binding);
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_binding);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &unit);
  glActiveTexture(GL_TEXTURE0);
  glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &bound);
  while (glGetError() != GL_NO_ERROR) {
  }
  GLuint fresh{};
  glGenTextures(1, &fresh);
  glBindTexture(GL_TEXTURE_2D_ARRAY, fresh);
  glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, page_size, page_size, pages_ + pages, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               nullptr);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  const int kept = pages_ + std::min(dynamic_pages_, pages);
  if (hardware_copy())
    glCopyImageSubData(texture_, GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0, fresh, GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0, page_size,
                       page_size, kept);
  else {
    // Blits are clipped by the scissor test, which the renderer enables while drawing.
    const bool scissor = glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE;
    glDisable(GL_SCISSOR_TEST);
    GLuint fbos[2]{};
    glGenFramebuffers(2, fbos);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbos[0]);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbos[1]);
    for (int layer = 0; layer < kept; ++layer) {
      glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, texture_, 0, layer);
      glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, fresh, 0, layer);
      glBlitFramebuffer(0, 0, page_size, page_size, 0, 0, page_size, page_size, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDeleteFramebuffers(2, fbos);
    if (scissor) glEnable(GL_SCISSOR_TEST);
  }
  if (kept < pages_ + pages) {
    const std::vector<unsigned char> blank(static_cast<std::size_t>(page_size) * page_size * 4);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int layer = kept; layer < pages_ + pages; ++layer)
      glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, page_size, page_size, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                      blank.data());
  }
  const bool failed = glGetError() != GL_NO_ERROR;
  glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(read_binding));
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(draw_binding));
  if (failed) {
    glBindTexture(GL_TEXTURE_2D_ARRAY, static_cast<GLuint>(bound));
    glActiveTexture(static_cast<GLenum>(unit));
    glDeleteTextures(1, &fresh);
    throw std::runtime_error("GPU atlas resize failed");
  }
  const auto old = texture_;
  texture_ = fresh;
  dynamic_pages_ = pages;
  ++resizes_;
  glBindTexture(GL_TEXTURE_2D_ARRAY, static_cast<GLuint>(bound) == old ? fresh : static_cast<GLuint>(bound));
  glActiveTexture(static_cast<GLenum>(unit));
  glDeleteTextures(1, &old);
  telemetry::add(telemetry::Counter::atlas_resizes);
  telemetry::add(telemetry::Counter::texture_storage_bytes, bytes());
}
SpriteRegion Atlas::sprite(std::size_t sheet_index, int index) const {
  const auto& s = flash::sheets.at(sheet_index);
  if (index < 0 || index >= s.count) throw std::out_of_range("Sprite index outside Flash image set");
  if (s.asset < 0) return {0, page_size - 1, page_size - 1, 1, 1};
  const auto& a = regions_.at(static_cast<std::size_t>(s.asset));
  const int columns = a.width / s.width;
  return {a.page, a.x + index % columns * s.width, a.y + index / columns * s.height, s.width, s.height};
}
void Atlas::update_additional_region(std::size_t index, const Image& image, int x, int y, int width, int height) {
  const auto r = additional(index);
  if (image.width != r.width || image.height != r.height || x < 0 || y < 0 || width < 1 || height < 1 ||
      width > image.width - x || height > image.height - y ||
      image.pixels.size() != static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4)
    throw std::invalid_argument("Invalid partial atlas update");
  const int left = x - (x == 0), top = y - (y == 0), right = x + width + (x + width == r.width),
            bottom = y + height + (y + height == r.height);
  Image pixels{
      right - left, bottom - top,
      std::vector<unsigned char>(static_cast<std::size_t>(right - left) * static_cast<std::size_t>(bottom - top) * 4)};
  for (int row = top; row < bottom; ++row)
    for (int col = left; col < right; ++col) {
      const auto src = (static_cast<std::size_t>(std::clamp(row, 0, r.height - 1)) * static_cast<std::size_t>(r.width) +
                        static_cast<std::size_t>(std::clamp(col, 0, r.width - 1))) *
                       4;
      const auto dst = (static_cast<std::size_t>(row - top) * static_cast<std::size_t>(pixels.width) +
                        static_cast<std::size_t>(col - left)) *
                       4;
      std::copy_n(image.pixels.data() + src, 4, pixels.pixels.data() + dst);
    }
  premultiply_flash_bitmap(pixels);
  glBindTexture(GL_TEXTURE_2D_ARRAY, texture_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  telemetry::add(telemetry::Counter::texture_upload_bytes, pixels.pixels.size());
  glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, r.x + left, r.y + top, r.page, pixels.width, pixels.height, 1, GL_RGBA,
                  GL_UNSIGNED_BYTE, pixels.pixels.data());
}
}
