#pragma once
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>
namespace realm {
// Object stats (stat id -> value) as one sorted contiguous array of 8-byte entries, replacing a node-based hash map
// that cost a heap node, a bucket pointer and a hash probe per stat (a player carries ~40; thousands of players are
// resident). Same lookup interface as the map it replaces. Unlike it, an insertion may move entries: iterators,
// pointers and references into the map are invalidated by operator[] on a new key and by erase, so hold them only until
// the next write.
class StatMap {
 public:
  using value_type = std::pair<std::uint8_t, std::int32_t>;
  using iterator = std::vector<value_type>::iterator;
  using const_iterator = std::vector<value_type>::const_iterator;
  [[nodiscard]] iterator begin() noexcept { return entries_.begin(); }
  [[nodiscard]] iterator end() noexcept { return entries_.end(); }
  [[nodiscard]] const_iterator begin() const noexcept { return entries_.begin(); }
  [[nodiscard]] const_iterator end() const noexcept { return entries_.end(); }
  [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
  [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
  [[nodiscard]] std::size_t capacity() const noexcept { return entries_.capacity(); }
  void clear() noexcept { entries_.clear(); }
  void reserve(std::size_t count) { entries_.reserve(count); }
  [[nodiscard]] iterator find(std::uint8_t key) noexcept {
    const auto it = lower(key);
    return it != entries_.end() && it->first == key ? it : entries_.end();
  }
  [[nodiscard]] const_iterator find(std::uint8_t key) const noexcept {
    const auto it = lower(key);
    return it != entries_.end() && it->first == key ? it : entries_.end();
  }
  [[nodiscard]] bool contains(std::uint8_t key) const noexcept { return find(key) != entries_.end(); }
  [[nodiscard]] std::size_t count(std::uint8_t key) const noexcept { return contains(key) ? 1 : 0; }
  [[nodiscard]] std::int32_t& at(std::uint8_t key) {
    const auto it = find(key);
    if (it == entries_.end()) throw std::out_of_range("stat");
    return it->second;
  }
  [[nodiscard]] const std::int32_t& at(std::uint8_t key) const {
    const auto it = find(key);
    if (it == entries_.end()) throw std::out_of_range("stat");
    return it->second;
  }
  // The stat's value, or `fallback` when the stat is absent.
  [[nodiscard]] std::int32_t value_or(std::uint8_t key, std::int32_t fallback) const noexcept {
    const auto it = find(key);
    return it == entries_.end() ? fallback : it->second;
  }
  // Enum keys (flash::StatId) are read by their underlying byte.
  template <class E>
    requires std::is_enum_v<E>
  [[nodiscard]] std::int32_t value_or(E key, std::int32_t fallback) const noexcept {
    return value_or(static_cast<std::uint8_t>(key), fallback);
  }
  std::int32_t& operator[](std::uint8_t key) {
    auto it = lower(key);
    if (it == entries_.end() || it->first != key) it = entries_.insert(it, value_type{key, 0});
    return it->second;
  }
  std::size_t erase(std::uint8_t key) {
    const auto it = find(key);
    if (it == entries_.end()) return 0;
    entries_.erase(it);
    return 1;
  }

 private:
  [[nodiscard]] iterator lower(std::uint8_t key) noexcept {
    return std::lower_bound(entries_.begin(), entries_.end(), key,
                            [](const value_type& e, std::uint8_t k) { return e.first < k; });
  }
  [[nodiscard]] const_iterator lower(std::uint8_t key) const noexcept {
    return std::lower_bound(entries_.begin(), entries_.end(), key,
                            [](const value_type& e, std::uint8_t k) { return e.first < k; });
  }
  std::vector<value_type> entries_;
};
}
