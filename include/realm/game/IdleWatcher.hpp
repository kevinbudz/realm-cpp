#pragma once
#include <cstdint>

namespace realm {
// Game.IdleWatcher: input resets the counter; only the playing game ticks it.
class IdleWatcher {
 public:
  enum class Event : std::uint8_t {
    none,
    warning_10_minutes,
    warning_15_minutes,
    disconnect_20_minutes
  };
  static constexpr std::int32_t minute_ms = 60 * 1000;
  static constexpr std::int32_t warning_10_ms = 10 * minute_ms;
  static constexpr std::int32_t warning_15_ms = 15 * minute_ms;
  static constexpr std::int32_t disconnect_ms = 20 * minute_ms;

  // GameSprite's MOUSE_MOVE and KEY_DOWN listeners.
  void reset() {
    idle_time_ = 0;
    warned_10_ = warned_15_ = disconnected_ = false;
  }

  // GameSprite.onEnterFrame clamps dt to [0, 100] and emits each milestone once.
  [[nodiscard]] Event update(std::int32_t dt_ms);

  [[nodiscard]] std::int32_t idle_time() const { return idle_time_; }

 private:
  std::int32_t idle_time_{};
  bool warned_10_{};
  bool warned_15_{};
  bool disconnected_{};
};
}  // namespace realm
