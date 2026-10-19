#include "VkArena.hpp"
#include <algorithm>
#include <stdexcept>

namespace realm::gfx::vk {
namespace {
constexpr VkDeviceSize block_bytes = 16u << 20;
constexpr VkBufferUsageFlags arena_usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                           VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                           VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
}  // namespace

std::uint32_t Arena::create_block(VkDeviceSize size, bool dedicated) {
  // Reuse a dead slot so block indices stay small; allocations keep their block index while live.
  auto slot =
      std::find_if(blocks_.begin(), blocks_.end(), [](const Block& block) { return !block.live; });
  if (slot == blocks_.end()) {
    blocks_.emplace_back();
    slot = blocks_.end() - 1;
  }
  slot->buffer = create_device_buffer(size, arena_usage);
  slot->free.assign(1, Range{0, size});
  slot->live = true;
  slot->dedicated = dedicated;
  return static_cast<std::uint32_t>(slot - blocks_.begin());
}

Arena::Allocation Arena::allocate(VkDeviceSize size, VkDeviceSize alignment) {
  if (size == 0) {
    throw std::invalid_argument("Empty arena allocation");
  }
  for (std::size_t index = 0; index < blocks_.size(); ++index) {
    auto& block = blocks_[index];
    if (!block.live) {
      continue;
    }
    for (auto range = block.free.begin(); range != block.free.end(); ++range) {
      const auto offset = (range->offset + alignment - 1) / alignment * alignment;
      if (offset + size > range->offset + range->size) {
        continue;
      }
      // Keep the pieces before and after the slice as free ranges.
      const Range after{offset + size, range->offset + range->size - (offset + size)};
      const Range before{range->offset, offset - range->offset};
      block.free.erase(range);
      if (after.size) {
        block.free.insert(
            std::find_if(block.free.begin(), block.free.end(),
                         [&](const Range& other) { return other.offset > after.offset; }),
            after);
      }
      if (before.size) {
        block.free.insert(
            std::find_if(block.free.begin(), block.free.end(),
                         [&](const Range& other) { return other.offset > before.offset; }),
            before);
      }
      return {block.buffer.buffer, offset, size, static_cast<std::uint32_t>(index)};
    }
  }
  // Nothing fits: a new block (a dedicated one for a large request).
  const auto index = create_block(std::max(size, block_bytes), size > block_bytes);
  auto& fresh = blocks_[index];
  fresh.free.clear();
  if (fresh.buffer.size > size) {
    fresh.free.push_back(Range{size, fresh.buffer.size - size});
  }
  return {fresh.buffer.buffer, 0, size, index};
}

void Arena::free(const Allocation& allocation) noexcept {
  if (allocation.buffer == VK_NULL_HANDLE || allocation.block >= blocks_.size()) {
    return;
  }
  auto& block = blocks_[allocation.block];
  const Range freed{allocation.offset, allocation.size};
  auto next = std::find_if(block.free.begin(), block.free.end(),
                           [&](const Range& other) { return other.offset > freed.offset; });
  next = block.free.insert(next, freed);
  // Merge with the following range, then with the preceding one.
  auto after = next + 1;
  if (after != block.free.end() && next->offset + next->size == after->offset) {
    next->size += after->size;
    block.free.erase(after);
  }
  if (next != block.free.begin()) {
    auto before = next - 1;
    if (before->offset + before->size == next->offset) {
      before->size += next->size;
      block.free.erase(next);
    }
  }
  // A block with nothing in use goes, unless it is the only live block.
  const bool empty = block.free.size() == 1 && block.free.front().offset == 0 &&
                     block.free.front().size == block.buffer.size;
  if (!empty) {
    return;
  }
  std::size_t live = 0;
  for (const auto& other : blocks_) {
    live += other.live;
  }
  if (block.dedicated || live > 1) {
    destroy_buffer(block.buffer);
    block.live = false;
    block.free.clear();
  }
}

void Arena::destroy() noexcept {
  for (auto& block : blocks_) {
    if (block.live) {
      destroy_buffer(block.buffer);
      block.live = false;
    }
    block.free.clear();
  }
  blocks_.clear();
}

VkDeviceSize Arena::reserved() const {
  VkDeviceSize total = 0;
  for (const auto& block : blocks_) {
    if (block.live) {
      total += block.buffer.size;
    }
  }
  return total;
}

std::size_t Arena::live_blocks() const {
  return static_cast<std::size_t>(
      std::count_if(blocks_.begin(), blocks_.end(), [](const Block& block) { return block.live; }));
}

Arena& arena() {
  static Arena instance;
  return instance;
}
}  // namespace realm::gfx::vk
