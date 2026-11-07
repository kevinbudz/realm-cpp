#include "realm/stage3D/Renderer.hpp"
#include "GraphicsBackend.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace realm {
namespace {
// Copies the GPU instances of list (the first quad_instance_bytes of each quad) into out.
void pack_instances(std::span<const Quad> list, std::byte* out) {
  for (std::size_t i = 0; i < list.size(); ++i) {
    std::memcpy(out + i * quad_instance_bytes, static_cast<const void*>(&list[i]),
                quad_instance_bytes);
  }
}

std::array<float, 2> floats(Vec2 p) {
  return {static_cast<float>(p.x), static_cast<float>(p.y)};
}
}

SurfaceTexture::~SurfaceTexture() {
  if (texture_) {
    gfx::backend().release_texture(texture_);
  }
}

void SurfaceTexture::update(const Image& image, bool premultiplied) {
  update_region(image, 0, 0, image.width, image.height, premultiplied);
}

void SurfaceTexture::update_region(const Image& image, int x, int y, int w, int h,
                                   bool premultiplied) {
  REALM_ZONE("render.texture_upload");
  if (image.width < 1 || image.height < 1 ||
      image.pixels.size() !=
          static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4 ||
      x < 0 || y < 0 || w < 1 || h < 1 || x + w > image.width || y + h > image.height) {
    throw std::invalid_argument("Invalid UI texture upload");
  }

  if (!texture_limit_) {
    texture_limit_ = gfx::backend().limits().max_texture_size;
  }
  const int limit = texture_limit_;
  if (image.width > limit || image.height > limit || reserved_width_ > limit ||
      reserved_height_ > limit) {
    throw std::runtime_error("UI surface exceeds GPU texture limit");
  }

  const bool created = !texture_;
  if (created) {
    texture_ = gfx::backend().create_surface(linear_filter_);
  }

  const bool resized = width_ != image.width || height_ != image.height;
  if (resized) {
    width_ = image.width;
    height_ = image.height;
    x = y = 0;
    w = width_;
    h = height_;
  }

  if ((!retain_capacity_ && resized) || std::max(width_, reserved_width_) > storage_width_ ||
      std::max(height_, reserved_height_) > storage_height_) {
    const auto capacity = [&](int size) {
      int result = 1;
      while (result < size) {
        result *= 2;
      }
      return std::min(result, limit);
    };
    storage_width_ = retain_capacity_ ? capacity(std::max(width_, reserved_width_))
                                      : std::max(width_, reserved_width_);
    storage_height_ = retain_capacity_ ? capacity(std::max(height_, reserved_height_))
                                       : std::max(height_, reserved_height_);
    telemetry::add(telemetry::Counter::texture_storage_bytes,
                   static_cast<std::uint64_t>(storage_width_) *
                       static_cast<std::uint64_t>(storage_height_) * 4);
    gfx::backend().allocate_surface(texture_, storage_width_, storage_height_);
  }

  telemetry::add(telemetry::Counter::texture_upload_bytes,
                 static_cast<std::uint64_t>(w) * static_cast<std::uint64_t>(h) * 4);
  telemetry::add(telemetry::Counter::ui_native_texture_uploads);
  telemetry::add(telemetry::Counter::ui_native_texture_upload_bytes,
                 static_cast<std::uint64_t>(w) * static_cast<std::uint64_t>(h) * 4);
  // Already-premultiplied complete rows can go straight to the backend; its upload consumes client
  // memory before returning.
  if (premultiplied && x == 0 && w == image.width) {
    gfx::backend().upload_surface(
        texture_, x, y, w, h,
        reinterpret_cast<const std::byte*>(
            image.pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 4));
    return;
  }
  // Share scratch storage across uploads instead of retaining a second image in every cached
  // surface.
  static thread_local Image pixels;
  pixels.width = w;
  pixels.height = h;
  pixels.pixels.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
  for (int row = 0; row < h; ++row) {
    std::copy_n(
        image.pixels.data() +
            (static_cast<std::size_t>(row + y) * static_cast<std::size_t>(image.width) +
             static_cast<std::size_t>(x)) *
                4,
        static_cast<std::size_t>(w) * 4,
        pixels.pixels.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(w) * 4);
  }
  if (!premultiplied) {
    premultiply_flash_bitmap(pixels);
  }
  gfx::backend().upload_surface(texture_, x, y, w, h,
                                reinterpret_cast<const std::byte*>(pixels.pixels.data()));
}

void SurfaceTexture::set_linear_filter(bool linear) {
  if (linear_filter_ == linear) {
    return;
  }
  linear_filter_ = linear;
  if (!texture_) {
    return;
  }
  gfx::backend().set_surface_filter(texture_, linear);
}

Quad SurfaceTexture::quad(Vec2 origin, Vec2 ax, Vec2 ay, int x, int y, int w, int h) const {
  if (!texture_) {
    throw std::logic_error("UI surface was not uploaded");
  }
  if (w == 0) {
    w = width_;
  }
  if (h == 0) {
    h = height_;
  }
  Quad q;
  q.origin = floats(origin);
  q.axis_x = floats(ax);
  q.axis_y = floats(ay);
  q.texture = texture_;
  q.kind = 5;
  const auto width = static_cast<float>(storage_width_);
  const auto height = static_cast<float>(storage_height_);
  q.uv = {static_cast<float>(x) / width, static_cast<float>(y) / height,
          static_cast<float>(w) / width, static_cast<float>(h) / height};
  return q;
}

TerrainBuffer::TerrainBuffer(std::span<const Quad> quads) : count_(quads.size()) {
  std::vector<std::byte> packed(quads.size() * quad_instance_bytes);
  pack_instances(quads, packed.data());
  buffer_ = gfx::backend().create_buffer(packed);
  telemetry::add(telemetry::Counter::quad_upload_bytes, packed.size());
}

TerrainBuffer::~TerrainBuffer() {
  if (buffer_) {
    gfx::backend().release_buffer(buffer_);
  }
}

QuadLayer::~QuadLayer() {
  if (buffer) {
    gfx::backend().release_buffer(buffer);
  }
}
}  // namespace realm
