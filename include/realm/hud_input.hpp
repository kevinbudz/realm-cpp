#pragma once
#include "realm/hud.hpp"
#include <optional>
namespace realm {
enum class InventoryActionKind { swap, drop, use, equip, transfer, store, potion };
// For potion actions, source is the PotionInventoryView position (0 health, 1 magic).
// Slots from container_slot address the nearby container's ContainerGrid.
struct InventoryAction {
  InventoryActionKind kind{};
  int source{-1}, target{-1};
};
// Coordinates are HUD logical pixels; window resizing is handled at the SDL
// boundary. The source's single-click event has no inventory action.
class HudInput {
 public:
  [[nodiscard]] static int slot_at(Vec2 point, int selected, bool container = false);
  // Mouse picking covers the cut-edge background and the icon bitmap
  // bounds; the count TextField is not mouse-enabled.
  [[nodiscard]] static int potion_at(Vec2 point, int selected);
  // A TextButton's cut-edge shape; its label is not mouse-enabled.
  [[nodiscard]] static bool button_at(HudRect button, Vec2 point);
  [[nodiscard]] static bool portal_button_at(Vec2 point) { return button_at(portal_button, point); }
  // TextButton CLICK needs the press and release on the same visible
  // button; a replaced panel is a different button.
  void button_down(Vec2 point, const HudState::PanelButton& button) {
    pressed_button_ = button.id >= 0 && button_at(button.area, point) ? button.id : -1;
  }
  [[nodiscard]] bool button_up(Vec2 point, const HudState::PanelButton& button) {
    const auto pressed = pressed_button_;
    pressed_button_ = -1;
    return pressed >= 0 && pressed == button.id && button_at(button.area, point);
  }
  void down(Vec2 point, const HudState::Inventory& state);
  void move(Vec2 point, int selected, bool container = false);
  [[nodiscard]] std::optional<InventoryAction> up(Vec2 point, bool shift, bool control, std::int32_t time,
                                                  const HudState::Inventory& state);
  void cancel(bool reset_tabs = false);
  // Replacing the interaction panel removes its tiles and their drag or click state.
  void container_changed();
  void cycle_tabs(bool backpack);
  [[nodiscard]] int selected() const { return selected_; }
  [[nodiscard]] int dragging() const { return dragging_ ? down_slot_ : -1; }
  [[nodiscard]] Vec2 pointer() const { return pointer_; }

 private:
  [[nodiscard]] static int tab_at(Vec2 point, bool backpack);
  int selected_{}, down_slot_{-1}, click_slot_{-1}, down_tab_{-1}, click_potion_{-1};
  std::int32_t click_time_{}, potion_time_{}, pressed_button_{-1};
  Vec2 start_{}, pointer_{};
  bool dragging_{};
};
}
