#pragma once
// Vulkan 1.1 core for the Vulkan backend, loaded at run time through SDL: no link-time dependency on
// libvulkan. Internal to realm_render; nothing outside src/stage3D/vk includes this header.
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES 1
#endif
#include <vulkan/vulkan.h>
#include "../GraphicsBackend.hpp"
#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace realm::gfx::vk {
// Every entry point the backend calls. Instance- and device-level names are both listed; the device
// loader replaces an entry with its device-level pointer when the driver offers one.
// Entry points by level: global (no instance), instance (instance or physical device) and
// device (loaded through vkGetDeviceProcAddr, which only accepts device-level names).
// global
#define REALM_VK_GLOBAL_FUNCTIONS(X) \
  X(vkCreateInstance) X(vkEnumerateInstanceVersion) \
  X(vkEnumerateInstanceExtensionProperties) X(vkEnumerateInstanceLayerProperties)
// instance
#define REALM_VK_INSTANCE_FUNCTIONS(X) \
  X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties) \
  X(vkGetPhysicalDeviceMemoryProperties) X(vkGetPhysicalDeviceQueueFamilyProperties) \
  X(vkGetPhysicalDeviceFormatProperties) X(vkEnumerateDeviceExtensionProperties) \
  X(vkCreateDevice) X(vkGetDeviceProcAddr) X(vkDestroySurfaceKHR) \
  X(vkGetPhysicalDeviceSurfaceSupportKHR) X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
  X(vkGetPhysicalDeviceSurfaceFormatsKHR) X(vkGetPhysicalDeviceSurfacePresentModesKHR) \
  X(vkCreateDebugUtilsMessengerEXT) X(vkDestroyDebugUtilsMessengerEXT)
// device
#define REALM_VK_DEVICE_FUNCTIONS(X) \
  X(vkDestroyDevice) X(vkGetDeviceQueue) X(vkDeviceWaitIdle) X(vkQueueSubmit) \
  X(vkQueueWaitIdle) X(vkQueuePresentKHR) X(vkCreateSwapchainKHR) \
  X(vkDestroySwapchainKHR) X(vkGetSwapchainImagesKHR) X(vkAcquireNextImageKHR) \
  X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkResetCommandPool) \
  X(vkAllocateCommandBuffers) X(vkFreeCommandBuffers) X(vkBeginCommandBuffer) \
  X(vkEndCommandBuffer) X(vkResetCommandBuffer) X(vkCmdPipelineBarrier) \
  X(vkCmdBeginRenderPass) X(vkCmdEndRenderPass) X(vkCmdBindPipeline) \
  X(vkCmdBindDescriptorSets) X(vkCmdBindVertexBuffers) X(vkCmdDraw) X(vkCmdSetViewport) \
  X(vkCmdSetScissor) X(vkCmdPushConstants) X(vkCmdCopyBufferToImage) \
  X(vkCmdCopyImageToBuffer) X(vkCmdCopyBuffer) X(vkCmdCopyImage) X(vkCmdClearColorImage) X(vkCmdResetQueryPool) \
  X(vkCmdWriteTimestamp) X(vkCreateFence) X(vkDestroyFence) X(vkWaitForFences) \
  X(vkResetFences) X(vkGetFenceStatus) X(vkCreateSemaphore) X(vkDestroySemaphore) \
  X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements) \
  X(vkBindImageMemory) X(vkCreateImageView) X(vkDestroyImageView) X(vkCreateBuffer) \
  X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) X(vkBindBufferMemory) \
  X(vkAllocateMemory) X(vkFreeMemory) X(vkMapMemory) X(vkUnmapMemory) \
  X(vkFlushMappedMemoryRanges) X(vkCreateSampler) X(vkDestroySampler) \
  X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) \
  X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets) \
  X(vkFreeDescriptorSets) X(vkUpdateDescriptorSets) X(vkCreatePipelineLayout) \
  X(vkDestroyPipelineLayout) X(vkCreateShaderModule) X(vkDestroyShaderModule) \
  X(vkCreateRenderPass) X(vkDestroyRenderPass) X(vkCreateFramebuffer) \
  X(vkDestroyFramebuffer) X(vkCreateGraphicsPipelines) X(vkDestroyPipeline) \
  X(vkCreatePipelineCache) X(vkDestroyPipelineCache) X(vkCreateQueryPool) \
  X(vkDestroyQueryPool) X(vkGetQueryPoolResults)
#define REALM_VK_FUNCTIONS(X) REALM_VK_GLOBAL_FUNCTIONS(X) REALM_VK_INSTANCE_FUNCTIONS(X) REALM_VK_DEVICE_FUNCTIONS(X)

struct Dispatch {
#define REALM_VK_MEMBER(name) PFN_##name name = nullptr;
  REALM_VK_FUNCTIONS(REALM_VK_MEMBER)
#undef REALM_VK_MEMBER

  void load_global(PFN_vkGetInstanceProcAddr get_instance);
  void load_instance(PFN_vkGetInstanceProcAddr get_instance, VkInstance instance);
  void load_device(VkDevice device);
};

// A Vulkan call failed: the reason and the VkResult, as the backend's error text.
inline void check(VkResult result, const char* what) {
  if (result != VK_SUCCESS) {
    throw std::runtime_error(std::string(what) + " failed (VkResult " +
                             std::to_string(static_cast<int>(result)) + ")");
  }
}

// The process's Vulkan instance, device and the validation tally. One instance exists while the
// Vulkan backend is initialised; the backend owns it.
class Context {
 public:
  Context() = default;
  ~Context();
  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;

  // Creates the instance (with validation when REALM_VK_VALIDATION=1), the surface for window and a
  // device that meets the baseline. Throws with the reason; nothing is left half-created then.
  void initialise(const WindowSurface& window);
  [[nodiscard]] bool ready() const { return device != VK_NULL_HANDLE; }
  // Destroys the device, surface and instance; idempotent.
  void shutdown() noexcept { destroy(); }

  // The memory type with every required property bit, among the types allowed by type_bits.
  [[nodiscard]] std::uint32_t memory_type(std::uint32_t type_bits,
                                          VkMemoryPropertyFlags required) const;
  // The same, preferring the optional properties, and falling back to required alone.
  [[nodiscard]] std::uint32_t memory_type(std::uint32_t type_bits, VkMemoryPropertyFlags required,
                                          VkMemoryPropertyFlags preferred) const;
  [[nodiscard]] GraphicsInfo info() const;

  Dispatch fn;
  VkInstance instance = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkPhysicalDevice physical = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  std::uint32_t family = 0;
  VkPhysicalDeviceProperties properties{};
  VkPhysicalDeviceMemoryProperties memory{};
  VkPhysicalDeviceLimits limits{};
  // Timestamps: bits of a valid timestamp (0 when the queue has none) and nanoseconds per tick.
  std::uint32_t timestamp_bits = 0;
  float timestamp_period_ns = 0;
  bool validation = false;
  // Errors and warnings the validation layers reported (REALM_VK_VALIDATION=1 only).
  std::atomic<std::uint64_t> validation_errors{};
  std::atomic<std::uint64_t> validation_warnings{};

 private:
  void destroy() noexcept;
  void create_instance(const WindowSurface& window);
  void select_device();
};

// A buffer with its memory. Host buffers are persistently mapped and coherent (CPU writes need no
// flush before a submission).
struct Buffer {
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkDeviceSize size = 0;
  std::byte* mapped = nullptr;  // host buffers only
};

Buffer create_host_buffer(VkDeviceSize size, VkBufferUsageFlags usage);
Buffer create_device_buffer(VkDeviceSize size, VkBufferUsageFlags usage);
// Host-visible memory for the per-frame ring; prefers memory that is device-local too (see
// VkCommon.cpp).
Buffer create_ring_buffer(VkDeviceSize size, VkBufferUsageFlags usage);
void destroy_buffer(Buffer& buffer) noexcept;

// The process's context; the Vulkan backend initialises it.
[[nodiscard]] Context& context();
}  // namespace realm::gfx::vk
