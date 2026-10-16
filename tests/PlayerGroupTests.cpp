#include "realm/ui/menu/PlayerGroup.hpp"
#include "realm/minimap/Minimap.hpp"
#include "flash_assets.hpp"
#include <cmath>
#include <iostream>

namespace {
int checks{};

void check(bool value, const char* text) {
  ++checks;
  if (!value) {
    throw std::runtime_error(text);
  }
}
}

int main() {
  try {
    using namespace realm;
    Catalog catalog(flash::source_root);
    World world(catalog);
    world.initialize(100, 100);
    const auto wizard = catalog.object("Wizard")->type;
    world.add(99, wizard, {50, 50});
    world.set_player_id(99);
    world.add(7, wizard, {51, 50});
    world.add(2, wizard, {51.5, 50});
    world.add(8, wizard, {90, 90});
    world.add(3, wizard, {85, 85});
    world.add(1, catalog.object("Loot Bag")->type, {51, 50});
    AnimationLibrary animations(flash::source_root);
    Minimap map(catalog, animations, flash::source_root);
    map.update(world, *world.entity(99), 0);
    const auto hover = [&](Vec2 p) { return map.hover_players(world, *world.entity(99), p); };
    check(hover({95, 91}) == std::vector<std::int32_t>{7, 2},
          "group preserves encountered order without ID or distance sorting");
    check(hover({90, 91}).empty(), "exact five-pixel radius excludes the player");
    check(hover({90.01, 91}) == std::vector<std::int32_t>{7},
          "inside five-pixel radius includes the player");
    check(hover({178, 178}) == std::vector<std::int32_t>{8, 3},
          "off-window players group at ray-clamped corner");
    check(hover({182, 180}).empty() && hover({-1, 91}).empty(), "outside map hides tooltip");
    auto* definition = const_cast<ObjectDef*>(catalog.object(wizard));
    definition->no_minimap = true;
    check(hover({95, 91}).empty(), "NoMiniMap players excluded from hover");
    definition->no_minimap = false;
    world.goto_entity(8, {90, 70});
    const auto edge =
        map.marker_position(world, *world.entity(99), world.entity(8)->motion.position);
    check(std::abs(edge.x - 180) < 1e-9 && std::abs(edge.y - 135.5) < 1e-9, "edge clamp retains ray direction");
    world.goto_entity(8, {0, 0});
    world.goto_entity(99, {1, 1});
    map.update(world, *world.entity(99), 1.2);
    check(std::abs(map.marker_position(world, *world.entity(99), {0, 0}).x - 4) < 1e-9,
          "focus near edge uses translated map bounds");
    world.goto_entity(99, {50, 50});
    world.goto_entity(8, {90, 90});
    map.zoom_out();
    map.update(world, *world.entity(99), 0);
    check(map.marker_position(world, *world.entity(99), {51, 50}).x == 93,
          "hover projection uses current zoom");
    map.zoom_in();
    map.update(world, *world.entity(99), 0);
    const std::array<std::int32_t, 4> ids{7, 2, 1234, 99};
    auto rows = player_group_targets(world, ids);
    check(rows.size() == 2 && rows[0].id == 7 && rows[1].id == 2,
          "snapshots preserve order and omit self/dropped objects");
    world.entity(7)->combat.condition = invisible_condition;
    check(first_teleport_player(world, rows) == 2, "teleport skips invisible first row");
    world.entity(2)->combat.condition = invisible_condition;
    check(!first_teleport_player(world, rows), "all invisible has no eligible player");
    world.entity(7)->combat.condition = 0;
    check(first_teleport_player(world, rows) == 7, "eligibility rechecked against live state");
    {
      FontBook fonts;
      HudArt art;
      PlayerGroup group(catalog, fonts, art);
      rows[0].name = "Tank";
      rows[0].type = 782;
      rows[0].level = 20;
      check(group.tooltip(rows, {100, 100}, 800, 600, 1) && group.visible() && !group.menu(),
            "tooltip created");
      auto n = group.rebuilds();
      group.tooltip(rows, {101, 100}, 800, 600, 1);
      check(group.rebuilds() == n, "pointer movement doesn't rebuild");
      check(group.open(rows, true, {100, 100}, 800, 600, 1), "menu opens from snapshot");
      check(group.canvas_position().x + PlayerGroup::origin == 112 &&
                group.canvas_position().y + PlayerGroup::origin == 112,
            "left top placement");
      const Vec2 option{142, 120}, row{142, 180};
      group.update(option, true, 4000, 1);
      n = group.rebuilds();
      group.update(option, true, 4000, 1);
      check(n == group.rebuilds(), "cooldown bitmap retained when unchanged");
      group.update(option, true, 2000, 1);
      check(n + 1 == group.rebuilds(), "cooldown progress redraws");
      check(group.pointer_down(option) && !group.pointer_up(row),
            "release on player row doesn't activate");
      check(group.pointer_down(row) && !group.pointer_up(row), "player rows have no actions");
      check(group.pointer_down(option) && group.pointer_up(option),
            "cooldown click remains an action for refusal chat");
      group.open(rows, false, {100, 100}, 800, 600, 1);
      check(group.pointer_down(option) && !group.pointer_up(option),
            "map flag removes teleport hit area");
      group.open(rows, true, {100, 100}, 800, 600, 1);
      group.pointer_down(option);
      group.focus_lost();
      check(!group.pointer_up(option), "focus loss cancels press");
      group.update(option, true, 0, 1);
      group.update({90, 90}, true, 0, 1);
      check(!group.visible(), "roll out closes menu");
      group.open(rows, true, {100, 100}, 800, 600, 1);
      group.update({0, 0}, true, 0, 1);
      check(!group.visible(), "forty pixel distance closes");
      group.open(rows, true, {700, 500}, 800, 600, 1);
      check(group.canvas_position().x + 22 == 700 - (group.frame_width() + 1) - 1 &&
                group.canvas_position().y + 22 == 500 - (group.frame_height() + 1) - 1,
            "right bottom placement");
      group.tooltip({}, {}, 800, 600, 1);
      check(group.menu(), "open menu suppresses hover recomputation");
      group.close();
      group.tooltip({}, {}, 800, 600, 1);
      check(!group.visible(), "empty hover closes tooltip");
      for (const double scale : {1.2, 2., 3.6}) {
        fonts.set_pixel_scale(scale);
        group.open(rows, true, {100, 100}, 800, 600, scale);
        group.update({100, 100}, false, 4000, scale);
        check(group.image().width == static_cast<int>(std::ceil(group.canvas_width() * scale)),
              "drawable-resolution surface");
      }
      fonts.set_pixel_scale(1);
      group.open(rows, true, {100, 100}, 800, 600, 1);
      group.pointer_down(option);
      fonts.set_pixel_scale(1.2);
      group.update(option, false, 0, 1.2);
      check(group.menu() && !group.pointer_up({148, 122}),
            "resize retains menu and cancels stale press");
      group.close();
      rows.resize(200, rows[0]);
      group.render_state(rows, true, true, false, 0);
      check(group.canvas_height() <= 644, "dense group surface bounded to visible window");
    }
    world.remove(7);
    check(!first_teleport_player(world, rows), "dropped target not dereferenced or selected");
    std::cout << checks << " minimap grouping/menu checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
