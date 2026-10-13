#include "realm/lib/ui/UIStack.hpp"
#include <algorithm>

namespace realm::ui {
Vec2 Viewport::drawable(double window_x, double window_y) const {
  return {window_width > 0 ? window_x * drawable_width / window_width : window_x,
          window_height > 0 ? window_y * drawable_height / window_height : window_y};
}
Vec2 Viewport::sidebar(Vec2 p) const { return {(p.x - drawable_width) / scale() + 200, p.y / scale()}; }
Vec2 Viewport::from_sidebar(Vec2 p) const { return {(p.x - 200) * scale() + drawable_width, p.y * scale()}; }

namespace {
bool pointer(EventKind kind) {
  return kind == EventKind::pointer_move || kind == EventKind::pointer_down || kind == EventKind::pointer_up ||
         kind == EventKind::wheel;
}
}

void Stack::add(Layer& layer) {
  if (std::find(layers_.begin(), layers_.end(), &layer) == layers_.end()) layers_.push_back(&layer);
}
void Stack::remove(Layer& layer) {
  std::erase(layers_, &layer);
  std::erase(moved_, &layer);
  if (capture_ == &layer) capture_ = nullptr;
  if (hover_ == &layer) hover_ = nullptr;
  if (last_modal_ == &layer) last_modal_ = nullptr;
}
const Layer* Stack::modal() const {
  for (auto it = layers_.rbegin(); it != layers_.rend(); ++it)
    if ((*it)->active() && (*it)->modal()) return *it;
  return nullptr;
}
bool Stack::wants_text() const {
  if (const auto* top = modal()) return top->wants_text();
  return std::any_of(layers_.begin(), layers_.end(),
                     [](const Layer* layer) { return layer->active() && layer->wants_text(); });
}
bool Stack::pointer_free(Vec2 point) const {
  if (capture_ || modal()) return false;
  return std::none_of(layers_.begin(), layers_.end(),
                      [&](const Layer* layer) { return layer->active() && layer->covers(point); });
}
// Layers that received the previous move but not this one have lost the pointer.
void Stack::moved(const std::vector<Layer*>& received, Vec2 point) {
  for (auto* layer : moved_)
    if (layer->active() && std::find(received.begin(), received.end(), layer) == received.end()) {
      Event left;
      left.kind = EventKind::pointer_left;
      left.point = point;
      layer->handle(left);
    }
  moved_ = received;
}
void Stack::set_hover(Layer* layer) {
  if (layer != hover_) ++stats_.hover_changes;
  hover_ = layer;
}
void Stack::modal_check() {
  if (capture_ && !capture_->active()) {
    capture_->cancel();
    ++stats_.cancels;
    capture_ = nullptr;
  }
  if (hover_ && !hover_->active()) hover_ = nullptr;
  std::erase_if(moved_, [](const Layer* layer) { return !layer->active(); });
  const auto* top = modal();
  if (top && top != last_modal_) {
    // A modal that appears takes the pointer and keyboard from everything below it, mid-gesture included.
    for (auto* layer : layers_) {
      if (layer == top) break;
      if (layer->active()) {
        layer->cancel();
        ++stats_.cancels;
      }
    }
    if (capture_ != top) capture_ = nullptr;
    if (hover_ != top) hover_ = nullptr;
    std::erase_if(moved_, [&](const Layer* layer) { return layer != top; });
  }
  last_modal_ = top;
}
void Stack::update() { modal_check(); }
void Stack::reset(Reset reason) {
  for (auto* layer : layers_) layer->reset(reason);
  for (auto* layer : layers_)
    if (layer->active()) {
      layer->cancel();
      ++stats_.cancels;
    }
  capture_ = hover_ = nullptr;
  moved_.clear();
  last_modal_ = modal();
}
Dispatch Stack::dispatch(const Event& event) {
  ++stats_.events;
  modal_check();
  const auto done = [&](const Layer* by) {
    ++stats_.consumed;
    return Dispatch{true, by};
  };
  switch (event.kind) {
    case EventKind::resized:
      for (auto* layer : layers_) layer->handle(event);
      return {};
    case EventKind::focus_lost:
      for (auto* layer : layers_)
        if (layer->active()) layer->handle(event);
      capture_ = hover_ = nullptr;
      moved_.clear();
      return {};
    case EventKind::pointer_left:
      // The window lost the pointer; a held button keeps its capture (SDL still reports the release).
      for (auto* layer : moved_)
        if (layer->active()) layer->handle(event);
      moved_.clear();
      hover_ = nullptr;
      return {};
    default:
      break;
  }
  if (pointer(event.kind)) {
    if (capture_) {
      auto* holder = capture_;
      holder->handle(event);
      if (event.kind == EventKind::pointer_up && event.button == capture_button_) capture_ = nullptr;
      if (event.kind == EventKind::pointer_move) {
        moved({holder}, event.point);
        set_hover(holder);
      }
      return done(holder);
    }
    std::vector<Layer*> received;
    for (auto it = layers_.rbegin(); it != layers_.rend(); ++it) {
      auto* layer = *it;
      if (!layer->active()) continue;
      received.push_back(layer);
      const auto result = layer->handle(event);
      if (result == Result::pass && !layer->modal()) continue;
      if (result == Result::pass) ++stats_.modal_swallowed;
      if (event.kind == EventKind::pointer_down && result == Result::capture) {
        capture_ = layer;
        capture_button_ = event.button;
        ++stats_.captures;
      }
      if (event.kind == EventKind::pointer_move) {
        moved(received, event.point);
        set_hover(layer);
      }
      return done(layer);
    }
    if (event.kind == EventKind::pointer_move) {
      moved(received, event.point);
      set_hover(nullptr);
    }
    return {};
  }
  // Keyboard, text and IME composition.
  if (const auto* top = modal()) {
    auto* layer = const_cast<Layer*>(top);
    if (layer->handle(event) == Result::pass) ++stats_.modal_swallowed;
    return done(layer);
  }
  Layer* focus{};
  for (auto it = layers_.rbegin(); it != layers_.rend(); ++it)
    if ((*it)->active() && (*it)->wants_text()) {
      focus = *it;
      break;
    }
  if (focus && focus->handle(event) != Result::pass) return done(focus);
  for (auto it = layers_.rbegin(); it != layers_.rend(); ++it)
    if (*it != focus && (*it)->active() && (*it)->handle(event) != Result::pass) return done(*it);
  return {};
}
}
