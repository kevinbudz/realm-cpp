#pragma once
#include "realm/ui/fonts/FontBook.hpp"
#include "realm/hud/Hud.hpp"
#include "realm/messaging/Messages.hpp"
#include <array>
#include <optional>

namespace realm {
// Native Dialog/TextButton. Coordinates remain in the 600-high design
// space; the retained bitmap is rebuilt at drawable density. This component
// has no backdrop; its owner chooses a modal layer (DialogsView uses .6).
class Dialog {
 public:
  static constexpr double width = 300, padding = 16;
  struct Content {
    std::string body;
    std::optional<std::string> title;
    std::array<std::optional<std::string>, 2> buttons;
  };
  void show(Content content);
  void message_close(std::string title, std::string body, std::string button = "Close");
  // onBuyResult: true only for the result handled by MessageCloseDialog.
  bool purchase_result(const net::incoming::BuyResult& result);
  void close();
  [[nodiscard]] bool visible() const { return visible_; }
  [[nodiscard]] const Content& content() const { return content_; }
  void set_enabled(int button, bool enabled);
  void set_base_alpha(double alpha);
  void set_offset(Vec2 offset) {
    offset_ = offset;
    dirty_ = true;
    focus_lost();
  }
  void focus_lost();
  // Geometry is ready after update. Pointer positions are drawable pixels.
  // A button event is returned only after a press/release on the same
  // enabled button. MessageCloseDialog also closes on button 1.
  void pointer_move(Vec2 point);
  void pointer_down(Vec2 point);
  [[nodiscard]] std::optional<int> pointer_up(Vec2 point);
  bool update(const FontBook& fonts, double stage_width, double stage_height);
  [[nodiscard]] const Image& image() const { return image_; }
  [[nodiscard]] double height() const { return height_; }
  [[nodiscard]] double scale() const { return scale_; }
  [[nodiscard]] Vec2 position() const { return position_; }
  [[nodiscard]] Vec2 canvas_position() const {
    return {position_.x - padding * scale_, position_.y - padding * scale_};
  }
  [[nodiscard]] HudRect button(int index) const { return buttons_.at(static_cast<std::size_t>(index)); }

 private:
  [[nodiscard]] int hit(Vec2 point) const;
  Content content_;
  bool visible_{}, message_close_{}, dirty_{true}, pointer_valid_{};
  std::array<bool, 2> enabled_{true, true};
  std::array<HudRect, 2> buttons_{};
  int hovered_{-1}, pressed_{-1};
  double height_{}, scale_{1}, stage_width_{}, stage_height_{}, alpha_{1};
  Vec2 position_{}, offset_{}, pointer_{};
  Image image_;
};
}
