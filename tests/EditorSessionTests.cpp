#include "realm/messaging/Session.hpp"
#include "flash_assets.hpp"
#include <iostream>
#include <stdexcept>

int main() {
  try {
    using namespace realm;
    Catalog catalog(flash::source_root);
    World world(catalog);
    SessionOptions options;
    options.game_id = -4;
    options.map_json = "{}";
    int packets{};
    Session session(world, [&](const auto&) { ++packets; }, options);
    net::Writer created;
    created.i32(99);
    created.i32(7);
    session.replay({static_cast<std::uint8_t>(flash::PacketId::CREATE_SUCCESS), created.bytes()},
                   0);
    if (session.state() != SessionState::playing) {
      throw std::runtime_error("editor test starts playing");
    }
    net::Writer reconnect;
    reconnect.i32(-1);
    session.replay({static_cast<std::uint8_t>(flash::PacketId::RECONNECT), reconnect.bytes()}, 100);
    if (session.state() != SessionState::disconnected || session.game_id() != -4 || packets != 0) {
      throw std::runtime_error(
          "leaving a map test returns to editor without opening another server connection");
    }
    const auto events = session.take_events();
    if (events.empty() || events.back().id != flash::PacketId::RECONNECT) {
      throw std::runtime_error("editor reconnect still reaches the application reset path");
    }
    std::cout << "Map test reconnect ends session and preserves editor return semantics\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
