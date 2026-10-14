#pragma once
#include "realm/util/Image.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace realm {
// Davy Jones's Locker keys in KeysView's order.
enum class DavyKey { purple, green, red, yellow };

// KeysView: the panel of Davy Jones's Locker keys. GlobalNotification "showKeyUI" opens it and the key colors light its
// keys. Keys light only while the panel is open, and the panel is reopened empty.
class KeysView {
 public:
  // ShowKeyUICommand: a panel that is already open is not stacked.
  void show() {
    if (in_locker_ && !visible_) {
      visible_ = true;
      ++revision_;
    }
  }
  // ShowKeySignal reaches no panel while none is open.
  void light(DavyKey key) {
    if (visible_ && !lit_[index(key)]) {
      lit_[index(key)] = true;
      ++revision_;
    }
  }
  // Hiding or clearing the panel changes its retained rendering.
  void reset() {
    if (visible_ || lit_ != std::array<bool, 4>{}) {
      visible_ = false;
      lit_ = {};
      ++revision_;
    }
  }
  // The C++ game closes the key view when the server moves the player out of Davy's Locker.
  void map_changed(std::string_view name) {
    in_locker_ = name == "Davy Jones's Locker";
    if (!in_locker_) reset();
  }
  // GameClosedSignal removes the view and forgets its map context.
  void game_closed() {
    reset();
    in_locker_ = false;
  }
  // GlobalNotification dispatches either the display signal or one of the four key signals.
  void notification(std::string_view text) {
    if (text == "showKeyUI")
      show();
    else if (const auto key = key_named(text))
      light(*key);
  }
  [[nodiscard]] bool visible() const { return visible_; }
  [[nodiscard]] bool in_locker() const { return in_locker_; }
  [[nodiscard]] bool lit(DavyKey key) const { return lit_[index(key)]; }
  [[nodiscard]] std::uint64_t revision() const { return revision_; }
  // The key a GlobalNotification text names: "yellow", "red", "green" or "purple".
  [[nodiscard]] static std::optional<DavyKey> key_named(std::string_view text);

 private:
  static std::size_t index(DavyKey key) { return static_cast<std::size_t>(key); }
  bool visible_{};
  bool in_locker_{};
  std::array<bool, 4> lit_{};
  std::uint64_t revision_{};
};

// Margin of the panel from the top-left corner of the game view, in logical pixels before the window scale.
inline constexpr double keys_view_margin = 4;

// KeysView's background with each lit key drawn in its slot, in straight alpha at logical size.
[[nodiscard]] Image render_keys_view(const KeysView& view, double density);
}  // namespace realm
