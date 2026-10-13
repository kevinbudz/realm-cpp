#include "realm/dialog.hpp"
#include <iostream>
namespace {
int checks{};
void check(bool value, const char* text) {
  ++checks;
  if (!value) throw std::runtime_error(text);
}
}
int main() {
  try {
    using namespace realm;
    FontBook fonts;
    Dialog dialog;
    check(!dialog.purchase_result({0, "Success"}) && !dialog.purchase_result({9, "Error"}) && !dialog.visible(),
          "other BUYRESULT codes remain chat feedback");
    check(dialog.purchase_result({1, "Purchase Error: Insufficient Funds."}) && dialog.visible(),
          "BUYRESULT 1 opens Purchase Error");
    check(dialog.content().title == "Purchase Error" && dialog.content().buttons[0] == "Close" &&
              !dialog.content().buttons[1],
          "MessageClose defaults");
    check(dialog.update(fonts, 800, 600) && dialog.height() == 100 && dialog.button(0).y == 66,
          "source title/body/button metrics");
    check(dialog.position().x == 249.5 && dialog.position().y == 249.5, "outline-inclusive stage centering");
    check(!dialog.update(fonts, 800, 600), "unchanged dialog reuses surface");
    const auto point = [&](int index) {
      const auto b = dialog.button(index);
      const auto p = dialog.position();
      return Vec2{p.x + (b.x + 10) * dialog.scale(), p.y + (b.y + 10) * dialog.scale()};
    };
    check(!dialog.pointer_up(point(0)), "release without press cannot activate");
    dialog.pointer_down({0, 0});
    check(!dialog.pointer_up(point(0)), "outside press cannot activate");
    dialog.pointer_down(point(0));
    check(!dialog.pointer_up({0, 0}) && dialog.visible(), "drag out does not close");
    dialog.pointer_down(point(0));
    dialog.focus_lost();
    check(!dialog.pointer_up(point(0)), "focus loss cancels press");
    dialog.pointer_down(point(0));
    dialog.set_enabled(0, false);
    check(!dialog.pointer_up(point(0)) && dialog.visible(), "disabling cancels pressed button");
    dialog.set_enabled(0, true);
    dialog.pointer_down(point(0));
    fonts.set_pixel_scale(2);
    dialog.update(fonts, 1600, 1200);
    check(!dialog.pointer_up(point(0)), "resize cancels stale press");
    check(dialog.image().pixel_scale == 2 && dialog.image().width == 664 && dialog.position().x == 499,
          "drawable density and geometry");
    dialog.pointer_down(point(0));
    check(dialog.pointer_up(point(0)) == 0 && !dialog.visible(), "MessageClose closes on matching click");
    fonts.set_pixel_scale(1);
    dialog.show(
        {"Are you sure you want to remove NativeFriend from your guild?", "Remove NativeFriend", {"Cancel", "Remove"}});
    dialog.update(fonts, 800, 600);
    check(dialog.height() == 117 && dialog.button(0).y == 83 && dialog.button(0).x == 15 && dialog.button(1).x == 165,
          "wrapped two-button layout");
    dialog.pointer_down(point(1));
    check(dialog.pointer_up(point(1)) == 1 && dialog.visible(), "ordinary button dispatch leaves lifecycle to owner");
    dialog.show({"Loading...", std::nullopt, {}});
    dialog.update(fonts, 800, 600);
    check(dialog.height() == 30 && dialog.button(0).width == 0, "no-title no-button loading state");
    dialog.show(
        {"This longer message checks word wrapping, multiple paragraphs, and the original dialog layout.\n\nThe Close "
         "button follows the complete body.",
         "A title with two lines\nand a second line",
         {"Close", std::nullopt}});
    dialog.update(fonts, 800, 600);
    check(dialog.height() == 208, "long body and multiline title use original layout without missing phase failures");
    dialog.message_close("Title", "Body", "");
    check(dialog.content().buttons[0] == "Close", "empty close label uses source default");
    dialog.close();
    check(!dialog.visible(), "teardown closes modal");
    std::cout << "Dialog checks: " << checks << '\n';
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
