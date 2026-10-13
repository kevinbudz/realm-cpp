#include "realm/map/partyoverlay/PartyArrows.hpp"
#include "realm/ui/menu/PlayerGroup.hpp"
#include "flash_assets.hpp"
#include <iostream>
#include <numbers>
namespace {
int checks{};
void check(bool x, const char* text) {
  ++checks;
  if (!x) throw std::runtime_error(text);
}
bool near(double a, double b) { return std::abs(a - b) < 1e-8; }
}
int main() {
  try {
    using namespace realm;
    Catalog catalog(flash::source_root);
    World world(catalog);
    world.initialize(100, 100);
    const auto type = catalog.object("Wizard")->type;
    world.add(99, type, {50, 50});
    world.set_player_id(99);
    Camera camera;
    camera.configure({50, 50}, 0, 800, 600);
    auto p = PartyArrows::project(camera, {90, 50});
    check(p && near(p->position.x, 600) && near(p->position.y, 325) && near(std::abs(p->rotation), std::numbers::pi),
          "right ray and inward arrow rotation");
    p = PartyArrows::project(camera, {10, 50});
    check(p && near(p->position.x, 0) && near(p->position.y, 325) && near(p->rotation, 0), "left ray");
    p = PartyArrows::project(camera, {50, 10});
    check(p && near(p->position.x, 300) && near(p->position.y, 0) && near(p->rotation, std::numbers::pi / 2),
          "top ray");
    p = PartyArrows::project(camera, {50, 90});
    check(p && near(p->position.x, 300) && near(p->position.y, 600) && near(p->rotation, -std::numbers::pi / 2),
          "bottom ray");
    check(!PartyArrows::project(camera, {50, 50}), "on screen needs no arrow");
    p = PartyArrows::project(camera, {90, 90});
    check(p && near(p->position.x, 575) && near(p->position.y, 600) && near(p->rotation, -3 * std::numbers::pi / 4),
          "bottom edge angle clamp");
    camera.configure({50, 50}, std::numbers::pi / 2, 800, 600);
    p = PartyArrows::project(camera, {90, 50});
    check(p && near(p->position.y, 0), "camera rotation changes edge");
    camera.configure({50, 50}, 0, 800, 600, 1, false);
    p = PartyArrows::project(camera, {90, 50});
    check(p && near(p->position.y, 450), "offset camera projection");
    for (const auto size : {Vec2{1280, 720}, Vec2{1600, 1200}, Vec2{3840, 2160}}) {
      camera.configure({50, 50}, 0, size.x, size.y);
      p = PartyArrows::project(camera, {99, 50});
      check(p && near(p->position.x, size.x - size.y / 3), "resized HUD allowance");
    }
    camera.configure({50, 50}, 0, 800, 600, 2);
    p = PartyArrows::project(camera, {90, 50});
    check(p && near(p->position.x, 600), "zoom keeps edge on playfield");
    camera.configure({50, 50}, 0, 800, 600);
    Party party;
    PartyArrows arrows;
    world.add(7, type, {90, 50});
    world.add(2, type, {90, 50.1});
    world.add(8, type, {50, 10});
    party.update(world, 0);
    arrows.update(world, party, camera, {}, false, 800, 600);
    check(arrows.arrows()[1].players == std::vector<std::int32_t>{8}, "party nearest ordering used");
    check(arrows.arrows()[0].players == std::vector<std::int32_t>({7, 2}), "overlaps merge in party order");
    const Vec2 hit{590, 325};
    check(arrows.hit(hit) && arrows.hit(hit)->players.size() == 2, "triangle hit");
    check(!arrows.hit({596, 333}), "outside triangle not a hit");
    arrows.update(world, party, camera, hit, true, 800, 600);
    check(arrows.hovered() && arrows.hovered()->hover, "hover art activates");
    world.goto_entity(7, {10, 50});
    world.goto_entity(2, {10, 51});
    party.update(world, 500);
    arrows.update(world, party, camera, hit, true, 800, 600);
    check(arrows.hovered() && arrows.hovered()->players == std::vector<std::int32_t>({7, 2}) &&
              near(arrows.hovered()->position.x, 600),
          "hover retains position and ordered group across movement/reorder");
    arrows.update(world, party, camera, {}, true, 800, 600);
    check(!arrows.hovered() && arrows.arrows()[0].position.x == 0, "rollout resumes projection");
    arrows.update(world, party, camera, {10, 325}, true, 800, 600);
    world.remove(7);
    arrows.update(world, party, camera, {10, 325}, true, 800, 600);
    check(!arrows.hovered() || arrows.hovered()->players.front() != 7, "removed primary cancels frozen hover");
    arrows.reset();
    world.goto_entity(2, {90, 50});
    world.goto_entity(8, {90, 51.0666666666667});
    party.update(world, 1000);
    arrows.update(world, party, camera, {}, false, 800, 600);
    check(arrows.arrows()[0].players.size() == 1 && arrows.arrows()[1].players.size() == 1,
          "strict eight pixel radius excludes boundary");
    world.goto_entity(8, {90, 51.06});
    party.update(world, 1500);
    arrows.update(world, party, camera, {}, false, 800, 600);
    check(arrows.arrows()[0].players.size() == 2, "inside eight pixel radius merges");
    world.entity(2)->combat.condition = invisible_condition;
    arrows.update(world, party, camera, {}, false, 800, 600);
    check(arrows.arrows()[0].players.size() == 2, "invisible members still have source arrows");
    auto* definition = const_cast<ObjectDef*>(catalog.object(type));
    definition->no_minimap = true;
    arrows.update(world, party, camera, {}, false, 800, 600);
    check(arrows.arrows()[0].players.size() == 2, "NoMiniMap does not suppress party arrows");
    definition->no_minimap = false;
    world.entity(2)->combat.condition = 0;
    world.tile(90, 50)->type = 0;
    check(!PartyArrows::drawn(world, *world.entity(2), camera, 600),
          "known distant square remains outside source draw radius");
    auto& near_player = world.add(6, type, {51, 50});
    check(!PartyArrows::drawn(world, near_player, camera, 600), "unknown square not drawn");
    world.tile(51, 50)->type = 0;
    check(PartyArrows::drawn(world, near_player, camera, 600), "known near square drawn");
    check(!PartyArrows::drawn(world, *world.entity(2), camera, 600), "distant square undrawn");
    arrows.update(world, party, camera, {590, 325}, true, 800, 600);
    camera.configure({50, 50}, 0, 1280, 720);
    arrows.update(world, party, camera, {590, 325}, false, 1280, 720);
    check(!arrows.hovered(), "resize clears frozen hover");
    {
      FontBook fonts;
      HudArt art;
      PlayerGroup group(catalog, fonts, art);
      const std::array ids{2, 8};
      auto rows = player_group_targets(world, ids);
      group.arrow_tooltip(rows, {600, 325}, std::numbers::pi, 1, 1);
      check(near(group.canvas_position().x + 22, 574 - (group.frame_width() - 12)), "right tooltip sits inward");
      group.close();
      group.arrow_tooltip(rows, {300, 0}, std::numbers::pi / 2, 1, 1);
      check(near(group.canvas_position().y + 22, 26), "top tooltip begins 26px inward");
      auto rebuilds = group.rebuilds();
      group.arrow_tooltip(rows, {310, 0}, std::numbers::pi / 2, 1, 1);
      check(group.rebuilds() == rebuilds, "moving anchor reuses surface");
      fonts.set_pixel_scale(2);
      group.arrow_tooltip(rows, {1200, 650}, std::numbers::pi, 1, 2);
      check(group.image().pixel_scale == 2 && group.canvas_position().x + 44 < 1200,
            "resized tooltip redraws and fits inward");
    }
    world.tile(90, 50)->type = 0;
    camera.configure({50, 50}, 0, 3840, 2160);
    check(PartyArrows::drawn(world, *world.entity(2), camera, 2160), "wide camera expands source draw radius");
    arrows.reset();
    check(!arrows.hit(hit) && !arrows.hovered(), "teardown clears groups");
    std::cout << checks << " party arrow checks passed\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
