#pragma once
// Flash: com/company/assembleegameclient/parameters/Parameters.as
#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace realm {
// Parameters.data_ and the Options screen's option table. Every value is an
// integer: key bindings hold Flash key codes (KeyCodes.as), toggles 1/0, choices the
// value the option lists and sliders a number in their range. Option names come from
// the Flash source; the volume sliders and the Max FPS range are native additions.
//
// The screen groups rows into categories (its sidebar) and sections (the headings
// inside a category, also listed under the open category in the sidebar).
enum class OptionsCategory { keybinds, gameplay, video, audio };
inline constexpr std::size_t options_category_count = 4;
[[nodiscard]] std::string_view options_category_name(OptionsCategory category);

struct OptionDef {
  enum class Kind { key, toggle, choice, slider, hidden_key, hidden_value };
  OptionsCategory category{};
  std::string_view section;
  Kind kind{Kind::key};
  std::string_view param;
  std::string_view label;
  std::string_view tooltip;
  int fallback{};
  // Choice labels and values in source order.
  std::vector<std::pair<std::string_view, int>> choices;
  // KeyMapper disabled state: the rotate keys follow allowRotation.
  std::string_view enabled_by;
  // Sliders: the inclusive range, the drag step and the unit shown after the number.
  // With top_label set, one step past max stores 0 and reads as top_label ("Unlimited").
  int min{};
  int max{};
  int step{1};
  std::string_view unit;
  std::string_view top_label;
  // A choice whose values index an image set the screen can preview, shown as a gallery of
  // clickable tiles instead of a dropdown ("cursors": the 4x4 cursor sheet, -1 the OS cursor).
  std::string_view preview;
};

// Rows in table order, plus the parameters registered without a row (escapeToNexus2).
[[nodiscard]] std::span<const OptionDef> option_defs();
// The visible rows of one category, in table order.
[[nodiscard]] std::vector<const OptionDef*> category_options(OptionsCategory category);
// The section headings of one category, in first-use order.
[[nodiscard]] std::vector<std::string_view> category_sections(OptionsCategory category);
[[nodiscard]] const OptionDef* find_option(std::string_view param);

// A slider's position for a stored value, and the stored value for a position. The
// top_label stop sits one step past max.
[[nodiscard]] int slider_position(const OptionDef& option, int value);
[[nodiscard]] int slider_value(const OptionDef& option, int position);
[[nodiscard]] int slider_end(const OptionDef& option);
// What a choice or slider row shows for a value ("On", "60", "75%", "Unlimited").
[[nodiscard]] std::string value_text(const OptionDef& option, int value);

// KeyCodes.CharCodeStrings; empty for a code without a name.
[[nodiscard]] std::string_view key_name(int code);
inline constexpr int key_unset = 0;
inline constexpr int key_escape = 27;
inline constexpr int key_enter = 13;

// Settings the game systems read each frame.
struct Preferences {
  bool hp_bars{true};
  bool particles{true};
  bool draw_shadows{true};
  bool proj_outline{true};
  bool smart_projectiles{false};
  bool text_bubbles{true};
  bool bar_text{true};
  bool screen_shake{true};
  bool ally_shots{true};
  bool ally_damage{true};
  bool ally_notifs{true};
  bool ally_particles{false};
  bool show_tier_tag{true};
  bool show_quest_portraits{true};
  bool show_guild_invite_popup{true};
  bool show_trade_popup{true};
  bool fade_players{true};
  bool fade_guild{false};
  bool fade_locked{false};
  bool play_sfx{true};
  bool play_pew_pew{true};
  // Linear gains, 0 to 1.
  float master_volume{1};
  float sfx_volume{1};
  float weapon_volume{1};
  // Player Opacity, 0 to 1: the alpha of a player whose category is faded.
  float player_alpha{1};
};

// The alpha a player draws at, 0 to 1. The local player is never faded. Another player is faded
// only when the toggle of their category is on: Locked (starred in the party list) takes
// precedence over Guild, and the rest are Regular.
[[nodiscard]] float player_alpha(const Preferences& prefs, bool is_local, bool locked,
                                 bool same_guild);

class Settings {
 public:
  Settings();
  [[nodiscard]] int get(std::string_view param) const;

  [[nodiscard]] bool flag(std::string_view param) const { return get(param) != 0; }

  // Parameters.setKey: any other registered binding on the same code is unset.
  void set_key(std::string_view param, int code);
  // Toggle, choice and slider values only; anything else is ignored. Returns whether it
  // changed.
  bool set_value(std::string_view param, int value);
  // Options.onResetToDefaultsClick: only the rows of the open category are reset.
  void reset_category(OptionsCategory category);
  void reset_all();

  // KeyMapper.setDisabled: whether the row currently accepts clicks.
  [[nodiscard]] bool enabled(const OptionDef& option) const {
    return option.enabled_by.empty() || flag(option.enabled_by);
  }

  // Parameters.cameraAngle: persisted with the settings.
  [[nodiscard]] double camera_angle() const { return camera_angle_; }

  void set_camera_angle(double angle) { camera_angle_ = angle; }

  [[nodiscard]] double default_camera_angle() const;
  [[nodiscard]] Preferences preferences() const;

  // The key bound to an action, or key_unset.
  [[nodiscard]] int key(std::string_view param) const { return get(param); }

  [[nodiscard]] const std::map<std::string, int, std::less<>>& values() const { return values_; }

  // A missing, unreadable or malformed file yields the defaults. Unknown names are
  // dropped and invalid values fall back per option, so a hand-edited file cannot
  // inject a state the screen could not produce.
  [[nodiscard]] static Settings load(const std::filesystem::path& path);
  // Atomic write (temporary file and rename); false when the location is unwritable.
  bool save(const std::filesystem::path& path) const;
  [[nodiscard]] static std::filesystem::path path_beside(const std::filesystem::path& account_file);

 private:
  [[nodiscard]] static bool valid(const OptionDef& option, int value);
  std::map<std::string, int, std::less<>> values_;
  double camera_angle_;
};
}
