#include "VkFrame.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace realm::gfx::vk {
namespace {
constexpr VkDeviceSize ring_chunk_bytes = 8u << 20;
// Steady state keeps at most this much per slot; chunks idle for kRingIdleFrames frames go.
constexpr VkDeviceSize kRingBudgetBytes = 16u << 20;
constexpr std::uint32_t kRingIdleFrames = 3;

VkDeviceSize align_up(VkDeviceSize value, VkDeviceSize alignment) {
  return alignment ? (value + alignment - 1) / alignment * alignment : value;
}

template <typename T>
T make(VkStructureType type) {
  T value{};
  value.sType = type;
  return value;
}

// The present mode for a swap interval: vsync on is FIFO; off prefers MAILBOX, then IMMEDIATE.
// REALM_VK_PRESENT=immediate|mailbox|fifo overrides both (testing); an unsupported request is FIFO.
VkPresentModeKHR choose_mode(const std::vector<VkPresentModeKHR>& supported, int interval) {
  const auto has = [&](VkPresentModeKHR mode) {
    return std::find(supported.begin(), supported.end(), mode) != supported.end();
  };
  if (const char* request = std::getenv("REALM_VK_PRESENT"); request && *request) {
    const std::string_view name = request;
    VkPresentModeKHR wanted = VK_PRESENT_MODE_FIFO_KHR;
    if (name == "immediate") {
      wanted = VK_PRESENT_MODE_IMMEDIATE_KHR;
    } else if (name == "mailbox") {
      wanted = VK_PRESENT_MODE_MAILBOX_KHR;
    } else if (name != "fifo") {
      throw std::runtime_error("REALM_VK_PRESENT takes immediate, mailbox or fifo");
    }
    return has(wanted) ? wanted : VK_PRESENT_MODE_FIFO_KHR;
  }
  if (interval != 0) {
    return VK_PRESENT_MODE_FIFO_KHR;
  }
  if (has(VK_PRESENT_MODE_MAILBOX_KHR)) {
    return VK_PRESENT_MODE_MAILBOX_KHR;
  }
  if (has(VK_PRESENT_MODE_IMMEDIATE_KHR)) {
    return VK_PRESENT_MODE_IMMEDIATE_KHR;
  }
  return VK_PRESENT_MODE_FIFO_KHR;
}

// B8G8R8A8 or R8G8B8A8 UNORM with the sRGB-nonlinear colour space (no sRGB conversion: the GL
// backbuffer is plain RGBA8 too). Returns the first match in preference order.
VkFormat choose_format(const std::vector<VkSurfaceFormatKHR>& formats) {
  for (const VkFormat wanted : {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM}) {
    for (const auto& format : formats) {
      if (format.format == wanted && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        return wanted;
      }
    }
  }
  throw std::runtime_error("The surface offers no UNORM RGBA8 or BGRA8 format");
}
}  // namespace

Ring::Span Ring::allocate(VkDeviceSize bytes, VkDeviceSize alignment) {
  for (;;) {
    if (current_ < chunks_.size()) {
      auto& chunk = chunks_[current_];
      const auto offset = align_up(used_, alignment);
      if (offset + bytes <= chunk.buffer.size) {
        used_ = offset + bytes;
        chunk.used = true;
        return {chunk.buffer.buffer, offset, chunk.buffer.mapped + offset};
      }
      ++current_;
      used_ = 0;
      continue;
    }
    Chunk chunk;
    chunk.buffer =
        create_ring_buffer(std::max(bytes, ring_chunk_bytes),
                           VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                               VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    chunk.used = true;
    chunks_.push_back(chunk);
  }
}

void Ring::reset() noexcept {
  // Runs after the slot's frame finished. A chunk that went unused for kRingIdleFrames is released,
  // and so is an oversized one-off after a single idle frame; idle chunks also go while the ring
  // holds more than kRingBudgetBytes. Chunks in use stay, so steady frames keep their memory.
  for (auto& chunk : chunks_) {
    chunk.idle = chunk.used ? 0 : chunk.idle + 1;
    chunk.used = false;
  }
  for (auto it = chunks_.begin(); it != chunks_.end();) {
    const bool oversized = it->buffer.size > ring_chunk_bytes;
    if (it->idle >= kRingIdleFrames || (oversized && it->idle >= 1)) {
      destroy_buffer(it->buffer);
      it = chunks_.erase(it);
    } else {
      ++it;
    }
  }
  while (retained() > kRingBudgetBytes) {
    auto victim = chunks_.end();
    for (auto it = chunks_.begin(); it != chunks_.end(); ++it) {
      if (it->idle >= 1 && (victim == chunks_.end() || it->buffer.size > victim->buffer.size)) {
        victim = it;
      }
    }
    if (victim == chunks_.end()) {
      break;
    }
    destroy_buffer(victim->buffer);
    chunks_.erase(victim);
  }
  current_ = 0;
  used_ = 0;
}

VkDeviceSize Ring::retained() const {
  VkDeviceSize total = 0;
  for (const auto& chunk : chunks_) {
    total += chunk.buffer.size;
  }
  return total;
}

void Ring::destroy() noexcept {
  for (auto& chunk : chunks_) {
    destroy_buffer(chunk.buffer);
  }
  chunks_.clear();
  reset();
}

void Frames::create() {
  auto& device = context();
  std::uint32_t count = 0;
  device.fn.vkGetPhysicalDeviceSurfaceFormatsKHR(device.physical, device.surface, &count, nullptr);
  std::vector<VkSurfaceFormatKHR> formats(count);
  device.fn.vkGetPhysicalDeviceSurfaceFormatsKHR(device.physical, device.surface, &count,
                                                 formats.data());
  format_ = choose_format(formats);
  slots_.resize(frames_in_flight);
  for (auto& slot : slots_) {
    auto pool_info = make<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = device.family;
    check(device.fn.vkCreateCommandPool(device.device, &pool_info, nullptr, &slot.pool),
          "vkCreateCommandPool");
    auto allocate = make<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
    allocate.commandPool = slot.pool;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    check(device.fn.vkAllocateCommandBuffers(device.device, &allocate, &slot.command),
          "vkAllocateCommandBuffers");
    const auto fence_info = make<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
    check(device.fn.vkCreateFence(device.device, &fence_info, nullptr, &slot.fence), "vkCreateFence");
    const auto semaphore_info =
        make<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
    check(device.fn.vkCreateSemaphore(device.device, &semaphore_info, nullptr, &slot.acquire),
          "vkCreateSemaphore");
  }
  current_ = 0;
  has_last_ = false;
  state_ = State::idle;
}

void Frames::destroy() noexcept {
  if (slots_.empty() || !context().ready()) {
    slots_.clear();
    return;
  }
  auto& device = context();
  // Frames in flight finish, then the presentation engine lets go of the swapchain images.
  wait_in_flight();
  device.fn.vkQueueWaitIdle(device.queue);
  for (auto& slot : slots_) {
    for (auto& destroy_fn : slot.retired) {
      destroy_fn();
    }
    slot.retired.clear();
  }
  for (auto view : views_) {
    device.fn.vkDestroyImageView(device.device, view, nullptr);
  }
  for (auto semaphore : render_done_) {
    device.fn.vkDestroySemaphore(device.device, semaphore, nullptr);
  }
  if (swapchain_ != VK_NULL_HANDLE) {
    device.fn.vkDestroySwapchainKHR(device.device, swapchain_, nullptr);
  }
  for (auto& slot : slots_) {
    slot.ring.destroy();
    device.fn.vkDestroyFence(device.device, slot.fence, nullptr);
    device.fn.vkDestroySemaphore(device.device, slot.acquire, nullptr);
    device.fn.vkDestroyCommandPool(device.device, slot.pool, nullptr);
  }
  slots_.clear();
  views_.clear();
  images_.clear();
  render_done_.clear();
  swapchain_ = VK_NULL_HANDLE;
  acquired_ = false;
  presentable_ = false;
  state_ = State::idle;
  dirty_ = true;
}

void Frames::wait_in_flight() {
  auto& device = context();
  for (auto& slot : slots_) {
    if (slot.in_flight) {
      check(device.fn.vkWaitForFences(device.device, 1, &slot.fence, VK_TRUE,
                                      std::numeric_limits<std::uint64_t>::max()),
            "vkWaitForFences");
      slot.in_flight = false;
    }
  }
}

void Frames::open() {
  if (state_ == State::submitted) {
    throw std::logic_error("Vulkan frame already submitted: finish it before recording more");
  }
  if (state_ != State::idle) {
    return;
  }
  auto& device = context();
  auto& slot = slots_[current_];
  if (slot.in_flight) {
    check(device.fn.vkWaitForFences(device.device, 1, &slot.fence, VK_TRUE,
                                    std::numeric_limits<std::uint64_t>::max()),
          "vkWaitForFences");
    slot.in_flight = false;
  }
  // The slot's previous frame has finished: release what it retired and reuse its storage.
  for (auto& destroy_fn : slot.retired) {
    destroy_fn();
  }
  slot.retired.clear();
  slot.ring.reset();
  check(device.fn.vkResetFences(device.device, 1, &slot.fence), "vkResetFences");
  check(device.fn.vkResetCommandPool(device.device, slot.pool, 0), "vkResetCommandPool");
  auto begin = make<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  check(device.fn.vkBeginCommandBuffer(slot.command, &begin), "vkBeginCommandBuffer");
  state_ = State::recording;
  acquired_ = false;
  presentable_ = false;
  written_ = false;
}

VkCommandBuffer Frames::record() {
  open();
  return slots_[current_].command;
}

Ring& Frames::ring() {
  open();
  return slots_[current_].ring;
}

void Frames::rebuild(std::uint32_t width, std::uint32_t height) {
  auto& device = context();
  wait_in_flight();
  if (swapchain_ != VK_NULL_HANDLE) {
    device.fn.vkQueueWaitIdle(device.queue);
  }

  VkSurfaceCapabilitiesKHR caps{};
  check(device.fn.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device.physical, device.surface, &caps),
        "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
  std::uint32_t count = 0;
  device.fn.vkGetPhysicalDeviceSurfacePresentModesKHR(device.physical, device.surface, &count,
                                                      nullptr);
  std::vector<VkPresentModeKHR> modes(count);
  device.fn.vkGetPhysicalDeviceSurfacePresentModesKHR(device.physical, device.surface, &count,
                                                      modes.data());
  modes_ = modes;
  mode_ = choose_mode(modes_, swap_interval_);

  VkExtent2D extent = caps.currentExtent;
  if (extent.width == std::numeric_limits<std::uint32_t>::max()) {
    extent.width = std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width);
    extent.height = std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height);
  }
  if (extent.width != width || extent.height != height) {
    throw std::runtime_error("The swapchain cannot match the drawable size (" +
                             std::to_string(width) + "x" + std::to_string(height) + ")");
  }
  if ((caps.supportedUsageFlags & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) !=
      (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) {
    throw std::runtime_error("The surface lacks colour-attachment or transfer-source usage");
  }
  std::uint32_t image_count = caps.minImageCount + 1;
  if (caps.maxImageCount > 0) {
    image_count = std::min(image_count, caps.maxImageCount);
  }
  VkCompositeAlphaFlagBitsKHR composite = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  if (!(caps.supportedCompositeAlpha & composite)) {
    composite = static_cast<VkCompositeAlphaFlagBitsKHR>(
        caps.supportedCompositeAlpha & -caps.supportedCompositeAlpha);
  }

  auto info = make<VkSwapchainCreateInfoKHR>(VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
  info.surface = device.surface;
  info.minImageCount = image_count;
  info.imageFormat = format_;
  info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  info.imageExtent = extent;
  info.imageArrayLayers = 1;
  info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  info.preTransform = caps.currentTransform;
  info.compositeAlpha = composite;
  info.presentMode = mode_;
  info.clipped = VK_TRUE;
  info.oldSwapchain = swapchain_;
  VkSwapchainKHR created = VK_NULL_HANDLE;
  check(device.fn.vkCreateSwapchainKHR(device.device, &info, nullptr, &created),
        "vkCreateSwapchainKHR");

  for (auto view : views_) {
    device.fn.vkDestroyImageView(device.device, view, nullptr);
  }
  for (auto semaphore : render_done_) {
    device.fn.vkDestroySemaphore(device.device, semaphore, nullptr);
  }
  if (swapchain_ != VK_NULL_HANDLE) {
    device.fn.vkDestroySwapchainKHR(device.device, swapchain_, nullptr);
  }
  swapchain_ = created;
  extent_ = extent;

  count = 0;
  device.fn.vkGetSwapchainImagesKHR(device.device, swapchain_, &count, nullptr);
  images_.assign(count, VK_NULL_HANDLE);
  check(device.fn.vkGetSwapchainImagesKHR(device.device, swapchain_, &count, images_.data()),
        "vkGetSwapchainImagesKHR");
  views_.clear();
  render_done_.clear();
  for (const auto image : images_) {
    auto view_info = make<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    view_info.image = image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format_;
    view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageView view = VK_NULL_HANDLE;
    check(device.fn.vkCreateImageView(device.device, &view_info, nullptr, &view), "vkCreateImageView");
    views_.push_back(view);
    const auto semaphore_info = make<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
    VkSemaphore semaphore = VK_NULL_HANDLE;
    check(device.fn.vkCreateSemaphore(device.device, &semaphore_info, nullptr, &semaphore),
          "vkCreateSemaphore");
    render_done_.push_back(semaphore);
  }
  generation_++;
  dirty_ = false;
}

const Target& Frames::target(std::uint32_t width, std::uint32_t height) {
  if (state_ == State::submitted && acquired_) {
    return target_;  // a capture of the submitted frame reads its image
  }
  open();
  if (!acquired_) {
    const auto wanted = choose_mode(modes_, swap_interval_);
    if (dirty_ || swapchain_ == VK_NULL_HANDLE || extent_.width != width ||
        extent_.height != height || wanted != mode_) {
      rebuild(width, height);
    }
    auto& device = context();
    auto& slot = slots_[current_];
    std::uint32_t index = 0;
    for (int attempt = 0;; ++attempt) {
      const auto result = device.fn.vkAcquireNextImageKHR(
          device.device, swapchain_, std::numeric_limits<std::uint64_t>::max(), slot.acquire,
          VK_NULL_HANDLE, &index);
      if (result == VK_ERROR_OUT_OF_DATE_KHR && attempt < 2) {
        rebuild(width, height);
        continue;
      }
      if (result == VK_SUBOPTIMAL_KHR) {
        dirty_ = true;  // the image is usable; rebuild before the next frame
        break;
      }
      check(result, "vkAcquireNextImageKHR");
      break;
    }
    target_ = {images_[index], views_[index], extent_, index};
    acquired_ = true;
  }
  return target_;
}

void Frames::mark_written() {
  written_ = true;
}

void Frames::set_swap_interval(int interval) {
  swap_interval_ = interval;
}

void Frames::retire(std::function<void()> destroy_fn) {
  if (slots_.empty()) {
    destroy_fn();  // the frames are gone (shutdown): the device is idle, nothing can use the object
  } else if (state_ != State::idle) {
    slots_[current_].retired.push_back(std::move(destroy_fn));
  } else if (has_last_) {
    slots_[last_].retired.push_back(std::move(destroy_fn));
  } else {
    destroy_fn();
  }
}

void Frames::submit(bool wait) {
  auto& device = context();
  auto& slot = slots_[current_];
  if (before_submit_) {
    before_submit_(slot.command);
  }
  if (acquired_ && !presentable_) {
    // Leave the image ready for presentation: the last pass wrote it, or nothing did.
    auto barrier = make<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
    barrier.srcAccessMask = written_ ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT : 0;
    barrier.dstAccessMask = 0;
    barrier.oldLayout = written_ ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = target_.image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    device.fn.vkCmdPipelineBarrier(slot.command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                   VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr,
                                   1, &barrier);
    presentable_ = true;
  }
  check(device.fn.vkEndCommandBuffer(slot.command), "vkEndCommandBuffer");

  auto submit_info = make<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
  const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  if (acquired_) {
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &slot.acquire;
    submit_info.pWaitDstStageMask = &stage;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &render_done_[target_.index];
  }
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &slot.command;
  check(device.fn.vkQueueSubmit(device.queue, 1, &submit_info, slot.fence), "vkQueueSubmit");
  slot.in_flight = true;
  last_ = current_;
  has_last_ = true;
  state_ = State::submitted;
  if (wait) {
    check(device.fn.vkWaitForFences(device.device, 1, &slot.fence, VK_TRUE,
                                    std::numeric_limits<std::uint64_t>::max()),
          "vkWaitForFences");
    slot.in_flight = false;
  }
}

void Frames::present() {
  auto& device = context();
  auto wait = render_done_[target_.index];
  auto index = target_.index;
  auto info = make<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
  info.waitSemaphoreCount = 1;
  info.pWaitSemaphores = &wait;
  info.swapchainCount = 1;
  info.pSwapchains = &swapchain_;
  info.pImageIndices = &index;
  const auto result = device.fn.vkQueuePresentKHR(device.queue, &info);
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    dirty_ = true;
  } else {
    check(result, "vkQueuePresentKHR");
  }
}

void Frames::submit_and_wait() {
  if (state_ == State::recording) {
    submit(true);
  }
}

void Frames::read_target(VkBuffer destination, std::uint32_t width, std::uint32_t height) {
  if (state_ != State::recording || !acquired_) {
    throw std::logic_error("Vulkan capture needs a frame that has not been submitted");
  }
  auto& device = context();
  const auto command = slots_[current_].command;
  const auto subresource = VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  auto to_source = make<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
  to_source.srcAccessMask = written_ ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT : 0;
  to_source.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  to_source.oldLayout = written_ ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
  to_source.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  to_source.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  to_source.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  to_source.image = target_.image;
  to_source.subresourceRange = subresource;
  device.fn.vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                 &to_source);
  VkBufferImageCopy region{};
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageExtent = {width, height, 1};
  device.fn.vkCmdCopyImageToBuffer(command, target_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   destination, 1, &region);
  auto to_present = to_source;
  to_present.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  to_present.dstAccessMask = 0;
  to_present.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  device.fn.vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                 &to_present);
  presentable_ = true;
  submit(true);
}

void Frames::finish() {
  if (state_ != State::idle) {
    ++serial_;
  }
  const bool submitted = state_ == State::submitted || state_ == State::recording;
  if (state_ == State::recording) {
    submit(false);
  }
  if (state_ == State::submitted && acquired_) {
    present();
  }
  if (submitted) {
    current_ = (current_ + 1) % frames_in_flight;
  }
  state_ = State::idle;
  acquired_ = false;
  presentable_ = false;
  written_ = false;
}

VkDeviceSize Frames::ring_retained() const {
  VkDeviceSize total = 0;
  for (const auto& slot : slots_) {
    total += slot.ring.retained();
  }
  return total;
}

VkDeviceSize Frames::ring_retained_max() const {
  VkDeviceSize largest = 0;
  for (const auto& slot : slots_) {
    largest = std::max(largest, slot.ring.retained());
  }
  return largest;
}

Frames& frames() {
  static Frames instance;
  return instance;
}
}  // namespace realm::gfx::vk
