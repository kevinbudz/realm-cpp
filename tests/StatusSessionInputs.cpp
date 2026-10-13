#include "realm/util/ConditionEffect.hpp"
#include "realm/messaging/GameServerConnection.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>
// Real server, status cues: a seeded Pirate Rum (slot 4) makes the CONDITION stat carry Drunk, which must
// yield the Drunk icon frame (lofiInterface2 43); then the ability spends MP and a seeded Magic Potion (slot 5)
// makes the server send a NOTIFICATION "+N" in the MP color, which must reach the floating-text queue for 2 s.
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
    int ready = -1, mp_before = -1, potion_at = -1;
    std::uint32_t base{};
    bool rum{}, ability{}, potion{}, drunk{};
    std::vector<std::uint8_t> frames;
    StatusEvent heal;
    const auto mp = [](const Entity& e) {
      const auto it = e.profile.stats.find(static_cast<std::uint8_t>(flash::StatId::MP_STAT));
      return it == e.profile.stats.end() ? -1 : it->second;
    };
    while (true) {
      const auto time = static_cast<std::int32_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
      if (time > 30000)
        throw std::runtime_error("Real status session timed out (rum=" + std::to_string(rum) +
                                 " drunk=" + std::to_string(drunk) + " ability=" + std::to_string(ability) +
                                 " potion=" + std::to_string(potion) + ")");
      session.poll(time);
      world.update(1);
      (void)session.take_events();
      if (session.state() == SessionState::failed) throw std::runtime_error(session.error());
      if (session.state() == SessionState::playing && world.entity(world.player_id()) && ready < 0) ready = time;
      if (auto* p = world.entity(world.player_id()); p && ready >= 0) {
        if (!rum && time > ready + 500) {
          if (p->combat.condition & condition::bit(condition::drunk)) throw std::runtime_error("Player already drunk");
          base = p->combat.condition;
          session.use_item({p->id, 4}, time);
          rum = true;
        }
        if (rum && !drunk && (p->combat.condition & condition::bit(condition::drunk))) {
          drunk = true;
          condition_icon_frames(p->combat.condition, 0, frames);
          // The Nexus may already carry its own bits (Invincible has no icon); Drunk adds exactly its frame.
          std::vector<std::uint8_t> before;
          condition_icon_frames(base, 0, before);
          if (std::find(frames.begin(), frames.end(), 43) == frames.end() || frames.size() != before.size() + 1)
            throw std::runtime_error("Drunk did not add exactly the Drunk icon frame");
          mp_before = mp(*p);
          ability = true;
          session.use_ability({p->motion.position.x + 4, p->motion.position.y}, time);
        }
        if (ability && !potion && mp_before > 0 && mp(*p) >= 0 && mp(*p) < mp_before - 5) {
          session.use_item({p->id, 5}, time);
          potion = true;
          potion_at = time;
        }
        if (potion)
          for (auto& event : world.take_status_events())
            if (event.object_id == p->id && event.text.size() > 1 && event.text[0] == '+') heal = event;
        if (potion && !heal.text.empty()) break;
        if (potion && time > potion_at + 4000) throw std::runtime_error("The server's heal notification never arrived");
      }
      session.finish_frame(time);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if ((heal.color & 0xffffffU) != 0x6084e0U || heal.lifetime != 2000 || heal.offset_time != 0)
      throw std::runtime_error("Notification not queued as MP-colored 2 s text: " + heal.text);
    session.disconnect();
    std::cout << "Actual server Drunk icon frame 43; NOTIFICATION '" << heal.text << "' queued as color " << std::hex
              << (heal.color & 0xffffffU) << std::dec << " for " << heal.lifetime << " ms\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
