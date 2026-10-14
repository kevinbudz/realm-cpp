#include "realm/map/partyoverlay/QuestArrow.hpp"
#include "realm/ui/tooltip/PortraitToolTip.hpp"
#include "realm/ui/tooltip/QuestToolTip.hpp"
#include "flash_assets.hpp"
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
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
    world.add(7, type, {90, 50});
    Camera camera;
    camera.configure({50, 50}, 0, 800, 600);

    // Quest.as timing: a new target is available 4 s after it arrives and reads as new for 2 s more.
    Quest quest(1);
    check(quest.target(0) == -1, "no target before QUESTOBJID");
    quest.set_object(7, 1000);
    check(quest.target(4999) == -1 && quest.target(5000) == 7, "target available four seconds after arrival");
    check(quest.is_new(6999) && !quest.is_new(7000), "target reads as new for two seconds");
    quest.set_object(9, 2000);
    check(quest.target(5000) == 9 && !quest.is_new(7000), "changing the target keeps the running timers");
    quest.set_object(-1, 3000);
    check(quest.target(9000) == -1 && quest.object_id() == -1, "-1 clears the target");
    quest.set_object(3, 9000);
    check(quest.target(12999) == -1 && quest.target(13000) == 3 && quest.is_new(14999) && !quest.is_new(15000),
          "a first target after clearing starts new timers");
    // Quest.completed: the next target waits between 5 and 15 seconds.
    quest.completed(20000);
    check(quest.target(24999) == -1 && quest.target(35000) == 3, "completion waits five to fifteen seconds");
    quest.reset();
    check(quest.object_id() == -1 && quest.target(1 << 30) == -1, "MAPINFO clears the quest");

    // QuestArrow: the arrow sits where PartyArrows would place an off-screen object.
    QuestArrow arrow;
    quest.set_object(7, 1000);
    arrow.update(world, quest, camera, {}, false, 800, 600, 4999);
    check(!arrow.visible() && arrow.tooltip() == QuestArrow::Tooltip::none, "no arrow before the target is available");
    arrow.update(world, quest, camera, {}, false, 800, 600, 5000);
    check(arrow.visible() && near(arrow.position().x, 600) && near(arrow.position().y, 325) &&
              near(std::abs(arrow.rotation()), std::numbers::pi),
          "off-screen target sits on the right edge pointing inward");
    check(arrow.tooltip() == QuestArrow::Tooltip::quest, "a new quest shows the full tooltip");
    arrow.update(world, quest, camera, {}, false, 800, 600, 7000);
    check(arrow.tooltip() == QuestArrow::Tooltip::portrait, "an expired quest shows the portrait when enabled");
    auto preferences = world.preferences();
    preferences.show_quest_portraits = false;
    world.set_preferences(preferences);
    arrow.update(world, quest, camera, {}, false, 800, 600, 7000);
    check(arrow.visible() && arrow.tooltip() == QuestArrow::Tooltip::none,
          "portraits off leaves the arrow without a tooltip");
    world.goto_entity(7, {50, 50});
    arrow.update(world, quest, camera, {}, false, 800, 600, 7000);
    check(!arrow.visible(), "an on-screen target has no arrow");
    world.goto_entity(7, {90, 50});
    arrow.update(world, quest, camera, {}, false, 800, 600, 7000);
    arrow.update(world, quest, camera, {590, 325}, true, 800, 600, 7000);
    check(arrow.hovered() && arrow.tooltip() == QuestArrow::Tooltip::quest, "hover shows the full tooltip");
    world.goto_entity(7, {10, 50});
    arrow.update(world, quest, camera, {590, 325}, true, 800, 600, 7000);
    check(arrow.hovered() && near(arrow.position().x, 600) && near(std::abs(arrow.rotation()), std::numbers::pi),
          "hover freezes the placement while the target moves");
    arrow.update(world, quest, camera, {}, true, 800, 600, 7000);
    check(!arrow.hovered() && near(arrow.position().x, 0) && near(arrow.rotation(), 0),
          "rolling out resumes the projection");
    world.remove(7);
    arrow.update(world, quest, camera, {}, true, 800, 600, 7000);
    check(!arrow.visible() && arrow.target() == 7, "a removed target hides the arrow");
    world.add(7, type, {90, 50});
    arrow.update(world, quest, camera, {590, 325}, true, 800, 600, 7000);
    check(arrow.hovered(), "a returned target can be hovered again");
    world.remove(7);
    arrow.update(world, quest, camera, {590, 325}, true, 800, 600, 7000);
    check(!arrow.hovered() && !arrow.visible(), "removing the hovered target cancels the hover");
    world.add(7, type, {90, 50});
    arrow.update(world, quest, camera, {}, false, 800, 600, 7000);
    check(arrow.visible() && arrow.hit({590, 325}), "hit tests the visible triangle");
    check(!arrow.hit({596, 333}), "outside the triangle is not a hit");

    // quest_target snapshots what the tooltips draw; QuestToolTip and PortraitToolTip redraw only on change.
    const auto target = quest_target(world, 7);
    check(target && target->type == type && !target->player && target->level == -1,
          "a non-player target snapshots without a level");
    check(!quest_target(world, 404), "a missing object has no snapshot");
    FontBook fonts;
    HudArt art;
    QuestToolTip tooltip(catalog, fonts, art);
    check(tooltip.update(*target) && !tooltip.update(*target), "quest tooltip redraws only when its target changes");
    check(tooltip.content_width() > 0 && tooltip.content_height() > 0 &&
              tooltip.image().width == tooltip.canvas_width(),
          "quest tooltip measures its content");
    const auto rebuilds = tooltip.rebuilds();
    tooltip.update(*target);
    check(tooltip.rebuilds() == rebuilds, "unchanged target reuses the surface");
    PortraitToolTip portrait(fonts, art);
    check(portrait.update(*target), "portrait tooltip renders");
    const auto full = art.portrait(target->type, target->skin, target->tex1, target->tex2);
    check(portrait.content_width() == full.width - 20 && portrait.content_height() == full.height - 20,
          "portrait is cropped ten pixels from each side");

    // GameObjectArrow.positionTooltip: an inward-pointing tooltip sits 26 pixels out, vertically centered.
    const auto placed = GameObjectArrow::tooltip_position({600, 325}, std::numbers::pi, 100, 40, 1);
    check(near(placed.x, 574 - 100) && near(placed.y, 325 - 20), "tooltip sits beside an inward arrow");
    std::cout << checks << " quest arrow checks passed\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
