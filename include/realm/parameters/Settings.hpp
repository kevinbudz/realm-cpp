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
// integer: key bindings hold Flash key codes (KeyCodes.as), toggles 1/0 and
// choices the value the ChoiceOption lists. Option names come from the Flash source.
enum class OptionsTab { controls, gameplay, hotkeys, chat, graphics, sound };
inline constexpr std::size_t options_tab_count = 6;
[[nodiscard]] std::string_view options_tab_name(OptionsTab tab);

struct OptionDef {
  enum class Kind { key, choice, spacer, hidden_key, hidden_value };
  OptionsTab tab{};
  Kind kind{Kind::key};
  std::string_view param;
  std::string_view label;
  std::string_view tooltip;
  int fallback{};
  // Choice labels and values in source order; a click advances to the next.
  std::vector<std::pair<std::string_view, int>> choices;
  // KeyMapper disabled state: the rotate keys follow allowRotation.
  std::string_view enabled_by;
  // The row takes effect when the client next starts. Its tooltip says so, and says when the
  // running value differs.
  bool restart{};
};

// Rows of every tab in add order, plus the parameters registered without a row
// (escapeToNexus2, showTradePopup).
[[nodiscard]] std::span<const OptionDef> option_defs();
[[nodiscard]] std::vector<const OptionDef*> tab_options(OptionsTab tab);
[[nodiscard]] const OptionDef* find_option(std::string_view param);

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
  bool text_bubbles{true};
  bool ally_shots{true};
  bool ally_damage{true};
  bool ally_notifs{true};
  bool show_tier_tag{true};
  bool show_quest_portraits{true};
  bool show_guild_invite_popup{true};
  bool show_trade_popup{true};
  bool play_sfx{true};
  bool play_pew_pew{true};
};

// Parameters.cameraZoom bounds. Zoom 1 is the default and a larger zoom draws objects bigger.
inline constexpr double min_camera_zoom = 0.8;
inline constexpr double max_camera_zoom = 2.0;
// One wheel notch is 0.1 (positive zooms in). The result is rounded to the nearest 0.1 and clamped
// to the range. A non-finite zoom reads as 1 and non-finite steps as none.
[[nodiscard]] double step_camera_zoom(double zoom, double steps);

class Settings {
 public:
  Settings();
  [[nodiscard]] int get(std::string_view param) const;

  [[nodiscard]] bool flag(std::string_view param) const { return get(param) != 0; }

  // Parameters.setKey: any other registered binding on the same code is unset.
  void set_key(std::string_view param, int code);
  // ChoiceBox values only; anything else is ignored. Returns whether it changed.
  bool set_choice(std::string_view param, int value);
  // Options.onResetToDefaultsClick: only the rows of the visible tab are reset
  // (their values are deleted, then setDefaults fills the missing ones).
  void reset_tab(OptionsTab tab);
  void reset_all();

  // KeyMapper.setDisabled: whether the row currently accepts clicks.
  [[nodiscard]] bool enabled(const OptionDef& option) const {
    return option.enabled_by.empty() || flag(option.enabled_by);
  }

  // Parameters.cameraAngle: persisted with the settings.
  [[nodiscard]] double camera_angle() const { return camera_angle_; }

  void set_camera_angle(double angle) { camera_angle_ = angle; }

  [[nodiscard]] double default_camera_angle() const;

  // Parameters.cameraZoom: persisted with the settings and kept by reset_tab and reset_all.
  [[nodiscard]] double camera_zoom() const { return camera_zoom_; }

  void set_camera_zoom(double zoom) { camera_zoom_ = step_camera_zoom(zoom, 0); }

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
  double camera_zoom_;
};
}
