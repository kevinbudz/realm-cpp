#pragma once
#include "realm/camera.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>
namespace realm {
// One bit per slot of World's dense entity list for each of `Sets` membership sets, moved along with its swap-removal.
// Visiting a set's bits in index order visits its members in exactly the order a scan of World::entities() meets them,
// so first-found and tie rules of the scans they replace are unchanged; a scan of 246,000 static objects becomes a walk
// over 3,850 words. The sets are interleaved per 64 slots (one block), so an add or a swap-removal touches at most two
// blocks whatever the count.
template <std::size_t Sets>
class DenseSets {
  static_assert(Sets <= 8);

 public:
  void clear() noexcept {
    blocks_.clear();
    masks_.clear();
  }
  void release() {
    if (blocks_.capacity() > 64 && blocks_.capacity() > blocks_.size() * 8) blocks_.shrink_to_fit();
    if (masks_.capacity() > 4096 && masks_.capacity() > masks_.size() * 8) masks_.shrink_to_fit();
  }
  // A new last slot, member of the sets whose bit is set in `mask`.
  void push(std::uint32_t mask) {
    const auto i = masks_.size();
    if (i % 64 == 0) blocks_.push_back({});
    masks_.push_back(static_cast<std::uint8_t>(mask));
    for (auto m = mask; m; m &= m - 1)
      blocks_[i / 64][static_cast<std::size_t>(std::countr_zero(m))] |= std::uint64_t{1} << (i % 64);
  }
  void set(std::size_t k, std::size_t i) noexcept {
    blocks_[i / 64][k] |= std::uint64_t{1} << (i % 64);
    masks_[i] |= static_cast<std::uint8_t>(1U << k);
  }
  void reset(std::size_t k, std::size_t i) noexcept {
    blocks_[i / 64][k] &= ~(std::uint64_t{1} << (i % 64));
    masks_[i] &= static_cast<std::uint8_t>(~(1U << k));
  }
  [[nodiscard]] bool test(std::size_t k, std::size_t i) const noexcept { return masks_[i] >> k & 1U; }
  // World::remove: the last slot's membership moves into slot i, and the list shrinks by one. Each slot also keeps its
  // membership as one byte, so only the words of sets either slot belongs to are touched (a projectile: one).
  void swap_remove(std::size_t i) noexcept {
    const auto last = masks_.size() - 1;
    const auto moved = masks_[last];
    for (std::uint32_t m = moved; m; m &= m - 1)
      blocks_[last / 64][static_cast<std::size_t>(std::countr_zero(m))] &= ~(std::uint64_t{1} << (last % 64));
    if (i != last) {
      for (std::uint32_t m = masks_[i]; m; m &= m - 1)
        blocks_[i / 64][static_cast<std::size_t>(std::countr_zero(m))] &= ~(std::uint64_t{1} << (i % 64));
      for (std::uint32_t m = moved; m; m &= m - 1)
        blocks_[i / 64][static_cast<std::size_t>(std::countr_zero(m))] |= std::uint64_t{1} << (i % 64);
      masks_[i] = moved;
    }
    masks_.pop_back();
    if (masks_.size() % 64 == 0) blocks_.pop_back();
  }
  [[nodiscard]] std::size_t size() const noexcept { return masks_.size(); }
  [[nodiscard]] std::size_t count(std::size_t k) const noexcept {
    std::size_t n{};
    for (const auto& b : blocks_) n += static_cast<std::size_t>(std::popcount(b[k]));
    return n;
  }
  // Ascending index order. `visit` may clear the bit it is given (the word is copied first), nothing else.
  template <class Visit>
  void each(std::size_t k, Visit&& visit) const {
    for (std::size_t w = 0; w < blocks_.size(); ++w)
      for (auto bits = blocks_[w][k]; bits; bits &= bits - 1)
        visit(w * 64 + static_cast<std::size_t>(std::countr_zero(bits)));
  }

 private:
  std::vector<std::array<std::uint64_t, Sets>> blocks_;
  std::vector<std::uint8_t> masks_;
};
// Static (stationary-definition) entities bucketed by 8x8-tile cell, holding dense indices. Positions outside the map
// clamp to the border cells, and so do query bounds, which keeps every query a superset of the entities inside its
// rectangle. Each entity's cell and slot sit in arrays parallel to the dense list so insertion, removal, a move and the
// dense list's swap-removal are O(1). Order inside a cell is arbitrary; callers impose their own (Scene sorts by depth
// and id).
class StaticGrid {
 public:
  static constexpr std::uint32_t none = ~std::uint32_t{0};
  static constexpr int cell_tiles = 8;
  void reset(int width, int height) {
    columns_ = std::max(1, (width + cell_tiles - 1) / cell_tiles);
    rows_ = std::max(1, (height + cell_tiles - 1) / cell_tiles);
    std::vector<std::vector<std::uint32_t>>(static_cast<std::size_t>(columns_) * static_cast<std::size_t>(rows_))
        .swap(cells_);
    place_.clear();
    count_ = 0;
    if (place_.capacity() > 4096) std::vector<Place>().swap(place_);
  }
  // A new dense slot (World::add), outside the grid until inserted.
  void push_slot() { place_.push_back({}); }
  [[nodiscard]] bool contains(std::uint32_t index) const noexcept { return place_[index].cell != none; }
  void insert(std::uint32_t index, Vec2 position) {
    const auto cell = cell_of(position);
    auto& list = cells_[cell];
    place_[index] = {cell, static_cast<std::uint32_t>(list.size())};
    list.push_back(index);
    ++count_;
  }
  void erase(std::uint32_t index) noexcept {
    auto& list = cells_[place_[index].cell];
    const auto slot = place_[index].slot;
    list[slot] = list.back();
    place_[list[slot]].slot = slot;
    list.pop_back();
    place_[index].cell = none;
    --count_;
  }
  // A member changed position: re-bucket it only if it crossed into another cell.
  void move(std::uint32_t index, Vec2 position) {
    if (place_[index].cell == none || cell_of(position) == place_[index].cell) return;
    erase(index);
    insert(index, position);
  }
  // World::remove: slot `index` has already been erased from the grid if it was a member; the last slot moves into it.
  void swap_remove(std::uint32_t index) noexcept {
    const auto last = static_cast<std::uint32_t>(place_.size() - 1);
    if (index != last) {
      place_[index] = place_[last];
      if (place_[index].cell != none) cells_[place_[index].cell][place_[index].slot] = index;
    }
    place_.pop_back();
  }
  [[nodiscard]] std::size_t size() const noexcept { return count_; }
  [[nodiscard]] std::uint32_t cell_of(Vec2 p) const noexcept {
    return static_cast<std::uint32_t>(row(p.y) * columns_ + column(p.x));
  }
  // Every member whose cell overlaps [x0,x1]x[y0,y1] (world tiles); a member is visited once.
  template <class Visit>
  void query(double x0, double y0, double x1, double y1, Visit&& visit) const {
    const int cx0 = clamp_cell(x0, columns_, false), cx1 = clamp_cell(x1, columns_, true),
              cy0 = clamp_cell(y0, rows_, false), cy1 = clamp_cell(y1, rows_, true);
    for (int y = cy0; y <= cy1; ++y)
      for (int x = cx0; x <= cx1; ++x)
        for (const auto index : cells_[static_cast<std::size_t>(y * columns_ + x)]) visit(index);
  }

 private:
  // Floor then clamp is monotone, so a member inside a range lands in the range's cells; an infinite bound clamps to
  // the border and a NaN bound widens to the whole axis (`high` picks which end it stands for).
  [[nodiscard]] static int clamp_cell(double tiles, int count, bool high = false) noexcept {
    const double c = std::floor(tiles / cell_tiles);
    if (std::isnan(c)) return high ? count - 1 : 0;
    if (c <= 0) return 0;
    return c >= count - 1 ? count - 1 : static_cast<int>(c);
  }
  [[nodiscard]] int column(double x) const noexcept { return clamp_cell(x, columns_); }
  [[nodiscard]] int row(double y) const noexcept { return clamp_cell(y, rows_); }
  int columns_{1}, rows_{1};
  std::vector<std::vector<std::uint32_t>> cells_ = std::vector<std::vector<std::uint32_t>>(1);
  struct Place {
    std::uint32_t cell{none}, slot{};
  };
  std::vector<Place> place_;
  std::size_t count_{};
};
}
