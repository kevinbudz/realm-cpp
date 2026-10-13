#pragma once
#include "realm/hud/HudState.hpp"
#include "realm/ui/fonts/FontBook.hpp"
#include "realm/map/World.hpp"
#include "realm/assets/HudArt.hpp"
#include "realm/objects/Party.hpp"
#include <array>
#include <optional>
#include <string_view>
namespace realm {
// PotionInventoryView layout below the 84-pixel InventoryGrid.
inline constexpr int potion_view_y = 468;
inline constexpr std::array<int, 2> potion_view_x{14, 102};
inline constexpr std::array<int, 2> potion_types{2594, 2595};
// The empty 172-pixel ContainerGrid is centered in the panel at HUD y 500.
inline constexpr int container_x = 14, container_y = 508;
// The IconButton's 40x40 bitmap, 12 units up and left of the button, in HUD coordinates.
inline constexpr HudRect header_button{160, 190, 40, 40};
[[nodiscard]] std::optional<TextBitmap> tier_bitmap(const FontBook& fonts, const ObjectDef& item);
[[nodiscard]] Image rank_star_bitmap(const FontBook& fonts, const HudArt& art, int stars);

// Persistent CPU panel. It is rebuilt only when visible character stats change.
// Finite original icon/decorative art is optional for meter-only fixtures.
class Hud {
 public:
  explicit Hud(const FontBook& fonts, const HudArt* art = nullptr, const Catalog* catalog = nullptr);
  bool update(const HudState& state);
  [[nodiscard]] const Image& image() const { return image_; }
  [[nodiscard]] std::uint64_t rebuilds() const { return rebuilds_; }
  [[nodiscard]] const std::vector<HudRegion>& changes() const { return changes_; }

 private:
  void blit(const Image& image, int x, int y, HudRegion clip = {0, 0, 200, 600});
  void inventory(const HudState::Inventory& state);
  void potions(const std::array<int, 2>& counts);
  void container(const HudState::Inventory& state);
  void portal(const HudState::Portal& state);
  void button(const HudState::Button& state);
  void sellable(const HudState::Sellable& state);
  void party(const HudState::Party& state);
  void social(const HudState::Social& state);
  void native_button(HudRect area, std::string_view label, bool hover);
  void tier(const ObjectDef& item, int x, int y);
  void draw(TextBitmap bitmap, double x, double y, double alpha = 1, const ColorTransform* transform = nullptr);
  // An unfiltered TextField renders straight into the stage rather than
  // through a filter bitmap, interpolating toward its color by coverage.
  void draw_direct(const TextBitmap& bitmap, double x, double y, std::uint32_t color);
  void tile(const HudState::Inventory& state, int index, int position, int x, int y, bool equipped);
  void stats(const std::array<HudStatState, 6>& state);
  void heading(const HudState& state);
  void rectangle(double x, int y, double width, int height, std::uint32_t color);
  void text(std::string_view value, int size, bool bold, std::uint32_t color, double x, double y, double alpha = 1,
            unsigned shadow_strength = 1, int field_width = 0);
  void bar(const StatusBarState& state);
  const FontBook& fonts_;
  const HudArt* art_{};
  const Catalog* state_catalog_{};
  Image image_{200, 600, std::vector<unsigned char>(200 * 600 * 4)};
  HudState state_;
  bool initialized_{};
  std::uint64_t rebuilds_{};
  std::vector<HudRegion> changes_;
};
}
