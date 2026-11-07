#pragma once
// GPU resources behind the handles of GraphicsBackend.hpp: surfaces (2D RGBA8), layer arrays (the
// atlas) and static buffers, plus the per-frame Stream the instance and uniform data are written to.
// A handle is index + 1 into the table; 0 means none. Writes go through queue.writeTexture and
// queue.writeBuffer, which WebGPU runs before the next submission; a layer resize copies on a
// submission of its own, so later uploads keep their order.
#include "WgpuContext.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace realm::gfx::wgpu {
class Resources {
 public:
  // Creates the samplers. Needs the device (the context is initialised).
  void create();
  // Releases everything. Idempotent.
  void destroy() noexcept;

  // Surfaces: RGBA8 2D textures, clamp to edge, nearest unless linear.
  [[nodiscard]] std::uint32_t create_surface(bool linear);
  void allocate_surface(std::uint32_t texture, int width, int height);
  void upload_surface(std::uint32_t texture, int x, int y, int width, int height, const std::byte* rgba);
  void set_surface_filter(std::uint32_t texture, bool linear);

  // Layer arrays (atlas): size x size RGBA8 layers, nearest, clamp to edge, zero at creation.
  [[nodiscard]] std::uint32_t create_layers(int size, int layers);
  void upload_layer(std::uint32_t texture, int layer, int x, int y, int width, int height,
                    const std::byte* rgba);
  // The first `kept` layers are copied from `from`; the rest are zero. The caller releases `from`.
  [[nodiscard]] std::uint32_t resize_layers(std::uint32_t from, int size, int layers, int kept);

  // Static buffers (terrain, model geometry, layers), written once with their bytes.
  [[nodiscard]] std::uint32_t create_buffer(std::span<const std::byte> bytes);
  // Releases a handle. The WebGPU objects go with their last reference, so frames in flight are safe.
  void release(std::uint32_t handle) noexcept;

  // The objects behind a handle, for the draw path (valid until released).
  [[nodiscard]] WGPUTexture texture(std::uint32_t handle) const { return entry(handle).texture; }
  [[nodiscard]] WGPUTextureView view(std::uint32_t handle) const;
  [[nodiscard]] WGPUBuffer buffer(std::uint32_t handle) const;
  [[nodiscard]] bool linear(std::uint32_t handle) const { return entry(handle).linear; }
  [[nodiscard]] WGPUSampler sampler(bool linear) const { return linear ? linear_ : nearest_; }
  // Called with a handle whenever its view, sampler or lifetime changes (its bind groups go stale).
  void set_release_hook(std::function<void(std::uint32_t)> hook) { release_hook_ = std::move(hook); }

 private:
  enum class Kind : std::uint8_t { none, surface, layers, buffer };
  struct Entry {
    Kind kind = Kind::none;
    WGPUTexture texture = nullptr;
    WGPUTextureView view = nullptr;
    WGPUBuffer buffer = nullptr;
    int width = 0;
    int height = 0;
    int layers = 0;
    bool linear = false;
  };

  std::vector<Entry> entries_;
  std::vector<std::uint32_t> free_;  // indices of released entries
  WGPUSampler nearest_ = nullptr;
  WGPUSampler linear_ = nullptr;
  std::function<void(std::uint32_t)> release_hook_;

  Entry& entry(std::uint32_t handle);
  const Entry& entry(std::uint32_t handle) const;
  std::uint32_t add(Entry entry);
  void retire_entry(Entry& entry) noexcept;
  void notify(std::uint32_t handle) const;
  void create_surface_texture(Entry& entry, int width, int height, int layers, bool array);
};

// The process's resources (the WebGPU backend creates them with its device).
[[nodiscard]] Resources& resources();

// Per-frame data: a bump allocator over chunk buffers that are reused every frame. Each allocation
// is written at once with queue.writeBuffer; within a frame no two allocations overlap, and WebGPU
// orders a frame's writes after the previous frame's submission. Uniform blocks are read through a
// bind group per chunk with a dynamic offset, so every span also names its chunk's group.
class Stream {
 public:
  struct Span {
    WGPUBuffer buffer = nullptr;
    WGPUBindGroup uniform = nullptr;  // the chunk's group (binding 0, the uniform size)
    std::uint64_t offset = 0;
  };

  // uniform_layout: the group layout with one dynamic uniform binding of uniform_size bytes.
  void create(WGPUBindGroupLayout uniform_layout, std::uint64_t uniform_size);
  void destroy() noexcept;
  // Starts a frame: every chunk is free again.
  void reset() noexcept;
  // Copies bytes into the frame at an offset that is a multiple of alignment.
  [[nodiscard]] Span write(const void* data, std::size_t bytes, std::uint64_t alignment);

 private:
  struct Chunk {
    WGPUBuffer buffer = nullptr;
    WGPUBindGroup uniform = nullptr;
    std::uint64_t size = 0;
    std::uint64_t used = 0;
  };

  std::vector<Chunk> chunks_;
  std::size_t current_ = 0;
  WGPUBindGroupLayout layout_ = nullptr;
  std::uint64_t uniform_size_ = 0;
};
}  // namespace realm::gfx::wgpu
