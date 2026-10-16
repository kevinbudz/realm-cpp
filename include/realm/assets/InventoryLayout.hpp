#pragma once
// Inventory side-panel tab layout, shared by the HUD art (HudArt.cpp) and the tab hit tests
// (HudInput.cpp). Coordinates are backdrop-local: the backdrop is drawn at HUD x 0 and HUD y 300.
namespace realm {
// The tab strip spans the straight part of the content panel's top edge, between its top
// chamfers (the panel's outer light is cut 8 from x 8): x 16 up to, not including, 184.
inline constexpr int inventory_tab_left = 16;
inline constexpr int inventory_tab_right = 184;
inline constexpr int inventory_tab_gap = 2;
// Tabs cover rows 56..77 (bottom exclusive 78), and that is the tab hit range. Their bottoms stand
// on the content panel's light top band (rows 78..79). A selected tab's solid fill runs through
// those rows; they are not a tab hit. The tabs are 6 px below the equipped box, whose last row is 49.
inline constexpr int inventory_tab_top = 56;
inline constexpr int inventory_tab_bottom = 78;
// HUD y of the backdrop's top row.
inline constexpr int inventory_panel_y = 300;
// The backdrop covers HUD rows 300..503. The panel's bottom outline ends at row 201 (HUD 501), and the
// rows under it are the lower panel's background.
inline constexpr int inventory_backdrop_height = 204;

struct InventoryTabSlot {
  int x{};
  int width{};
};

// Tab `index` of `count` tabs (2 without a backpack, 3 with one). The widths share the strip evenly
// and the remainder pixels go to the leftmost tabs. The first tab starts at inventory_tab_left and
// the last one ends at inventory_tab_right.
[[nodiscard]] constexpr InventoryTabSlot inventory_tab_rect(int count, int index) {
  const int total = inventory_tab_right - inventory_tab_left - inventory_tab_gap * (count - 1);
  const int base = total / count;
  const int extra = total % count;
  int x = inventory_tab_left;
  for (int tab = 0; tab < index; ++tab) {
    x += base + (tab < extra ? 1 : 0) + inventory_tab_gap;
  }
  return {x, base + (index < extra ? 1 : 0)};
}
}  // namespace realm
