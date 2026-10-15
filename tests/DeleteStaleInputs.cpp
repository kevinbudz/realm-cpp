#include "realm/screens/Startup.hpp"
#include "realm/appengine/SavedCharactersList.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

// Drives the production Startup controller against the Python mock in delete_stale_tests.py. The
// mock holds /char/delete until it receives /__release, so each mode can change the session while
// the reply is pending.
namespace {
using namespace realm;
using Clock = std::chrono::steady_clock;

struct Harness {
  FontBook fonts;
  HudArt art;
  Catalog catalog{flash::source_root};
  std::unique_ptr<Startup> startup;
  std::int32_t time{};
  std::uint16_t port{};

  void update(int frames = 1) {
    for (int i = 0; i < frames; ++i) {
      time += 16;
      fonts.set_pixel_scale(1);
      startup->update(time, 16, 800, 600);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  template <class F>
  void until(F condition, const char* why, int seconds = 10) {
    const auto end = Clock::now() + std::chrono::seconds(seconds);
    while (!condition()) {
      update();
      if (Clock::now() > end) {
        throw std::runtime_error(std::string("Timed out waiting for ") + why);
      }
    }
  }

  void release() { (void)net::http_post("127.0.0.1", port, "/__release", {}); }

  [[nodiscard]] std::vector<int> ids() const {
    std::vector<int> result;
    if (const auto* list = startup->char_list()) {
      for (const auto& c : list->characters) {
        result.push_back(c.id);
      }
    }
    return result;
  }

  [[nodiscard]] bool dialog() const { return startup->view().dialog; }

  [[nodiscard]] bool menus() const { return startup->view().menus; }

  // The dialog text is a bitmap; its state shows in the number of clickable buttons: confirm 2,
  // error 1, deleting 0.
  [[nodiscard]] int buttons() const {
    const auto& view = startup->view();
    if (!view.deletion) {
      return -1;
    }
    return static_cast<int>(
        std::count_if(view.elements.begin(), view.elements.end(), [](const auto& e) {
          return e.hit && e.action &&
                 (e.action->kind == StartupAction::Kind::cancel ||
                  e.action->kind == StartupAction::Kind::submit);
        }));
  }
};

void expect(bool ok, const char* why) {
  if (!ok) {
    throw std::runtime_error(why);
  }
}

std::string describe(const std::vector<int>& ids) {
  std::string s;
  for (const auto id : ids) {
    s += std::to_string(id) + ' ';
  }
  return s;
}
}

int main(int argc, char** argv) {
  try {
    if (argc != 4) {
      throw std::runtime_error("Usage: realm_delete_stale_inputs MODE HTTP_PORT ACCOUNT_FILE");
    }
    const std::string mode = argv[1];
    Harness h;
    h.port = static_cast<std::uint16_t>(std::stoi(argv[2]));
    StartupOptions options;
    options.app_port = h.port;
    options.account_file = argv[3];
    h.startup = std::make_unique<Startup>(h.fonts, &h.art, h.catalog, flash::source_root, options);
    h.until([&] { return h.startup->char_list() != nullptr; }, "the character list");
    h.startup->perform({StartupAction::Kind::play});
    h.update(2);
    expect(h.ids() == std::vector<int>({5, 6, 7}), "fixture characters loaded");
    // The second of three characters, then Delete.
    h.startup->perform({StartupAction::Kind::delete_character, 1});
    h.update(2);
    expect(h.buttons() == 2, "confirmation shown");
    h.startup->perform({StartupAction::Kind::submit});
    h.update(2);
    expect(h.buttons() == 0, "deleting dialog shown while the reply is held");
    // Everything beneath the Deleting dialog is inert: logout, account, play, back and a second
    // delete.
    for (const auto kind : {StartupAction::Kind::login, StartupAction::Kind::account,
                            StartupAction::Kind::play, StartupAction::Kind::back,
                            StartupAction::Kind::legends, StartupAction::Kind::new_character,
                            StartupAction::Kind::cancel, StartupAction::Kind::submit}) {
      h.startup->perform({kind});
    }
    h.startup->perform({StartupAction::Kind::delete_character, 0});
    h.update(2);
    expect(h.buttons() == 0 && h.ids() == std::vector<int>({5, 6, 7}),
           "input under the held request is ignored");
    (void)h.startup->key(StartupKey::escape);
    h.update(1);
    expect(h.buttons() == 0, "Escape cannot cancel a sent deletion");
    if (mode == "quit") {
      // Destroying the controller drains the worker: it must wait for the held reply, then exit
      // cleanly.
      std::thread releaser([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        h.release();
      });
      const auto begin = Clock::now();
      h.startup.reset();
      const auto waited =
          std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - begin).count();
      releaser.join();
      expect(waited >= 250, "the destructor waited for the in-flight request");
      std::cout << "delete stale quit-while-held passed\n";
      return 0;
    }
    if (mode == "left_game") {
      h.startup->left_game("");
    } else if (mode == "renamed") {
      h.startup->renamed_account("Other");
      h.startup->left_game("");
    } else if (mode == "entered_game") {
      h.startup->entered_game();
    } else if (mode == "same") {
    } else {
      throw std::runtime_error("Unknown mode " + mode);
    }
    h.update(2);
    h.release();
    if (mode == "same") {
      h.until([&] { return !h.dialog() && h.menus() && h.ids() == std::vector<int>({5, 7}); },
              "the deleted row to disappear");
      h.update(30);
      expect(h.ids() == std::vector<int>({5, 7}), "success removes only the confirmed character");
    } else if (mode == "entered_game") {
      h.update(60);
      expect(!h.menus(), "a late reply does not bring the menus back during play");
      h.startup->left_game("");
      h.until([&] { return h.menus() && h.ids() == std::vector<int>({5, 7}); },
              "the post-game refresh");
    } else {
      const std::vector<int> expected =
          mode == "renamed" ? std::vector<int>({6, 8, 9}) : std::vector<int>({5, 7});
      // The other account also owns id 6: a stale reply must never remove it, even for a frame
      // before the refresh.
      h.until(
          [&] {
            const auto now = h.ids();
            if (mode == "renamed" && !now.empty() &&
                std::find(now.begin(), now.end(), 6) == now.end()) {
              throw std::runtime_error("the stale reply removed id 6 from the visible list: " +
                                       describe(now));
            }
            return now == expected;
          },
          "the new session's list");
      h.update(40);
      expect(h.ids() == expected,
             (std::string("the new session list must be untouched by the stale reply, got ") +
              describe(h.ids()))
                 .c_str());
      expect(!h.dialog() && h.menus() && h.buttons() < 0, "no dialog leaks into the new screen");
    }
    h.update(10);
    std::cout << "delete stale " << mode << " passed\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
