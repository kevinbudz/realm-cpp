#include "realm/messaging/GameServerConnection.hpp"
#include "flash_assets.hpp"
#include <chrono>
#include <thread>
#include <iostream>
#include <set>
int main(int argc, char** argv) {
  try {
    if (argc != 4) throw std::runtime_error("Expected port, account and required effect types");
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
    bool used{}, potion{};
    std::set<int> seen;
    std::size_t maximum{};
    int uses{};
    int ready = -1;
    std::vector<int> inv_results;
    while (true) {
      const auto time = static_cast<std::int32_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
      if (time > 20000) throw std::runtime_error("Real particle session timed out");
      if (ready >= 0 && time > ready + 4500) break;
      session.poll(time);
      world.update(1);
      for (const auto& event : session.take_events())
        if (event.id == flash::PacketId::INVRESULT)
          inv_results.push_back(std::get<net::incoming::InvResult>(event.data).result);
        else if (event.id == flash::PacketId::SHOWEFFECT) {
          const auto& effect = std::get<net::incoming::ShowEffect>(event.data);
          seen.insert(effect.type);
          if (world.particles().particles().empty())
            throw std::runtime_error("Actual SHOWEFFECT did not create native particles type=" +
                                     std::to_string(effect.type));
        }
      if (session.state() == SessionState::failed) throw std::runtime_error(session.error());
      if (session.state() == SessionState::playing && world.entity(world.player_id()) && ready < 0) ready = time;
      if (ready >= 0 && session.state() != SessionState::playing)
        throw std::runtime_error("Fixture left playing state=" + std::to_string(static_cast<int>(session.state())) +
                                 " " + session.error());
      if (auto* p = world.entity(world.player_id()); p && ready >= 0 && time > ready + 500) {
        if (!used) {
          session.use_ability({p->motion.position.x + 2, p->motion.position.y}, time);
          used = true;
          ++uses;
        }
        if (!potion && time > ready + 2300) {
          session.use_item({p->id, 4}, time);
          if (p->profile.stats.at(12) != -1) throw std::runtime_error("Consumable native input did not dispatch");
          potion = true;
          ++uses;
        }
      }
      session.finish_frame(time);
      maximum = std::max(maximum, world.particles().particles().size());
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::string required = argv[3];
    std::size_t begin{};
    while (begin < required.size()) {
      const auto end = required.find(',', begin);
      const int id = std::stoi(required.substr(begin, end - begin));
      if (!seen.contains(id)) {
        std::string info = "Missing real server effect=" + std::to_string(id) + " inventory replies=";
        for (int result : inv_results) info += std::to_string(result) + ",";
        if (const auto* p = world.entity(world.player_id())) {
          const auto it = p->profile.stats.find(12);
          info += " slot4=" + std::to_string(it == p->profile.stats.end() ? -999 : it->second);
        }
        throw std::runtime_error(info);
      }
      begin = end == std::string::npos ? required.size() : end + 1;
    }
    if (maximum == 0 || uses != 2) throw std::runtime_error("Live effects/uses absent");
    session.disconnect();
    if (!world.particles().particles().empty() || world.particles().emitter_count())
      throw std::runtime_error("disconnect leaked particles");
    std::cout << "Actual server ";
    for (auto id : seen) std::cout << id << ',';
    std::cout << " native particles peak=" << maximum << " USEITEM=" << uses << " and disconnect teardown passed\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
