#include "realm/messaging/Session.hpp"
#include "flash_assets.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* message) {
  if (!value) {
    throw std::runtime_error(message);
  }
}

realm::net::Packet packet(realm::flash::PacketId id, realm::net::Bytes body) {
  return {static_cast<std::uint8_t>(id), std::move(body)};
}
}

int main() {
  try {
    using namespace realm;
    Catalog catalog(flash::source_root);
    World world(catalog);
    std::vector<net::Packet> sent;
    Session session(world, [&](const auto& value) { sent.push_back(value); }, {});
    net::Writer map;
    map.i32(100);
    map.i32(100);
    map.utf("Tutorial");
    map.utf("Tutorial");
    map.u32(1);
    map.i32(0);
    map.boolean(true);
    map.boolean(true);
    session.replay(packet(flash::PacketId::MAPINFO, map.bytes()), 0);
    check(session.map_info().name == "Tutorial" && session.quest().object_id() == -1,
          "MAPINFO establishes a clean quest state");

    net::Writer created;
    created.i32(99);
    created.i32(1234);
    session.replay(packet(flash::PacketId::CREATE_SUCCESS, created.bytes()), 0);
    check(session.state() == SessionState::playing, "CREATE_SUCCESS starts the playing session");
    const auto wizard = catalog.object("Wizard")->type;
    world.add(99, wizard, {50, 50});

    net::Writer target;
    target.i32(7);
    session.replay(packet(flash::PacketId::QUESTOBJID, target.bytes()), 1000);
    check(session.quest().object_id() == 7 && session.quest().target(4999) == -1 &&
              session.quest().target(5000) == 7,
          "QUESTOBJID reaches the delayed quest target model");

    net::Writer completed;
    completed.i32(99);
    completed.utf("Quest Complete!");
    completed.u32(0xffffff);
    session.replay(packet(flash::PacketId::NOTIFICATION, completed.bytes()), 2000);
    check(session.quest().target(2000) == -1 && session.quest().target(6999) == -1 &&
              session.quest().target(17000) == 7,
          "the player's own Quest Complete notification delays the next target by five to fifteen "
          "seconds");

    session.join_guild("Guild Name");
    check(
        sent.size() >= 2 && sent.back().id == static_cast<std::uint8_t>(flash::PacketId::JOINGUILD),
        "accepting a guild invitation sends JOINGUILD");
    net::Reader join(sent.back().body);
    check(join.utf() == "Guild Name", "join request carries the invited guild name");

    net::Writer next_map;
    next_map.i32(100);
    next_map.i32(100);
    next_map.utf("Nexus");
    next_map.utf("Nexus");
    next_map.u32(2);
    next_map.i32(0);
    next_map.boolean(true);
    next_map.boolean(true);
    session.replay(packet(flash::PacketId::MAPINFO, next_map.bytes()), 18000);
    check(session.quest().object_id() == -1 && session.quest().target(18000) == -1,
          "a later MAPINFO resets the previous map's quest target");
    std::cout << "Quest packet replay, completion notification and guild join packet passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
