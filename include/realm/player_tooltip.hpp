#pragma once
#include "realm/hud.hpp"
#include "realm/inventory.hpp"

namespace realm {
// PartyPanel freezes displayed identities during hover; the actual Party keeps
// updating. Held IDs are resolved every frame so drops cannot leave stale pointers.
class PartyHover {
 public:
  void reset() {
    frozen_ = false;
    target_ = -1;
  }
  void update(const World& world, const Party& party, HudState& state, const HudState& displayed, Vec2 pointer,
              bool enabled, std::int32_t time_ms);
  [[nodiscard]] std::int32_t target() const { return target_; }
  [[nodiscard]] bool frozen() const { return frozen_; }
  [[nodiscard]] static int cell(Vec2 point);

 private:
  bool frozen_{};
  std::int32_t target_{-1};
  std::array<std::int32_t, 6> ids_{};
};
struct PlayerTooltipTarget {
  std::int32_t id{-1};
  int type{}, level{}, stars{-1}, guild_rank{};
  int skin{};
  std::uint32_t tex1{}, tex2{};
  std::string name, guild;
  bool fellow_guild{};
  std::array<StatusBarState, 2> bars;
  std::array<InventoryItem, 4> equipment;
  std::array<int, 4> slot_types{};
  bool operator==(const PlayerTooltipTarget&) const = default;
};
[[nodiscard]] std::optional<PlayerTooltipTarget> player_tooltip_target(const World& world, std::int32_t id);
// One fixed atlas reservation; only changed visible data triggers rasterization.
class PlayerTooltip {
 public:
  static constexpr int width = 512, maximum_height = 380, padding = 16;
  PlayerTooltip(const Catalog& catalog, const FontBook& fonts, const HudArt& art)
      : catalog_(catalog), fonts_(fonts), art_(art) {}
  bool update(const std::optional<PlayerTooltipTarget>& target);
  [[nodiscard]] bool visible() const { return target_.has_value(); }
  [[nodiscard]] int height() const { return height_; }
  [[nodiscard]] int draw_width() const { return draw_width_; }
  [[nodiscard]] const Image& image() const { return image_; }
  [[nodiscard]] std::uint64_t rebuilds() const { return rebuilds_; }
  [[nodiscard]] static Vec2 position(Vec2 pointer, int window_width, int window_height, int draw_width, int height,
                                     double scale);

 private:
  void render(const PlayerTooltipTarget& target);
  void blit(const Image& source, int x, int y, bool premultiplied = false);
  void rectangle(double x, int y, double w, int h, std::uint32_t color);
  void text(std::string_view value, int size, bool bold, std::uint32_t color, double x, int y, double opacity = 1,
            int field_width = 0, int field_height = 0);
  void shadow();
  const Catalog& catalog_;
  const FontBook& fonts_;
  const HudArt& art_;
  std::optional<PlayerTooltipTarget> target_;
  Image image_{width, maximum_height, std::vector<unsigned char>(width * maximum_height * 4)};
  int height_{}, draw_width_{};
  std::uint64_t rebuilds_{};
};
}
