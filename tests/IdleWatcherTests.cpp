#include "realm/game/IdleWatcher.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
}

int main() {
  try {
    using Event = realm::IdleWatcher::Event;
    realm::IdleWatcher watcher;
    check(watcher.update(0) == Event::none, "zero time does not warn");
    for (int i = 0; i < 5999; ++i) check(watcher.update(100) == Event::none, "no early 10-minute warning");
    check(watcher.idle_time() == realm::IdleWatcher::warning_10_ms - 100, "idle time advances by clamped frame time");
    check(watcher.update(500) == Event::warning_10_minutes, "10-minute warning fires at its threshold");
    for (int i = 0; i < 2999; ++i) check(watcher.update(100) == Event::none, "no early 15-minute warning");
    check(watcher.update(100) == Event::warning_15_minutes, "15-minute warning fires at its threshold");
    for (int i = 0; i < 2999; ++i) check(watcher.update(100) == Event::none, "no early disconnect");
    check(watcher.update(100) == Event::disconnect_20_minutes, "disconnect fires at 20 minutes");
    check(watcher.idle_time() == realm::IdleWatcher::disconnect_ms && watcher.update(100) == Event::none,
          "disconnect milestone is emitted once");

    watcher.reset();  // GameSprite's MOUSE_MOVE and KEY_DOWN listeners.
    check(watcher.idle_time() == 0 && watcher.update(100) == Event::none, "input resets the idle clock and warnings");
    check(watcher.update(1000) == Event::none && watcher.idle_time() == 200,
          "frame time is clamped to 100 milliseconds");
    check(watcher.update(-100) == Event::none && watcher.idle_time() == 200, "negative frame time is ignored");
    std::cout << "Idle warnings, disconnect threshold, input reset and frame clamp passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
