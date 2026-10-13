#pragma once
#include "realm/hud/Hud.hpp"
#include "realm/messaging/GameServerConnection.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace realm {
class Catalog;
class FontBook;
class HudArt;
class SoundCues;
class World;
struct CharList;
namespace net::incoming {
struct Death;
}

struct StartupOptions {
  // Parameters.SERVER_ADDRESS, APP_PORT, GAME_PORT and BUILD_VERSION.
  std::string host{"127.0.0.1"}, build_version{"1.0.0"};
  std::uint16_t app_port{7777}, game_port{2050};
  std::filesystem::path account_file;
};
enum class StartupKey { backspace, tab, enter, escape };
struct StartupAction {
  enum class Kind {
    none,
    play,
    account,
    legends,
    editor,
    login,
    register_account,
    back,
    classes,
    new_character,
    buy_slot,
    character,
    class_type,
    cancel,
    submit,
    link,
    field,
    legend_tab,
    legend_row,
    delete_character,
    account_logout
  };
  Kind kind{Kind::none};
  std::size_t index{};
  friend bool operator==(const StartupAction&, const StartupAction&) = default;
};
struct StartupFieldSnapshot {
  std::size_t index{};
  std::string label, value, error;
  bool password{};
  Vec2 position{}, size{};      // Drawable-pixel bounds of the editable field.
  std::size_t insert_before{};  // Element index: after the field background and before its native value text.
  bool focused{};
};
struct StartupElementSnapshot {
  HudRegion source{};
  Vec2 position{}, size{}, text_position{}, pulse_center{};  // Drawable pixels.
  std::array<float, 4> color{1, 1, 1, 1}, font_color{1, 1, 1, 1}, hover_color{};
  std::optional<StartupAction> action;
  std::string text;
  int font_size{};
  bool solid{}, full_width{}, hit{}, hovered{}, pulse{}, font_bold{}, font_shadow{}, field_value{};
  std::size_t field_index{};
};
struct StartupViewSnapshot {
  std::uint64_t revision{}, structure_revision{}, sheet_revision{};
  int width{}, height{};
  double scale{1};
  std::vector<StartupElementSnapshot> elements;
  std::vector<StartupFieldSnapshot> fields;
  std::vector<HudRegion> sheet_dirty;
  std::size_t dialog_insertion{};
  bool characters{}, busy{}, menus{}, dialog{}, skin_visible{}, skin_modal_input{}, fame{}, legends{}, deletion{};
  const Image* skin_image{};
  std::uint64_t skin_revision{};
  std::vector<HudRegion> skin_dirty;
};

// Startup coordinates the account, character, fame and legends screens. Its public API remains the boundary used by
// the application and RmlUi adapter; the implementation owns separate screen state, HTTP task and view-model parts.
class Startup {
 public:
  static constexpr int sheet_size = 1024;
  static constexpr double background_angle = 7 * 3.14159265358979323846 / 4;

  Startup(const FontBook& fonts, const HudArt* art, const Catalog& catalog, const std::filesystem::path& flash_root,
          StartupOptions options);
  ~Startup();
  Startup(const Startup&) = delete;
  Startup& operator=(const Startup&) = delete;

  // Reserved atlas image holding every rasterized menu element.
  [[nodiscard]] const Image& sheet() const;
  // MapBackground: the embedded map, written twice with a 10-tile border.
  void load_background(World& world);
  [[nodiscard]] bool menus() const;
  [[nodiscard]] bool background() const;
  [[nodiscard]] Vec2 background_position() const;
  // Pointer coordinates are window pixels in the same space as width/height.
  void pointer_move(Vec2 position);
  void pointer_down(Vec2 position);
  void pointer_up(Vec2 position);
  void cancel_pointer();
  bool wheel(double steps);
  void text(std::string_view utf8);
  // Returns false when the menus do not use the key.
  bool key(StartupKey key);
  [[nodiscard]] bool wants_text() const;
  void update(std::int32_t time_ms, double dt_ms, double width, double height);
  // Set once Play resolves to a character; the caller starts the session.
  [[nodiscard]] std::optional<SessionOptions> take_launch();
  void entered_game();
  [[nodiscard]] std::optional<std::string> take_skin_error();
  [[nodiscard]] const CharList* char_list() const;
  // Retained view data for the incremental RmlUi adapter. Coordinates are final drawable pixels.
  [[nodiscard]] const StartupViewSnapshot& view() const;
  // Typed action entry point used by the RmlUi view.
  void perform(StartupAction action);
  // Keep the controller's credential and validation model synchronized with an editable RmlUi input.
  void set_field_value(std::size_t index, std::string value);
  [[nodiscard]] std::string field_value(std::size_t index) const;
  // The caller acknowledges the RmlUi sheet upload after submitting the matching revision.
  void acknowledge_sheet_upload(std::uint64_t revision);
  void acknowledge_skin_upload(std::uint64_t revision);
  void renamed_account(const std::string& name);
  // Session ended (death, failure, disconnect or leaving); returns to the character screen with fresh data.
  void left_game(const std::string& message);
  void died(const net::incoming::Death& death, std::int32_t time_ms);
  // Sounds requested by menu actions (button_click on a click, death_screen when the fame view opens), drained per
  // frame.
  [[nodiscard]] SoundCues& sound_cues();
  // TitleMenuOption.onMouseClick and ClickableText.onMouseClick: a mouse click on a menu item plays button_click.
  void play_click();
  [[nodiscard]] double death_fade(std::int32_t time_ms) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
