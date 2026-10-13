#include "realm/map/World.hpp"
#include "flash_assets.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
namespace {
int checks{};
void check(bool ok, const char* message) {
  ++checks;
  if (!ok) throw std::runtime_error(message);
}
template <class F>
void rejects(F f) {
  bool rejected{};
  try {
    f();
  } catch (const std::exception&) {
    rejected = true;
  }
  check(rejected, "malformed input accepted");
}
}
int main(int argc, char** argv) {
  try {
    realm::Catalog catalog(realm::flash::source_root);
    std::cout << "Catalog: " << catalog.objects().size() << " objects, " << catalog.grounds().size() << " grounds\n";
    for (const auto& id : catalog.unresolved_textures()) std::cout << "Unresolved texture: " << id << '\n';
    const auto* wizard = catalog.object("Wizard");
    check(wizard && wizard->type == 0x030e && wizard->player && wizard->max_hp == 100, "wizard definition");
    check(wizard->slot_types.size() == 12 && wizard->slot_types[0] == 17, "wizard slots");
    check(wizard->stat_caps == std::array<int, 6>{75, 25, 50, 75, 40, 60}, "wizard stat caps in HUD order");
    const auto* grass = catalog.ground("Grass");
    check(grass && grass->type == 0x48 && grass->random_offset && !grass->no_walk && grass->blend_priority == 6,
          "grass definition");
    for (const auto& object : catalog.objects()) check(catalog.object(object.type) == &object, "object lookup");
    for (const auto& ground : catalog.grounds()) check(catalog.ground(ground.type) == &ground, "ground lookup");
    realm::World world(catalog);
    world.load_jm(std::filesystem::path(realm::flash::source_root).parent_path() /
                  "realm-server/Resources/Worlds/Nexus/0.jm");
    check(world.width() == 142 && world.height() == 130, "Nexus dimensions");
    check(world.entities().size() > 100, "Nexus population");
    check(world.walkable(world.spawn_position().x, world.spawn_position().y), "Nexus spawn walkable");
    std::cout << "Nexus entities=" << world.entities().size() << " spawn=" << world.spawn_position().x << ','
              << world.spawn_position().y << " unresolved=" << world.unresolved_ids().size() << '\n';
    for (const auto& id : world.unresolved_ids()) std::cout << "Unresolved map ID: " << id << '\n';
    world.initialize(10, 10);
    realm::net::Update update;
    update.tiles.push_back({2, 2, grass->type});
    update.objects.push_back(
        {wizard->type, {99, {2.5F, 2.5F}, {{1, std::int32_t{80}}, {28, std::string{"Port tester"}}}}});
    world.apply(update);
    check(world.walkable(2.5, 2.5) && !world.walkable(0.5, 0.5), "tile collision");
    check(world.entity(99)->combat.hp == 80 && world.entity(99)->profile.name == "Port tester", "status stats");
    realm::net::Tick tick;
    tick.statuses.push_back({99, {4.5F, 2.5F}, {}});
    world.apply(tick);
    world.update(50);
    check(std::abs(world.entity(99)->motion.position.x - (2.5 + 100.0 / 127)) < 1e-12, "tick interpolation");
    const auto velocity = world.entity(99)->motion.velocity.x;
    world.apply(tick);
    check(world.entity(99)->motion.velocity.x == velocity, "duplicate tick retains interpolation velocity");
    world.update(200);
    check(world.entity(99)->motion.position.x == 4.5 && world.entity(99)->motion.velocity.x == 0,
          "interpolation clamp");
    world.set_player_id(99);
    tick.statuses[0].position = {9, 9};
    world.apply(tick);
    check(world.entity(99)->motion.position.x == 4.5, "local player tick leaves prediction intact");
    world.goto_entity(99, {1.5, 1.5});
    check(world.entity(99)->motion.position.x == 1.5, "goto");
    world.add(100, wizard->type, {2, 2});
    world.add(101, wizard->type, {3, 3});
    world.remove(100);
    check(!world.entity(100) && world.entity(101)->id == 101 && world.entity(99)->id == 99,
          "dense removal lookup repair");
    rejects([&] { world.initialize(-1, 2); });
    rejects([&] { world.update(-1); });
    rejects([&] { world.apply(tick, 0); });
    rejects([&] { world.load_jm_text(R"({"width":1,"height":1,"dict":[{}],"data":"AAAA"})"); });
    rejects([&] { world.load_jm_text(R"({"width":4097,"height":1,"dict":[{}],"data":""})"); });
    if (argc > 1) {
      for (const auto& entry : std::filesystem::recursive_directory_iterator(argv[1])) {
        if (entry.path().extension() == ".jm") {
          world.load_jm(entry.path());
          check(world.width() > 0, "JM map loaded");
        }
        if (entry.path().extension() == ".wmap") {
          world.load_wmap(entry.path());
          check(world.width() > 0, "WMAP map loaded");
        }
      }
    }
    std::cout << checks << " catalog/world checks passed\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
