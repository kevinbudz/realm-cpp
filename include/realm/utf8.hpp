#pragma once
#include <cstddef>
#include <string_view>

namespace realm {
// Counts UTF-8 code points: each byte that is not a continuation byte (10xxxxxx) starts one. No validation; stray
// continuation bytes are not counted.
[[nodiscard]] inline std::size_t code_points(std::string_view text) noexcept {
  std::size_t count{};
  for (const char c : text)
    if ((static_cast<unsigned char>(c) & 0xC0U) != 0x80U) ++count;
  return count;
}
}
