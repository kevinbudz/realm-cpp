#include "realm/game/IdleWatcher.hpp"
#include <algorithm>

namespace realm {
IdleWatcher::Event IdleWatcher::update(std::int32_t dt_ms) {
  if (disconnected_) {
    return Event::none;
  }
  idle_time_ = std::min(disconnect_ms, idle_time_ + std::clamp(dt_ms, 0, 100));
  if (!warned_10_ && idle_time_ >= warning_10_ms) {
    warned_10_ = true;
    return Event::warning_10_minutes;
  }
  if (!warned_15_ && idle_time_ >= warning_15_ms) {
    warned_15_ = true;
    return Event::warning_15_minutes;
  }
  if (idle_time_ >= disconnect_ms) {
    disconnected_ = true;
    return Event::disconnect_20_minutes;
  }
  return Event::none;
}
}  // namespace realm
