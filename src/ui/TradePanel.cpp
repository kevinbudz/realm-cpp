#include "realm/ui/TradePanel.hpp"
#include "realm/util/GraphicsUtil.hpp"
#include <algorithm>
#include <cmath>

namespace realm {
namespace {
// TradeInventory placement within TradePanel and slot pitch.
constexpr double inventory_x = 14, their_y = 174, slot_y = 46, pitch = 44;
constexpr std::uint32_t background = 3552822, neutral = 11776947, error_red = 16711680, accepted_green = 9022300;
Vec2 slot_origin(int side, std::size_t index) {
  return {inventory_x + static_cast<double>(index % 4) * pitch,
          TradeView::origin_y + side * their_y + slot_y + static_cast<double>(index / 4) * pitch};
}
int slot_index(Vec2 point, int side) {
  for (std::size_t i = 0; i < Trade::slots; ++i) {
    const auto at = slot_origin(side, i);
    if (point.x >= at.x && point.x < at.x + 40 && point.y >= at.y && point.y < at.y + 40) return static_cast<int>(i);
  }
  return -1;
}
bool inside(HudRect area, Vec2 p) {
  return p.x >= area.x && p.x < area.x + area.width && p.y >= area.y && p.y < area.y + area.height;
}
}

void Trade::update(std::int32_t time) {
  if (request_ && time - request_time_ >= request_time) close_request();
  // TradeButton.draw enables the button once the countdown completes.
  if (active_ && button_ == Button::countdown && time - button_reset_ >= wait_time) {
    button_ = Button::normal;
    ++revision_;
  }
}

void Trade::start(const net::incoming::TradeStart& start, std::string my_name, std::int32_t time) {
  active_ = true;
  request_.reset();
  sides_[0] = Side{std::move(my_name)};
  sides_[1] = Side{start.their_name};
  for (std::size_t i = 0; i < slots; ++i) {
    sides_[0].items[i] = i < start.my_items.size() ? start.my_items[i] : net::TradeItem{-1, 0, false, false};
    sides_[1].items[i] = i < start.their_items.size() ? start.their_items[i] : net::TradeItem{-1, 0, false, false};
  }
  check(time);
  // TradeButton.onAddedToStage resets the countdown after the first checkTrade.
  reset_button(time);
  ++revision_;
}

std::vector<bool> Trade::offer(const Side& side) {
  std::vector<bool> result;
  for (const auto& item : side.items) result.push_back(item.included);
  return result;
}

void Trade::check(std::int32_t time) {
  // TradePanel.checkTrade: each side must have room for what it receives.
  const auto count = [](const Side& side, bool empty) {
    int n{};
    for (std::size_t i = 0; i < slots; ++i) n += empty ? (i >= 4 && side.items[i].item == -1) : side.items[i].included;
    return n;
  };
  const int my_trading = count(sides_[0], false), my_empty = count(sides_[0], true);
  const int your_trading = count(sides_[1], false), your_empty = count(sides_[1], true);
  bool valid = true;
  if (your_trading - my_trading - my_empty > 0) {
    sides_[0].message = Message::no_space;
    valid = false;
  } else
    sides_[0].message = Message::click_items;
  if (my_trading - your_trading - your_empty > 0) {
    sides_[1].message = Message::no_space;
    valid = false;
  } else
    sides_[1].message = Message::waiting;
  if (valid)
    reset_button(time);
  else
    button_ = Button::disabled;
}

void Trade::changed(const net::incoming::TradeChanged& changed, std::int32_t time) {
  if (!active_) return;
  for (std::size_t i = 0; i < slots && i < changed.offer.size(); ++i) sides_[1].items[i].included = changed.offer[i];
  check(time);
  ++revision_;
}

void Trade::accepted(const net::incoming::TradeAccepted& accepted) {
  // youAccepted applies only when both offers match what is displayed.
  if (active_ && offer(sides_[0]) == accepted.my_offer && offer(sides_[1]) == accepted.their_offer) {
    sides_[1].message = Message::accepted;
    ++revision_;
  }
}

std::optional<std::vector<bool>> Trade::toggle(std::size_t index, std::int32_t time) {
  if (!active_ || index >= slots || !sides_[0].items[index].tradeable) return {};
  sides_[0].items[index].included = !sides_[0].items[index].included;
  check(time);
  ++revision_;
  return offer(sides_[0]);
}

std::optional<std::pair<std::vector<bool>, std::vector<bool>>> Trade::accept(std::int32_t time) {
  update(time);
  if (!active_ || button_ != Button::normal) return {};
  button_ = Button::waiting;
  sides_[0].message = Message::accepted;
  ++revision_;
  return std::pair{offer(sides_[0]), offer(sides_[1])};
}

double Trade::countdown(std::int32_t time) const {
  return button_ == Button::countdown ? std::clamp((time - button_reset_) / static_cast<double>(wait_time), 0.0, 1.0)
                                      : 0;
}

int TradeView::slot_at(Vec2 point) { return slot_index(point, 0); }
int TradeView::their_slot_at(Vec2 point) { return slot_index(point, 1); }

TradeView::RequestButtons TradeView::request_buttons() const {
  // TextButton(16,label): the field width plus 12; Panel is 188x84 at (6,508).
  const double reject = fonts_.layout("Reject", 16, true).width + 12,
               accept = fonts_.layout("Accept", 16, true).width + 12;
  return {{6 + 47 - reject / 2, 508 + 84 - 26 - 4, reject, 26}, {6 + 141 - accept / 2, 508 + 84 - 26 - 4, accept, 26}};
}

void TradeView::text_button(Image& image, HudRect area, double top, std::string_view label, bool hover) {
  const auto path = cut_edge_rect(area.x, area.y - top, area.width, area.height, 4);
  ui_path(image, path, hover ? 16768133U : 0xffffffU);
  const auto bitmap = fonts_.rasterize(label, 16, true, false, 3552822);
  const double text_x = area.x + area.width / 2 - fonts_.measure(label, 16, true) / 2 - 2;
  ui_blit_text(image, bitmap, text_x + bitmap.offset.x, area.y - top + 1 + bitmap.offset.y, false, true);
}

bool TradeView::update(const Trade& trade, Vec2 pointer, std::int32_t time) {
  int hover = -1;
  if (trade.active()) {
    if (inside(cancel_button, pointer))
      hover = 0;
    else if (inside(trade_button, pointer) && trade.button() == Trade::Button::normal)
      hover = 1;
  } else if (trade.request()) {
    const auto buttons = request_buttons();
    if (inside(buttons.reject, pointer))
      hover = 2;
    else if (inside(buttons.accept, pointer))
      hover = 3;
  }
  const int bar = trade.button() == Trade::Button::countdown
                      ? static_cast<int>(std::lround(trade.countdown(time) * 80 * fonts_.pixel_scale()))
                      : -1;
  if (trade.revision() == revision_ && hover == hover_ && bar == bar_ && scale_ == fonts_.pixel_scale()) return false;
  revision_ = trade.revision();
  hover_ = hover;
  bar_ = bar;
  scale_ = fonts_.pixel_scale();
  if (trade.active())
    render_panel(trade, time);
  else if (trade.request())
    render_request(*trade.request());
  return true;
}

void TradeView::render_panel(const Trade& trade, std::int32_t time) {
  panel_ = ui_image(width, panel_height, fonts_.pixel_scale());
  const double top = panel_top;
  ui_rectangle(panel_, 0, 0, width, panel_height, background, true);
  for (int side = 0; side < 2; ++side) {
    const auto& state = side == 0 ? trade.mine() : trade.theirs();
    const auto color = state.message == Trade::Message::no_space   ? error_red
                       : state.message == Trade::Message::accepted ? accepted_green
                                                                   : neutral;
    const double y = origin_y + side * their_y - top;
    const auto name = fonts_.rasterize(state.name, 20, true, true, color);
    ui_blit_text(panel_, name, inventory_x + name.offset.x, y + name.offset.y, false, true);
    static constexpr std::array<std::string_view, 4> taglines{
        "Click items you want to trade", "Not enough space for trade!", "Trade accepted!", "Player is selecting items"};
    const auto tagline = fonts_.rasterize(taglines[static_cast<std::size_t>(state.message)], 12, false, true, color);
    ui_blit_text(panel_, tagline, inventory_x + tagline.offset.x, y + 22 + tagline.offset.y, false, true);
    for (std::size_t i = 0; i < Trade::slots; ++i) {
      const auto& item = state.items[i];
      const auto at = slot_origin(side, i);
      // Each TradeSlot is composed alone so veryDarkCT darkens only the slot.
      auto slot = ui_image(48, 48, fonts_.pixel_scale());
      const auto key = item.item == -1 ? "tradeempty" + std::to_string(i) + "t" +
                                             std::to_string(i < 4 ? std::clamp(item.slot_type, 0, 30) : 0)
                                       : "tradefill" + std::to_string(i) + (item.included ? "on" : "");
      if (art_.contains(key)) ui_blit(slot, art_.image(key), 0, 0, true, true);
      if (item.item > 0 && art_.contains("item" + std::to_string(item.item))) {
        const auto& icon = art_.image("item" + std::to_string(item.item));
        ui_blit(slot, icon, 4 + 20 - icon.width / 2.0, 4 + 20 - icon.height / 2.0, true, true);
      }
      if (!item.tradeable)
        for (std::size_t p = 0; p < slot.pixels.size(); p += 4)
          for (std::size_t c = 0; c < 3; ++c)
            slot.pixels[p + c] = static_cast<unsigned char>(slot.pixels[p + c] * 2 / 5);
      ui_blit(panel_, slot, at.x - 4, at.y - 4 - top, true, true);
    }
  }
  text_button(panel_, cancel_button, top, "Cancel", hover_ == 0);
  // TradeButton: grey while disabled, counting down or waiting; a lighter bar
  // fills during the countdown inside a white outline.
  const auto button = trade.button();
  const auto area = trade_button;
  const auto path = cut_edge_rect(area.x, area.y - top, area.width, area.height, 4);
  ui_path(panel_, path, button == Trade::Button::normal ? (hover_ == 1 ? 16768133U : 0xffffffU) : 8355711U);
  if (button == Trade::Button::countdown) {
    ui_path(panel_, path, 12566463U, {}, 1, 1, area.x + area.width * trade.countdown(time));
    ui_path(panel_, path, {}, 0xffffffU, 2);
  }
  const std::string_view label = button == Trade::Button::waiting ? "Waiting" : "Trade";
  const auto bitmap = fonts_.rasterize(label, 16, true, false, 3552822);
  ui_blit_text(panel_, bitmap, area.x + area.width / 2 - fonts_.measure(label, 16, true) / 2 - 2 + bitmap.offset.x,
               area.y - top + 1 + bitmap.offset.y, false, true);
  // Convert to straight alpha for upload; the panel is opaque.
}

void TradeView::render_request(const std::string& name) {
  request_name_ = name;
  request_ = ui_image(width, request_height, fonts_.pixel_scale());
  ui_rectangle(request_, 0, 0, width, request_height, background, true);
  // SimpleText(18, white, width 188), centered and word-wrapped, with a shadow.
  const auto text = name + " wants to trade with you";
  const auto title = fonts_.rasterize_centered(text, 18, true, true, 0xffffff, 188);
  ui_blit_text(request_, title, 6 + title.offset.x, 8 + title.offset.y, false, true);
  const auto buttons = request_buttons();
  text_button(request_, buttons.reject, request_top, "Reject", hover_ == 2);
  text_button(request_, buttons.accept, request_top, "Accept", hover_ == 3);
}
}
