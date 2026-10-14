#pragma once
#include "realm/ui/tooltip/QuestToolTip.hpp"

namespace realm {
// PortraitToolTip.as: the target's portrait cropped ten pixels inside on every side, in a ToolTip frame with no drop
// shadow. The frame is the cropped bitmap's bounds plus six pixels on every side.
class PortraitToolTip {
 public:
  static constexpr int origin = QuestToolTip::origin;
  PortraitToolTip(const FontBook& fonts, const HudArt& art) : fonts_(fonts), art_(art) {}
  // Redraws only when the target or the font density changes. Returns whether the image changed.
  bool update(const QuestTarget& target);
  [[nodiscard]] const Image& image() const { return image_; }
  [[nodiscard]] int content_width() const { return content_width_; }
  [[nodiscard]] int content_height() const { return content_height_; }
  [[nodiscard]] int canvas_width() const { return content_width_ + 12 + 2 * origin; }
  [[nodiscard]] int canvas_height() const { return content_height_ + 12 + 2 * origin; }
  [[nodiscard]] std::uint64_t rebuilds() const { return rebuilds_; }

 private:
  void render();
  const FontBook& fonts_;
  const HudArt& art_;
  std::optional<QuestTarget> target_;
  Image image_;
  int content_width_{}, content_height_{};
  std::uint64_t rebuilds_{};
};
}
