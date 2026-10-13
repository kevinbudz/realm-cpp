#pragma once
#include <cstdint>
#include <stdexcept>

namespace realm {
// This client's exact uint arithmetic, including zero and 0x7fffffff seeds.
class FlashRandom {
 public:
  explicit FlashRandom(std::uint32_t seed) : seed_(seed) {}
  std::uint32_t next() {
    std::uint32_t lb = 16807U * (seed_ & 0xffffU);
    const std::uint32_t hb = 16807U * (seed_ >> 16U);
    lb += (hb & 32767U) << 16U;
    lb += hb >> 15U;
    if (lb > 2147483647U) lb -= 2147483647U;
    return seed_ = lb;
  }
  std::uint32_t range(std::uint32_t min, std::uint32_t max) {
    if (min > max) throw std::invalid_argument("Invalid RNG range");
    return min == max ? min : min + next() % (max - min);
  }
  void drop(std::uint32_t count) {
    while (count--) next();
  }
  [[nodiscard]] std::uint32_t seed() const { return seed_; }

 private:
  std::uint32_t seed_;
};
}
