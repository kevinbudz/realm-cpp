#pragma once
#include "realm/camera.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

// Shared UI foundation, headless half: one coordinate model and one input owner for every screen.
//
// Every client UI (startup screens, HUD, menus, dialogs, RmlUi documents) is a Layer in one Stack. The Stack owns the
// event routing and the "pressed" state that each screen would otherwise track on its own: it decides which layer
// receives each event, who holds the pointer while a button is down, who owns the keyboard and IME, which layers are
// blocked by a modal, whether the world may act on the pointer, and what closes on map change, disconnect or death.
namespace realm::ui {

// The window's two coordinate systems and the client's 800x600 design space. SDL reports pointer positions in window
// coordinates; everything the client draws is in drawable pixels (HiDPI); UI layout is logical pixels scaled uniformly
// by drawable_height/600.
struct Viewport {
  int window_width{}, window_height{}, drawable_width{}, drawable_height{};
  [[nodiscard]] bool valid() const {
    return window_width > 0 && window_height > 0 && drawable_width > 0 && drawable_height > 0;
  }
  [[nodiscard]] double scale() const { return drawable_height / 600.0; }
  // Window coordinates -> drawable pixels.
  [[nodiscard]] Vec2 drawable(double window_x, double window_y) const;
  // Drawable pixels -> the HUD sidebar's logical coordinates (x 0..200 is the sidebar, anchored to the right edge).
  [[nodiscard]] Vec2 sidebar(Vec2 drawable) const;
  // Logical sidebar coordinates -> drawable pixels (inverse of sidebar()).
  [[nodiscard]] Vec2 from_sidebar(Vec2 logical) const;
  // The play area's right edge in drawable pixels: everything right of it is the 200-logical-px sidebar.
  [[nodiscard]] double world_right() const { return drawable_width - drawable_height / 3.0; }
  friend bool operator==(const Viewport&, const Viewport&) = default;
};

enum class EventKind : std::uint8_t {
  pointer_move,
  pointer_down,
  pointer_up,
  wheel,
  key_down,
  key_up,
  text,
  composition,
  pointer_left,  // the pointer left this layer (another layer or the world took it) or the window
  focus_lost,    // the window lost keyboard focus
  resized,       // the viewport changed; `viewport` holds the new one
};
struct Event {
  EventKind kind{};
  Vec2 point{};               // drawable pixels (pointer events and wheel)
  int button{1};              // SDL button index (1 left, 2 middle, 3 right)
  double wheel{};             // SDL wheel steps, positive scrolls up
  int key{};                  // SDL keycode
  std::uint16_t modifiers{};  // SDL KMOD_* bits
  bool repeat{};              // key auto-repeat
  std::string text;           // UTF-8 (text, composition)
  int cursor{-1};             // composition cursor in code points, -1 = end
  Viewport viewport{};        // resized
};

enum class Result : std::uint8_t {
  pass,      // not handled: lower layers (and finally the world) see the event
  consumed,  // handled: propagation stops
  capture,   // handled, and (on pointer_down) this layer receives every pointer event until that button is released
};

enum class Reset : std::uint8_t {
  map_change,   // MAPINFO, RECONNECT or DEATH: the game session continues on a new map
  session_end,  // the game session closed (death screen, disconnect, Home): back to the startup screens
};

class Layer {
 public:
  virtual ~Layer() = default;
  [[nodiscard]] virtual std::string_view name() const = 0;
  // Inactive layers receive nothing except reset() and resized events.
  [[nodiscard]] virtual bool active() const = 0;
  // A modal layer receives every input event (whether or not it handles it); layers below it and the world get none.
  [[nodiscard]] virtual bool modal() const { return false; }
  // Pointer occlusion: true where this layer draws something the pointer can hit. Used for hover ownership and the
  // world gate; a layer may still pass an event it covers.
  [[nodiscard]] virtual bool covers(Vec2) const { return false; }
  // This layer holds keyboard focus for text entry: the Stack routes keys and text to it first and asks SDL for text
  // input (IME). The world's movement/ability keys are suppressed while any active layer wants text.
  [[nodiscard]] virtual bool wants_text() const { return false; }
  virtual Result handle(const Event& event) = 0;
  // Drop transient interaction state (hover, press, drag) without acting: a modal opened above, the window lost
  // focus, or the pointer capture was taken away. Must be idempotent.
  virtual void cancel() {}
  // Close transient UI owned by this layer. Called on every layer, active or not.
  virtual void reset(Reset) {}
};

// A layer assembled from functions, for screens whose model and view are not Layers themselves (the coordinator wraps
// each existing panel in one). Unset functions take the Layer defaults; `handle` defaults to passing everything.
class FunctionLayer final : public Layer {
 public:
  explicit FunctionLayer(std::string name) : name_(std::move(name)) {}
  std::function<bool()> is_active, is_modal, is_text;
  std::function<bool(Vec2)> is_covering;
  std::function<Result(const Event&)> on_event;
  std::function<void()> on_cancel;
  std::function<void(Reset)> on_reset;
  [[nodiscard]] std::string_view name() const override { return name_; }
  [[nodiscard]] bool active() const override { return is_active && is_active(); }
  [[nodiscard]] bool modal() const override { return is_modal && is_modal(); }
  [[nodiscard]] bool covers(Vec2 p) const override { return is_covering && is_covering(p); }
  [[nodiscard]] bool wants_text() const override { return is_text && is_text(); }
  Result handle(const Event& event) override { return on_event ? on_event(event) : Result::pass; }
  void cancel() override {
    if (on_cancel) on_cancel();
  }
  void reset(Reset reason) override {
    if (on_reset) on_reset(reason);
  }

 private:
  std::string name_;
};

// Dispatch result for the coordinator.
struct Dispatch {
  bool consumed{};    // a layer handled the event, or a modal swallowed it
  const Layer* by{};  // the layer that consumed it (the modal for swallowed events)
};

struct StackStats {
  std::uint64_t events{}, consumed{}, captures{}, cancels{}, modal_swallowed{}, hover_changes{};
};

// Layers are ordered bottom to top in add() order. Dispatch rules:
// - Pointer events while a layer holds the capture go to it alone; the capture ends after that button's pointer_up.
//   A capture holder that becomes inactive is sent cancel() and loses the capture.
// - Otherwise pointer events (move, down, up, wheel) go top to bottom through active layers. The topmost modal layer
//   ends the walk: it receives the event and the event counts as consumed. Any other layer may consume it.
//   Active layers above the topmost modal still see pointer events first (none should be added above a modal that
//   is not itself meant to be on top of it).
// - Hover: the layer that consumed the last pointer_move owns hover. A layer that received the previous move but not
//   this one (a higher layer consumed it, a capture holds the pointer, or a modal appeared) receives pointer_left.
// - Keys, text and composition go to the topmost modal layer if there is one (and are always consumed there).
//   Otherwise the topmost active layer that wants_text() receives them first, then the remaining layers top to
//   bottom until one consumes.
// - focus_lost goes to every active layer and ends any capture; resized goes to every layer, active or not.
// - When a modal layer becomes active (detected by update() or dispatch), every active layer below it is sent
//   cancel(), the capture is released, and hover is cleared — so opening a dialog never leaves a drag or press behind.
class Stack {
 public:
  void add(Layer& layer);     // on top of the layers already added
  void remove(Layer& layer);  // releases capture/hover held by it
  Dispatch dispatch(const Event& event);
  // Once per frame before drawing: modal transitions and inactive capture/hover holders are resolved here as well
  // as during dispatch, so a layer opened by game state (not by input) cancels lower interaction immediately.
  void update();
  void reset(Reset reason);  // reset() on every layer, cancel() on every active one; clears capture and hover
  [[nodiscard]] const Layer* modal() const;  // topmost active modal layer
  [[nodiscard]] const Layer* capture() const { return capture_; }
  [[nodiscard]] const Layer* hover() const { return hover_; }
  [[nodiscard]] bool wants_text() const;  // the topmost modal's wants_text(), else any active layer's
  // The world may act on the pointer at this drawable point: no modal, no capture, and no active layer covers it.
  [[nodiscard]] bool pointer_free(Vec2 drawable) const;
  // World keyboard controls (movement, ability): no modal and no layer wants text.
  [[nodiscard]] bool keys_free() const { return !modal() && !wants_text(); }
  [[nodiscard]] const StackStats& stats() const { return stats_; }
  [[nodiscard]] const std::vector<Layer*>& layers() const { return layers_; }

 private:
  void modal_check();
  void set_hover(Layer* layer);
  void moved(const std::vector<Layer*>& received, Vec2 point);
  std::vector<Layer*> layers_, moved_;  // moved_: layers that received the last pointer_move
  Layer *capture_{}, *hover_{};
  const Layer* last_modal_{};
  int capture_button_{};
  StackStats stats_;
};
}
