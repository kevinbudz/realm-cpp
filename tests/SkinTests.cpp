#include "realm/ui/SkinChooser.hpp"
#include "realm/ui/menu/PlayerGroup.hpp"
#include "realm/ui/tooltip/PlayerTooltip.hpp"
#include "flash_assets.hpp"
#include <iostream>
namespace {
int checks{};
void check(bool b, const char* s) {
  ++checks;
  if (!b) throw std::runtime_error(s);
}
}
int main() {
  try {
    realm::Catalog catalog(realm::flash::source_root);
    realm::FontBook fonts;
    realm::HudArt art;
    auto data = realm::parse_char_list(
        "<Chars><Account><Stats><Credits>2000</Credits><ClassStats "
        "objectType='782'><BestFame>321</BestFame><BestLevel>20</BestLevel></ClassStats></Stats></"
        "Account><OwnedSkins>836,835,836,65536,-1</OwnedSkins><Char "
        "id='7'><ObjectType>782</ObjectType><Texture>836</Texture></Char></Chars>");
    check(data.owned_skins == std::vector<int>({836, 835}), "owned IDs parse/dedupe/range");
    check(data.best_level.at(782) == 20 && data.characters[0].skin == 836, "startup skin and level parse");
    check(catalog.skins().size() == 15, "all source skins");
    check(catalog.skin(836, 782) && !catalog.skin(835, 782), "class check");
    check(catalog.player_texture(782, 836).index == 2, "Merlin animation");
    check(catalog.portrait_type(782, 0) == 782 && catalog.portrait_type(782, 65535) == 782 &&
              catalog.portrait_type(782, 835) == 782,
          "default and invalid fallback");
    realm::SkinChooser chooser(catalog, fonts, art);
    chooser.open(782, 836, data);
    chooser.update(800, 600);
    check(chooser.rows().size() == 3 && chooser.rows()[0].id == 0 && chooser.rows()[1].id == 836 &&
              chooser.rows()[2].id == 848,
          "source class order with Classic first");
    check(chooser.owned(836) && !chooser.owned(835) && !chooser.owned(848), "only owned current-class skins select");
    check(chooser.selected() == 836 && chooser.view_height() == 277, "current skin and source height");
    check(!chooser.update(800, 600), "unchanged modal does not redraw");
    const auto lp = chooser.list_position();
    chooser.pointer_down({lp.x + 100, lp.y + 140});
    chooser.pointer_up({lp.x + 100, lp.y + 140});
    check(chooser.selected() == 836, "locked row does not select");
    chooser.pointer_down({lp.x + 100, lp.y + 10});
    chooser.pointer_up({lp.x + 100, lp.y + 10});
    check(chooser.selected() == 0, "Classic reset");
    chooser.pointer_down({lp.x + 100, lp.y + 75});
    fonts.set_pixel_scale(1.2);
    chooser.update(1280, 720);
    chooser.pointer_up({lp.x + 100, lp.y + 75});
    check(chooser.selected() == 0, "resize cancels stale press");
    chooser.close();
    check(!chooser.visible() && !chooser.take_selection(), "close clears queued action");
    chooser.open(782, 835, data);
    check(chooser.selected() == 0, "foreign current skin defaults");
    realm::World world(catalog);
    world.initialize(100, 100);
    world.add(99, 782, {50, 50});
    world.set_player_id(99);
    world.add(7, 782, {51, 50}).profile.name = "Target";
    realm::Party party;
    party.update(world, 0);
    auto target = realm::player_menu_target(world, party, 7, true);
    realm::PlayerMenu menu(catalog, fonts, art);
    menu.open(*target, {100, 100}, 800, 600, 1.2);
    const auto mr = menu.rebuilds();
    realm::PlayerGroup group(catalog, fonts, art);
    const std::array<std::int32_t, 1> ids{7};
    group.open(realm::player_group_targets(world, ids), true, {100, 100}, 800, 600, 1.2);
    const auto gr = group.rebuilds();
    const auto before = realm::HudState::player(world, *world.entity(7));
    world.entity(7)->appearance.texture = 836;
    check(
        realm::HudState::player(world, *world.entity(7)).inventory.portrait == 836 && before.inventory.portrait == 782,
        "HUD portrait follows live skin");
    check(realm::player_tooltip_target(world, 7)->skin == 836, "player tooltip live skin");
    check(menu.update(world, {100, 100}, false, 0, 1.2) && menu.rebuilds() == mr + 1,
          "open menu refreshes skin snapshot");
    check(group.refresh_appearance(world) && group.rebuilds() == gr + 1 && group.players()[0].skin == 836,
          "group refresh keeps order and appearance");
    realm::HudState state;
    state.party_members(world, *world.entity(99), party,
                        ids.size() == 1 ? std::array<std::int32_t, 6>{7, -1, -1, -1, -1, -1} : party.members(), 0);
    check(state.party.members[0]->portrait == 836, "party portrait live skin");
    for (const auto& skin : catalog.skins()) {
      check(art.contains("portrait" + std::to_string(skin.id)), "skin portrait exists");
      (void)art.image("portrait" + std::to_string(skin.id));
      for (int n = 0; n < 2; ++n)
        check(art.contains("skinwalk" + std::to_string(skin.class_type) + "-" + std::to_string(skin.id) + "-" +
                           std::to_string(n)),
              "skin walking frame exists");
    }
    check(art.cache_entries() <= 256 && art.cache_bytes() <= 8 * 1024 * 1024, "combined appearance art cache bounded");
    for (const auto size : std::array<realm::Vec2, 4>{{{800, 600}, {1280, 720}, {1600, 1200}, {3840, 2160}}}) {
      fonts.set_pixel_scale(size.y / 600);
      chooser.update(size.x, size.y);
      check(chooser.image().width == size.x && chooser.image().height == size.y,
            "drawable resolution retained surface");
    }
    fonts.set_pixel_scale(3.6);
    chooser.open(782, 836, data, true);
    chooser.update(3840, 2160, 0);
    check(chooser.change().width == 3840, "startup first upload fills viewport");
    chooser.update(3840, 2160, 200);
    check(chooser.change().width <= 375 && chooser.change().height <= 375,
          "walking preview updates a bounded sprite region");
    std::cout << checks << " skin ownership/class/selection/resize/live portrait/art checks passed\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
