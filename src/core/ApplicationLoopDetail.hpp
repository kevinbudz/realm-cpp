#pragma once
#include <chrono>

namespace realm::application::detail {
using Clock = std::chrono::steady_clock;

inline double milliseconds(Clock::duration duration) {
  return std::chrono::duration<double, std::milli>(duration).count();
}
}  // namespace realm::application::detail
