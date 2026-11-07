#pragma once
// GPU resources behind the handles of GraphicsBackend.hpp: surfaces (2D RGBA8), layer arrays (the
// atlas) and static buffers, plus the per-frame Stream the instance and uniform data are written to.
// A handle is index + 1 into the table; 0 means none. Resource writes go through queue.writeTexture
// and queue.writeBuffer. Per-frame stream writes are staged until frame submission; a layer resize
// copies on a submission of its own.
#include "WgpuContext.hpp"
#include "realm/stage3D/wgpu/StreamStager.hpp"
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
  // Replaces a buffer's bytes from offset 0. The bytes go into the existing buffer when they fit and
  // in_place is set; otherwise a new buffer object takes the handle (grown geometrically when the bytes
  // do not fit), and the old object goes with its last reference, so frames that recorded it are safe.
  void write_buffer(std::uint32_t handle, std::span<const std::byte> bytes, bool in_place);
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
    std::uint64_t capacity = 0;  // buffers: the object's size in bytes
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

// Per-frame data: a CPU bump allocator stages bytes into reusable chunks. flush() queues one upload
// per used GPU chunk before the frame command buffer is submitted. Uniform blocks are read through a
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
  struct Reservation {
    Span span;
    std::byte* data = nullptr;  // valid until the next write() or reserve()
  };
  // Reserves bytes at an offset that is a multiple of alignment and returns where to pack them.
  [[nodiscard]] Reservation reserve(std::size_t bytes, std::uint64_t alignment);
  // Copies bytes into the frame at an offset that is a multiple of alignment.
  [[nodiscard]] Span write(const void* data, std::size_t bytes, std::uint64_t alignment);
  // Queues all bytes written since reset() or the previous flush().
  void flush();

 private:
  struct Chunk {
    WGPUBuffer buffer = nullptr;
    WGPUBindGroup uniform = nullptr;
  };

  // Creates the GPU buffer and group of chunk index if they do not exist yet.
  void ensure_chunk(std::size_t index);

  std::vector<Chunk> chunks_;
  StreamStager stager_;
  WGPUBindGroupLayout layout_ = nullptr;
  std::uint64_t uniform_size_ = 0;
};
}  // namespace realm::gfx::wgpu
