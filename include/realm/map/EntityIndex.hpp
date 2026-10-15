#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace realm {
// Object id -> dense entity slot. An open-addressing table (linear probing, backward-shift
// deletion, no tombstones) in one contiguous array: adding or removing an entity allocates nothing
// between growth steps, and a lookup is one multiply and usually one cache line. Capacity is a
// power of two kept between 2x and 16x the live count (above a small floor), so a crowd that leaves
// hands its table back. The slot value is an index into World's dense list, which moves on
// swap-removal; callers re-point it (`assign`) whenever an entity changes slot.
class EntityIndex {
 public:
  static constexpr std::uint32_t none = ~std::uint32_t{0};

  [[nodiscard]] std::uint32_t find(std::int32_t id) const noexcept {
    if (slots_.empty()) {
      return none;
    }
    for (auto at = home(id);; at = (at + 1) & mask_) {
      const auto& slot = slots_[at];
      if (slot.index == none) {
        return none;
      }
      if (slot.id == id) {
        return slot.index;
      }
    }
  }

  // Inserts or re-points `id` at `index`; `index` must not be `none`.
  void assign(std::int32_t id, std::uint32_t index) {
    if (!slots_.empty()) {
      for (auto at = home(id);; at = (at + 1) & mask_) {
        auto& slot = slots_[at];
        if (slot.index == none) {
          break;
        }
        if (slot.id == id) {
          slot.index = index;
          return;
        }
      }
    }

    if ((count_ + 1) * 2 > slots_.size()) {
      rehash(slots_.empty() ? minimum : slots_.size() * 2);
    }

    for (auto at = home(id);; at = (at + 1) & mask_) {
      auto& slot = slots_[at];
      if (slot.index == none) {
        slot = {id, index};
        ++count_;
        return;
      }
    }
  }

  void erase(std::int32_t id) {
    if (slots_.empty()) {
      return;
    }
    auto at = home(id);
    for (;; at = (at + 1) & mask_) {
      if (slots_[at].index == none) {
        return;
      }
      if (slots_[at].id == id) {
        break;
      }
    }
    // Backward shift: pull later members of the probe run into the hole unless they are already at
    // home.
    for (auto next = (at + 1) & mask_; slots_[next].index != none; next = (next + 1) & mask_) {
      const auto ideal = home(slots_[next].id);
      if (((next - ideal) & mask_) >= ((next - at) & mask_)) {
        slots_[at] = slots_[next];
        at = next;
      }
    }
    slots_[at].index = none;
    --count_;
    if (slots_.size() > shrink_floor && count_ * 16 < slots_.size()) {
      rehash(slots_.size() / 4);
    }
  }

  // Drops every entry; a table grown by a crowd is released rather than kept for the next map.
  void clear() {
    count_ = 0;
    if (slots_.size() > shrink_floor) {
      slots_.clear();
      slots_.shrink_to_fit();
      mask_ = 0;
    } else {
      for (auto& slot : slots_) {
        slot.index = none;
      }
    }
  }

  [[nodiscard]] std::size_t size() const noexcept { return count_; }

  [[nodiscard]] std::size_t capacity() const noexcept { return slots_.size(); }

  [[nodiscard]] std::size_t bytes() const noexcept { return slots_.capacity() * sizeof(Slot); }

 private:
  static constexpr std::size_t minimum = 16;
  static constexpr std::size_t shrink_floor = 1024;

  struct Slot {
    std::int32_t id{};
    std::uint32_t index{none};
  };

  [[nodiscard]] std::size_t home(std::int32_t id) const noexcept {
    // Fibonacci hashing on the high bits: sequential ids (the server's, and the 0x7f000000 local
    // range) and ids with a power-of-two stride both spread across the table.
    return static_cast<std::uint32_t>(id) * 2654435761U >> shift_;
  }

  void rehash(std::size_t capacity) {
    std::vector<Slot> old(capacity);
    old.swap(slots_);
    mask_ = capacity - 1;
    shift_ = 32;
    for (auto size = capacity; size > 1; size >>= 1) {
      --shift_;
    }
    for (const auto& slot : old) {
      if (slot.index != none) {
        for (auto at = home(slot.id);; at = (at + 1) & mask_) {
          if (slots_[at].index == none) {
            slots_[at] = slot;
            break;
          }
        }
      }
    }
  }

  std::vector<Slot> slots_;
  std::size_t mask_{};
  std::size_t count_{};
  unsigned shift_{32};
};
}
