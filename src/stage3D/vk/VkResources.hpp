#pragma once
// GPU resources behind the handles of GraphicsBackend.hpp: surfaces (2D RGBA8), layer arrays (the
// atlas) and static or updated buffers. A handle is index + 1 into the table; 0 means none. Uploads
// are staged in the current frame's ring and copied into the frame's command buffer, outside any
// render pass. Released objects are destroyed once the frames that may use them have finished.
#include "VkArena.hpp"
#include "VkCommon.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace realm::gfx::vk {
class Resources {
 public:
  // Creates the shared samplers. Needs the device; frames are created before resources.
  void create();
  // Releases everything (after the frames have finished). Idempotent.
  void destroy() noexcept;

  // Surfaces: RGBA8 2D images, clamp to edge, nearest unless linear.
  [[nodiscard]] std::uint32_t create_surface(bool linear);
  void allocate_surface(std::uint32_t texture, int width, int height);
  void upload_surface(std::uint32_t texture, int x, int y, int width, int height, const std::byte* rgba);
  void set_surface_filter(std::uint32_t texture, bool linear);

  // Layer arrays (atlas): size x size RGBA8 layers, nearest, clamp to edge, cleared at creation.
  [[nodiscard]] std::uint32_t create_layers(int size, int layers);
  void upload_layer(std::uint32_t texture, int layer, int x, int y, int width, int height,
                    const std::byte* rgba);
  // The first `kept` layers are copied from `from`; the rest are cleared. The caller releases `from`.
  [[nodiscard]] std::uint32_t resize_layers(std::uint32_t from, int size, int layers, int kept);

  // Buffers: device-local, with the bytes staged and copied into the frame.
  [[nodiscard]] std::uint32_t create_buffer(std::span<const std::byte> bytes);
  void release(std::uint32_t handle) noexcept;

  // Records the barriers that make this frame's transfers visible to the shaders and vertex fetch.
  // Called before each render pass and before submission; cheap when nothing was uploaded.
  void close_transfers(VkCommandBuffer command);

  // The objects behind a handle, for the draw path (valid until released).
  [[nodiscard]] VkImageView view(std::uint32_t handle) const;
  [[nodiscard]] VkBuffer buffer(std::uint32_t handle) const;
  [[nodiscard]] VkDeviceSize buffer_offset(std::uint32_t handle) const;
  [[nodiscard]] bool linear(std::uint32_t handle) const { return entry(handle).linear; }
  // Called with a handle when it is released (its descriptor sets go with it).
  void set_release_hook(std::function<void(std::uint32_t)> hook) { release_hook_ = std::move(hook); }
  [[nodiscard]] VkSampler sampler(bool linear) const { return linear ? linear_ : nearest_; }

  // Synchronous readback for tests and captures of resources: copies a region of one layer into
  // `out` (tightly packed RGBA). Ends the current frame's submission.
  void read_layer(std::uint32_t handle, int layer, int x, int y, int width, int height,
                  std::vector<std::byte>& out);

 private:
  enum class Kind : std::uint8_t { none, surface, layers, buffer };
  struct Entry {
    Kind kind = Kind::none;
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    int width = 0;   // image width; the byte size of a buffer
    int height = 0;  // image height
    int layers = 0;
    bool linear = false;
    Arena::Allocation alloc;  // buffers: a range of the arena
    bool pending = false;  // in this frame's transfer batch
  };

  std::vector<Entry> entries_;
  std::vector<std::uint32_t> free_;  // indices of released entries
  std::vector<std::uint32_t> pending_images_;
  std::vector<std::uint32_t> pending_buffers_;
  VkSampler nearest_ = VK_NULL_HANDLE;
  VkSampler linear_ = VK_NULL_HANDLE;
  std::function<void(std::uint32_t)> release_hook_;

  Entry& entry(std::uint32_t handle);
  const Entry& entry(std::uint32_t handle) const;
  std::uint32_t add(Entry entry);
  void begin_transfer(std::uint32_t index, VkCommandBuffer command);
  void stage_image(std::uint32_t index, int layer, int x, int y, int width, int height,
                   const std::byte* rgba);
  void create_image(Entry& image, int width, int height, int layers, bool array);
  void retire_entry(Entry& entry) noexcept;
};

// The process's resources (the Vulkan backend creates them with its device).
[[nodiscard]] Resources& resources();
}  // namespace realm::gfx::vk
