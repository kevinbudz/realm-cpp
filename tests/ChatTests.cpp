#include "realm/ui/Chat.hpp"
#include "flash_assets.hpp"
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
    using Text = net::incoming::Text;
    Chat chat;
    check(chat.visible(0).empty(), "empty log shows nothing");
    for (int i = 0; i < 15; ++i) chat.add("", "line " + std::to_string(i), i * 1000);
    auto shown = chat.visible(14000);
    check(shown.size() == 11 && shown.front() == 4 && shown.back() == 14, "eleven newest lines are shown");
    shown = chat.visible(24500);
    check(shown.size() == 10 && shown.front() == 5, "lines older than 20 seconds disappear");
    chat.scroll_up();
    shown = chat.visible(100000);
    check(shown.size() == 11 && shown.back() == 14, "first PageUp reveals expired lines in place");
    chat.scroll_up();
    shown = chat.visible(100000);
    check(shown.front() == 1 && shown.back() == 11, "later PageUp moves back three lines");
    for (int i = 0; i < 10; ++i) chat.scroll_up();
    shown = chat.visible(100000);
    check(shown.front() == 0 && shown.back() == 10, "scrolling stops at the oldest window");
    for (int i = 0; i < 5; ++i) chat.scroll_down();
    check(chat.visible(100000).empty(), "PageDown at the newest line hides expired lines again");
    // Tells: incoming and outgoing tells feed the Tab history; guild chat does not.
    chat.add(Text{"Alice", 1, 3, 0, "Me", "hi"}, "Me", 0);
    chat.add(Text{"Me", 2, 3, 0, "Bob", "yo"}, "Me", 0);
    chat.add(Text{"Carl", 3, 3, 0, "*Guild*", "guild"}, "Me", 0);
    check(chat.lines().back().recipient == "*Guild*" && chat.lines()[chat.lines().size() - 3].to_me,
          "tell direction recorded");
    chat.tell(false);
    check(chat.typing() && chat.input() == "/tell Bob ", "Tab inserts the newest teller");
    chat.tell(true);
    check(chat.input() == "/tell Bob ", "Shift+Tab clamps at the newest entry from the start");
    chat.tell(false);
    check(chat.input() == "/tell Alice ", "Tab steps to the previous teller");
    chat.tell(false);
    check(chat.input() == "/tell Alice ", "Tab clamps at the oldest teller");
    check(chat.submit() == "/tell Alice " && !chat.typing(), "Enter sends and closes");
    chat.open();
    check(chat.typing() && chat.input().empty(), "Enter opens an empty field");
    check(!chat.submit() && !chat.typing(), "empty Enter sends nothing but closes");
    chat.open("/");
    chat.text("g héllo");
    check(chat.input() == "/g héllo", "typed UTF-8 is appended");
    chat.backspace();
    chat.backspace();
    check(chat.input() == "/g hél", "backspace removes whole code points");
    chat.close();
    chat.open("");
    chat.text(std::string(200, 'x'));
    check(chat.input().size() == Chat::max_chars, "input keeps maxChars 128");
    chat.text("\x07");
    check(chat.input().size() == Chat::max_chars, "control characters are ignored");
    chat.close();
    chat.text("ignored");
    check(!chat.typing(), "closed field ignores text");
    Chat bounded;
    for (std::size_t i = 0; i < Chat::capacity + 50; ++i) bounded.add("", "x", 0);
    check(bounded.lines().size() == Chat::capacity, "log storage is bounded");
    {
      Catalog catalog(flash::source_root);
      World world(catalog);
      world.initialize(10, 10);
      world.add(1, catalog.object("Wizard")->type, {2, 2});
      world.add(2, catalog.object("Wizard")->type, {3, 2});
      SpeechBalloons balloons;
      balloons.add(world, Text{"A", 1, 0, 5, "", "first"});
      balloons.add(world, Text{"A", 1, 0, 5, "", "second"});
      check(balloons.balloons().size() == 1 && balloons.balloons().front().text == "second",
            "a new balloon replaces the speaker's previous one");
      balloons.add(world, Text{"", -1, -1, 0, "", "server"});
      balloons.add(world, Text{"X", 99, -1, 5, "", "gone"});
      check(balloons.balloons().size() == 1, "messages without a present object have no balloon");
      balloons.add(world, Text{"B", 2, 0, 5, "*Guild*", "g"});
      check(balloons.balloons().back().background == 0x3e8a00 && balloons.balloons().front().background == 0xe1dfdc,
            "guild and normal balloon colors");
      check(balloons.balloons()[0].slot != balloons.balloons()[1].slot, "each balloon owns a GPU slot");
      balloons.add(world, Text{"B", 2, 0, 5, "A", "t"});
      check(balloons.balloons().back().outline == 0x00f0ff, "tell balloons are blue");
      check(balloons.balloons().front().texture_height == 8, "player sprites use their 8-pixel texture height");
      world.remove(2);
      {
        FontBook fonts;
        balloons.update(world, fonts, 1000);
        check(balloons.balloons().size() == 1 && balloons.balloons().front().start == 1000,
              "removed speakers lose their balloon; lifetime starts when drawn");
        const auto& balloon = balloons.balloons().front();
        check(balloon.field_width == std::floor(fonts.measure("second", 14) + 4) && !balloon.image.pixels.empty(),
              "balloon field width is textWidth+4");
        const auto at = SpeechBalloons::position(balloon, {400, 300}, 100, 1);
        check(at.x == std::trunc(400 - balloon.field_width / 2) - SpeechBalloons::margin &&
                  at.y == std::trunc(300 - balloon.full_height - 40 - 2) - SpeechBalloons::margin,
              "balloon tail sits 2 pixels above the sprite");
        balloons.update(world, fonts, 6000);
        check(balloons.balloons().size() == 1, "balloon lasts bubbleTime seconds");
        balloons.update(world, fonts, 6001);
        check(balloons.balloons().empty(), "expired balloons are removed");
      }
      for (int i = 0; i < 40; ++i) {
        world.add(100 + i, catalog.object("Wizard")->type, {1, 1});
        balloons.add(world, Text{"N", 100 + i, 0, 5, "", "x"});
      }
      check(balloons.balloons().size() == SpeechBalloons::capacity, "balloon count is bounded");
    }
    {
      FontBook fonts;
      HudArt art;
      ChatView view(fonts, &art);
      Chat lines;
      check(view.update(lines, 0) && view.empty(), "nothing to draw without lines or input");
      lines.add(Text{"Tank", 5, 12, 0, "", "hello"}, "Me", 0);
      check(view.update(lines, 0) && !view.empty() && view.top() < 600 - 20, "a line draws above the bottom edge");
      const auto rebuilds = view.rebuilds();
      check(!view.update(lines, 1000) && view.rebuilds() == rebuilds, "unchanged chat does not redraw");
      const double closed = view.top();
      lines.open();
      view.update(lines, 1000);
      check(view.top() == closed - 22, "opening the field lifts the lines by 22 pixels");
      lines.close();
      check(view.update(lines, 20001) && view.empty(), "expiry redraws to nothing");
      lines.add("*Error*", std::string(400, 'w') + " end", 0);
      view.update(lines, 0);
      check(view.top() < 600 - 80, "long words wrap across rows");
    }
    std::cout << "Chat checks: " << checks << '\n';
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
