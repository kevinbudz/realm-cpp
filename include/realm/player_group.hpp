#pragma once
#include "realm/player_menu.hpp"

namespace realm {
// Snapshots own IDs/data, never pointers into World's movable entity storage.
[[nodiscard]] std::vector<PlayerMenuTarget> player_group_targets(const World& world, std::span<const std::int32_t> ids);
[[nodiscard]] std::optional<std::int32_t> first_teleport_player(const World& world,
                                                                std::span<const PlayerMenuTarget> players);
inline constexpr std::string_view no_teleport_players = "No players are eligible for teleporting";

// Shared PlayerGroupToolTip / PlayerGroupMenu layout and GameObjectListItem rows.
class PlayerGroup {
 public:
  static constexpr int origin = 22;
  PlayerGroup(const Catalog& catalog, const FontBook& fonts, const HudArt& art)
      : catalog_(catalog), fonts_(fonts), art_(art) {}
  bool tooltip(std::vector<PlayerMenuTarget> players, Vec2 pointer, int width, int height, double scale);
  // PlayerArrow uses a fixed inward anchor instead of the mouse-follow rule.
  bool arrow_tooltip(std::vector<PlayerMenuTarget> players, Vec2 anchor, double rotation, double arrow_scale,
                     double scale);
  bool open(std::vector<PlayerMenuTarget> players, bool allow_teleport, Vec2 pointer, int width, int height,
            double scale);
  void close() {
    players_.clear();
    menu_ = false;
    pressed_ = entered_ = false;
  }
  [[nodiscard]] bool visible() const { return !players_.empty(); }
  [[nodiscard]] bool menu() const { return visible() && menu_; }
  [[nodiscard]] const std::vector<PlayerMenuTarget>& players() const { return players_; }
  bool refresh_appearance(const World& world);
  bool update(Vec2 pointer, bool valid, std::int32_t cooldown_ms, double scale);
  [[nodiscard]] bool contains(Vec2 pointer) const;
  bool pointer_down(Vec2 pointer);
  // Returns a click even during cooldown or when no eligible player remains.
  bool pointer_up(Vec2 pointer);
  void focus_lost() { pressed_ = false; }
  [[nodiscard]] Vec2 canvas_position() const { return {position_.x - origin * scale_, position_.y - origin * scale_}; }
  [[nodiscard]] double scale() const { return scale_; }
  [[nodiscard]] int frame_width() const { return frame_width_; }
  [[nodiscard]] int frame_height() const { return frame_height_; }
  [[nodiscard]] int canvas_width() const { return frame_width_ + 2 * origin; }
  [[nodiscard]] int canvas_height() const { return std::min(644, frame_height_ + 2 * origin); }
  [[nodiscard]] std::uint64_t rebuilds() const { return rebuilds_; }
  [[nodiscard]] const Image& image() const { return image_; }
  void render_state(std::vector<PlayerMenuTarget> players, bool menu, bool teleport, bool hover, int cooldown);

 private:
  [[nodiscard]] Vec2 local(Vec2 p) const { return {(p.x - position_.x) / scale_, (p.y - position_.y) / scale_}; }
  [[nodiscard]] bool teleport_at(Vec2 p) const;
  void layout();
  void render();
  void position(Vec2 pointer, int width, int height);
  const Catalog& catalog_;
  const FontBook& fonts_;
  const HudArt& art_;
  std::vector<PlayerMenuTarget> players_;
  Image image_;
  Vec2 position_{};
  double scale_{1};
  int frame_width_{}, frame_height_{}, option_width_{}, cooldown_{}, cooldown_key_{-2};
  bool menu_{}, teleport_{}, hover_{}, pressed_{}, entered_{};
  std::uint64_t rebuilds_{};
};
}
