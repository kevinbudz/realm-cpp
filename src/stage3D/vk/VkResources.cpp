#include "VkResources.hpp"
#include "VkFrame.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace realm::gfx::vk {
namespace {
constexpr VkFormat image_format = VK_FORMAT_R8G8B8A8_UNORM;
constexpr VkDeviceSize staging_alignment = 16;  // buffer offsets of buffer-to-image copies

template <typename T>
T make(VkStructureType type) {
  T value{};
  value.sType = type;
  return value;
}

VkImageSubresourceRange color_range(std::uint32_t layers) {
  return {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers};
}

void image_barrier(VkCommandBuffer command, VkImage image, VkImageLayout from, VkImageLayout to,
                   VkPipelineStageFlags source_stage, VkAccessFlags source_access,
                   VkPipelineStageFlags destination_stage, VkAccessFlags destination_access,
                   std::uint32_t layers) {
  auto barrier = make<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
  barrier.srcAccessMask = source_access;
  barrier.dstAccessMask = destination_access;
  barrier.oldLayout = from;
  barrier.newLayout = to;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange = color_range(layers);
  context().fn.vkCmdPipelineBarrier(command, source_stage, destination_stage, 0, 0, nullptr, 0,
                                    nullptr, 1, &barrier);
}
}  // namespace

void Resources::create() {
  auto& device = context();
  auto info = make<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
  info.magFilter = VK_FILTER_NEAREST;
  info.minFilter = VK_FILTER_NEAREST;
  info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  info.maxLod = 0;
  check(device.fn.vkCreateSampler(device.device, &info, nullptr, &nearest_), "vkCreateSampler");
  info.magFilter = VK_FILTER_LINEAR;
  info.minFilter = VK_FILTER_LINEAR;
  check(device.fn.vkCreateSampler(device.device, &info, nullptr, &linear_), "vkCreateSampler");
}

void Resources::destroy() noexcept {
  if (!context().ready()) {
    entries_.clear();
    return;
  }
  auto& device = context();
  // Shutdown only: nothing may still use the samplers or the entries while they are retired.
  device.fn.vkDeviceWaitIdle(device.device);
  for (auto& entry : entries_) {
    if (entry.kind != Kind::none) {
      retire_entry(entry);
    }
  }
  entries_.clear();
  free_.clear();
  pending_images_.clear();
  pending_buffers_.clear();
  device.fn.vkDestroySampler(device.device, nearest_, nullptr);
  device.fn.vkDestroySampler(device.device, linear_, nullptr);
  nearest_ = linear_ = VK_NULL_HANDLE;
}

Resources::Entry& Resources::entry(std::uint32_t handle) {
  if (handle == 0 || handle > entries_.size() || entries_[handle - 1].kind == Kind::none) {
    throw std::logic_error("Vulkan resource handle is not live");
  }
  return entries_[handle - 1];
}

const Resources::Entry& Resources::entry(std::uint32_t handle) const {
  if (handle == 0 || handle > entries_.size() || entries_[handle - 1].kind == Kind::none) {
    throw std::logic_error("Vulkan resource handle is not live");
  }
  return entries_[handle - 1];
}

std::uint32_t Resources::add(Entry value) {
  if (!free_.empty()) {
    const auto index = free_.back();
    free_.pop_back();
    entries_[index] = std::move(value);
    return index + 1;
  }
  entries_.push_back(std::move(value));
  return static_cast<std::uint32_t>(entries_.size());
}

void Resources::retire_entry(Entry& value) noexcept {
  auto& frame = frames();
  if (value.kind == Kind::buffer) {
    const auto allocation = value.alloc;
    value.alloc = Arena::Allocation{};
    frame.retire([allocation] { arena().free(allocation); });
  } else {
    const auto image = value.image;
    const auto view = value.view;
    const auto memory = value.memory;
    frame.retire([image, view, memory] {
      auto& dev = context();
      dev.fn.vkDestroyImageView(dev.device, view, nullptr);
      dev.fn.vkDestroyImage(dev.device, image, nullptr);
      dev.fn.vkFreeMemory(dev.device, memory, nullptr);
    });
  }
  value = Entry{};
}

void Resources::create_image(Entry& image, int width, int height, int layers, bool array) {
  auto& device = context();
  auto info = make<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
  info.imageType = VK_IMAGE_TYPE_2D;
  info.format = image_format;
  info.extent = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
  info.mipLevels = 1;
  info.arrayLayers = static_cast<std::uint32_t>(layers);
  info.samples = VK_SAMPLE_COUNT_1_BIT;
  info.tiling = VK_IMAGE_TILING_OPTIMAL;
  info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
               VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  check(device.fn.vkCreateImage(device.device, &info, nullptr, &image.image), "vkCreateImage");
  VkMemoryRequirements requirements{};
  device.fn.vkGetImageMemoryRequirements(device.device, image.image, &requirements);
  auto allocation = make<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
  allocation.allocationSize = requirements.size;
  allocation.memoryTypeIndex = device.memory_type(requirements.memoryTypeBits,
                                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  check(device.fn.vkAllocateMemory(device.device, &allocation, nullptr, &image.memory),
        "vkAllocateMemory");
  check(device.fn.vkBindImageMemory(device.device, image.image, image.memory, 0), "vkBindImageMemory");
  auto view = make<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
  view.image = image.image;
  view.viewType = array ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
  view.format = image_format;
  view.subresourceRange = color_range(static_cast<std::uint32_t>(layers));
  check(device.fn.vkCreateImageView(device.device, &view, nullptr, &image.view), "vkCreateImageView");
  image.layout = VK_IMAGE_LAYOUT_UNDEFINED;
  image.width = width;
  image.height = height;
  image.layers = layers;
}

void Resources::begin_transfer(std::uint32_t index, VkCommandBuffer command) {
  auto& value = entries_[index];
  if (value.pending) {
    return;
  }
  // The previous frames may still read the image or write it; the barrier orders this copy after them.
  image_barrier(command, value.image, value.layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                static_cast<std::uint32_t>(std::max(value.layers, 1)));
  value.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  value.pending = true;
  pending_images_.push_back(index);
}

void Resources::stage_image(std::uint32_t index, int layer, int x, int y, int width, int height,
                            const std::byte* rgba) {
  const auto bytes = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4;
  auto span = frames().ring().allocate(bytes, staging_alignment);
  std::memcpy(span.data, rgba, bytes);
  const auto command = frames().record();
  begin_transfer(index, command);
  VkBufferImageCopy region{};
  region.bufferOffset = span.offset;
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, static_cast<std::uint32_t>(layer), 1};
  region.imageOffset = {x, y, 0};
  region.imageExtent = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
  context().fn.vkCmdCopyBufferToImage(command, span.buffer, entries_[index].image,
                                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

void Resources::close_transfers(VkCommandBuffer command) {
  if (pending_images_.empty() && pending_buffers_.empty()) {
    return;
  }
  for (const auto index : pending_images_) {
    auto& value = entries_[index];
    if (value.kind == Kind::none) {
      continue;  // released in the same frame; its copies are ordered by the retire list
    }
    image_barrier(command, value.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                  VK_ACCESS_SHADER_READ_BIT, static_cast<std::uint32_t>(std::max(value.layers, 1)));
    value.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    value.pending = false;
  }
  pending_images_.clear();
  if (!pending_buffers_.empty()) {
    std::vector<VkBufferMemoryBarrier> barriers;
    for (const auto index : pending_buffers_) {
      auto& value = entries_[index];
      if (value.kind == Kind::none) {
        continue;
      }
      auto barrier = make<VkBufferMemoryBarrier>(VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER);
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
      barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.buffer = value.alloc.buffer;
      barrier.offset = value.alloc.offset;
      barrier.size = value.alloc.size;
      barriers.push_back(barrier);
      value.pending = false;
    }
    if (!barriers.empty()) {
      context().fn.vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
                                            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                        0, 0, nullptr, static_cast<std::uint32_t>(barriers.size()),
                                        barriers.data(), 0, nullptr);
    }
    pending_buffers_.clear();
  }
}

std::uint32_t Resources::create_surface(bool linear) {
  Entry value;
  value.kind = Kind::surface;
  value.linear = linear;
  return add(std::move(value));
}

void Resources::allocate_surface(std::uint32_t texture, int width, int height) {
  if (width <= 0 || height <= 0) {
    throw std::invalid_argument("Invalid surface size");
  }
  Entry& value = entry(texture);
  std::erase(pending_images_, texture - 1);
  if (value.image != VK_NULL_HANDLE) {
    // Frames in flight may still read the old image: its retire list keeps it until they finish.
    Entry old;
    old.kind = Kind::surface;
    old.image = value.image;
    old.view = value.view;
    old.memory = value.memory;
    retire_entry(old);
  }
  value.image = VK_NULL_HANDLE;
  value.view = VK_NULL_HANDLE;
  value.memory = VK_NULL_HANDLE;
  value.pending = false;
  create_image(value, width, height, 1, false);
}

void Resources::upload_surface(std::uint32_t texture, int x, int y, int width, int height,
                               const std::byte* rgba) {
  const auto& value = entry(texture);
  if (value.image == VK_NULL_HANDLE || x < 0 || y < 0 || width <= 0 || height <= 0 ||
      x + width > value.width || y + height > value.height) {
    throw std::logic_error("Invalid surface upload");
  }
  stage_image(texture - 1, 0, x, y, width, height, rgba);
}

void Resources::set_surface_filter(std::uint32_t texture, bool linear) {
  entry(texture).linear = linear;
}

std::uint32_t Resources::create_layers(int size, int layers) {
  if (size <= 0 || layers <= 0) {
    throw std::invalid_argument("Invalid layer array");
  }
  Entry value;
  value.kind = Kind::layers;
  create_image(value, size, size, layers, true);
  const auto handle = add(std::move(value));
  // Cleared at creation: nothing reads undefined texels.
  const auto command = frames().record();
  begin_transfer(handle - 1, command);
  VkClearColorValue clear{};
  const auto range = color_range(static_cast<std::uint32_t>(layers));
  context().fn.vkCmdClearColorImage(command, entries_[handle - 1].image,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
  return handle;
}

void Resources::upload_layer(std::uint32_t texture, int layer, int x, int y, int width, int height,
                             const std::byte* rgba) {
  const auto& value = entry(texture);
  if (value.kind != Kind::layers || layer < 0 || layer >= value.layers || x < 0 || y < 0 ||
      width <= 0 || height <= 0 || x + width > value.width || y + height > value.height) {
    throw std::logic_error("Invalid layer upload");
  }
  stage_image(texture - 1, layer, x, y, width, height, rgba);
}

std::uint32_t Resources::resize_layers(std::uint32_t from, int size, int layers, int kept) {
  if (size <= 0 || layers <= 0 || kept < 0 || kept > layers) {
    throw std::invalid_argument("Invalid layer resize");
  }
  const auto& old = entry(from);
  if (old.kind != Kind::layers || old.width != size) {
    throw std::logic_error("Layer resize of a different array");
  }
  Entry value;
  value.kind = Kind::layers;
  create_image(value, size, size, layers, true);
  const auto handle = add(std::move(value));
  const auto command = frames().record();
  auto& source = entries_[from - 1];
  // The old array is read by the copy; its layout goes back to readable afterwards.
  if (source.pending) {
    image_barrier(command, source.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_ACCESS_TRANSFER_READ_BIT, static_cast<std::uint32_t>(source.layers));
    source.pending = false;
    std::erase(pending_images_, from - 1);
  } else {
    image_barrier(command, source.image, source.layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                  static_cast<std::uint32_t>(source.layers));
  }
  source.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  begin_transfer(handle - 1, command);
  auto& target = entries_[handle - 1];
  if (kept > 0) {
    VkImageCopy copy{};
    copy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, static_cast<std::uint32_t>(kept)};
    copy.dstSubresource = copy.srcSubresource;
    copy.extent = {static_cast<std::uint32_t>(size), static_cast<std::uint32_t>(size), 1};
    context().fn.vkCmdCopyImage(command, source.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                target.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
  }
  if (kept < layers) {
    VkClearColorValue clear{};
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1,
                                        static_cast<std::uint32_t>(kept),
                                        static_cast<std::uint32_t>(layers - kept)};
    context().fn.vkCmdClearColorImage(command, target.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                      &clear, 1, &range);
  }
  return handle;
}

std::uint32_t Resources::create_buffer(std::span<const std::byte> bytes) {
  const auto size = std::max<VkDeviceSize>(bytes.size(), 16);
  Entry value;
  value.kind = Kind::buffer;
  value.alloc = arena().allocate(size, staging_alignment);
  value.width = static_cast<int>(bytes.size());
  const auto handle = add(std::move(value));
  if (!bytes.empty()) {
    auto span = frames().ring().allocate(bytes.size(), staging_alignment);
    std::memcpy(span.data, bytes.data(), bytes.size());
    const auto command = frames().record();
    VkBufferCopy copy{span.offset, entries_[handle - 1].alloc.offset, bytes.size()};
    context().fn.vkCmdCopyBuffer(command, span.buffer, entries_[handle - 1].alloc.buffer, 1, &copy);
    entries_[handle - 1].pending = true;
    pending_buffers_.push_back(handle - 1);
  }
  return handle;
}

void Resources::release(std::uint32_t handle) noexcept {
  if (handle == 0 || handle > entries_.size() || entries_[handle - 1].kind == Kind::none) {
    return;
  }
  if (release_hook_) {
    release_hook_(handle);
  }
  const auto index = handle - 1;
  std::erase(pending_images_, index);
  std::erase(pending_buffers_, index);
  retire_entry(entries_[index]);
  free_.push_back(index);
}

VkImageView Resources::view(std::uint32_t handle) const {
  return entry(handle).view;
}

VkBuffer Resources::buffer(std::uint32_t handle) const {
  return entry(handle).alloc.buffer;
}

VkDeviceSize Resources::buffer_offset(std::uint32_t handle) const {
  return entry(handle).alloc.offset;
}

void Resources::read_layer(std::uint32_t handle, int layer, int x, int y, int width, int height,
                           std::vector<std::byte>& out) {
  const auto index = handle - 1;
  const auto command = frames().record();
  close_transfers(command);
  auto& value = entries_[index];
  const auto bytes = static_cast<VkDeviceSize>(width) * height * 4;
  auto host = create_host_buffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
  image_barrier(command, value.image, value.layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                static_cast<std::uint32_t>(value.layers));
  VkBufferImageCopy region{};
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, static_cast<std::uint32_t>(layer), 1};
  region.imageOffset = {x, y, 0};
  region.imageExtent = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
  context().fn.vkCmdCopyImageToBuffer(command, value.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                      host.buffer, 1, &region);
  image_barrier(command, value.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT, static_cast<std::uint32_t>(value.layers));
  value.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  try {
    // A readback is its own submission: the frame is submitted, waited for and finished here.
    frames().submit_and_wait();
    out.resize(static_cast<std::size_t>(bytes));
    std::memcpy(out.data(), host.mapped, out.size());
    frames().finish();
  } catch (...) {
    destroy_buffer(host);
    throw;
  }
  destroy_buffer(host);
}

Resources& resources() {
  static Resources instance;
  return instance;
}
}  // namespace realm::gfx::vk
