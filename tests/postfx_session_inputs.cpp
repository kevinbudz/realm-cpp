#include "realm/post_effects.hpp"
#include "realm/session.hpp"
#include "flash_assets.hpp"
#include <chrono>
#include <iostream>
#include <thread>
// Real server: a seeded Pirate Rum is used through the native input path; the server's CONDITION stat must
// switch on Drunk and turn the screen effects into a blur, while the sidebar gradient shows as soon as the player
// exists.
int main(int argc, char** argv) {
  try {
    if (argc != 3) throw std::runtime_error("Expected port and account");
    using namespace realm;
    std::string password;
    std::getline(std::cin, password);
    Catalog catalog(flash::source_root);
    World world(catalog);
    Session session(world);
    SessionOptions options;
    options.host = "localhost";
    options.port = static_cast<std::uint16_t>(std::stoi(argv[1]));
    options.username = argv[2];
    options.password = password;
    options.character_id = 0;
    session.connect(options);
    const auto start = std::chrono::steady_clock::now();
    int ready = -1;
    bool used{}, drunk{};
    int sober_checks{};
    float peak_blur{};
    while (true) {
      const auto time = static_cast<std::int32_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
      if (time > 20000) throw std::runtime_error("Real post effect session timed out");
      if (ready >= 0 && time > ready + 4000) break;
      session.poll(time);
      world.update(1);
      (void)session.take_events();
      if (session.state() == SessionState::failed) throw std::runtime_error(session.error());
      if (session.state() == SessionState::playing && world.entity(world.player_id()) && ready < 0) ready = time;
      if (ready >= 0 && session.state() != SessionState::playing)
        throw std::runtime_error("Fixture left playing state " + session.error());
      if (auto* p = world.entity(world.player_id()); p && ready >= 0) {
        const auto fx = screen_effects(p, time, 1);
        if (fx.sidebar != sidebar_gradient_width) throw std::runtime_error("Sidebar gradient missing while playing");
        if (!used && time > ready + 500) {
          if (p->combat.condition & drunk_condition) throw std::runtime_error("Player already drunk before the item");
          if (fx.filtered() || fx.vignette != 0) throw std::runtime_error("Effects active on a sober player");
          ++sober_checks;
          session.use_item({p->id, 4}, time);
          used = true;
        }
        if (used && (p->combat.condition & drunk_condition)) {
          drunk = true;
          if (!fx.filtered() || fx.blind || fx.blur < 10 * 1 - .01F || fx.blur > 30 + .01F)
            throw std::runtime_error("Drunk condition did not produce the 10..30 px blur");
          peak_blur = std::max(peak_blur, fx.blur);
        }
      }
      session.finish_frame(time);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!used || !drunk || sober_checks != 1) throw std::runtime_error("The server never made the player Drunk");
    if (session.map_info().background != 0)
      throw std::runtime_error("Unexpected MAPINFO background " + std::to_string(session.map_info().background));
    session.disconnect();
    std::cout << "Actual server Drunk (condition bit 8) after USEITEM: blur up to " << peak_blur << " px, sidebar "
              << sidebar_gradient_width << " px, MAPINFO background " << session.map_info().background << '\n';
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
