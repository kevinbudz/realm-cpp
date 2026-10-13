#pragma once
#include "realm/hud_input.hpp"
#include "realm/inventory.hpp"

namespace realm {
enum class TooltipOwner { player, container, merchant, other_player };
struct ItemTooltipTarget {
  InventoryItem item;
  InventoryItem equipped;
  TooltipOwner owner{};
  int slot{-1}, player_type{}, use_special{32};
  bool usable{}, inventory_full{};
  std::array<int, 8> stats{};
  bool operator==(const ItemTooltipTarget&) const = default;
};
struct ItemTooltipContent {
  std::string title, tier, speciality, description;
  std::uint32_t speciality_color{0xffffff};
  int bag_type{};
  std::vector<RichLine> custom;
  std::uint32_t title_color{0xffffff}, tier_color{0xffffff};
  std::vector<RichLine> effects, restrictions;
};
[[nodiscard]] std::optional<ItemTooltipTarget> item_tooltip_target(const World& world, const HudState& hud,
                                                                   Vec2 pointer);
[[nodiscard]] std::optional<ItemTooltipTarget> item_tooltip_target(const World& world, InventoryItem item,
                                                                   TooltipOwner owner);
[[nodiscard]] std::optional<ItemTooltipTarget> complete_tooltip_target(const World& world, ItemTooltipTarget target);
[[nodiscard]] ItemTooltipContent item_tooltip_content(const Catalog& catalog, const ItemTooltipTarget& target);
// Retained bitmap including the frame and its shadow. Pointer movement only changes placement.
class ItemTooltip {
 public:
  static constexpr int content_width = 230, padding = 30, width = content_width + padding * 2;
  ItemTooltip(const Catalog& catalog, const FontBook& fonts, const HudArt& art)
      : catalog_(catalog), fonts_(fonts), art_(art) {}
  bool update(const std::optional<ItemTooltipTarget>& target);
  [[nodiscard]] bool visible() const { return target_.has_value(); }
  [[nodiscard]] int height() const { return content_height_ + padding * 2; }
  [[nodiscard]] int content_height() const { return content_height_; }
  [[nodiscard]] const Image& image() const { return image_; }
  [[nodiscard]] std::uint64_t rebuilds() const { return rebuilds_; }
  [[nodiscard]] static Vec2 position(Vec2 pointer, int window_width, int window_height, int height, double scale);

 private:
  void render(const ItemTooltipTarget& target);
  void blit(const Image& source, int x, int y, bool premultiplied);
  void text(std::span<const RichLine> lines, int size, bool bold, int field_width, double field_height, double x,
            double y);
  void shadow();
  const Catalog& catalog_;
  const FontBook& fonts_;
  const HudArt& art_;
  std::optional<ItemTooltipTarget> target_;
  Image image_;
  int content_height_{};
  std::uint64_t rebuilds_{};
};
}
