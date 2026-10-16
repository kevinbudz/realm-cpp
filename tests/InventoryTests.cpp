#include "realm/assets/InventoryLayout.hpp"
#include "realm/game/Inventory.hpp"
#include "realm/hud/HudInput.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <iostream>

namespace {
int checks{};

void check(bool condition, const char* reason) {
  ++checks;
  if (!condition) {
    throw std::runtime_error(reason);
  }
}
}

int main() {
  try {
    realm::Catalog catalog(realm::flash::source_root);
    realm::World world(catalog);
    world.initialize(8, 8);
    const realm::FontBook fonts;
    world.add(99, catalog.object("Wizard")->type, {2.5, 3.5});
    world.set_player_id(99);
    auto& player = *world.entity(99);
    const realm::ObjectDef *staff{}, *robe{}, *sword{}, *potion{}, *invuse{}, *ability{};
    for (const auto& item : catalog.objects()) {
      if (item.class_name == "Equipment") {
        if (item.slot_type == 17 && !staff) {
          staff = &item;
        }
        if (item.slot_type == 14 && !robe) {
          robe = &item;
        }
        if (item.slot_type == 1 && !sword) {
          sword = &item;
        }
        if (item.consumable && !potion) {
          potion = &item;
        }
        if (item.inventory_use && !item.consumable && !invuse) {
          invuse = &item;
        }
        if (item.usable && !ability) {
          ability = &item;
        }
      }
    }
    check(staff && robe && sword && potion && invuse && ability, "inventory fixture definitions");
    realm::Inventory inv(world);
    const auto put = [&](int slot, const realm::ObjectDef* item, int data) {
      realm::set_inventory_item(player, slot, {item->type, data});
    };
    put(4, staff, 123);
    put(0, staff, 456);
    auto packet = inv.swap({99, 4}, {99, 0}, 77);
    check(packet && packet->id == static_cast<std::uint8_t>(realm::flash::PacketId::INVSWAP),
          "swap packet ID");
    realm::net::Reader reader(packet->body);
    check(reader.i32() == 77 && reader.f32() == 2.5F && reader.f32() == 3.5F,
          "swap uses player position/time");
    check(reader.i32() == 99 && reader.u8() == 4 && reader.i32() == 99 && reader.u8() == 0 &&
              reader.remaining() == 0,
          "swap slot wire layout");
    check(realm::inventory_item(player, 0) == realm::InventoryItem{staff->type, 123} &&
              realm::inventory_item(player, 4).data == 456,
          "paired item/data prediction");
    inv.result(1);
    check(realm::inventory_item(player, 0).data == 456 &&
              realm::inventory_item(player, 4).data == 123 && inv.pending() == 0,
          "rejected swap restores both slots");
    packet = inv.swap({99, 4}, {99, 0}, 78);
    inv.result(0);
    check(realm::inventory_item(player, 0).data == 123, "accepted swap retains prediction");
    put(5, sword, 789);
    check(!inv.swap({99, 5}, {99, 0}, 79) && inv.pending() == 0,
          "wrong equipment type rejected locally");
    put(2, robe, 55);
    check(!inv.swap({99, 0}, {99, 2}, 80), "reverse equipment constraint checked");
    check(!inv.swap({99, 0}, {99, 0}, 80), "same-slot swap ignored");
    check(!inv.swap({99, 0}, {99, 12}, 80), "backpack requires HASBACKPACK");
    player.profile.stats[69] = 1;
    packet = inv.swap({99, 0}, {99, 12}, 81);
    check(packet && realm::inventory_item(player, 12).data == 123,
          "backpack slot mapping/prediction");
    check(player.profile.stats[61] == staff->type && player.profile.stats[83] == 123,
          "backpack stat IDs");
    inv.result(1);
    packet = inv.swap({99, 0}, {99, 12}, 81);
    player.profile.stats[69] = 0;
    inv.result(1);
    check(realm::inventory_item(player, 0).data == 123 &&
              realm::inventory_item(player, 12).type == -1,
          "rollback restores retained backpack slots after feature change");
    player.profile.stats[69] = 1;
    check(!inv.swap({99, 19}, {99, 4}, 82), "empty source does not generate operation");
    packet = inv.drop(5);
    check(packet && packet->body == std::vector<std::uint8_t>{5},
          "drop packet contains only slot byte");
    check(realm::inventory_item(player, 5) == realm::InventoryItem{}, "drop clears type and data");
    inv.result(1);
    check(realm::inventory_item(player, 5).data == 789, "drop rollback");
    put(6, potion, 321);
    player.combat.hp = 77;
    player.profile.stats[4] = 88;
    packet = inv.use({99, 6}, 100);
    check(packet && packet->id == static_cast<std::uint8_t>(realm::flash::PacketId::USEITEM),
          "consumable packet ID");
    reader = realm::net::Reader(packet->body);
    check(reader.i32() == 100 && reader.i32() == 99 && reader.u8() == 6 && reader.f32() == 0 &&
              reader.f32() == 0 && reader.remaining() == 0,
          "inventory use has source zero target position");
    check(realm::inventory_item(player, 6) == realm::InventoryItem{} && player.combat.hp == 77 &&
              player.profile.stats[4] == 88,
          "consumable predicts only inventory");
    inv.result(1);
    check(realm::inventory_item(player, 6).data == 321, "consumable rollback preserves modifiers");
    put(7, invuse, 11);
    packet = inv.use({99, 7}, 101);
    check(packet && realm::inventory_item(player, 7).data == 11,
          "InvUse preserves non-consumable slot");
    inv.result(0);
    check(!inv.use({99, 5}, 102), "ordinary equipment is not usable from inventory");
    put(8, staff, 888);
    packet = inv.equip_or_use({99, 8}, 103);
    check(packet && realm::inventory_item(player, 0).data == 888,
          "double-click equips matching slot");
    inv.result(1);
    packet = inv.equip_or_use({99, 6}, 104);
    check(packet && realm::inventory_item(player, 6).type == -1, "double-click uses consumable");
    inv.result(1);
    put(1, ability, 909);
    auto ability_packet =
        realm::net::outgoing::encode(realm::net::outgoing::UseItem{105, {99, 1}, {4, 5}});
    inv.track_ability(ability_packet);
    packet = inv.drop(5);
    check(inv.pending() == 2, "ability and item action share FIFO");
    inv.result(0);
    check(inv.pending() == 1 && realm::inventory_item(player, 5).type == -1,
          "ability result does not consume drop rollback");
    inv.result(1);
    check(realm::inventory_item(player, 5).data == 789,
          "subsequent drop result restores correct operation");
    const realm::ObjectDef* container{};
    for (const auto& def : catalog.objects()) {
      if (def.class_name == "Container" && def.slot_types.size() >= 8) {
        container = &def;
        break;
      }
    }
    check(container, "container definition");
    world.add(88, container->type, {2.5, 3.5});
    realm::set_inventory_item(*world.entity(88), 0, {staff->type, 13});
    packet = inv.equip_or_use({88, 0}, 106);
    check(packet && realm::inventory_item(*world.entity(99), 9).data == 13 &&
              realm::inventory_item(*world.entity(99), 0).data == 123,
          "container double-click prioritizes free inventory over equipment");
    inv.result(1);
    packet = inv.swap({88, 0}, {99, 9}, 106);
    check(packet && realm::inventory_item(*world.entity(99), 9).data == 13,
          "container swap prediction");
    world.remove(88);
    inv.result(1);
    check(realm::inventory_item(*world.entity(99), 9).type == -1,
          "missing container rollback remains safe");
    packet = inv.drop(5);
    inv.reset();
    inv.result(1);
    check(inv.pending() == 0 && realm::inventory_item(*world.entity(99), 5).type == -1,
          "map reset clears pending snapshots");
    realm::HudInput input;
    realm::HudState::Inventory state;
    state.items.fill(staff->type);
    state.backpack = true;
    check(
        realm::HudInput::slot_at({20, 310}, 0) == 0 && realm::HudInput::slot_at({150, 310}, 0) == 3,
        "equipment hit positions");
    check(realm::HudInput::slot_at({20, 386}, 0) == 4 &&
              realm::HudInput::slot_at({150, 450}, 0) == 11,
          "inventory hit positions");
    check(realm::HudInput::slot_at({150, 450}, 2) == 19 &&
              realm::HudInput::slot_at({20, 386}, 1) == -1,
          "backpack and stats hit routing");
    check(realm::HudInput::slot_at({14, 305}, 0) == -1 &&
              realm::HudInput::slot_at({55, 310}, 0) == -1 &&
              realm::HudInput::slot_at({20, 426}, 0) == -1 &&
              realm::HudInput::slot_at({20, 427}, 0) == -1 &&
              realm::HudInput::slot_at({20, 428}, 0) == 8,
          "cuts and gaps are not slots");
    // The 2 px gap between columns 0 and 1 is x 55..56. The grid edges are x 13 to 186 and rows 384
    // to 469 (inclusive). Column 0's top-left cut hides the grid's corner pixel at (13, 384), so the
    // left edge is probed lower in the cell.
    check(realm::HudInput::slot_at({55, 386}, 0) == -1 &&
              realm::HudInput::slot_at({56, 386}, 0) == -1 &&
              realm::HudInput::slot_at({57, 386}, 0) == 5 &&
              realm::HudInput::slot_at({186, 450}, 0) == 11 &&
              realm::HudInput::slot_at({187, 450}, 0) == -1 &&
              realm::HudInput::slot_at({12, 400}, 0) == -1 &&
              realm::HudInput::slot_at({13, 400}, 0) == 4 &&
              realm::HudInput::slot_at({20, 469}, 0) == 8 &&
              realm::HudInput::slot_at({20, 470}, 0) == -1,
          "inventory grid gaps and edges");
    input.down({20, 386}, state);
    input.move({23, 386}, 0);
    check(input.dragging() == -1, "three-pixel drag threshold exclusive");
    input.move({24, 386}, 0);
    check(input.dragging() == 4, "drag starts beyond threshold");
    auto action = input.up({65, 386}, false, false, 200, state);
    check(action && action->kind == realm::InventoryActionKind::swap && action->source == 4 &&
              action->target == 5,
          "drag target dispatch");
    input.down({20, 386}, state);
    input.move({24, 386}, 0);
    action = input.up({-1, 300}, false, false, 201, state);
    check(action && action->kind == realm::InventoryActionKind::drop, "drag to world drops item");
    input.down({20, 386}, state);
    input.move({24, 386}, 0);
    action = input.up({20, 386}, false, false, 202, state);
    check(!action, "drag back to original slot is ignored");
    input.down({20, 386}, state);
    check(!input.up({20, 386}, false, false, 300, state), "first click performs no action");
    input.down({20, 386}, state);
    action = input.up({20, 386}, false, false, 549, state);
    check(action && action->kind == realm::InventoryActionKind::equip,
          "double-click under 250ms equips");
    input.down({20, 386}, state);
    (void)input.up({20, 386}, false, false, 600, state);
    input.down({20, 386}, state);
    check(!input.up({20, 386}, false, false, 850, state), "250ms double-click deadline exclusive");
    input.down({20, 386}, state);
    action = input.up({20, 386}, true, false, 900, state);
    check(action && action->kind == realm::InventoryActionKind::use, "shift-click use");
    input.down({20, 386}, state);
    action = input.up({20, 386}, false, true, 901, state);
    check(action && action->kind == realm::InventoryActionKind::transfer, "ctrl-click transfer");
    input.down({157, 360}, state);
    (void)input.up({157, 360}, false, false, 902, state);
    check(input.selected() == 2, "backpack tab selected");
    input.down({20, 386}, state);
    input.move({24, 386}, 2);
    check(input.dragging() == 12, "backpack drag slot");
    input.cancel();
    check(input.selected() == 2 && input.dragging() == -1,
          "focus loss cancels drag and preserves tab");
    input.cancel(true);
    check(input.selected() == 0, "map reset restores default tab");
    input.down({100, 360}, state);
    (void)input.up({100, 360}, false, false, 903, state);
    check(input.selected() == 1, "stats tab selected");
    input.down({20, 386}, state);
    input.move({24, 386}, 1);
    check(input.dragging() == -1 && !input.up({20, 386}, true, false, 904, state),
          "stats body has no inventory actions");
    input.down({20, 310}, state);
    input.move({24, 310}, 1);
    check(input.dragging() == 0, "equipment remains usable in stats tab");
    input.cycle_tabs(true);
    check(input.selected() == 2 && input.dragging() == -1,
          "tab hotkey cancels drag and cycles to backpack");
    input.cycle_tabs(true);
    check(input.selected() == 0, "three-tab cycle wraps");
    input.cycle_tabs(false);
    check(input.selected() == 1, "two-tab cycle includes stats");
    input.cycle_tabs(false);
    check(input.selected() == 0, "two-tab cycle excludes backpack");
    state.backpack = false;
    input.down({196, 360}, state);
    (void)input.up({196, 360}, false, false, 905, state);
    check(input.selected() == 0, "click beyond the two-tab strip is ignored");
    // The two-tab gap is x 99..100 (tab 0 ends at 98, tab 1 starts at 101).
    input.down({99, 360}, state);
    (void)input.up({99, 360}, false, false, 906, state);
    check(input.selected() == 0, "tab padding ignores clicks");
    input.down({100, 360}, state);
    (void)input.up({42, 360}, false, false, 907, state);
    check(input.selected() == 0, "release over tab without pressing it does not select");
    check(realm::inventory_tab_rect(2, 0).x == 17 &&
              realm::inventory_tab_rect(2, 0).width == 82 &&
              realm::inventory_tab_rect(2, 1).x == 101 &&
              realm::inventory_tab_rect(2, 1).width == 82 &&
              realm::inventory_tab_rect(3, 0).width == 54 &&
              realm::inventory_tab_rect(3, 2).x + realm::inventory_tab_rect(3, 2).width == 183,
          "tabs split the strip from x 17 to 183, remainder to the leftmost tabs");
    // Tabs end at HUD y 379 (backdrop row 79); the panel's light band below them (HUD rows 380..381)
    // is not a hit.
    input.down({150, 380}, state);
    (void)input.up({150, 380}, false, false, 908, state);
    check(input.selected() == 0, "panel outline below an unselected tab is not a tab hit");
    // The two-tab strip is x 17 to 182 (tab 0 is 17 to 98, tab 1 is 101 to 182). Each boundary
    // probe is run from a state where a wrong hit would change the selection.
    input.down({183, 360}, state);
    (void)input.up({183, 360}, false, false, 909, state);
    check(input.selected() == 0, "pixel 183, just past the strip, is ignored");
    input.down({182, 360}, state);
    (void)input.up({182, 360}, false, false, 910, state);
    check(input.selected() == 1, "last strip pixel 182 selects tab 1");
    input.down({16, 360}, state);
    (void)input.up({16, 360}, false, false, 911, state);
    check(input.selected() == 1, "pixel 16, just before the strip, is ignored");
    input.down({17, 360}, state);
    (void)input.up({17, 360}, false, false, 912, state);
    check(input.selected() == 0, "first strip pixel 17 selects tab 0");
    // Drag-to-store covers HUD rows 358..502 (inventory_store_top up to, not including, 503).
    input.down({20, 386}, state);
    input.move({24, 386}, 0);
    action = input.up({100, 357}, false, false, 913, state);
    check(!action, "drag released above the store area does not store");
    input.down({20, 386}, state);
    input.move({24, 386}, 0);
    action = input.up({100, 358}, false, false, 914, state);
    check(action && action->kind == realm::InventoryActionKind::store && action->source == 4,
          "drag released on the store area's top row stores");
    input.down({20, 386}, state);
    input.move({24, 386}, 0);
    action = input.up({100, 502}, false, false, 915, state);
    check(action && action->kind == realm::InventoryActionKind::store, "bottom store row stores");
    input.down({20, 386}, state);
    input.move({24, 386}, 0);
    action = input.up({100, 503}, false, false, 916, state);
    check(!action, "drag released below the store area does not store");
    const auto hud = realm::HudState::player(world, *world.entity(99));
    check(hud.stats[3].cap == 75 && hud.stats[4].cap == 40 && hud.stats[5].cap == 60,
          "HUD class cap mapping");
    world.entity(99)->profile.stats[25] = 31;
    world.entity(99)->profile.stats[50] = -5;
    world.entity(99)->profile.stats[23] = 42;
    world.entity(99)->profile.stats[48] = 2;
    const auto changed = realm::HudState::player(world, *world.entity(99));
    check(changed.stats[3].value == 31 && changed.stats[3].boost == -5 &&
              changed.stats[4].value == 42 && changed.stats[4].boost == 2,
          "HUD authoritative dexterity and vitality mapping");
    auto& hero = *world.entity(99);
    hero.profile.stats[59] = 2;
    hero.profile.stats[60] = 0;
    hero.combat.hp = hero.combat.max_hp = 100;
    hero.profile.stats[3] = 50;
    hero.profile.stats[4] = 10;
    check(!inv.use_potion(0, 1000) && inv.pending() == 0 && hero.profile.stats[59] == 2,
          "full HP sends no health potion");
    check(!inv.use_potion(1, 1000) && inv.pending() == 0, "empty magic stack sends nothing");
    hero.combat.hp = 40;
    hero.profile.stats[60] = 3;
    packet = inv.use_potion(0, 1001);
    check(packet && packet->id == static_cast<std::uint8_t>(realm::flash::PacketId::USEITEM),
          "health potion packet ID");
    reader = realm::net::Reader(packet->body);
    check(reader.i32() == 1001 && reader.i32() == 99 && reader.u8() == 254 &&
              reader.f32() == 2.5F && reader.f32() == 3.5F && reader.remaining() == 0,
          "health potion uses slot 254 at player position");
    check(hero.profile.stats[59] == 1 && hero.combat.hp == 40,
          "health potion predicts only its stack");
    packet = inv.use_potion(1, 1002);
    reader = realm::net::Reader(packet.value().body);
    (void)reader.i32();
    (void)reader.i32();
    check(reader.u8() == 255 && hero.profile.stats[60] == 2, "magic potion uses slot 255");
    inv.result(0);
    inv.result(1);
    check(hero.profile.stats[59] == 1 && hero.profile.stats[60] == 3 && inv.pending() == 0,
          "rejected potion restores the stacks captured before it was sent");
    hero.profile.stats[4] = 50;
    check(!inv.use_potion(1, 1003), "full MP sends no magic potion");
    using realm::HudInput;
    check(HudInput::potion_at({20, 474}, 0) == 0 && HudInput::potion_at({110, 474}, 2) == 1 &&
              HudInput::potion_at({20, 474}, 1) == -1,
          "potion views on both inventory tabs only");
    // The four corner pixels of the two potion slots (x 13..98 and 101..186, rows 472..495).
    check(HudInput::potion_at({13, 472}, 0) == -1 && HudInput::potion_at({13, 495}, 0) == -1 &&
              HudInput::potion_at({186, 472}, 0) == -1 && HudInput::potion_at({186, 495}, 0) == -1,
          "potion cut corners");
    // The 2 px gap between the potions is x 99..100, and the row ends at HUD 495.
    check(HudInput::potion_at({98, 474}, 0) == 0 && HudInput::potion_at({99, 474}, 0) == -1 &&
              HudInput::potion_at({100, 474}, 0) == -1 && HudInput::potion_at({101, 474}, 0) == 1 &&
              HudInput::potion_at({20, 495}, 0) == 0 && HudInput::potion_at({20, 496}, 0) == -1,
          "potion gaps");
    check(HudInput::potion_at({30, 462}, 0) == 0 && HudInput::slot_at({30, 462}, 0) == -1 &&
              HudInput::slot_at({20, 462}, 0) == 8 && HudInput::slot_at({120, 462}, 2) == -1,
          "potion icons cover the lower grid row");
    HudInput pin;
    check(!pin.up({20, 474}, false, false, 1000, state), "first potion click waits");
    action = pin.up({20, 474}, false, false, 1249, state);
    check(action && action->kind == realm::InventoryActionKind::potion && action->source == 0,
          "potion double-click under 250ms");
    (void)pin.up({20, 474}, false, false, 1300, state);
    check(!pin.up({20, 474}, false, false, 1550, state), "potion double-click deadline exclusive");
    (void)pin.up({20, 474}, false, false, 2000, state);
    pin.move({20, 444}, 0);
    check(!pin.up({20, 474}, false, false, 2100, state),
          "leaving the view cancels its pending click");
    pin.cancel();
    action = pin.up({110, 474}, true, false, 2200, state);
    check(action && action->source == 1, "shift-click uses immediately");
    (void)pin.up({20, 474}, false, false, 2300, state);
    check(!pin.up({110, 474}, false, false, 2350, state), "double-click requires the same view");
    action = pin.up({110, 474}, false, false, 2400, state);
    check(action && action->source == 1, "second view double-click");
    check(HudInput::slot_at({20, 514}, 0, true) == 20 &&
              HudInput::slot_at({150, 590}, 0, true) == 27 &&
              HudInput::slot_at({20, 514}, 1, true) == 20,
          "container slots on every tab");
    // The container's 42 px tiles span x 14 to 187 and rows 508 to 593 (inclusive). Its
    // bottom-right corner pixel is (187, 593), which the six-pixel cut removes.
    check(HudInput::slot_at({20, 514}, 0) == -1 && HudInput::slot_at({14, 508}, 0, true) == -1 &&
              HudInput::slot_at({185, 508}, 0, true) == -1 &&
              HudInput::slot_at({187, 593}, 0, true) == -1 &&
              HudInput::slot_at({14, 591}, 0, true) == -1,
          "container cuts and absence");
    check(HudInput::slot_at({187, 530}, 0, true) == 23 &&
              HudInput::slot_at({188, 530}, 0, true) == -1 &&
              HudInput::slot_at({20, 593}, 0, true) == 24 &&
              HudInput::slot_at({20, 594}, 0, true) == -1,
          "container right and bottom edges");
    auto bag_state = state;
    bag_state.container = 77;
    bag_state.items[20] = staff->type;
    HudInput bag;
    bag.down({20, 514}, bag_state);
    action = bag.up({20, 514}, true, false, 3000, bag_state);
    check(action && action->kind == realm::InventoryActionKind::use && action->source == 20,
          "container shift-click uses");
    bag.down({20, 514}, bag_state);
    check(!bag.up({20, 514}, false, true, 3001, bag_state), "container ctrl-click does nothing");
    bag.down({20, 514}, bag_state);
    (void)bag.up({20, 514}, false, false, 3100, bag_state);
    bag.down({20, 514}, bag_state);
    action = bag.up({20, 514}, false, false, 3200, bag_state);
    check(action && action->kind == realm::InventoryActionKind::equip && action->source == 20,
          "container double-click");
    bag.down({20, 514}, bag_state);
    bag.move({24, 514}, 0, true);
    action = bag.up({20, 386}, false, false, 3300, bag_state);
    check(action && action->kind == realm::InventoryActionKind::swap && action->source == 20 &&
              action->target == 4,
          "container drag into inventory");
    bag.down({20, 514}, bag_state);
    bag.move({24, 514}, 0, true);
    check(bag.dragging() == 20, "container drag");
    bag.container_changed();
    check(bag.dragging() == -1, "replaced container cancels its drag");
    bag.down({20, 386}, bag_state);
    bag.move({24, 386}, 0, true);
    bag.container_changed();
    check(bag.dragging() == 4, "replaced container keeps inventory drags");
    bag.cancel();
    hero.motion.position = {2.5, 3.5};
    world.add(77, catalog.object("Loot Bag")->type, {2.5, 3.9});
    auto* loot = world.entity(77);
    realm::set_inventory_item(*loot, 0, {robe->type, 1});
    inv.reset();
    realm::set_inventory_item(*world.entity(99), 5, {sword->type, 789});
    const auto near = realm::HudState::player(world, *world.entity(99)).inventory;
    check(near.container == 77 && near.items[20] == robe->type && near.items[21] == -1,
          "nearest container fills the interaction slots");
    packet = inv.drop_to_ground(5, 3400);
    check(packet && packet->id == static_cast<std::uint8_t>(realm::flash::PacketId::INVSWAP),
          "drop beside a container swaps into it");
    reader = realm::net::Reader(packet->body);
    (void)reader.i32();
    (void)reader.f32();
    (void)reader.f32();
    check(reader.i32() == 99 && reader.u8() == 5 && reader.i32() == 77 && reader.u8() == 1,
          "drop fills the first empty container slot");
    inv.result(1);
    for (int i = 0; i < 8; ++i) {
      realm::set_inventory_item(*loot, i, {robe->type, i});
    }
    packet = inv.drop_to_ground(5, 3401);
    check(packet && packet->id == static_cast<std::uint8_t>(realm::flash::PacketId::INVDROP),
          "full container drop becomes INVDROP");
    inv.result(1);
    world.remove(77);
    world.add(76, catalog.object("Gift Chest")->type, {2.5, 3.9});
    check(!inv.swap({99, 5}, {76, 0}, 3402), "OneWayContainer rejects swaps");
    packet = inv.drop_to_ground(5, 3403);
    check(packet && packet->id == static_cast<std::uint8_t>(realm::flash::PacketId::INVSWAP),
          "dropWithoutDestTile skips the OneWayContainer check");
    inv.result(1);
    world.entity(76)->motion.position = {2.5, 4.5};
    check(realm::HudState::player(world, *world.entity(99)).inventory.container == -1,
          "interaction distance is exclusive");
    packet = inv.drop_to_ground(5, 3404);
    check(packet && packet->id == static_cast<std::uint8_t>(realm::flash::PacketId::INVDROP),
          "drop without a nearby container");
    inv.result(1);
    check(HudInput::portal_button_at({100, 575}) && HudInput::portal_button_at({73, 566}) &&
              HudInput::portal_button_at({126, 584}),
          "portal button body and cut edges");
    check(!HudInput::portal_button_at({73, 562}) && !HudInput::portal_button_at({126, 562}) &&
              !HudInput::portal_button_at({73, 587}) && !HudInput::portal_button_at({126, 587}) &&
              !HudInput::portal_button_at({127, 575}) && !HudInput::portal_button_at({100, 588}),
          "portal button corners and bounds");
    const realm::HudState::PanelButton enter{75, realm::portal_button}, hidden{},
        other{76, realm::portal_button};
    HudInput gate;
    gate.button_down({100, 575}, enter);
    check(gate.button_up({110, 580}, enter), "portal click on the button");
    gate.button_down({100, 575}, enter);
    check(!gate.button_up({100, 600}, enter), "portal release outside cancels");
    gate.button_down({100, 540}, enter);
    check(!gate.button_up({100, 575}, enter), "portal press outside cancels");
    gate.button_down({100, 575}, hidden);
    check(!gate.button_up({100, 575}, hidden), "hidden button ignores clicks");
    gate.button_down({100, 575}, enter);
    gate.cancel();
    check(!gate.button_up({100, 575}, enter), "focus loss cancels the portal press");
    gate.button_down({100, 575}, enter);
    check(!gate.button_up({100, 575}, other), "a replaced panel's button is not clicked");
    const realm::HudState::PanelButton change{77, realm::button_panel_buttons[2]};
    check(HudInput::button_at(change.area, {66, 575}) &&
              !HudInput::button_at(realm::portal_button, {66, 575}) &&
              !HudInput::button_at(change.area, {65, 562}),
          "Change button shape");
    gate.button_down({66, 575}, change);
    check(gate.button_up({133, 570}, change), "Change button click");
    for (std::size_t kind = 0; kind < realm::button_panel_classes.size(); ++kind) {
      const auto found =
          std::find_if(catalog.objects().begin(), catalog.objects().end(), [&](const auto& object) {
            return object.class_name == realm::button_panel_classes[kind];
          });
      check(found != catalog.objects().end(), "button panel class defined");
      world.add(78, found->type, {2.5, 3.9});
      const auto panel = realm::HudState::player(world, *world.entity(99));
      check(panel.button.kind == static_cast<int>(kind) && panel.button.id == 78 &&
                panel.portal.id == -1 && panel.panel_button(fonts).id == 78 &&
                panel.panel_button(fonts).area.x == realm::button_panel_buttons[kind].x,
            "button panel target");
      world.remove(78);
    }
    check(realm::HudState::player(world, *world.entity(99)).panel_button(fonts).id == -1,
          "no panel button without a target");
    const auto class_type = [&](std::string_view name) {
      return std::find_if(catalog.objects().begin(), catalog.objects().end(),
                          [&](const auto& object) { return object.class_name == name; })
          ->type;
    };
    world.add(79, class_type("Merchant"), {2.5, 3.9});
    check(realm::HudState::player(world, *world.entity(99)).sellable.id == -1,
          "merchant without merchandise has no panel");
    auto& merchant = *world.entity(79);
    merchant.profile.stats[31] = staff->type;
    merchant.profile.stats[32] = 125;
    merchant.profile.stats[37] = 1;
    auto sale = realm::HudState::player(world, *world.entity(99));
    check(sale.sellable.id == 79 && sale.sellable.name == staff->display_id &&
              sale.sellable.icon == "item" + std::to_string(staff->type) &&
              sale.sellable.price == 125 && sale.sellable.currency == 1 && sale.sellable.button,
          "merchant stats");
    merchant.profile.stats[31] = sword->type;
    merchant.profile.stats[42] = 3;
    world.entity(99)->profile.stats[27] = 2;
    auto later = realm::HudState::player(world, *world.entity(99));
    later.retain(sale);
    check(later.sellable.name == staff->display_id && later.sellable.rank == 3 &&
              !later.sellable.button,
          "panel keeps its name; rank requirement hides the button");
    merchant.profile.stats[42] = 5;
    sale = later;
    later = realm::HudState::player(world, *world.entity(99));
    later.retain(sale);
    check(later.sellable.rank == 3, "a shown rank notice keeps its text");
    world.entity(99)->profile.stats[27] = 5;
    sale = later;
    later = realm::HudState::player(world, *world.entity(99));
    later.retain(sale);
    check(later.sellable.rank == 0 && later.sellable.button, "meeting the rank shows the button");
    world.remove(79);
    world.add(80, class_type("GuildMerchant"), {2.5, 3.9});
    sale = realm::HudState::player(world, *world.entity(99));
    check(sale.sellable.id == 80 && sale.sellable.guild && !sale.sellable.button &&
              sale.sellable.currency == 2 &&
              sale.sellable.icon == "sellicon" + std::to_string(world.entity(80)->type),
          "guild merchant needs a leader");
    world.entity(99)->profile.stats[57] = 30;
    later = realm::HudState::player(world, *world.entity(99));
    later.retain(sale);
    check(later.sellable.guild && later.sellable.button, "the guild notice is never removed");
    world.remove(80);
    world.add(81, class_type("ClosedVaultChest"), {2.5, 3.9});
    sale = realm::HudState::player(world, *world.entity(99));
    check(
        sale.sellable.name == "Vault Chest" && sale.sellable.currency == -1 && sale.sellable.button,
        "vault chest defaults");
    world.remove(81);
    world.entity(99)->profile.stats.erase(27);
    world.entity(99)->profile.stats.erase(57);
    world.remove(76);
    world.add(75, catalog.object("Nexus Portal")->type, {2.5, 3.9});
    auto view = realm::HudState::player(world, *world.entity(99));
    check(view.portal.id == 75 && view.portal.name == "Nexus Portal" && view.portal.active &&
              !view.portal.locked && view.inventory.container == -1,
          "portal display ID and default active state");
    world.entity(75)->profile.name = "Medusa (3)";
    world.entity(75)->profile.stats[34] = 0;
    view = realm::HudState::player(world, *world.entity(99));
    check(view.portal.name == "Medusa (3)" && !view.portal.active, "portal NAME and ACTIVE stats");
    world.remove(75);
    world.add(74, catalog.object("Locked Wine Cellar Portal")->type, {2.5, 3.9});
    world.entity(74)->profile.name = "Locked Locked Cellar";
    view = realm::HudState::player(world, *world.entity(99));
    check(view.portal.locked && view.portal.name == "Locked Cellar",
          "LockedPortal strips one prefix");
    world.entity(74)->motion.position = {2.5, 4.5};
    check(realm::HudState::player(world, *world.entity(99)).portal.id == -1,
          "distant portal shows no panel");
    std::cout << checks << " inventory prediction, wire, rollback and input checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
