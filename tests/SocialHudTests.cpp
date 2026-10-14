#include "realm/hud/KeysView.hpp"
#include "realm/ui/panels/GuildInvite.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
bool same(const realm::Image& a, const realm::Image& b) {
  return a.width == b.width && a.height == b.height && a.pixels == b.pixels;
}
}

int main() {
  try {
    using namespace realm;
    KeysView keys;
    keys.notification("purple");
    check(!keys.visible() && !keys.lit(DavyKey::purple), "key notifications are ignored before the panel opens");
    keys.notification("showKeyUI");
    check(!keys.visible(), "showKeyUI is ignored outside Davy Jones's Locker");
    keys.map_changed("Davy Jones's Locker");
    keys.notification("showKeyUI");
    check(keys.visible(), "showKeyUI opens the Locker panel");
    keys.notification("purple");
    keys.notification("green");
    check(keys.lit(DavyKey::purple) && keys.lit(DavyKey::green) && !keys.lit(DavyKey::red),
          "only named Locker key notifications light keys");
    const auto lit = render_keys_view(keys, 1);
    keys.notification("not-a-key");
    check(!keys.lit(DavyKey::yellow), "unrecognized key notification has no effect");
    keys.map_changed("Nexus");
    check(!keys.visible() && !keys.lit(DavyKey::purple), "leaving the Locker hides and clears the key panel");
    const auto empty = render_keys_view(keys, 1);
    check(lit.width == 176 && lit.height == 36 && !same(lit, empty), "lit keys are rendered in the retained panel");
    keys.map_changed("Davy Jones's Locker");
    keys.notification("showKeyUI");
    check(keys.visible() && !keys.lit(DavyKey::purple), "reopening the panel starts with no keys lit");
    keys.game_closed();  // Both DEATH and GameClosedSignal use this teardown.
    check(!keys.visible() && !keys.in_locker(), "death or game closure clears the panel and map context");

    GuildInvite invite;
    invite.invited("Alice", "First Guild", 1000);
    check(invite.invitation() && invite.invitation()->guild == "First Guild", "incoming guild invitation opens state");
    invite.invited("Bob", "Second Guild", 5000);
    check(invite.invitation()->name == "Bob", "a newer invitation replaces the visible one");
    invite.update(24999);
    check(invite.invitation().has_value(), "invitation stays open before its 20-second timer");
    invite.update(25000);
    check(!invite.invitation(), "invitation closes at 20 seconds");
    invite.invited("Alice", "Guild", 30000);
    FontBook fonts;
    GuildInviteView view(fonts);
    check(view.update(invite, {}), "guild invite panel renders on open");
    check(view.image().width == GuildInviteView::width && view.image().height == GuildInvite::override_height,
          "guild invite surface uses the shared override panel dimensions");
    const auto button = view.buttons().accept;
    check(button.width > 0 && button.height == 26 && button.y >= GuildInvite::override_top,
          "accept button has actionable HUD coordinates");
    check(view.update(invite, {button.x + button.width / 2, button.y + button.height / 2}),
          "hovering the accept button redraws its hover state");
    const auto revision = invite.revision();
    invite.close();
    check(invite.revision() == revision + 1 && !invite.invitation(), "accept or reject closes the invitation state");
    std::cout << "Locker keys and guild invitation state/view passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
