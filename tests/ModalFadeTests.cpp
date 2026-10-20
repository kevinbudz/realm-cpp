#include "realm/lib/ui/ModalFade.hpp"
#include <iostream>
#include <stdexcept>

namespace {
int checks{};

void check(bool value, const char* text) {
  ++checks;
  if (!value) {
    throw std::runtime_error(text);
  }
}
}

int main() {
  try {
    using realm::ui::ModalFade;
    constexpr std::int32_t span = ModalFade::duration_ms;
    ModalFade fade;
    check(!fade.active() && fade.opacity() == 0, "a modal never shown is not drawn");
    fade.update(true, 1000);
    check(fade.active() && fade.opacity() == 1, "a visible modal is opaque");
    fade.update(false, 1100);
    check(fade.active() && fade.opacity() == 1, "the close frame starts the fade at full opacity");
    fade.update(false, 1100 + span / 2);
    check(fade.active() && fade.opacity() == .5, "the fade is linear over duration_ms");
    fade.update(true, 1100 + span / 2 + 10);
    check(fade.active() && fade.opacity() == 1, "reopening mid-fade cancels it");
    fade.update(false, 2000);
    fade.update(false, 2000 + span - 1);
    check(fade.active() && fade.opacity() > 0, "still drawn just before the fade ends");
    fade.update(false, 2000 + span);
    check(!fade.active() && fade.opacity() == 0, "gone once the fade ends");
    fade.update(false, 2000 + span + 500);
    check(!fade.active(), "stays gone while closed");
    fade.update(false, 1500);
    check(!fade.active(), "a clock that runs backwards does not revive a finished fade");
    fade.update(true, 3000);
    fade.update(false, 2999);
    check(fade.opacity() == 1, "a clock that runs backwards never raises opacity above 1");
    std::cout << "ModalFade checks: " << checks << '\n';
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
