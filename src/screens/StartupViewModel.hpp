#pragma once

#include "realm/objects/Catalog.hpp"
#include "realm/assets/FontBook.hpp"
#include "realm/hud/Hud.hpp"
#include "realm/assets/HudArt.hpp"
#include "realm/ui/tooltip/SavedCharacterTooltip.hpp"
#include "realm/ui/tooltip/RankToolTip.hpp"
#include "realm/screens/Startup.hpp"
#include "StartupState.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace realm::startup_detail {
class StartupViewModel {
 public:
  StartupViewModel(const FontBook& fonts, const HudArt* art, const Catalog& catalog, std::string build_version);

  [[nodiscard]] const Image& sheet() const { return sheet_; }
  [[nodiscard]] const StartupViewSnapshot& snapshot() const { return view_; }
  [[nodiscard]] double width() const { return width_; }
  [[nodiscard]] double height() const { return height_; }
  [[nodiscard]] double list_height() const { return list_height_; }
  [[nodiscard]] bool dirty() const { return dirty_; }
  [[nodiscard]] bool pending() const { return view_pending_; }
  [[nodiscard]] bool has_character_tooltip() const { return character_tooltip_element_.has_value(); }
  [[nodiscard]] bool has_rank_tooltip() const { return rank_tooltip_element_.has_value(); }

  void set_logo(Image logo) { logo_ = std::move(logo); }
  void reset_character_tooltip();
  bool set_window(double width, double height);
  bool set_pixel_scale(double pixel_scale);
  void invalidate() { dirty_ = true; }
  void request_refresh() { view_pending_ = true; }
  void rebuild(StartupScreenState& state, bool busy);
  void refresh(const StartupScreenState& state, bool busy);
  int hit(const StartupScreenState& state, Vec2 point) const;
  void sync_field(std::size_t index, const StartupScreenState& state);
  void position_character_tooltip(const StartupScreenState& state);
  void position_rank_tooltip(const StartupScreenState& state);
  [[nodiscard]] double death_fade(const StartupScreenState& state, std::int32_t time) const;
  void acknowledge_sheet_upload(std::uint64_t revision);
  void acknowledge_skin_upload(std::uint64_t revision);
  void skin_changed(HudRegion region);

 private:
  enum class Anchor { content, window };
  struct Element {
    HudRegion source{};
    const Image* bitmap{};
    std::uint64_t bitmap_revision{};
    // A hit element is the invisible input rectangle of its group.
    bool solid{}, pulse{}, full_width{}, hit{};
    std::array<float, 4> color{1, 1, 1, 1}, over{};
    Anchor anchor{Anchor::content};
    // Content: design coordinates. Window: anchor fraction, scaled offset and unscaled pixels.
    Vec2 position{}, anchor_fraction{}, pixels{}, size{}, pulse_center{};
    int group{-1};
    std::string text;
    Vec2 text_origin{};
    std::array<float, 4> font_color{1, 1, 1, 1};
    int font_size{};
    bool font_bold{}, font_shadow{};
    bool field_value{};
    std::size_t field_index{};
  };

  void reset_sheet();
  HudRegion store(const Image& image);
  Element& add_solid(double x, double y, double w, double h, std::uint32_t color, double alpha = 1, int group = -1);
  // Adds a SimpleText; returns its TextField width and height.
  Vec2 add_text(std::string_view value, int size, bool bold, std::uint32_t color, double x, double y, int group = -1,
                bool shadow = true);
  Vec2 add_button(std::string_view label, int size, double x, double y, int action, bool pulse = false,
                  bool center = false);
  void add_image(const Image& image, double x, double y, double scale = 1, int group = -1);
  void add_bitmap(const Image& image, std::uint64_t revision, double x, double y, double width, double height);
  void add_window_text(std::string_view value, int size, bool bold, std::uint32_t color, Vec2 anchor, Vec2 offset,
                       Vec2 pixels, bool align_right, bool align_bottom, int action = -1);
  void menu_frame(bool background);
  void menu_backdrop(bool background);
  void menu_bottom_bar();
  void sound_icon();
  void loading_screen(std::string_view text);
  void fame_screen(StartupScreenState& state, bool busy);
  void legends_screen(StartupScreenState& state, bool busy);
  void account_header(const StartupScreenState& state);
  void title_screen(const StartupScreenState& state);
  void character_screen(StartupScreenState& state);
  void class_screen(const StartupScreenState& state);
  void skin_screen(const StartupScreenState& state);
  void dialog_frame(StartupScreenState& state, bool busy);
  [[nodiscard]] std::optional<StartupAction> typed_action(const StartupScreenState& state, int action) const;
  [[nodiscard]] Vec2 to_window(const Element& element, Vec2 point) const;

  const FontBook& fonts_;
  const HudArt* art_;
  const Catalog& catalog_;
  std::string build_version_;
  Image sheet_{Startup::sheet_size, Startup::sheet_size,
               std::vector<unsigned char>(static_cast<std::size_t>(Startup::sheet_size) * sheet_.width * 4)};
  Image logo_;
  int sheet_x_{1}, sheet_y_{1}, sheet_row_{}, sheet_used_{};
  std::vector<HudRegion> changed_regions_;
  std::vector<Element> elements_;
  StartupViewSnapshot view_;
  std::vector<StartupFieldSnapshot> field_views_;
  std::vector<HudRegion> view_uploads_;
  SavedCharacterTooltipImage character_tooltip_;
  int character_tooltip_id_{-1};
  double character_tooltip_density_{};
  // The saved-character and class tooltips share one element; tooltip_frame_ is the frame of whichever is shown.
  Vec2 tooltip_frame_{};
  std::optional<std::size_t> character_tooltip_element_;
  RankToolTipImage rank_tooltip_;
  int rank_tooltip_stars_{-1};
  double rank_tooltip_density_{};
  std::optional<std::size_t> rank_tooltip_element_;
  bool play_sfx_{true};
  double list_height_{};
  double width_{800}, height_{600};
  std::size_t dialog_begin_{};
  std::uint64_t view_revision_{}, structure_revision_{}, sheet_revision_{}, skin_revision_{};
  HudRegion pending_skin_change_{};
  bool dirty_{true}, view_pending_{}, sheet_replaced_{};
};
}
