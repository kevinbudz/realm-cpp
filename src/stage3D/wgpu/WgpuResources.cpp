#include "WgpuResources.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace realm::gfx::wgpu {
namespace {
constexpr WGPUTextureFormat color_format = WGPUTextureFormat_RGBA8Unorm;

Resources& owner() {
  static Resources instance;
  return instance;
}

WGPUSampler make_sampler(WGPUDevice device, WGPUFilterMode filter) {
  WGPUSamplerDescriptor descriptor{};
  descriptor.addressModeU = WGPUAddressMode_ClampToEdge;
  descriptor.addressModeV = WGPUAddressMode_ClampToEdge;
  descriptor.addressModeW = WGPUAddressMode_ClampToEdge;
  descriptor.magFilter = filter;
  descriptor.minFilter = filter;
  descriptor.mipmapFilter = WGPUMipmapFilterMode_Nearest;
  descriptor.lodMinClamp = 0.0F;
  descriptor.lodMaxClamp = 32.0F;
  descriptor.maxAnisotropy = 1;
  return wgpuDeviceCreateSampler(device, &descriptor);
}

// Full-size bytes of a copy that WebGPU accepts: a multiple of four.
std::vector<std::byte> padded(std::span<const std::byte> bytes) {
  std::vector<std::byte> copy(bytes.begin(), bytes.end());
  copy.resize((copy.size() + 3) & ~std::size_t{3}, std::byte{0});
  return copy;
}
}  // namespace

Resources& resources() {
  return owner();
}

void Resources::create() {
  auto* device = context().device();
  nearest_ = make_sampler(device, WGPUFilterMode_Nearest);
  linear_ = make_sampler(device, WGPUFilterMode_Linear);
}

void Resources::destroy() noexcept {
  for (std::uint32_t handle = 1; handle <= entries_.size(); ++handle) {
    retire_entry(entries_[handle - 1]);
  }
  entries_.clear();
  free_.clear();
  if (nearest_) {
    wgpuSamplerRelease(nearest_);
    nearest_ = nullptr;
  }
  if (linear_) {
    wgpuSamplerRelease(linear_);
    linear_ = nullptr;
  }
}

Resources::Entry& Resources::entry(std::uint32_t handle) {
  if (handle == 0 || handle > entries_.size() || entries_[handle - 1].kind == Kind::none) {
    throw std::logic_error("Invalid WebGPU resource handle");
  }
  return entries_[handle - 1];
}

const Resources::Entry& Resources::entry(std::uint32_t handle) const {
  if (handle == 0 || handle > entries_.size() || entries_[handle - 1].kind == Kind::none) {
    throw std::logic_error("Invalid WebGPU resource handle");
  }
  return entries_[handle - 1];
}

std::uint32_t Resources::add(Entry added) {
  if (!free_.empty()) {
    const auto index = free_.back();
    free_.pop_back();
    entries_[index] = added;
    return index + 1;
  }
  entries_.push_back(added);
  return static_cast<std::uint32_t>(entries_.size());
}

void Resources::retire_entry(Entry& entry) noexcept {
  if (entry.view) {
    wgpuTextureViewRelease(entry.view);
  }
  if (entry.texture) {
    wgpuTextureRelease(entry.texture);
  }
  if (entry.buffer) {
    wgpuBufferRelease(entry.buffer);
  }
  entry = Entry{};
}

void Resources::notify(std::uint32_t handle) const {
  if (release_hook_) {
    release_hook_(handle);
  }
}

void Resources::create_surface_texture(Entry& target, int width, int height, int layers, bool array) {
  WGPUTextureDescriptor descriptor{};
  descriptor.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst |
                     WGPUTextureUsage_CopySrc;
  descriptor.dimension = WGPUTextureDimension_2D;
  descriptor.size = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
                     static_cast<std::uint32_t>(layers)};
  descriptor.format = color_format;
  descriptor.mipLevelCount = 1;
  descriptor.sampleCount = 1;
  target.texture = wgpuDeviceCreateTexture(context().device(), &descriptor);
  if (!target.texture) {
    throw std::runtime_error("WebGPU texture creation failed");
  }
  WGPUTextureViewDescriptor view{};
  view.format = color_format;
  view.dimension = array ? WGPUTextureViewDimension_2DArray : WGPUTextureViewDimension_2D;
  view.mipLevelCount = 1;
  view.arrayLayerCount = static_cast<std::uint32_t>(layers);
  view.aspect = WGPUTextureAspect_All;
  target.view = wgpuTextureCreateView(target.texture, &view);
  target.width = width;
  target.height = height;
  target.layers = layers;
}

std::uint32_t Resources::create_surface(bool linear) {
  Entry created;
  created.kind = Kind::surface;
  created.linear = linear;
  return add(created);
}

void Resources::allocate_surface(std::uint32_t texture, int width, int height) {
  auto& target = entry(texture);
  if (target.kind != Kind::surface) {
    throw std::logic_error("WebGPU handle is not a surface");
  }
  // The old texture goes with its last reference; the groups that name it are dropped first.
  notify(texture);
  const bool linear = target.linear;
  retire_entry(target);
  target.kind = Kind::surface;
  target.linear = linear;
  create_surface_texture(target, width, height, 1, false);
  notify(texture);
}

void Resources::upload_surface(std::uint32_t texture, int x, int y, int width, int height,
                               const std::byte* rgba) {
  const auto& target = entry(texture);
  if (!target.texture) {
    throw std::logic_error("WebGPU surface upload before allocation");
  }
  WGPUTexelCopyTextureInfo destination{};
  destination.texture = target.texture;
  destination.origin = {static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), 0};
  destination.aspect = WGPUTextureAspect_All;
  WGPUTexelCopyBufferLayout layout{};
  layout.bytesPerRow = static_cast<std::uint32_t>(width) * 4;
  layout.rowsPerImage = static_cast<std::uint32_t>(height);
  const WGPUExtent3D size{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
  wgpuQueueWriteTexture(context().queue(), &destination, rgba,
                        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, &layout, &size);
}

void Resources::set_surface_filter(std::uint32_t texture, bool linear) {
  entry(texture).linear = linear;
  notify(texture);
}

std::uint32_t Resources::create_layers(int size, int layers) {
  Entry created;
  created.kind = Kind::layers;
  created.linear = false;
  create_surface_texture(created, size, size, layers, true);
  created.kind = Kind::layers;
  return add(created);
}

void Resources::upload_layer(std::uint32_t texture, int layer, int x, int y, int width, int height,
                             const std::byte* rgba) {
  const auto& target = entry(texture);
  WGPUTexelCopyTextureInfo destination{};
  destination.texture = target.texture;
  destination.origin = {static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                        static_cast<std::uint32_t>(layer)};
  destination.aspect = WGPUTextureAspect_All;
  WGPUTexelCopyBufferLayout layout{};
  layout.bytesPerRow = static_cast<std::uint32_t>(width) * 4;
  layout.rowsPerImage = static_cast<std::uint32_t>(height);
  const WGPUExtent3D size{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
  wgpuQueueWriteTexture(context().queue(), &destination, rgba,
                        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, &layout, &size);
}

std::uint32_t Resources::resize_layers(std::uint32_t from, int size, int layers, int kept) {
  const auto& source = entry(from);
  Entry created;
  created.kind = Kind::layers;
  create_surface_texture(created, size, size, layers, true);
  if (kept > 0) {
    // A submission of its own: a copy recorded into the frame would run after this frame's
    // uploads of the kept layers and overwrite them.
    auto* device = context().device();
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
    const WGPUExtent3D extent{static_cast<std::uint32_t>(size), static_cast<std::uint32_t>(size), 1};
    for (int layer = 0; layer < kept && layer < source.layers; ++layer) {
      WGPUTexelCopyTextureInfo from_info{};
      from_info.texture = source.texture;
      from_info.origin = {0, 0, static_cast<std::uint32_t>(layer)};
      from_info.aspect = WGPUTextureAspect_All;
      WGPUTexelCopyTextureInfo to_info{};
      to_info.texture = created.texture;
      to_info.origin = {0, 0, static_cast<std::uint32_t>(layer)};
      to_info.aspect = WGPUTextureAspect_All;
      wgpuCommandEncoderCopyTextureToTexture(encoder, &from_info, &to_info, &extent);
    }
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuQueueSubmit(context().queue(), 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
  }
  created.kind = Kind::layers;
  return add(created);
}

std::uint32_t Resources::create_buffer(std::span<const std::byte> bytes) {
  Entry created;
  created.kind = Kind::buffer;
  const auto copy = padded(bytes);
  WGPUBufferDescriptor descriptor{};
  descriptor.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
  descriptor.size = std::max<std::uint64_t>(copy.size(), 4);
  created.buffer = wgpuDeviceCreateBuffer(context().device(), &descriptor);
  if (!created.buffer) {
    throw std::runtime_error("WebGPU buffer creation failed");
  }
  if (!copy.empty()) {
    wgpuQueueWriteBuffer(context().queue(), created.buffer, 0, copy.data(), copy.size());
  }
  return add(created);
}

void Resources::release(std::uint32_t handle) noexcept {
  if (handle == 0 || handle > entries_.size() || entries_[handle - 1].kind == Kind::none) {
    return;
  }
  notify(handle);
  retire_entry(entries_[handle - 1]);
  free_.push_back(handle - 1);
}

WGPUTextureView Resources::view(std::uint32_t handle) const {
  return entry(handle).view;
}

WGPUBuffer Resources::buffer(std::uint32_t handle) const {
  return entry(handle).buffer;
}

void Stream::create(WGPUBindGroupLayout uniform_layout, std::uint64_t uniform_size) {
  layout_ = uniform_layout;
  uniform_size_ = uniform_size;
}

void Stream::destroy() noexcept {
  for (auto& chunk : chunks_) {
    if (chunk.uniform) {
      wgpuBindGroupRelease(chunk.uniform);
    }
    if (chunk.buffer) {
      wgpuBufferRelease(chunk.buffer);
    }
  }
  chunks_.clear();
  current_ = 0;
}

void Stream::reset() noexcept {
  for (auto& chunk : chunks_) {
    chunk.used = 0;
  }
  current_ = 0;
}

Stream::Span Stream::write(const void* data, std::size_t bytes, std::uint64_t alignment) {
  constexpr std::uint64_t chunk_bytes = std::uint64_t{4} << 20;
  const auto need = static_cast<std::uint64_t>(bytes);
  // The first chunk (from the current one on) with room after alignment; a new one otherwise.
  std::size_t index = current_;
  for (; index < chunks_.size(); ++index) {
    const auto start = (chunks_[index].used + alignment - 1) / alignment * alignment;
    if (start + need <= chunks_[index].size) {
      break;
    }
  }
  if (index == chunks_.size()) {
    Chunk chunk;
    chunk.size = std::max(chunk_bytes, (need + alignment) * 2);
    chunk.size = (chunk.size + 255) & ~std::uint64_t{255};
    WGPUBufferDescriptor descriptor{};
    descriptor.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_Index | WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    descriptor.size = chunk.size;
    chunk.buffer = wgpuDeviceCreateBuffer(context().device(), &descriptor);
    if (!chunk.buffer) {
      throw std::runtime_error("WebGPU stream buffer creation failed");
    }
    WGPUBindGroupEntry binding{};
    binding.binding = 0;
    binding.buffer = chunk.buffer;
    binding.offset = 0;
    binding.size = uniform_size_;
    WGPUBindGroupDescriptor group{};
    group.layout = layout_;
    group.entryCount = 1;
    group.entries = &binding;
    chunk.uniform = wgpuDeviceCreateBindGroup(context().device(), &group);
    chunks_.push_back(chunk);
  }
  current_ = index;
  auto& chunk = chunks_[index];
  const auto offset = (chunk.used + alignment - 1) / alignment * alignment;
  chunk.used = offset + need;
  wgpuQueueWriteBuffer(context().queue(), chunk.buffer, offset, data, bytes);
  return Span{chunk.buffer, chunk.uniform, offset};
}
}  // namespace realm::gfx::wgpu
