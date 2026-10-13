#include "realm/ui/tooltip/PlayerToolTip.hpp"
#include "flash_assets.hpp"
#include <iostream>
#include <limits>

namespace {
int checks{};
void check(bool value, const char* reason) {
  ++checks;
  if (!value) throw std::runtime_error(reason);
}
}
int main(int argc, char** argv) {
  try {
    using namespace realm;
    Catalog catalog(flash::source_root);
    World world(catalog);
    world.initialize(100, 100);
    Party party;
    PartyHover hover;
    const auto type = [&](std::string_view name) { return catalog.object(name)->type; };
    world.add(99, type("Wizard"), {3, 3});
    world.set_player_id(99);
    world.entity(99)->profile.guild = "Native Guild";
    world.add(2, type("Knight"), {4, 3});
    world.add(3, type("Priest"), {5, 3});
    world.add(4, type("Rogue"), {6, 3});
    auto* member = world.entity(2);
    member->profile.name = "Tank";
    member->profile.guild = "Native Guild";
    member->combat.hp = 120;
    member->combat.max_hp = 200;
    member->profile.stats[3] = 100;
    member->profile.stats[4] = 75;
    member->profile.stats[7] = 20;
    member->profile.stats[27] = 12;
    member->profile.stats[57] = 20;
    set_inventory_item(*member, 0, {type("Sword of Acclaim"), 123});
    party.update(world, 0);
    auto displayed = HudState::player(world, *world.entity(99), &party);
    auto state = displayed;
    check(PartyHover::cell({8, 510}) == 0 && PartyHover::cell({150, 550}) == 3 && PartyHover::cell({150, 590}) == 5,
          "both columns and all three rows are reachable");
    for (const auto point : std::array<Vec2, 5>{{{5, 510}, {200, 510}, {8, 507}, {8, 600}, {-10, 530}}})
      check(PartyHover::cell(point) == -1, "outside panel cannot select a member");
    hover.update(world, party, state, displayed, {50, 520}, true, 0);
    check(hover.frozen() && hover.target() == 2, "hover selects the displayed first player");
    world.entity(2)->motion.position = {50, 50};
    world.entity(3)->motion.position = {3.1, 3};
    party.update(world, 500);
    state = HudState::player(world, *world.entity(99), &party, 500);
    check(state.party.members[0]->object_id == 3, "party membership continues updating while hovered");
    hover.update(world, party, state, displayed, {51, 520}, true, 500);
    check(state.party.members[0]->object_id == 2 && hover.target() == 2,
          "distance reorder cannot replace player beneath pointer");
    world.entity(2)->combat.hp = 20;
    world.entity(2)->profile.name = "Changed";
    state = HudState::player(world, *world.entity(99), &party, 700);
    hover.update(world, party, state, displayed, {51, 520}, true, 700);
    check(state.party.members[0]->name == "Changed" && state.party.members[0]->pulse >= 0,
          "held identities still receive name and critical HP changes");
    displayed = state;
    hover.update(world, party, state, displayed, {150, 520}, true, 700);
    check(hover.target() == 3, "moving between displayed cells selects the held identities");
    auto snapshot = player_tooltip_target(world, 2);
    check(snapshot && snapshot->name == "Changed" && snapshot->type == type("Knight") && snapshot->level == 20,
          "player tooltip captures identity and class level");
    check(snapshot->fellow_guild && snapshot->guild_rank == 20 && snapshot->stars == 12,
          "guild and account rank come from live stats");
    check(snapshot->bars[0].value == 20 && snapshot->bars[0].maximum == 200 && snapshot->bars[1].value == 75 &&
              snapshot->bars[1].maximum == 100,
          "HP and MP use authoritative values");
    check(snapshot->equipment[0].type == type("Sword of Acclaim") && snapshot->equipment[0].data == 123 &&
              snapshot->slot_types[0] == 1,
          "equipped gear preserves type, ItemData and class slots");
    check(!player_tooltip_target(world, 99) && !player_tooltip_target(world, 12345),
          "self and unknown identities have no party tooltip");
    world.add(88, type("Loot Bag"), {10, 10});
    check(!player_tooltip_target(world, 88), "non-player entity is rejected");
    world.remove(88);
    world.remove(2);
    state = HudState::player(world, *world.entity(99), &party, 800);
    hover.update(world, party, state, displayed, {50, 520}, true, 800);
    check(hover.frozen() && hover.target() == -1 && !state.party.members[0] && !player_tooltip_target(world, 2),
          "dropping hovered player hides it without substituting another beneath pointer");
    hover.update(world, party, state, displayed, {10, 350}, true, 800);
    check(!hover.frozen() && hover.target() == -1, "leaving party releases held identities");
    state = HudState::player(world, *world.entity(99), &party, 800);
    displayed = state;
    hover.update(world, party, state, displayed, {50, 520}, false, 800);
    check(!hover.frozen(), "focus loss or modal input suppression clears hover");
    hover.update(world, party, state, displayed, {50, 520}, true, 800);
    world.add(77, type("Vault Portal"), {3.1, 3});
    state = HudState::player(world, *world.entity(99), &party, 900);
    hover.update(world, party, state, displayed, {50, 520}, true, 900);
    check(!hover.frozen() && !state.party.shown, "interactive panel replacement releases hover");
    world.remove(77);
    world.remove(3);
    world.remove(4);
    party.update(world, 1500);
    state = HudState::player(world, *world.entity(99), &party, 1500);
    displayed = state;
    hover.update(world, party, state, displayed, {50, 520}, true, 1500);
    check(!hover.frozen(), "empty party cells have no tooltip");
    for (const auto dimensions : std::array<Vec2, 4>{{{800, 600}, {1280, 720}, {320, 240}, {100, 100}}}) {
      const double scale = dimensions.y / 600;
      for (const auto pointer :
           std::array<Vec2, 4>{{{0, 0}, {dimensions.x, 0}, {0, dimensions.y}, {dimensions.x, dimensions.y}}}) {
        const auto at = PlayerTooltip::position(pointer, static_cast<int>(dimensions.x), static_cast<int>(dimensions.y),
                                                218, 248, scale);
        check(at.x + 16 * scale >= 12 && at.y + 16 * scale >= 12,
              "tooltip preserves source stage-margin clamping through resizing");
      }
    }
    if (argc == 2) {
      FontBook fonts;
      HudArt art;
      PlayerTooltip tooltip(catalog, fonts, art);
      const std::filesystem::path output(argv[1]);
      std::filesystem::create_directories(output);
      check(tooltip.update(snapshot) && tooltip.visible(), "first hover builds native player tooltip");
      const auto before = tooltip.rebuilds();
      for (int i = 0; i < 1000; ++i) check(!tooltip.update(snapshot), "unchanged live state reuses rasterization");
      check(tooltip.rebuilds() == before, "unchanged hover makes no extra bitmap rebuilds");
      save_png(output / "guild.png", tooltip.image());
      snapshot->bars[0].value = 0;
      check(tooltip.update(snapshot), "server HP change invalidates retained bitmap");
      snapshot->equipment[0].data = -1;
      check(tooltip.update(snapshot), "equipment data change refreshes cached state");
      snapshot->equipment[1] = {type("Shield of Ogmur"), -1};
      snapshot->bars[1].value = 0;
      check(tooltip.update(snapshot), "insufficient mana refreshes ability appearance");
      save_png(output / "low-mana.png", tooltip.image());
      const auto dimmed = tooltip.image();
      snapshot->bars[1].value = 100;
      check(tooltip.update(snapshot), "restored mana refreshes ability appearance");
      bool icon_changed = false;
      for (int row = 158; row < 198; ++row)
        for (int col = 68; col < 108; ++col) {
          const auto at = static_cast<std::size_t>(row * PlayerTooltip::width + col) * 4;
          for (int c = 0; c < 4; ++c)
            if (dimmed.pixels[at + static_cast<std::size_t>(c)] !=
                tooltip.image().pixels[at + static_cast<std::size_t>(c)])
              icon_changed = true;
        }
      check(icon_changed, "ability tile brightens when authoritative MP becomes sufficient");
      snapshot->name = "WWWWWWWWWW";
      snapshot->guild = "WWWWWWWWWWWWWWWWWWWW";
      snapshot->guild_rank = 40;
      check(tooltip.update(snapshot) && tooltip.height() < PlayerTooltip::maximum_height,
            "long player and guild names fit bounded layout");
      save_png(output / "long-guild.png", tooltip.image());
      snapshot->guild = "";
      snapshot->fellow_guild = false;
      snapshot->name = "Player";
      snapshot->stars = -1;
      snapshot->bars[0].maximum = 0;
      snapshot->bars[0].value = -10;
      snapshot->bars[1].value = std::numeric_limits<int>::max();
      check(tooltip.update(snapshot), "missing rank, zero max and out-of-range bars render safely");
      save_png(output / "edge-values.png", tooltip.image());
      snapshot->bars[0].maximum = 200;
      snapshot->bars[0].value = 120;
      snapshot->bars[1].value = 75;
      for (const auto& def : catalog.objects())
        if (def.player) {
          snapshot->type = def.type;
          snapshot->equipment = {};
          snapshot->slot_types = {};
          for (std::size_t i = 0; i < 4 && i < def.slot_types.size(); ++i) snapshot->slot_types[i] = def.slot_types[i];
          check(tooltip.update(snapshot), "all class portraits and empty equipment slots render");
          save_png(output / (def.id + ".png"), tooltip.image());
        }
      check(!tooltip.update(std::nullopt) && !tooltip.visible(), "hover exit hides retained tooltip");
      std::cout << "Player tooltip captures: " << output << '\n';
    }
    std::cout << checks << " player tooltip and party hover checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
