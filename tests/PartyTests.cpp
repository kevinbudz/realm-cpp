#include "realm/objects/Party.hpp"
#include "flash_assets.hpp"
#include <iostream>
#include <stdexcept>
namespace {
int checks{};
void check(bool ok, const char* message) {
  ++checks;
  if (!ok) throw std::runtime_error(message);
}
realm::Entity& add(realm::World& world, std::int32_t id, realm::Vec2 position, std::int32_t account) {
  auto& player = world.add(id, world.catalog().object("Wizard")->type, position);
  player.profile.stats[static_cast<std::uint8_t>(realm::flash::StatId::ACCOUNT_ID_STAT)] = account;
  return player;
}
}
int main() {
  try {
    realm::Catalog catalog(realm::flash::source_root);
    realm::World world(catalog);
    world.initialize(200, 200);
    realm::Party party;
    party.update(world, -500);
    check(party.members()[0] == -1, "no members without a player");
    add(world, 1, {100, 100}, 10);
    world.set_player_id(1);
    // A non-player object is never a member.
    world.add(2, catalog.object("Nexus Portal")->type, {100.5, 100});
    add(world, 3, {103, 100}, 30);
    add(world, 4, {101, 100}, 40);
    add(world, 5, {150, 100}, 50);    // exactly 50 tiles: within PARTY_DISTANCE_SQ
    add(world, 6, {150, 100.1}, 60);  // beyond it
    party.update(world, 0);
    check(party.members()[0] == 4 && party.members()[1] == 3 && party.members()[2] == 5 && party.members()[3] == -1,
          "nearest first within 50 tiles");
    // Party.update runs at most every 500 ms, unless an account list arrives.
    add(world, 7, {100.5, 100.5}, 70);
    party.update(world, 499);
    check(party.members()[0] == 4, "membership throttled for 500 ms");
    party.update(world, 500);
    check(party.members()[0] == 7, "membership refreshed after 500 ms");
    party.receive({0, {60}});
    check(!party.starred(6), "stars apply on the next update");
    party.update(world, 501);
    check(party.starred(6) && party.members()[0] == 6 && party.members()[1] == 7, "starred far player listed first");
    // Player.starred_ is sticky; a later list never clears it.
    party.receive({0, {}});
    party.update(world, 1001);
    check(party.starred(6) && party.members()[0] == 6, "star persists for the object");
    party.receive({1, {30}});
    check(party.ignored(30) && !party.ignored(40), "ignore list");
    party.receive({1, {40}});
    check(!party.ignored(30) && party.ignored(40), "ignore list replaced");
    // Removed players vanish on the next update; reappearing objects lose the star.
    world.remove(6);
    party.update(world, 1501);
    check(party.members()[0] == 7 && !party.starred(6), "removed member and star dropped");
    add(world, 6, {150, 100.1}, 61);
    party.update(world, 2001);
    check(!party.starred(6) && party.members()[4] == -1, "new object with an unstarred account");
    for (int i = 0; i < 10; ++i) add(world, 100 + i, {100, 99.0 - i * 0.1}, 100 + i);
    party.receive({0, {109}});
    party.update(world, 2001);
    check(party.members()[0] == 109 && party.members()[5] != -1, "list capped at six with stars first");
    party.reset();
    check(party.members()[0] == -1 && !party.starred(109), "reset clears the map's party");
    std::cout << checks << " party checks passed\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
