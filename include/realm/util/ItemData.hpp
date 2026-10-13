#pragma once
#include <cstdint>
namespace realm::item_data {
inline constexpr std::uint32_t rate_of_fire = 1U << 16U, damage = 1U << 17U, cooldown = 1U << 18U;
[[nodiscard]] constexpr int rank(std::int32_t data) {
  if (data == -1) return -1;
  for (int i = 0; i < 8; ++i)
    if (static_cast<std::uint32_t>(data) & (1U << i)) return i + 1;
  return -1;
}
[[nodiscard]] constexpr double stat(std::int32_t data, std::uint32_t bit, double multiplier = .05) {
  const auto r = rank(data);
  return r >= 0 && (static_cast<std::uint32_t>(data) & bit) ? r * multiplier : 0;
}
[[nodiscard]] constexpr std::int32_t color(std::int32_t data) {
  constexpr std::int32_t colors[] = {0x00a6ff, 0x7300ff, 0xffc800, 0x84ff00, 0xf542e6, 0x00ffdd, 0xffffff, 0xff5500};
  const auto r = rank(data);
  return r < 0 ? -1 : colors[r - 1];
}
}
