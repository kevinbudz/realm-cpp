#include "realm/model.hpp"
#include "realm/session.hpp"
#include "flash_assets.hpp"
#include <chrono>
#include <iostream>
#include <map>
#include <thread>
// Logs in to the isolated server and projects every live model-bearing object the server sends.
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
    std::map<std::string, std::size_t> projected;
    std::size_t entities{}, hidden{};
    Camera camera;
    while (true) {
      const auto time = static_cast<std::int32_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
      if (time > 20000) throw std::runtime_error("Real model session timed out");
      if (ready >= 0 && time > ready + 2500) break;
      session.poll(time);
      world.update(1);
      if (session.state() == SessionState::failed) throw std::runtime_error(session.error());
      if (session.state() == SessionState::playing && world.entity(world.player_id()) && ready < 0) ready = time;
      session.finish_frame(time);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto* player = world.entity(world.player_id());
    if (!player) throw std::runtime_error("Player absent");
    camera.configure(player->motion.position, 0, 800, 600);
    for (const auto& e : world.entities()) {
      const auto* d = world.catalog().object(e.type);
      if (!d || d->model.empty()) continue;
      ++entities;
      if (!has_model(d->model)) throw std::runtime_error("Live object uses unregistered model " + d->model);
      // Same inputs as Scene: the entity position, XML Rotation and Color; textured when it has sprite art.
      const auto mesh =
          project_model(d->model, camera, e.motion.position, d->rotation, d->color, SpriteRegion{0, 0, 0, 8, 8});
      if (mesh)
        ++projected[d->id];
      else
        ++hidden;
    }
    if (entities == 0) throw std::runtime_error("The server sent no model objects");
    if (projected.empty()) throw std::runtime_error("No live model produced a visible mesh");
    session.disconnect();
    std::cout << "Actual server sent " << entities << " model objects (" << hidden << " edge-on/hidden); projected:";
    for (const auto& [name, count] : projected) std::cout << ' ' << name << 'x' << count;
    std::cout << '\n';
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
