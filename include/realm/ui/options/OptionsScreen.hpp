#pragma once
// Flash: com/company/assembleegameclient/ui/options/Options.as
#include "realm/ui/fonts/FontBook.hpp"
#include "realm/hud/Hud.hpp"
#include "realm/parameters/Settings.hpp"
#include <array>
#include <cstdint>
#include <vector>
namespace realm {
// Native Options screen (ui/options/Options.as): six tabs of key mappers and choice boxes,
// "continue", "reset to defaults" and "back to home". Coordinates stay in the 800x600
// design space, centered on the window and scaled by its height; the retained bitmap
// covers the whole window and is rebuilt at drawable density. Pointer positions are
// drawable pixels. Keys arrive as Flash key codes (see SdlKeyCodes.hpp).
class OptionsScreen {
 public:
  // Hit identifiers: tabs 0-5, key/choice boxes 100+, labels 200+, buttons 300-302.
  static constexpr int tab_hit = 0, box_hit = 100, label_hit = 200, continue_hit = 300, reset_hit = 301, home_hit = 302;
  // The settings are owned by the caller and must outlive the open screen.
  void open(Settings& settings);
  void close();
  [[nodiscard]] bool visible() const { return settings_ != nullptr; }
  [[nodiscard]] OptionsTab tab() const { return tab_; }
  void set_tab(OptionsTab tab);
  // Whether a KeyCodeBox is waiting for a key ("[Hit Key]").
  [[nodiscard]] bool capturing() const { return capture_ >= 0; }
  // A value or binding changed since the last call; the caller applies and saves.
  [[nodiscard]] bool take_changed() {
    const bool value = changed_;
    changed_ = false;
    return value;
  }
  // "back to home" was clicked (the screen is already closed).
  [[nodiscard]] bool take_home() {
    const bool value = home_;
    home_ = false;
    return value;
  }
  bool update(const FontBook& fonts, double stage_width, double stage_height, std::int32_t time_ms);
  void pointer_move(Vec2 point);
  void pointer_down(Vec2 point);
  void pointer_up(Vec2 point);
  void focus_lost();
  // The Options screen consumes every key while open. Returns true when it did.
  bool key_down(int flash_code);

  [[nodiscard]] const Image& image() const { return image_; }
  [[nodiscard]] double scale() const { return scale_; }
  [[nodiscard]] Vec2 position() const { return position_; }
  // Geometry (design units, relative to the 800x600 content) is ready after update().
  [[nodiscard]] HudRect tab_rect(int index) const { return tab_rects_.at(static_cast<std::size_t>(index)); }
  [[nodiscard]] HudRect box_rect(int index) const { return rows_.at(static_cast<std::size_t>(index)).box; }
  [[nodiscard]] HudRect label_rect(int index) const { return rows_.at(static_cast<std::size_t>(index)).label; }
  [[nodiscard]] HudRect button_rect(int hit) const { return buttons_.at(static_cast<std::size_t>(hit - continue_hit)); }
  [[nodiscard]] std::size_t option_count() const { return rows_.size(); }
  [[nodiscard]] const OptionDef& option(int index) const { return *rows_.at(static_cast<std::size_t>(index)).def; }
  [[nodiscard]] int hover() const { return hover_; }
  [[nodiscard]] bool tooltip_visible() const { return tooltip_; }
  // The design-space point under a drawable-pixel point.
  [[nodiscard]] Vec2 local(Vec2 point) const { return {(point.x - position_.x) / scale_, point.y / scale_}; }

 private:
  struct Row {
    const OptionDef* def{};
    HudRect box{}, label{};
  };
  [[nodiscard]] int hit(Vec2 point) const;
  void activate(int hit);
  void layout(const FontBook& fonts);
  void cancel_capture();
  void refresh_hover();
  Settings* settings_{};
  OptionsTab tab_{OptionsTab::controls};
  std::vector<Row> rows_;
  std::array<HudRect, options_tab_count> tab_rects_{};
  std::array<HudRect, 3> buttons_{};
  int hover_{-1}, pressed_{-1}, capture_{-1}, blink_{};
  bool dirty_{true}, changed_{}, home_{}, laid_out_{}, pointer_valid_{}, tooltip_{};
  double stage_width_{}, stage_height_{}, scale_{1};
  Vec2 position_{}, pointer_{};
  Image image_, base_;
};
}
