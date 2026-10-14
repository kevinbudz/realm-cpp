#pragma once
#include "realm/hud/Hud.hpp"
#include "realm/objects/Party.hpp"
#include <utility>
#include <vector>

namespace realm {
// PlayerMenu.as option kinds in their fixed top-to-bottom order.
enum class PlayerMenuAction { teleport, invite, lock, unlock, trade, ignore, unignore };
// Player.MS_BETWEEN_TELEPORT.
inline constexpr std::int32_t teleport_cooldown_ms = 10000;
// ConditionEffect.INVISIBLE_BIT.
inline constexpr std::uint32_t invisible_condition = 1U << 11U;

struct PlayerMenuTarget {
  std::int32_t id{-1};
  int type{}, level{};
  int skin{};
  std::uint32_t tex1{}, tex2{};
  std::string name;
  bool fellow_guild{};
  std::vector<PlayerMenuAction> options;
  bool operator==(const PlayerMenuTarget&) const = default;
};
// GameObjectListItem's long-version label: the name in bold, then the display id and level in parentheses (the
// level only when at least 1). Without a name the display id alone is bold. `color` is the text color.
[[nodiscard]] TextBitmap object_row_label(const Catalog& catalog, const FontBook& fonts, int type,
                                          std::string_view name, int level, std::uint32_t color);
// Shared GameObjectListItem long-version label used by player and group menus.
[[nodiscard]] TextBitmap player_row_label(const Catalog& catalog, const FontBook& fonts, const PlayerMenuTarget& row);
// The options PlayerMenu builds for another player; fixed while it stays open.
[[nodiscard]] std::optional<PlayerMenuTarget> player_menu_target(const World& world, const Party& party,
                                                                 std::int32_t id, bool allow_teleport);

// Menu/PlayerMenu: a GameObjectListItem header and 28-pixel option rows in a
// cut-edge frame. Coordinates passed in are window drawable pixels; the menu
// scales with the window height (LayoutHelper.scaleForHeight).
class PlayerMenu {
 public:
  struct Request {
    PlayerMenuAction action;
    std::int32_t object_id{-1};
    std::string name;
  };
  static constexpr int canvas_width = 320, canvas_height = 240;
  // The menu's local origin within the canvas; the frame starts 6 pixels above-left.
  static constexpr int origin = 22;
  PlayerMenu(const Catalog& catalog, const FontBook& fonts, const HudArt& art)
      : catalog_(catalog), fonts_(fonts), art_(art) {}
  // Menu.position: right/below the pointer in the left half/top third, else
  // left/above it; never closer than 12 pixels to the top-left edge.
  bool open(PlayerMenuTarget target, Vec2 pointer, int window_width, int window_height, double scale);
  void close() {
    target_.reset();
    pressed_ = -1;
    entered_ = false;
  }
  [[nodiscard]] bool visible() const { return target_.has_value(); }
  [[nodiscard]] std::int32_t target() const { return target_ ? target_->id : -1; }
  // Closes after a roll-out or once the pointer is over 40 pixels from the frame.
  // teleport_ms is Player.msUtilTeleport. Returns whether the image changed.
  bool update(const World& world, Vec2 pointer, bool pointer_valid, std::int32_t teleport_ms, double scale);
  [[nodiscard]] bool contains(Vec2 pointer) const;
  // MouseEvent.CLICK: press and release on the same option.
  bool pointer_down(Vec2 pointer);
  [[nodiscard]] std::optional<Request> pointer_up(Vec2 pointer);
  void focus_lost() { pressed_ = -1; }
  [[nodiscard]] const Image& image() const { return image_; }
  // Canvas top-left in window pixels and its displayed logical size.
  [[nodiscard]] Vec2 canvas_position() const;
  [[nodiscard]] double scale() const { return scale_; }
  [[nodiscard]] int frame_width() const { return frame_width_; }
  [[nodiscard]] int frame_height() const { return frame_height_; }
  [[nodiscard]] int hover() const { return hover_; }
  [[nodiscard]] std::uint64_t rebuilds() const { return rebuilds_; }
  // Renders without a window; fixtures and reference comparisons use this.
  void render_state(const PlayerMenuTarget& target, int hover, int cooldown_ms);

 private:
  void layout();
  void render();
  [[nodiscard]] Vec2 local(Vec2 pointer) const;
  [[nodiscard]] int option_at(Vec2 point) const;
  const Catalog& catalog_;
  const FontBook& fonts_;
  const HudArt& art_;
  std::optional<PlayerMenuTarget> target_;
  Vec2 position_{};
  double scale_{1};
  int frame_width_{}, frame_height_{}, hover_{-1}, pressed_{-1}, cooldown_ms_{}, rendered_cooldown_{-1};
  bool entered_{};
  std::vector<int> option_widths_;
  Image image_{canvas_width, canvas_height, std::vector<unsigned char>(canvas_width * canvas_height * 4)};
  std::uint64_t rebuilds_{};
};
}
