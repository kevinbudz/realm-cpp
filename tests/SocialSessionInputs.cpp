#include "realm/messaging/GameServerConnection.hpp"
#include "flash_assets.hpp"
#include <chrono>
#include <iostream>
#include <thread>
int main(int argc, char** argv) {
  try {
    if (argc != 2) throw std::runtime_error("Expected loopback port");
    realm::Catalog catalog(realm::flash::source_root);
    realm::World world(catalog);
    realm::Session session(world);
    realm::SessionOptions options;
    options.host = "localhost";
    options.port = static_cast<std::uint16_t>(std::stoi(argv[1]));
    options.username = "fixture";
    options.password = "test-password";
    options.character_id = 7;
    session.connect(options);
    const auto start = std::chrono::steady_clock::now();
    int stage = 0;
    while (stage < 7) {
      const auto time = static_cast<std::int32_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
      if (time > 5000) throw std::runtime_error("Social loopback timeout");
      session.poll(time);
      for (const auto& event : session.take_events()) {
        if (event.id == realm::flash::PacketId::NAMERESULT) {
          if (stage == 1) {
            if (std::get<realm::net::incoming::NameResult>(event.data).success)
              throw std::runtime_error("Expected name rejection");
            session.choose_name("Renamed");
            stage = 2;
          } else if (stage == 2) {
            if (!std::get<realm::net::incoming::NameResult>(event.data).success)
              throw std::runtime_error("Expected name acceptance");
            session.create_guild("Bad Guild");
            stage = 3;
          }
        } else if (event.id == realm::flash::PacketId::GUILDRESULT) {
          if (stage == 3) {
            if (std::get<realm::net::incoming::GuildResult>(event.data).success)
              throw std::runtime_error("Expected guild rejection");
            session.create_guild("Native Guild");
            stage = 4;
          } else if (stage == 4) {
            if (!std::get<realm::net::incoming::GuildResult>(event.data).success)
              throw std::runtime_error("Expected guild acceptance");
            session.leave_guild("Renamed");
            stage = 5;
          }
        }
      }
      if (stage == 0 && session.state() == realm::SessionState::playing && world.entity(99)) {
        session.choose_name("Taken");
        session.choose_name("Duplicate");
        stage = 1;
      }
      if (stage == 5)
        if (const auto* player = world.entity(99); player && player->profile.guild.empty()) {
          session.use_portal(55);
          stage = 6;
        }
      if (stage == 6 && session.state() == realm::SessionState::playing && session.map_info().name == "Guild Hall")
        stage = 7;
      if (session.state() == realm::SessionState::failed || session.state() == realm::SessionState::disconnected)
        throw std::runtime_error(session.error());
      session.finish_frame(time);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    session.disconnect();
    std::cout << "Social requests, rejection retry, rename reconnect credentials and guild portal passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
