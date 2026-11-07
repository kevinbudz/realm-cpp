#pragma once
// Device-local buffer suballocation for the GPU buffers that change or come and go: quad layers,
// terrain chunks, model geometry. Blocks are large; a range is a first-fit slice of one, coalesced
// with its neighbours when freed. A free happens when the frames that may use the range have
// finished (the owning resource retires it), so a range is never reused while the GPU reads it.
#include "VkCommon.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace realm::gfx::vk {
class Arena {
 public:
  struct Allocation {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize size = 0;
    std::uint32_t block = 0;
  };

  // A range of at least size bytes at alignment. Requests above the block size get a block of their
  // own.
  [[nodiscard]] Allocation allocate(VkDeviceSize size, VkDeviceSize alignment);
  // Returns a range; a block that becomes empty is destroyed unless it is the only one.
  void free(const Allocation& allocation) noexcept;
  // Destroys every block (shutdown; the device is idle).
  void destroy() noexcept;

  [[nodiscard]] VkDeviceSize reserved() const;  // bytes of the live blocks
  [[nodiscard]] std::size_t live_blocks() const;

 private:
  struct Range {
    VkDeviceSize offset;
    VkDeviceSize size;
  };

  struct Block {
    Buffer buffer;
    std::vector<Range> free;  // sorted by offset, never adjacent
    bool live = false;
    bool dedicated = false;
  };

  std::vector<Block> blocks_;
  [[nodiscard]] std::uint32_t create_block(VkDeviceSize size, bool dedicated);
};

// The process's arena (the Vulkan backend's resources allocate from it).
[[nodiscard]] Arena& arena();
}  // namespace realm::gfx::vk
