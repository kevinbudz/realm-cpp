#pragma once
#include "realm/assets/HudArt.hpp"
#include "realm/map/World.hpp"
#include "realm/objects/Catalog.hpp"
#include "realm/ui/fonts/FontBook.hpp"
#include <cstdint>
#include <optional>
#include <string>

namespace realm {
// The quest object as its tooltips and arrow see it: a snapshot, so no pointer into World's entity storage survives.
struct QuestTarget {
  std::int32_t id{-1};
  int type{}, level{-1}, skin{};
  std::uint32_t tex1{}, tex2{};
  std::string name;
  bool player{}, fellow_guild{};
  bool operator==(const QuestTarget&) const = default;
};
// Nullopt when the object is absent or its type is unknown.
[[nodiscard]] std::optional<QuestTarget> quest_target(const World& world, std::int32_t id);

// QuestToolTip.as: "Quest!" above the target's GameObjectListItem row, in a ToolTip frame with no drop shadow. The
// frame is ToolTip.draw's content plus six pixels on every side, and the content starts at the canvas origin.
class QuestToolTip {
 public:
  // PlayerGroup's canvas inset, so tooltips beside arrows share one placement.
  static constexpr int origin = 22;
  QuestToolTip(const Catalog& catalog, const FontBook& fonts, const HudArt& art)
      : catalog_(catalog), fonts_(fonts), art_(art) {}
  // Redraws only when the target or the font density changes. Returns whether the image changed.
  bool update(const QuestTarget& target);
  [[nodiscard]] const Image& image() const { return image_; }
  // ToolTip.contentWidth_ and contentHeight_: whole logical pixels.
  [[nodiscard]] int content_width() const { return content_width_; }
  [[nodiscard]] int content_height() const { return content_height_; }
  [[nodiscard]] int canvas_width() const { return content_width_ + 12 + 2 * origin; }
  [[nodiscard]] int canvas_height() const { return content_height_ + 12 + 2 * origin; }
  [[nodiscard]] std::uint64_t rebuilds() const { return rebuilds_; }

 private:
  void render();
  const Catalog& catalog_;
  const FontBook& fonts_;
  const HudArt& art_;
  std::optional<QuestTarget> target_;
  Image image_;
  int content_width_{}, content_height_{};
  std::uint64_t rebuilds_{};
};
}
