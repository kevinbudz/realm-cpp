#include "realm/lib/ui/UIStack.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// The shared input owner. Every rule in UIStack.hpp is exercised with recording layers.
namespace {
using namespace realm;
using namespace realm::ui;
int checks{};
void check(bool x, const std::string& text) {
  ++checks;
  if (!x) throw std::runtime_error(text);
}
bool near(double a, double b) { return std::abs(a - b) < 1e-9; }
struct Rect {
  double x{}, y{}, w{}, h{};
  [[nodiscard]] bool contains(Vec2 p) const { return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h; }
};
class Probe final : public Layer {
 public:
  explicit Probe(std::string name, Rect area = {}) : name_(std::move(name)), area_(area) {}
  [[nodiscard]] std::string_view name() const override { return name_; }
  [[nodiscard]] bool active() const override { return on; }
  [[nodiscard]] bool modal() const override { return is_modal; }
  [[nodiscard]] bool covers(Vec2 p) const override { return area_.contains(p); }
  [[nodiscard]] bool wants_text() const override { return text; }
  Result handle(const Event& e) override {
    log.push_back(e.kind);
    if (e.kind == EventKind::pointer_down || e.kind == EventKind::pointer_up || e.kind == EventKind::pointer_move ||
        e.kind == EventKind::wheel) {
      if (!covers(e.point)) return Result::pass;
      if (e.kind == EventKind::pointer_down) pressed = true;
      if (e.kind == EventKind::pointer_up) pressed = false;
      return e.kind == EventKind::pointer_down && capture_down ? Result::capture : Result::consumed;
    }
    if (e.kind == EventKind::key_down || e.kind == EventKind::text)
      return eat_keys || text ? Result::consumed : Result::pass;
    if (e.kind == EventKind::resized) last_viewport = e.viewport;
    return Result::pass;
  }
  void cancel() override {
    ++cancels;
    pressed = false;
  }
  void reset(Reset r) override {
    ++resets;
    last_reset = r;
    if (close_on_reset) on = false;
  }
  [[nodiscard]] int count(EventKind kind) const {
    int n{};
    for (auto k : log) n += k == kind;
    return n;
  }
  bool on{true}, is_modal{}, text{}, eat_keys{}, capture_down{true}, close_on_reset{}, pressed{};
  int cancels{}, resets{};
  Reset last_reset{};
  Viewport last_viewport{};
  std::vector<EventKind> log;

 private:
  std::string name_;
  Rect area_;
};
Event pointer(EventKind kind, double x, double y, int button = 1) {
  Event e;
  e.kind = kind;
  e.point = {x, y};
  e.button = button;
  return e;
}
Event key(int code) {
  Event e;
  e.kind = EventKind::key_down;
  e.key = code;
  return e;
}
}
int main() {
  try {
    // Viewport: window -> drawable (HiDPI 2x), sidebar logical coordinates and their inverse, play-area edge.
    Viewport v{800, 600, 1600, 1200};
    check(near(v.scale(), 2) && near(v.drawable(400, 300).x, 800) && near(v.drawable(400, 300).y, 600),
          "drawable conversion");
    check(near(v.sidebar({1600, 0}).x, 200) && near(v.sidebar({1200, 600}).x, 0) && near(v.sidebar({1200, 600}).y, 300),
          "sidebar coordinates");
    for (const Vec2 p : {Vec2{1300, 17}, Vec2{1599, 1199}, Vec2{0, 0}}) {
      const auto back = v.from_sidebar(v.sidebar(p));
      check(near(back.x, p.x) && near(back.y, p.y), "sidebar round trip");
    }
    check(near(v.world_right(), 1200) && near(Viewport{1280, 720, 1280, 720}.world_right(), 1040),
          "world edge = width - height/3");
    check(!Viewport{}.valid() && v.valid(), "viewport validity");
    // The sidebar mapping reduces to (mx-lw)/(lh/600)+200 when the HiDPI ratio is uniform.
    for (const int mx : {0, 333, 799}) {
      const double old = (mx - 800) / (600 / 600.0) + 200;
      check(near(v.sidebar(v.drawable(mx, 10)).x, old), "sidebar formula parity");
    }

    Stack stack;
    Probe world_hud("hud", {1200, 0, 400, 1200}), menu("menu", {100, 100, 200, 200}),
        dialog("dialog", {500, 500, 300, 300});
    dialog.on = false;
    dialog.is_modal = true;
    stack.add(world_hud);
    stack.add(menu);
    stack.add(dialog);
    stack.add(menu);
    check(stack.layers().size() == 3, "re-adding a layer is a no-op");

    // Topmost covering layer consumes; layers below it never see the event.
    auto d = stack.dispatch(pointer(EventKind::pointer_down, 150, 150));
    check(d.consumed && d.by == &menu && menu.pressed && world_hud.count(EventKind::pointer_down) == 0,
          "top layer consumes press");
    check(stack.capture() == &menu && !stack.pointer_free({700, 700}), "press captures; world blocked while held");
    // Capture: the drag leaves the menu and crosses the HUD; only the holder sees it, and its release ends the capture.
    d = stack.dispatch(pointer(EventKind::pointer_move, 1300, 50));
    check(d.by == &menu && world_hud.count(EventKind::pointer_move) == 0, "captured move goes to holder only");
    stack.dispatch(pointer(EventKind::pointer_up, 1300, 50, 3));
    check(stack.capture() == &menu, "another button's release keeps the capture");
    stack.dispatch(pointer(EventKind::pointer_up, 1300, 50));
    check(!stack.capture() && world_hud.count(EventKind::pointer_up) == 0,
          "release ends capture and is not re-delivered");
    check(stack.stats().captures == 1, "capture counted");

    // Uncovered points belong to the world.
    d = stack.dispatch(pointer(EventKind::pointer_down, 700, 700));
    check(!d.consumed && stack.pointer_free({700, 700}) && !stack.pointer_free({150, 150}) &&
              !stack.pointer_free({1300, 10}),
          "world gate from coverage");
    stack.dispatch(pointer(EventKind::pointer_up, 700, 700));

    // Hover: moving from the menu to the HUD sends pointer_left to the menu; moving into the world leaves the HUD.
    stack.dispatch(pointer(EventKind::pointer_move, 150, 150));
    check(stack.hover() == &menu, "menu hovered");
    const int menu_left = menu.count(EventKind::pointer_left);
    stack.dispatch(pointer(EventKind::pointer_move, 1300, 150));
    check(stack.hover() == &world_hud && menu.count(EventKind::pointer_left) == menu_left,
          "a layer that saw the move and passed keeps no hover but is not left");
    stack.dispatch(pointer(EventKind::pointer_move, 150, 150));
    check(stack.hover() == &menu && world_hud.count(EventKind::pointer_left) == 1,
          "lower layer gets pointer_left when a higher one takes the pointer");
    stack.dispatch(pointer(EventKind::pointer_move, 700, 700));
    check(!stack.hover(), "world owns hover");

    // Keys: no focus -> top to bottom until consumed; unconsumed keys reach the world.
    check(!stack.dispatch(key(65)).consumed && menu.count(EventKind::key_down) == 1 &&
              world_hud.count(EventKind::key_down) == 1,
          "unhandled key falls through to world");
    world_hud.eat_keys = true;
    check(stack.dispatch(key(65)).by == &world_hud, "lower layer may consume keys");
    world_hud.eat_keys = false;
    // Focus: the text layer gets keys first even though it is lower; world movement keys are suppressed.
    world_hud.text = true;
    check(stack.wants_text() && !stack.keys_free(), "text focus suppresses world keys");
    const int menu_keys = menu.count(EventKind::key_down);
    check(stack.dispatch(key(66)).by == &world_hud && menu.count(EventKind::key_down) == menu_keys,
          "focus layer first");
    world_hud.text = false;
    check(stack.keys_free(), "keys free again");

    // A modal opened by game state (not input) mid-gesture: lower layers are cancelled, capture dropped.
    stack.dispatch(pointer(EventKind::pointer_down, 150, 150));
    check(stack.capture() == &menu && menu.pressed, "menu pressed");
    dialog.on = true;
    stack.update();
    check(stack.modal() == &dialog && !stack.capture() && !menu.pressed && menu.cancels == 1 && world_hud.cancels == 1,
          "modal cancels everything below");
    check(dialog.cancels == 0, "modal itself not cancelled");
    stack.update();
    check(menu.cancels == 1, "modal transition cancels once");
    // Modal swallows everything: uncovered clicks, keys, wheel; the world gate is closed everywhere.
    const int menu_downs = menu.count(EventKind::pointer_down);
    d = stack.dispatch(pointer(EventKind::pointer_down, 150, 150));
    check(d.consumed && d.by == &dialog && menu.count(EventKind::pointer_down) == menu_downs,
          "modal swallows clicks outside itself");
    stack.dispatch(pointer(EventKind::pointer_up, 150, 150));
    check(stack.dispatch(key(67)).by == &dialog && !stack.pointer_free({700, 700}) && !stack.keys_free(),
          "modal owns keys and world");
    check(stack.stats().modal_swallowed >= 2, "swallowed events counted");
    dialog.text = true;
    check(stack.wants_text(), "modal decides text input");
    world_hud.text = true;
    dialog.text = false;
    check(!stack.wants_text(), "lower layer's text focus ignored under a modal");
    world_hud.text = false;
    // Modal closes: input returns to the layers below.
    dialog.on = false;
    stack.update();
    check(!stack.modal() && stack.dispatch(pointer(EventKind::pointer_down, 150, 150)).by == &menu,
          "input restored after modal closes");
    // Capture holder deactivates mid-drag (closed by game state): cancelled, capture released, world free again.
    menu.on = false;
    stack.update();
    check(!stack.capture() && menu.cancels == 2 && stack.pointer_free({150, 150}), "inactive capture holder released");
    menu.on = true;

    // Focus loss ends captures and hover everywhere.
    stack.dispatch(pointer(EventKind::pointer_down, 150, 150));
    Event lost;
    lost.kind = EventKind::focus_lost;
    stack.dispatch(lost);
    check(!stack.capture() && menu.count(EventKind::focus_lost) == 1 && world_hud.count(EventKind::focus_lost) == 1 &&
              dialog.count(EventKind::focus_lost) == 0,
          "focus_lost to active layers");
    // Resize reaches every layer, active or not.
    Event resized;
    resized.kind = EventKind::resized;
    resized.viewport = {1280, 720, 1280, 720};
    stack.dispatch(resized);
    check(dialog.last_viewport == resized.viewport && menu.last_viewport == resized.viewport, "resize to all layers");

    // Reset (map change / session end): every layer reset, active ones cancelled, transient UI closed.
    menu.close_on_reset = true;
    stack.dispatch(pointer(EventKind::pointer_down, 150, 150));
    stack.reset(Reset::map_change);
    check(menu.resets == 1 && dialog.resets == 1 && world_hud.resets == 1 && menu.last_reset == Reset::map_change &&
              !menu.on && !stack.capture() && !stack.hover(),
          "reset closes transient UI");
    stack.reset(Reset::session_end);
    check(world_hud.last_reset == Reset::session_end, "session end reset");
    menu.on = true;

    // Removal while holding capture/hover leaves no dangling pointers.
    stack.dispatch(pointer(EventKind::pointer_down, 150, 150));
    stack.remove(menu);
    check(!stack.capture() && stack.layers().size() == 2 &&
              stack.dispatch(pointer(EventKind::pointer_up, 150, 150)).consumed == false,
          "removed layer releases capture");

    // A modal below a non-modal top layer (tooltip-like layer added last) still blocks the layers beneath it.
    Probe top("top", {0, 0, 50, 50});
    stack.add(top);
    dialog.on = true;
    stack.update();
    check(stack.dispatch(pointer(EventKind::pointer_down, 10, 10)).by == &top, "layer above modal sees pointer first");
    stack.dispatch(pointer(EventKind::pointer_up, 10, 10));
    const int hud_downs = world_hud.count(EventKind::pointer_down);
    check(stack.dispatch(pointer(EventKind::pointer_down, 1300, 10)).by == &dialog &&
              world_hud.count(EventKind::pointer_down) == hud_downs,
          "modal blocks below");
    std::cout << "ui stack: " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL after " << checks << " checks: " << e.what() << '\n';
    return 1;
  }
}
