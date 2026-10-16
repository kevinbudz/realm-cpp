#include "realm/ui/tooltip/ItemTooltip.hpp"
#include "realm/objects/ItemData.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace {
int checks{};

void check(bool ok, const char* why) {
  ++checks;
  if (!ok) {
    throw std::runtime_error(why);
  }
}

bool contains(const std::vector<realm::RichLine>& lines, std::string_view text) {
  return std::any_of(lines.begin(), lines.end(), [&](const auto& line) {
    std::string value;
    for (const auto& span : line.spans) {
      value += span.text;
    }
    return value == text;
  });
}

std::uint32_t value_color(const std::vector<realm::RichLine>& lines, std::string_view prefix) {
  for (const auto& line : lines) {
    std::string value;
    for (const auto& span : line.spans) {
      value += span.text;
    }
    if (value.starts_with(prefix) && !line.spans.empty()) {
      return line.spans.back().color;
    }
  }
  return 0;
}

std::uint32_t span_color(const std::vector<realm::RichLine>& lines, std::string_view text) {
  for (const auto& line : lines) {
    for (const auto& span : line.spans) {
      if (span.text == text) {
        return span.color;
      }
    }
  }
  return 0;
}
}

int main(int argc, char** argv) {
  try {
    realm::Catalog catalog(realm::flash::source_root);
    realm::World world(catalog);
    world.initialize(8, 8);
    const auto type = [&](std::string_view name) {
      const auto* item = catalog.object(name);
      if (!item) {
        throw std::runtime_error("Missing fixture " + std::string(name));
      }
      return item->type;
    };
    world.add(99, type("Wizard"), {2.5, 3.5});
    world.set_player_id(99);
    auto& player = *world.entity(99);
    realm::set_inventory_item(player, 0, {type("Energy Staff"), -1});
    realm::set_inventory_item(
        player, 4,
        {type("Staff of the Cosmic Whole"),
         128 | static_cast<int>(realm::item_data::damage | realm::item_data::rate_of_fire)});
    realm::set_inventory_item(player, 5, {type("Robe of the Grand Sorcerer"), -1});
    realm::set_inventory_item(player, 12, {type("Prism of Apparitions"), -1});
    player.profile.stats[69] = 1;
    player.profile.stats[25] = 7;
    player.profile.stats[23] = 8;
    player.profile.stats[24] = 9;
    auto hud = realm::HudState::player(world, player);
    auto target = realm::item_tooltip_target(world, hud, {34, 400});
    check(target && target->item.type == type("Staff of the Cosmic Whole"),
          "inventory hover selects item");
    check(target->stats[5] == 7 && target->stats[6] == 8 && target->stats[7] == 9,
          "requirements read wire DEX/VIT/WIS IDs");
    auto content = realm::item_tooltip_content(catalog, *target);
    check(target->equipped.type == type("Energy Staff"),
          "tooltip target carries the matching current-class equipment item");
    check(content.title == "Staff of the Cosmic Whole" && content.tier == "T12",
          "title and tier are separate fields");
    check(contains(content.effects, "Damage: 84 - 140 (+40%)") &&
              contains(content.effects, "Range: 8.55") &&
              contains(content.effects, "Rate of Fire: 140% (+40%)"),
          "weapon effects match Flash labels and modifiers");
    check(contains(content.restrictions, "Double-Click to equip") &&
              contains(content.restrictions, "Must be equipped to use"),
          "equipment instructions match Flash");
    check(content.effects[1].spans.front().color == 0xb3b3b3 &&
              value_color(content.effects, "Damage:") == 0x00ff00,
          "modifier-adjusted weapon damage compares green against worn gear");
    auto equal_damage = *target;
    equal_damage.equipped = equal_damage.item;
    check(value_color(realm::item_tooltip_content(catalog, equal_damage).effects, "Damage:") ==
              0xffff8f,
          "equal weapon comparison is yellow");
    auto worse_damage = *target;
    worse_damage.item = {type("Energy Staff"), -1};
    worse_damage.equipped = {
        type("Staff of the Cosmic Whole"),
        128 | static_cast<int>(realm::item_data::damage | realm::item_data::rate_of_fire)};
    check(value_color(realm::item_tooltip_content(catalog, worse_damage).effects, "Damage:") ==
              0xff0000,
          "weaker weapon comparison is red including equipped ItemData modifiers");
    worse_damage.usable = false;
    check(value_color(realm::item_tooltip_content(catalog, worse_damage).effects, "Damage:") ==
              0xffff8f,
          "wrong-class item comparisons remain neutral");
    worse_damage.usable = true;
    worse_damage.equipped = {type("Robe of the Grand Sorcerer"), -1};
    check(value_color(realm::item_tooltip_content(catalog, worse_damage).effects, "Damage:") ==
              0x00ff00,
          "equipment from a different slot is ignored for comparisons");
    const auto prior_equipped = *target;
    realm::set_inventory_item(
        player, 0,
        {type("Energy Staff"),
         128 | static_cast<int>(realm::item_data::damage | realm::item_data::rate_of_fire)});
    const auto refreshed_target = realm::item_tooltip_target(world, hud, {34, 400});
    check(refreshed_target && refreshed_target->equipped.data != prior_equipped.equipped.data,
          "changing equipped ItemData refreshes the tooltip target");
    realm::set_inventory_item(player, 0, {0, -1});
    const auto cleared_target = realm::complete_tooltip_target(world, *refreshed_target);
    check(cleared_target && cleared_target->equipped.type < 0,
          "re-completing a target clears equipment removed since its previous snapshot");
    realm::set_inventory_item(player, 0, {type("Energy Staff"), -1});
    realm::ItemTooltipTarget heal_better;
    heal_better.usable = true;
    heal_better.item = {type("Fire Water"), -1};
    heal_better.equipped = {type("Health Potion"), -1};
    const auto better_heal = realm::item_tooltip_content(catalog, heal_better);
    check(span_color(better_heal.effects, "230") == 0x00ff00,
          "stronger same-slot Heal activation compares green");
    heal_better.equipped = heal_better.item;
    check(span_color(realm::item_tooltip_content(catalog, heal_better).effects, "230") == 0xffff8f,
          "equal Heal activation compares yellow");
    heal_better.item = {type("Health Potion"), -1};
    heal_better.equipped = {type("Fire Water"), -1};
    check(span_color(realm::item_tooltip_content(catalog, heal_better).effects, "100") == 0xff0000,
          "weaker same-slot Heal activation compares red");
    const auto check_lower_is_better = [&](std::string_view label, std::string_view prefix,
                                           auto qualifies, auto value) {
      const realm::ObjectDef* lower{};
      const realm::ObjectDef* higher{};
      for (const auto& candidate : catalog.objects()) {
        if (qualifies(candidate)) {
          for (const auto& worn : catalog.objects()) {
            if (worn.slot_type == candidate.slot_type && qualifies(worn) &&
                value(worn) > value(candidate)) {
              lower = &candidate;
              higher = &worn;
              break;
            }
          }
        }
      }
      if (!lower || !higher) {
        return false;
      }
      realm::ItemTooltipTarget comparison;
      comparison.usable = true;
      comparison.item = {lower->type, -1};
      comparison.equipped = {higher->type, -1};
      const auto better_message = std::string(label) + " lower value compares green",
                 equal_message = std::string(label) + " equal value compares yellow",
                 worse_message = std::string(label) + " higher value compares red";
      check(
          value_color(realm::item_tooltip_content(catalog, comparison).effects, prefix) == 0x00ff00,
          better_message.c_str());
      comparison.equipped = comparison.item;
      check(
          value_color(realm::item_tooltip_content(catalog, comparison).effects, prefix) == 0xffff8f,
          equal_message.c_str());
      comparison.item = {higher->type, -1};
      comparison.equipped = {lower->type, -1};
      check(
          value_color(realm::item_tooltip_content(catalog, comparison).effects, prefix) == 0xff0000,
          worse_message.c_str());
      return true;
    };
    check(check_lower_is_better(
              "cooldown", "Cooldown:",
              [](const auto& item) { return item.usable && item.xml.has("Cooldown"); },
              [](const auto& item) { return item.xml.number("Cooldown", .2); }),
          "cooldown comparison fixtures exist");
    check(check_lower_is_better(
              "MP cost", "MP Cost:",
              [](const auto& item) {
                return item.usable && item.xml.has("MpCost") && !item.xml.has("MultiPhase");
              },
              [](const auto& item) { return item.xml.number("MpCost"); }),
          "MP cost comparison fixtures exist");
    check(check_lower_is_better(
              "arc gap", "Arc Gap:",
              [](const auto& item) {
                return item.xml.has("Projectile") && item.xml.number("ArcGap", 11.25) > 0;
              },
              [](const auto& item) { return item.xml.number("ArcGap", 11.25); }),
          "arc gap comparison fixtures exist");
    auto tiered = realm::item_tooltip_content(
        catalog, {.item = {type("Energy Staff"), -1}, .equipped = {}, .usable = true});
    check(tiered.speciality == "Tiered" && tiered.speciality_color == 0xffffff,
          "tiered classification and name color follow Astrum metadata");
    auto potion = realm::item_tooltip_content(
        catalog, {.item = {type("Health Potion"), -1}, .equipped = {}, .usable = true});
    check(potion.speciality == "Tiered" && potion.bag_type == 0,
          "potion keeps its tier label and XML bag type");
    const auto untiered_def =
        std::find_if(catalog.objects().begin(), catalog.objects().end(), [](const auto& item) {
          return item.slot_type >= 1 && item.slot_type <= 25 &&
                 !(item.slot_type == 10 && (item.consumable || item.inventory_use)) &&
                 item.tier < 0 && !item.xml.has("Potion") && item.xml.attribute("setType").empty();
        });
    check(untiered_def != catalog.objects().end(), "untiered item fixture exists");
    realm::ItemTooltipTarget untiered_target;
    untiered_target.item = {untiered_def->type, -1};
    untiered_target.usable = true;
    const auto untiered = realm::item_tooltip_content(catalog, untiered_target);
    check(untiered.speciality == "Untiered" && untiered.title_color == 0xb3b3b3 &&
              untiered.speciality_color == 9055202 && untiered.tier_color == 9055202,
          "untiered name, subtitle and UT tag colors follow Astrum metadata");
    const auto misc_def =
        std::find_if(catalog.objects().begin(), catalog.objects().end(), [](const auto& item) {
          return (item.slot_type == 0 || item.slot_type > 25 ||
                  (item.slot_type == 10 && (item.consumable || item.inventory_use))) &&
                 item.tier < 0 && !item.xml.has("Potion");
        });
    if (misc_def != catalog.objects().end()) {
      realm::ItemTooltipTarget misc_target;
      misc_target.item = {misc_def->type, -1};
      const auto misc = realm::item_tooltip_content(catalog, misc_target);
      check(misc.speciality == "Miscellaneous" && misc.tier.empty(),
            "miscellaneous items without a tier omit the UT tag");
    }
    auto key = realm::item_tooltip_content(
        catalog, {.item = {type("Pirate Cave Key"), -1}, .equipped = {}, .usable = true});
    check(key.speciality == "Miscellaneous" && key.tier.empty(),
          "untiered slot-10 consumables without Potion metadata are miscellaneous");
    const auto unranked_potion_def =
        std::find_if(catalog.objects().begin(), catalog.objects().end(),
                     [](const auto& item) { return item.xml.has("Potion") && item.tier < 0; });
    if (unranked_potion_def != catalog.objects().end()) {
      realm::ItemTooltipTarget potion_target;
      potion_target.item = {unranked_potion_def->type, -1};
      check(realm::item_tooltip_content(catalog, potion_target).speciality == "Potion",
            "unranked potions receive the Potion speciality");
    }
    auto soulbound = realm::item_tooltip_content(
        catalog, {.item = {type("Elixir of Health 6"), -1}, .equipped = {}, .usable = true});
    check(contains(soulbound.restrictions, "Soulbound") &&
              soulbound.restrictions.front().spans.front().color == 0x8a2be2,
          "Soulbound is a purple specification");
    auto custom = realm::item_tooltip_content(
        catalog, {.item = {type("Loot Tier Potion"), -1}, .equipped = {}, .usable = true});
    check(!custom.custom.empty() && contains(custom.custom, "Duration: 30 min"),
          "ExtraTooltipData is rendered in its own custom section");
    const auto has_line = [](const std::vector<realm::RichLine>& lines, std::string_view prefix) {
      return std::any_of(lines.begin(), lines.end(), [&](const auto& line) {
        std::string value;
        for (const auto& span : line.spans) {
          value += span.text;
        }
        return value.starts_with(prefix);
      });
    };
    auto nova = realm::item_tooltip_content(
        catalog, {.item = {type("Elemental Detonation Spell"), -1}, .equipped = {}, .usable = true});
    check(!has_line(nova.effects, "Shots:") && !has_line(nova.effects, "Rate of Fire:") &&
              !has_line(nova.effects, "Arc Gap:") && has_line(nova.effects, "Damage:") &&
              contains(nova.effects, "-Spell: 20 shots"),
          "bullet-nova spells omit the duplicated shot count, rate of fire and arc gap");
    auto shuriken = realm::item_tooltip_content(
        catalog, {.item = {type("Basic Star"), -1}, .equipped = {}, .usable = true});
    check(has_line(shuriken.effects, "Shots:") && has_line(shuriken.effects, "Rate of Fire:") &&
              has_line(shuriken.effects, "Arc Gap:"),
          "shuriken keeps its projectile shot count and rate of fire");
    const auto first_with = [&](std::string_view kind) {
      for (const auto& object : catalog.objects()) {
        for (const auto activate : object.xml.children("Activate")) {
          if (activate.text() == kind) {
            return realm::item_tooltip_content(
                catalog, {.item = {object.type, -1}, .equipped = {}, .usable = true});
          }
        }
      }
      throw std::runtime_error("Missing fixture with activation " + std::string(kind));
    };
    for (const auto& object : catalog.objects()) {
      const auto activations = object.xml.children("Activate");
      const bool trap = std::any_of(activations.begin(), activations.end(),
                                    [](const auto& activation) { return activation.text() == "Trap"; });
      if (trap) {
        const auto trap_content = realm::item_tooltip_content(
            catalog, {.item = {object.type, -1}, .equipped = {}, .usable = true});
        check(has_line(trap_content.effects, "    Applies ") &&
                  !has_line(trap_content.effects, "    Applies Slowed for 0 secs") &&
                  !contains(trap_content.effects, "    Applies Paralyzed for 0 secs"),
              "trap durations come from condDuration");
      }
    }
    const auto star = realm::item_tooltip_content(
        catalog, {.item = {type("Basic Star"), -1}, .equipped = {}, .usable = true});
    check(!has_line(star.effects, "On Use:") && !has_line(star.effects, "-"),
          "shuriken throws do not list an unnamed On Use entry");
    const auto skull = first_with("VampireBlast");
    check(has_line(skull.effects, "-Skull: Deals ") && !has_line(skull.effects, "-Skull: Heals"),
          "skull describes its damage and healing without a fixed heal amount");
    const auto grenade = first_with("PoisonGrenade");
    check(has_line(grenade.effects, "-Poison: Deals ") &&
              !has_line(grenade.effects, "-Poison: Deals 0 damage"),
          "poison grenade shows its total damage over its duration");
    check(contains(first_with("Dye").effects, "-Changes texture of your character") &&
              contains(first_with("Backpack").effects, "-Adds 8 inventory spaces, once per character"),
          "dyes and backpacks describe what they do instead of a bare dash");
    hud.inventory.selected = 1;
    check(!realm::item_tooltip_target(world, hud, {34, 400}), "stats body has no item hover");
    check(realm::item_tooltip_target(world, hud, {34, 324}).has_value(),
          "equipment remains available on stats tab");
    hud.inventory.selected = 2;
    target = realm::item_tooltip_target(world, hud, {34, 400});
    check(target && target->item.type == type("Prism of Apparitions") && !target->usable,
          "backpack resolves wrong-class items");
    content = realm::item_tooltip_content(catalog, *target);
    check(contains(content.restrictions, "Not usable by Wizard") &&
              contains(content.effects, "    +2 Vitality") && contains(content.effects, "    +2 Wisdom"),
          "class warning and XML boost mapping");
    hud.inventory.dragging = 12;
    check(!realm::item_tooltip_target(world, hud, {34, 400}), "drag suppresses item tooltip");
    hud.inventory.dragging = -1;
    hud.inventory.selected = 0;
    check(!realm::item_tooltip_target(world, hud, {78, 324}) &&
              !realm::item_tooltip_target(world, hud, {55, 400}) &&
              !realm::item_tooltip_target(world, hud, {-20, 400}),
          "empty slots and gaps hide tooltip");
    check(!realm::item_tooltip_target(world, hud, {30, 480}), "potion slots have no Flash tooltip");
    world.add(77, type("Loot Bag"), {2.5, 3.9});
    realm::set_inventory_item(*world.entity(77), 0, {type("Potion of Dexterity"), -1});
    hud = realm::HudState::player(world, *world.entity(99));
    target = realm::item_tooltip_target(world, hud, {34, 528});
    check(target && target->owner == realm::TooltipOwner::container &&
              contains(realm::item_tooltip_content(catalog, *target).effects,
                       "-Increases Dexterity by 1"),
          "container resolves permanent potion effect");
    check(contains(realm::item_tooltip_content(catalog, *target).restrictions,
                   "Double-Click to consume"),
          "consumable instructions match Astrum");
    world.remove(77);
    check(!realm::item_tooltip_target(world, hud, {34, 528}),
          "removed container cannot leave stale tooltip");
    const auto merchant =
        std::find_if(catalog.objects().begin(), catalog.objects().end(),
                     [](const auto& item) { return item.class_name == "Merchant"; });
    check(merchant != catalog.objects().end(), "merchant fixture exists");
    world.add(78, merchant->type, {2.5, 3.9});
    world.entity(78)->profile.stats[31] = type("Robe of the Master");
    hud = realm::HudState::player(world, *world.entity(99));
    target = realm::item_tooltip_target(world, hud, {24, 520});
    check(target && target->owner == realm::TooltipOwner::merchant &&
              contains(realm::item_tooltip_content(catalog, *target).restrictions,
                       "Double-Click to take"),
          "merchant uses NPC instructions");
    world.entity(78)->profile.stats[31] = type("Energy Staff");
    target = realm::item_tooltip_target(world, hud, {24, 520});
    check(target && target->item.type == type("Energy Staff"), "merchant stock refreshes tooltip");
    check(!realm::item_tooltip_target(world, hud, {100, 575}), "purchase button has no item hover");
    auto trade = realm::item_tooltip_target(world, {type("Health Potion"), -1},
                                            realm::TooltipOwner::other_player);
    check(trade && contains(realm::item_tooltip_content(catalog, *trade).restrictions,
                            "Double-Click to consume"),
          "trade owner uses OTHER_PLAYER");
    for (int i = 4; i < 12; ++i) {
      realm::set_inventory_item(*world.entity(99), i, {type("Health Potion"), -1});
    }
    trade = realm::item_tooltip_target(world, {type("Health Potion"), -1},
                                       realm::TooltipOwner::other_player);
    check(!trade->inventory_full, "free backpack slots count toward inventory capacity");
    for (int i = 12; i < 20; ++i) {
      realm::set_inventory_item(*world.entity(99), i, {type("Health Potion"), -1});
    }
    trade = realm::item_tooltip_target(world, {type("Energy Staff"), -1},
                                       realm::TooltipOwner::other_player);
    check(trade->inventory_full && contains(realm::item_tooltip_content(catalog, *trade).restrictions,
                                            "Double-Click to equip"),
          "full inventory changes equipment instructions for other owners");
    realm::set_inventory_item(*world.entity(99), 4, {0, -1});
    trade = realm::item_tooltip_target(world, {type("Health Potion"), -1},
                                       realm::TooltipOwner::other_player);
    check(!trade->inventory_full, "zero-type inventory entries are empty, as in Flash");
    realm::ItemTooltipTarget fixture;
    fixture.player_type = type("Wizard");
    fixture.usable = true;
    fixture.item = {type("Cloak of the Planewalker"), -1};
    fixture.use_special = 65;
    check(contains(realm::item_tooltip_content(catalog, fixture).restrictions,
                   "Press [A] in world to use"),
          "ability tooltip uses current key binding");
    for (const auto& dimensions :
         std::array<realm::Vec2, 3>{{{800, 600}, {1280, 720}, {320, 240}}}) {
      for (const auto pointer : std::array<realm::Vec2, 4>{
               {{0, 0}, {dimensions.x, 0}, {0, dimensions.y}, {dimensions.x, dimensions.y}}}) {
        const double scale = dimensions.y / 600;
        const auto at = realm::ItemTooltip::position(pointer, static_cast<int>(dimensions.x),
                                                     static_cast<int>(dimensions.y), 260, scale);
        check(at.x + realm::ItemTooltip::padding * scale >= 12 - 1e-9 &&
                  at.y + realm::ItemTooltip::padding * scale >= 12 - 1e-9,
              "Flash placement clamps content origin to 12");
      }
    }
    if (argc == 2) {
      realm::FontBook fonts;
      realm::HudArt art;
      realm::ItemTooltip tooltip(catalog, fonts, art);
      const std::filesystem::path output(argv[1]);
      std::filesystem::create_directories(output);
      check(tooltip.update(fixture) && tooltip.visible(), "first hover builds bitmap");
      const auto rebuilds = tooltip.rebuilds();
      for (int i = 0; i < 1000; ++i) {
        check(!tooltip.update(fixture), "unchanged hover reuses bitmap");
      }
      check(tooltip.rebuilds() == rebuilds, "cached hover avoids rasterization");
      fixture.use_special = 32;
      check(tooltip.update(fixture), "binding change invalidates tooltip");
      fixture.item = {type("Staff of the Cosmic Whole"), -1};
      fixture.equipped = {type("Energy Staff"), -1};
      check(tooltip.update(fixture), "equipped item selection invalidates tooltip");
      fixture.equipped.data =
          128 | static_cast<int>(realm::item_data::damage | realm::item_data::rate_of_fire);
      check(tooltip.update(fixture), "equipped ItemData invalidates tooltip comparisons");
      fixture.equipped = {};
      fixture.item = {type("Cloak of the Planewalker"), -1};
      check(tooltip.update(fixture), "comparison fixture changes back to cloak");
      fixture.item.data = 1;
      check(tooltip.update(fixture), "item data invalidates tooltip");
      check(!tooltip.update(std::nullopt) && !tooltip.visible(), "hover exit hides tooltip");
      int tallest{};
      std::size_t rendered{};
      for (const auto& item : catalog.objects()) {
        if (item.slot_type >= 0) {
          fixture.item = {item.type, -1};
          check(tooltip.update(fixture), "all imported items rasterize");
          ++rendered;
          check(tooltip.image().height == tooltip.height() &&
                    tooltip.image().width == realm::ItemTooltip::width,
                "bitmap includes entire tooltip and padding");
          tallest = std::max(tallest, tooltip.content_height());
          if (item.id == "Robe of the Grand Sorcerer" || item.id == "Health Potion" ||
              item.id == "Cloak of the Planewalker") {
            realm::save_png(output / (item.id + ".png"), tooltip.image());
          }
        }
      }
      std::cout << rendered << " imported items rasterized; tallest content " << tallest
                << " pixels\n";
    }
    std::cout << checks << " tooltip content, hover, owner and cache checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
