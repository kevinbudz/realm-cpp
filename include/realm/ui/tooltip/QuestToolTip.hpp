#pragma once
#include "realm/assets/HudArt.hpp"
#include "realm/map/World.hpp"
#include "realm/objects/Catalog.hpp"
#include "realm/assets/FontBook.hpp"
#include <cstdint>
#include <optional>
#include <string>

namespace realm {
// The quest object as its tooltips and arrow see it: a snapshot, so no pointer into World's entity
// storage survives.
struct QuestTarget {
  std::int32_t id{-1};
  int type{};
  int level{-1};
  int skin{};
  std::uint32_t tex1{};
  std::uint32_t tex2{};
  std::string name;
  bool player{};
  bool fellow_guild{};
  // Live combat state, so a change in any of these redraws the tooltip.
  int hp{};
  int max_hp{};
  // GameObject.isInvulnerable, and isArmored without isArmorBroken.
  bool invulnerable{};
  bool armored{};
  bool operator==(const QuestTarget&) const = default;
};

// Nullopt when the object is absent or its type is unknown.
[[nodiscard]] std::optional<QuestTarget> quest_target(const World& world, std::int32_t id);

// Logical height of the quest HP bar drawn under a portrait, and its minimum width so the label
// fits under a small portrait.
inline constexpr double quest_hp_bar_height = 14;
inline constexpr double quest_hp_bar_min_width = 40;

// BossHealthBar-style HP bar with its top-left at logical (x, y): a cut-edge track tinted blue when
// invulnerable, brown when armored and green otherwise, a 0x20FF20 fill at hp / max_hp, and a
// centered bold "hp/max_hp". Callers skip targets with no max_hp.
void draw_quest_hp_bar(Image& image, const FontBook& fonts, const QuestTarget& target, double x,
                       double y, double width);

// QuestToolTip.as: "Quest!" above the target's GameObjectListItem row, in a ToolTip frame with no
// drop shadow. The frame is ToolTip.draw's content plus six pixels on every side, and the content
// starts at the canvas origin.
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
  int content_width_{};
  int content_height_{};
  std::uint64_t rebuilds_{};
};
}
