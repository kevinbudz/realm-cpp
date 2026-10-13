#pragma once
#include "realm/ui/fonts/FontBook.hpp"
#include "realm/hud/Hud.hpp"
#include "realm/assets/HudArt.hpp"
#include "realm/messaging/Messages.hpp"
#include <optional>
#include <utility>

namespace realm {
// TradePanel/TradeInventory/TradeButton and TradeRequestPanel state. The
// server owns the trade; offers sent here are requests it may sanitize.
class Trade {
 public:
  static constexpr std::size_t slots = 12;
  // TradeRequestPanel's Timer and TradeButton.WAIT_TIME.
  static constexpr std::int32_t request_time = 20000, wait_time = 3000;
  // TradeInventory message constants.
  enum class Message { click_items, no_space, accepted, waiting };
  enum class Button { countdown, normal, waiting, disabled };
  struct Side {
    std::string name;
    std::array<net::TradeItem, slots> items{};
    Message message{Message::click_items};
    bool operator==(const Side&) const = default;
  };
  // TRADEREQUESTED shows TradeRequestPanel (showTradePopup defaults on).
  void requested(std::string name, std::int32_t time) {
    request_ = std::move(name);
    request_time_ = time;
    ++revision_;
  }
  [[nodiscard]] const std::optional<std::string>& request() const { return request_; }
  // Reject, Accept or the 20-second timer close the request panel.
  void close_request() {
    if (request_) {
      request_.reset();
      ++revision_;
    }
  }
  void update(std::int32_t time);
  // HUDView.startTrade replaces the HUD's lower panels with TradePanel.
  void start(const net::incoming::TradeStart& start, std::string my_name, std::int32_t time);
  void changed(const net::incoming::TradeChanged& changed, std::int32_t time);
  void accepted(const net::incoming::TradeAccepted& accepted);
  // TRADEDONE, Cancel, or leaving the map.
  void end() {
    if (active_ || request_) {
      active_ = false;
      request_.reset();
      ++revision_;
    }
  }
  [[nodiscard]] bool active() const { return active_; }
  [[nodiscard]] const Side& mine() const { return sides_[0]; }
  [[nodiscard]] const Side& theirs() const { return sides_[1]; }
  // TradeInventory.onSlotClick on a tradeable slot of mine: the CHANGETRADE offer.
  [[nodiscard]] std::optional<std::vector<bool>> toggle(std::size_t index, std::int32_t time);
  // TradeButton click when enabled: ACCEPTTRADE's (mine, theirs).
  [[nodiscard]] std::optional<std::pair<std::vector<bool>, std::vector<bool>>> accept(std::int32_t time);
  // Stage ACTIVATE restarts the countdown, even for a trade without space.
  void activate(std::int32_t time) {
    if (active_) {
      reset_button(time);
      ++revision_;
    }
  }
  [[nodiscard]] Button button() const { return button_; }
  // Countdown progress from 0 to 1.
  [[nodiscard]] double countdown(std::int32_t time) const;
  [[nodiscard]] std::uint64_t revision() const { return revision_; }
  [[nodiscard]] static std::vector<bool> offer(const Side& side);

 private:
  void check(std::int32_t time);
  void reset_button(std::int32_t time) {
    button_ = Button::countdown;
    button_reset_ = time;
  }
  bool active_{};
  std::array<Side, 2> sides_;
  Button button_{Button::disabled};
  std::int32_t button_reset_{}, request_time_{};
  std::optional<std::string> request_;
  std::uint64_t revision_{};
};

// Renders TradePanel over the HUD from y 190 and TradeRequestPanel over the
// interaction panel, in HUD logical coordinates.
class TradeView {
 public:
  static constexpr int width = 200, panel_top = 190, panel_height = 410, request_top = 500, request_height = 100;
  // TradePanel origin within the HUD and its button areas.
  static constexpr double origin_y = 200;
  static constexpr HudRect cancel_button{10, origin_y + 364, 80, 26}, trade_button{110, origin_y + 364, 80, 26};
  TradeView(const FontBook& fonts, const HudArt& art) : fonts_(fonts), art_(art) {}
  // Rebuilds when trade state, hover or the countdown bar width changes.
  bool update(const Trade& trade, Vec2 pointer, std::int32_t time);
  [[nodiscard]] const Image& panel() const { return panel_; }
  [[nodiscard]] const Image& request() const { return request_; }
  // My slot under a HUD point, or -1.
  [[nodiscard]] static int slot_at(Vec2 point);
  // TradeRequestPanel buttons for a requester name.
  struct RequestButtons {
    HudRect reject, accept;
  };
  [[nodiscard]] RequestButtons request_buttons() const;
  // Their non-empty slot under a HUD point for tooltips, or -1.
  [[nodiscard]] static int their_slot_at(Vec2 point);

 private:
  void render_panel(const Trade& trade, std::int32_t time);
  void render_request(const std::string& name);
  void text_button(Image& image, HudRect area, double top, std::string_view label, bool hover);
  const FontBook& fonts_;
  const HudArt& art_;
  std::uint64_t revision_{~0ULL};
  int hover_{-1}, bar_{-1};
  double scale_{};
  std::string request_name_;
  Image panel_, request_;
};
}
