#include "realm/trade.hpp"
#include <algorithm>
#include <iostream>

namespace {
int checks{};
void check(bool value, const char* reason) {
  ++checks;
  if (!value) throw std::runtime_error(reason);
}
}
int main() {
  try {
    using namespace realm;
    using M = Trade::Message;
    using B = Trade::Button;
    const auto items = [](std::vector<int> types, std::vector<bool> tradeable) {
      std::vector<net::TradeItem> result;
      for (std::size_t i = 0; i < 12; ++i)
        result.push_back({types[i], i < 4 ? 1 : 0, static_cast<bool>(tradeable[i]), false});
      return result;
    };
    Trade trade;
    trade.requested("Friend", 0);
    trade.update(19999);
    check(trade.request() == "Friend", "request panel stays for 20 seconds");
    trade.update(20000);
    check(!trade.request(), "request panel closes after 20 seconds");
    trade.requested("Friend", 0);
    // Mine: four equipped items, two potions, six empty inventory slots.
    net::incoming::TradeStart start{
        items({1, 2, 3, 4, 10, 11, -1, -1, -1, -1, -1, -1},
              {false, false, false, false, true, true, false, false, false, false, false, false}),
        "Friend",
        items({5, 6, 7, 8, 12, 13, 14, 15, 16, 17, 18, -1},
              {false, false, false, false, true, true, true, true, true, true, true, false})};
    trade.start(start, "Me", 1000);
    check(trade.active() && !trade.request() && trade.mine().name == "Me" && trade.theirs().name == "Friend",
          "TRADESTART opens the panel and replaces the request");
    check(trade.button() == B::countdown && trade.mine().message == M::click_items &&
              trade.theirs().message == M::waiting,
          "valid trade starts the countdown");
    check(!trade.accept(2000), "Trade cannot be clicked during the countdown");
    check(!trade.toggle(0, 1500), "equipped slots are not tradeable");
    auto offer = trade.toggle(4, 1500);
    check(offer && offer->at(4) && std::count(offer->begin(), offer->end(), true) == 1,
          "toggling a tradeable slot sends a twelve-slot offer");
    check(trade.button() == B::countdown && trade.countdown(3000) == 0.5,
          "changing the offer restarts the three-second countdown");
    trade.update(4499);
    check(trade.button() == B::countdown, "countdown still running");
    trade.update(4500);
    check(trade.button() == B::normal, "Trade enables when the countdown completes");
    // They offer seven items; I have six empty slots plus one offered: room for seven.
    trade.changed({std::vector<bool>{false, false, false, false, true, true, true, true, true, true, true, false}},
                  5000);
    check(trade.mine().message == M::click_items && trade.button() == B::countdown, "exactly enough space is valid");
    (void)trade.toggle(4, 5000);
    check(trade.mine().message == M::no_space && trade.button() == B::disabled,
          "receiving more than free space disables Trade");
    trade.activate(5100);
    check(trade.button() == B::countdown, "window activation restarts the countdown even without space");
    (void)trade.toggle(4, 6000);
    trade.update(9000);
    const auto accepted = trade.accept(9000);
    check(accepted && accepted->first.at(4) && accepted->second.at(10) && trade.button() == B::waiting &&
              trade.mine().message == M::accepted,
          "accepting sends both offers and waits");
    check(!trade.accept(9500), "a waiting button cannot accept again");
    trade.accepted({Trade::offer(trade.mine()), std::vector<bool>(12, false)});
    check(trade.theirs().message != M::accepted, "their acceptance of other offers is ignored");
    trade.accepted({Trade::offer(trade.mine()), Trade::offer(trade.theirs())});
    check(trade.theirs().message == M::accepted, "matching acceptance shows Trade accepted!");
    trade.changed({std::vector<bool>(12, false)}, 9600);
    check(trade.mine().message == M::click_items && trade.theirs().message == M::waiting &&
              trade.button() == B::countdown,
          "a changed offer clears both acceptances");
    trade.end();
    check(!trade.active(), "TRADEDONE or Cancel closes the panel");
    check(!trade.toggle(4, 10000), "no offers after the trade ends");
    {
      FontBook fonts;
      HudArt art;
      TradeView view(fonts, art);
      trade.start(start, "Me", 0);
      check(view.update(trade, {0, 0}, 0) && !view.update(trade, {0, 0}, 0), "panel redraws only on change");
      check(view.update(trade, {0, 0}, 1500), "countdown bar progress redraws");
      check(TradeView::slot_at({14, 246}) == 0 && TradeView::slot_at({14 + 44 * 3 + 39, 246 + 44 * 2 + 39}) == 11 &&
                TradeView::slot_at({13, 246}) == -1,
            "my slot grid hit areas");
      check(TradeView::their_slot_at({14, 420}) == 0 && TradeView::slot_at({14, 420}) == -1, "their grid is separate");
      check(view.update(trade, {20, 570}, 1500), "hovering Cancel redraws");
      trade.end();
      trade.requested("Friend", 0);
      view.update(trade, {0, 0}, 0);
      const auto buttons = view.request_buttons();
      check(buttons.reject.y == 562 && buttons.accept.x > buttons.reject.x + buttons.reject.width,
            "request buttons sit at the panel bottom");
    }
    std::cout << "Trade checks: " << checks << '\n';
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
