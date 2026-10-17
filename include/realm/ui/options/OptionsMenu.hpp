#pragma once
// Flash: com/company/assembleegameclient/ui/options/Options.as
#include "realm/parameters/Settings.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace realm {
// The Options screen's state, without any drawing: the open category, the search query,
// the rows they list and the key binding waiting for a key. OptionsScreen draws it with
// RmlUi; tests and fixture scripts drive it directly. Keys arrive as Flash key codes
// (see SdlKeyCodes.hpp).
class OptionsMenu {
 public:
  // The settings are owned by the caller and must outlive the open menu.
  void open(Settings& settings);
  void close();

  [[nodiscard]] bool visible() const { return settings_ != nullptr; }

  [[nodiscard]] const Settings& settings() const { return *settings_; }

  [[nodiscard]] OptionsCategory category() const { return category_; }

  // Opens a category and clears the search.
  void set_category(OptionsCategory category);

  [[nodiscard]] const std::string& query() const { return query_; }

  // A non-empty query lists the matching rows of every category instead of one category.
  void set_query(std::string query);

  [[nodiscard]] bool searching() const { return !query_.empty(); }

  // The listed rows: the open category in table order, or every search match.
  [[nodiscard]] const std::vector<const OptionDef*>& rows() const { return rows_; }

  // Bumped whenever rows() changes, so a view knows to rebuild.
  [[nodiscard]] std::uint64_t layout_revision() const { return layout_revision_; }

  // Bumped whenever a value or the capture changes, so a view knows to refresh.
  [[nodiscard]] std::uint64_t value_revision() const { return value_revision_; }

  [[nodiscard]] int value(const OptionDef& option) const { return settings_->get(option.param); }

  [[nodiscard]] bool enabled(const OptionDef& option) const { return settings_->enabled(option); }

  // KeyCodeBox: the next key binds this row ("Press a key").
  void begin_capture(std::string_view param);
  void cancel_capture();

  [[nodiscard]] bool capturing() const { return !capture_.empty(); }

  [[nodiscard]] std::string_view capture() const { return capture_; }

  // Toggle, choice and slider rows; ignored for a disabled row or an invalid value.
  void set_value(std::string_view param, int value);
  void toggle(std::string_view param);
  // Options.onResetToDefaultsClick: the open category (or, while searching, the listed rows).
  void reset();
  // "Done" and "Back to Home".
  void request_close();
  void request_home();

  // While open the menu takes every key. Capture binds or (Escape) cancels; otherwise Escape
  // or the Show Settings key closes. Returns whether it took the key.
  bool key_down(int flash_code);

  // A value or binding changed since the last call; the caller applies and saves.
  [[nodiscard]] bool take_changed() {
    const bool value = changed_;
    changed_ = false;
    return value;
  }

  // "Back to Home" was clicked (the menu is already closed).
  [[nodiscard]] bool take_home() {
    const bool value = home_;
    home_ = false;
    return value;
  }

 private:
  void refresh_rows();
  void touched();
  Settings* settings_{};
  OptionsCategory category_{OptionsCategory::keybinds};
  std::string query_;
  std::vector<const OptionDef*> rows_;
  std::string capture_;
  std::uint64_t layout_revision_{};
  std::uint64_t value_revision_{};
  bool changed_{};
  bool home_{};
};

// Whether a row matches a search: every word of the query, case-insensitively, appears in
// its label, section or category name.
[[nodiscard]] bool option_matches(const OptionDef& option, std::string_view query);
}
