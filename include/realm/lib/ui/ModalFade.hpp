#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>

namespace realm::ui {
// Fade-out for a closing modal (a dialog, Settings, the fame overview). The owner feeds it the
// modal's visibility once per frame with the frame clock. After the modal closes it keeps drawing
// the last content at opacity(), ramping to 0 over duration_ms; active() stays true for that span,
// so the owner keeps the closing modal modal (it takes the pointer and keys without acting on them)
// until it is gone. Reopening during the fade cancels it.
class ModalFade {
 public:
  static constexpr std::int32_t duration_ms = 180;

  // Once per frame, with the modal's visibility and the frame clock in milliseconds.
  void update(bool visible, std::int32_t time_ms) {
    if (visible) {
      closed_at_.reset();
      opacity_ = 1;
      return;
    }
    if (opacity_ <= 0) {
      return;
    }
    if (!closed_at_) {
      closed_at_ = time_ms;
    }
    const auto elapsed = std::max(0, time_ms - *closed_at_);
    opacity_ = elapsed >= duration_ms ? 0 : 1 - static_cast<double>(elapsed) / duration_ms;
  }

  // 1 while the modal is visible, falling to 0 over the fade.
  [[nodiscard]] double opacity() const { return opacity_; }

  // The modal is drawn: visible, or still fading out.
  [[nodiscard]] bool active() const { return opacity_ > 0; }

 private:
  double opacity_{};
  std::optional<std::int32_t> closed_at_;
};
}
