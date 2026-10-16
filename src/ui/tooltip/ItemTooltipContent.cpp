#include "realm/ui/tooltip/ItemTooltip.hpp"
#include "realm/objects/ItemData.hpp"
#include "realm/parameters/Settings.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace realm {
namespace {
constexpr std::array<int, 8> stat_ids{0, 3, 20, 21, 22, 28, 26, 27};
constexpr std::array<int, 8> wire_stat_ids{0, 3, 20, 21, 22, 25, 23, 24};
constexpr std::array<std::string_view, 8> stat_names{
    "Maximum HP", "Maximum MP", "Attack", "Defense", "Speed", "Dexterity", "Vitality", "Wisdom"};
constexpr std::uint32_t body_color = 0xb3b3b3;
constexpr std::uint32_t effect_color = 0xffff8f;
constexpr std::uint32_t warning_color = 16549442;
constexpr std::uint32_t better_color = 0x00ff00;
constexpr std::uint32_t worse_color = 0xff0000;
constexpr std::uint32_t special_color = 0x8a2be2;
constexpr std::uint32_t untiered_color = 9055202;

std::string number(double value) {
  // TooltipHelper truncates to three decimals before Number.toString().
  std::ostringstream out;
  out << std::fixed << std::setprecision(3) << std::trunc(value * 1000) / 1000;
  auto result = out.str();
  while (result.size() > 1 && result.back() == '0') {
    result.pop_back();
  }
  if (result.back() == '.') {
    result.pop_back();
  }
  return result;
}

int stat_index(int id) {
  const auto found = std::find(stat_ids.begin(), stat_ids.end(), id);
  return found == stat_ids.end() ? -1 : static_cast<int>(found - stat_ids.begin());
}

const ObjectDef* item_definition(const Catalog& catalog, int type) {
  return type > 0 && type <= 65535 ? catalog.object(static_cast<std::uint16_t>(type)) : nullptr;
}

RichLine paragraph(std::string text, std::uint32_t color, bool bold = false) {
  return {{{std::move(text), color}}, bold, 0};
}

std::uint32_t comparison_color(double difference) {
  if (std::abs(difference) < 1e-9) {
    return effect_color;
  }
  return difference > 0 ? better_color : worse_color;
}

std::uint32_t no_comparison_color(bool usable) {
  return usable ? better_color : effect_color;
}

double attribute_number(const Xml& xml, std::string_view name) {
  const auto value = xml.attribute(name);
  return value.empty() ? 0 : std::stod(value);
}

double round_to_two(double value) {
  return std::round(value * 100) / 100;
}

// Astrum's EquipmentToolTip hides shots, dyes and pets from the On Use list. The realm-client
// data names the star throw ShurikenAbility, so both spellings are hidden.
bool activation_ignored(std::string_view kind) {
  return kind == "Shoot" || kind == "PermaPet" || kind == "Pet" || kind == "Create" ||
         kind == "UnlockPortal" || kind == "Shuriken" || kind == "ShurikenAbility" ||
         kind == "DazeBlast";
}

// Trap and Lightning store their condition as condEffect/condDuration, not effect/duration.
std::string activation_attribute(const Xml& activate, std::string_view name) {
  if (!activate.attribute(name).empty()) {
    return std::string(name);
  }
  if (name == "effect") {
    return "condEffect";
  }
  if (name == "duration") {
    return "condDuration";
  }
  return std::string(name);
}

std::string stat_label(int id) {
  const int index = stat_index(id);
  return index < 0 ? "Invalid Stat!" : std::string(stat_names[static_cast<std::size_t>(index)]);
}

// Astrum pairs an activation with the worn item's activation of the same kind and occurrence.
Xml matching_activation(const std::vector<Xml>& current, std::size_t index,
                        const std::vector<Xml>& equipped) {
  const auto kind = current[index].text();
  std::size_t occurrence{};
  for (std::size_t i = 0; i < index; ++i) {
    if (current[i].text() == kind) {
      ++occurrence;
    }
  }
  for (const auto& candidate : equipped) {
    if (candidate.text() == kind) {
      if (occurrence == 0) {
        return candidate;
      }
      --occurrence;
    }
  }
  return {};
}

bool has_activation(const std::vector<Xml>& activations, const std::string& kind,
                    std::string_view attribute, const std::string& value) {
  return std::any_of(activations.begin(), activations.end(), [&](const Xml& candidate) {
    return candidate.text() == kind && candidate.attribute(attribute) == value;
  });
}

int effective_damage(int base, std::int32_t data) {
  return base + static_cast<int>(base * item_data::stat(data, item_data::damage));
}

double effective_rate(const ObjectDef& item, std::int32_t data) {
  const double base = item.xml.number("RateOfFire", 1);
  return base + item_data::stat(data, item_data::rate_of_fire, .05 * base);
}

double effective_cooldown(const ObjectDef& item, std::int32_t data) {
  return item.xml.number("Cooldown", .5) * (1 - item_data::stat(data, item_data::cooldown));
}
}

std::optional<ItemTooltipTarget> item_tooltip_target(const World& world, const HudState& hud,
                                                     Vec2 pointer) {
  const auto* player = world.entity(world.player_id());
  if (!player || !hud.inventory.enabled || hud.inventory.dragging >= 0) {
    return {};
  }
  ItemTooltipTarget result;
  // PotionInventory has no EquipmentToolTip, even where it overlaps the grid.
  if (HudInput::potion_at(pointer, hud.inventory.selected) >= 0) {
    return {};
  }
  const int slot = HudInput::slot_at(pointer, hud.inventory.selected, hud.inventory.container >= 0);
  if (slot >= 0) {
    result.slot = slot;
    result.owner = slot >= container_slot ? TooltipOwner::container : TooltipOwner::player;
    const auto* owner = slot >= container_slot ? world.entity(hud.inventory.container) : player;
    if (!owner) {
      return {};
    }
    result.item = inventory_item(*owner, slot >= container_slot ? slot - container_slot : slot);
  } else if (hud.sellable.id >= 0 && pointer.x >= 0 && pointer.x < 50 && pointer.y >= 500 &&
             pointer.y < 550) {
    const auto* merchant = world.entity(hud.sellable.id);
    if (!merchant) {
      return {};
    }
    const auto* definition = world.catalog().object(merchant->type);
    if (!definition || definition->class_name != "Merchant") {
      return {};
    }
    result.item = {merchant->profile.stats.value_or(flash::StatId::MERCHANDISE_TYPE_STAT, -1), -1};
    result.owner = TooltipOwner::merchant;
  } else {
    return {};
  }
  return complete_tooltip_target(world, result);
}

std::optional<ItemTooltipTarget> item_tooltip_target(const World& world, InventoryItem item,
                                                     TooltipOwner owner) {
  ItemTooltipTarget result;
  result.item = item;
  result.owner = owner;
  return complete_tooltip_target(world, result);
}

std::optional<ItemTooltipTarget> complete_tooltip_target(const World& world,
                                                         ItemTooltipTarget result) {
  const auto* player = world.entity(world.player_id());
  if (!player) {
    return {};
  }
  result.player_type = player->type;
  const auto* class_def = world.catalog().object(player->type);
  const auto* item = item_definition(world.catalog(), result.item.type);
  if (!item || item->slot_type < 0) {
    return {};
  }

  result.usable = item->slot_type == 10;
  result.usable = result.usable || item->slot_type == 0 || item->slot_type > 25;
  if (class_def) {
    result.usable =
        result.usable || std::find(class_def->slot_types.begin(), class_def->slot_types.end(),
                                   item->slot_type) != class_def->slot_types.end();
  }
  result.inventory_full = true;
  for (int i = 4; i < (player->profile.stats.value_or(69, 0) ? 20 : 12); ++i) {
    if (inventory_item(*player, i).type <= 0) {
      result.inventory_full = false;
    }
  }
  for (std::size_t i = 0; i < wire_stat_ids.size(); ++i) {
    result.stats[i] =
        player->profile.stats.value_or(static_cast<std::uint8_t>(wire_stat_ids[i]), 0);
  }
  result.equipped = {};
  if (result.usable && item->slot_type >= 1 && item->slot_type <= 25) {
    for (int i = 0; i < 4; ++i) {
      const auto worn = inventory_item(*player, i);
      const auto* worn_definition = item_definition(world.catalog(), worn.type);
      if (worn_definition && worn_definition->slot_type == item->slot_type) {
        result.equipped = worn;
        break;
      }
    }
  }
  return result;
}

ItemTooltipContent item_tooltip_content(const Catalog& catalog, const ItemTooltipTarget& target) {
  ItemTooltipContent content;
  const auto* item = item_definition(catalog, target.item.type);
  if (!item) {
    return content;
  }

  const auto& xml = item->xml;
  const bool potion = xml.has("Potion");
  const bool equippable = item->slot_type >= 1 && item->slot_type <= 25 &&
                          !(item->slot_type == 10 && (item->consumable || item->inventory_use));
  content.speciality = item->tier >= 0 ? "Tiered"
                       : potion        ? "Potion"
                       : equippable    ? "Untiered"
                                       : "Miscellaneous";
  content.speciality_color =
      item->tier >= 0 || (!equippable && !potion) ? 0xffffffU : untiered_color;
  content.title = item->display_id;
  content.title_color = content.speciality == "Untiered" ? body_color : content.speciality_color;
  content.bag_type = std::clamp(xml.integer("BagType", 0), 0, 8);

  const auto* equipped_definition = item_definition(catalog, target.equipped.type);
  const auto* equipped =
      target.usable && equipped_definition && equipped_definition->slot_type == item->slot_type
          ? equipped_definition
          : nullptr;
  const auto equipped_data = target.equipped.data;
  if (xml.has("Soulbound")) {
    content.restrictions.push_back(paragraph("Soulbound", special_color));
  }

  for (const auto extra : xml.child("ExtraTooltipData").children("EffectInfo")) {
    auto name = extra.attribute("name");
    auto description = extra.attribute("description");
    RichLine line;
    if (!name.empty()) {
      line.spans.push_back({name + ": ", body_color});
    }
    line.spans.push_back({std::move(description), effect_color});
    content.custom.push_back(std::move(line));
  }
  bool pet = false;
  for (const auto activate : xml.children("Activate")) {
    pet = pet || activate.text() == "PermaPet";
  }
  if (!item->consumable && !pet && !xml.has("Treasure") && !xml.has("PetFood") &&
      !xml.has("NoTierTag") &&
      (item->tier >= 0 || equippable || !xml.attribute("setType").empty())) {
    content.tier = item->tier >= 0                    ? "T" + std::to_string(item->tier)
                   : xml.attribute("setType").empty() ? "UT"
                                                      : "ST";
    content.tier_color = item->tier >= 0                    ? 0xffffff
                         : xml.attribute("setType").empty() ? untiered_color
                                                            : 0xff9900;
  }
  content.description = xml.child("Description").text();

  auto effect = [&](std::string name, std::string value, std::uint32_t color = effect_color) {
    RichLine line;
    if (!name.empty()) {
      line.spans.push_back({name + ": ", body_color});
    }
    line.spans.push_back({std::move(value), color});
    content.effects.push_back(std::move(line));
  };
  // Astrum's EquipmentToolTip writes every activation as an indented, dash-prefixed line.
  auto make_line = [](std::initializer_list<std::pair<std::string, std::uint32_t>> spans,
                      double x) {
    RichLine line;
    line.x = x;
    for (const auto& span : spans) {
      if (!span.first.empty()) {
        line.spans.push_back({span.first, span.second});
      }
    }
    return line;
  };
  auto activation_line = [&](std::initializer_list<std::pair<std::string, std::uint32_t>> spans) {
    content.effects.push_back(make_line(spans, 10));
  };

  if (xml.has("Projectile")) {
    const auto projectile = xml.child("Projectile");
    const double mod = item_data::stat(target.item.data, item_data::damage);
    const auto other_projectile = equipped ? equipped->xml.child("Projectile") : Xml{};
    // A spell's bolt is only the template for its nova, so its shot count and rate of fire
    // duplicate the "Spell: N shots" On Use line and are omitted.
    const auto activations = xml.children("Activate");
    const bool spell = std::any_of(activations.begin(), activations.end(), [](const Xml& activation) {
      return activation.text() == "BulletNova";
    });
    const int shots = item->num_projectiles;
    const int other_shots = equipped ? equipped->num_projectiles : 0;
    if (!spell) {
      effect("Shots", std::to_string(shots),
             equipped ? comparison_color(shots - other_shots) : no_comparison_color(target.usable));
    }

    const auto conditions = projectile.children("ConditionEffect");
    if (!conditions.empty()) {
      content.effects.push_back(paragraph("Shot Effect(s):", body_color));
    }
    for (const auto condition : conditions) {
      const auto effect_name = condition.text();
      const auto duration_text = condition.attribute("duration");
      const double duration = duration_text.empty() ? 0 : std::stod(duration_text);
      double old_duration{};
      bool found{};
      if (equipped && other_projectile) {
        for (const auto old : other_projectile.children("ConditionEffect")) {
          if (old.text() == effect_name) {
            const auto old_text = old.attribute("duration");
            old_duration = old_text.empty() ? 0 : std::stod(old_text);
            found = true;
            break;
          }
        }
      }
      const auto color =
          equipped ? (found ? comparison_color(duration - old_duration) : better_color)
                   : no_comparison_color(target.usable);
      content.effects.push_back(make_line({{"    Inflicts ", body_color},
                                           {effect_name, color},
                                           {" for ", body_color},
                                           {number(duration), color},
                                           {" secs", body_color}},
                                          0));
    }

    const int base_minimum = projectile.integer("Damage", projectile.integer("MinDamage"));
    const int base_maximum = projectile.integer("Damage", projectile.integer("MaxDamage"));
    const int minimum = base_minimum + static_cast<int>(base_minimum * mod);
    const int maximum = base_maximum + static_cast<int>(base_maximum * mod);
    const double average = (minimum + maximum) / 2.0;
    const int old_minimum =
        other_projectile.integer("Damage", other_projectile.integer("MinDamage"));
    const int old_maximum =
        other_projectile.integer("Damage", other_projectile.integer("MaxDamage"));
    const double other_average = equipped && other_projectile
                                     ? (effective_damage(old_minimum, equipped_data) +
                                        effective_damage(old_maximum, equipped_data)) /
                                           2.0
                                     : 0;
    const auto damage_color =
        equipped ? target.usable ? comparison_color(average - other_average) : effect_color
                 : no_comparison_color(target.usable);
    effect("Damage",
           std::to_string(minimum) + (minimum == maximum ? "" : " - " + std::to_string(maximum)) +
               (mod ? " (+" + std::to_string(static_cast<int>(mod * 100)) + "%)" : ""),
           damage_color);
    const double range =
        round_to_two(projectile.number("Speed") * projectile.number("LifetimeMS") / 10000);
    const double other_range = equipped && other_projectile
                                   ? round_to_two(other_projectile.number("Speed") *
                                                  other_projectile.number("LifetimeMS") / 10000)
                                   : 0;
    effect("Range", number(range),
           equipped ? target.usable ? comparison_color(range - other_range) : effect_color
                    : no_comparison_color(target.usable));
    const double rate = effective_rate(*item, target.item.data);
    const double rate_mod = item_data::stat(target.item.data, item_data::rate_of_fire,
                                            .05 * xml.number("RateOfFire", 1));
    const double other_rate = equipped ? effective_rate(*equipped, equipped_data) : 0;
    if (!spell) {
      effect("Rate of Fire",
             std::to_string(static_cast<int>(xml.number("RateOfFire", 1) * 100) +
                            static_cast<int>(rate_mod * 100)) +
                 "%" +
                 (rate_mod ? " (+" + std::to_string(static_cast<int>(rate_mod * 100)) + "%)" : ""),
             equipped ? target.usable ? comparison_color(rate - other_rate) : effect_color
                      : no_comparison_color(target.usable));
    }
    const double arc = xml.number("ArcGap", 11.25);
    const double old_arc = equipped ? equipped->xml.number("ArcGap", 11.25) : 0;
    // Arc gap spreads a volley, but a nova always fires its fixed 20-shot pattern.
    if (arc > 0 && !spell) {
      effect("Arc Gap", number(arc),
             equipped ? target.usable ? comparison_color(old_arc - arc) : effect_color
                      : no_comparison_color(target.usable));
    }
    if (projectile.has("ArmorPiercing")) {
      effect("", "Shots ignore defense of target", special_color);
    }
    if (projectile.has("Boomerang")) {
      effect("", "Shots boomerang");
    }
    if (projectile.has("MultiHit")) {
      effect("", "Shots hit multiple targets");
    }
    if (projectile.has("PassesCover")) {
      effect("", "Shots pass through obstacles");
    }
    if (projectile.has("Parametric")) {
      effect("", "Shots are parametric");
    }
  }

  const auto activates = xml.children("Activate");
  const auto equipped_activates =
      equipped ? equipped->xml.children("Activate") : std::vector<Xml>{};
  const bool has_use = std::any_of(activates.begin(), activates.end(), [](const Xml& activation) {
    return !activation_ignored(activation.text());
  });
  if (has_use) {
    content.effects.push_back(paragraph("On Use:", body_color));
  }
  for (std::size_t index = 0; index < activates.size(); ++index) {
    const auto activate = activates[index];
    const auto kind = activate.text();
    if (activation_ignored(kind)) {
      continue;
    }
    const auto matched = matching_activation(activates, index, equipped_activates);
    // Astrum compares against the matching worn activation; unusable items stay neutral.
    auto value_color = [&](std::string_view attribute) {
      if (!target.usable) {
        return effect_color;
      }
      if (!matched) {
        return better_color;
      }
      return comparison_color(attribute_number(activate, activation_attribute(activate, attribute)) -
                              attribute_number(matched, activation_attribute(matched, attribute)));
    };
    const std::string condition = activate.attribute(activation_attribute(activate, "effect"));
    const std::string stat = activate.attribute("stat");
    auto condition_color = [&]() {
      if (!target.usable) {
        return effect_color;
      }
      if (!matched) {
        return better_color;
      }
      return has_activation(equipped_activates, kind, "effect", condition) ? effect_color
                                                                          : better_color;
    };
    auto stat_color = [&]() {
      if (!target.usable) {
        return effect_color;
      }
      if (!matched) {
        return better_color;
      }
      return has_activation(equipped_activates, kind, "stat", stat) ? effect_color : better_color;
    };
    const auto value = [&](std::string_view attribute) {
      return number(attribute_number(activate, activation_attribute(activate, attribute)));
    };
    const auto sign = [&](std::string_view attribute) {
      return attribute_number(activate, attribute) > 0 ? std::string("+") : std::string();
    };
    const int stat_id = std::stoi(stat.empty() ? "-1" : stat);

    if (kind == "IncrementStat") {
      activation_line({{"-", body_color},
                       {"Increases ", body_color},
                       {stat_label(stat_id), effect_color},
                       {" by ", body_color},
                       {value("amount"), effect_color}});
    } else if (kind == "Heal" || kind == "Magic") {
      activation_line({{"-", body_color},
                       {"Heals ", body_color},
                       {value("amount"), value_color("amount")},
                       {kind == "Heal" ? " HP" : " MP", body_color}});
    } else if (kind == "HealNova" || kind == "MagicNova") {
      activation_line({{"-", body_color},
                       {"Heals ", body_color},
                       {value("amount"), value_color("amount")},
                       {kind == "HealNova" ? " HP in " : " MP in ", body_color},
                       {value("range"), value_color("range")},
                       {" sqrs", body_color}});
    } else if (kind == "StatBoostSelf") {
      activation_line({{"-", body_color},
                       {"On Self: ", body_color},
                       {sign("amount") + value("amount"), value_color("amount")},
                       {" ", body_color},
                       {stat_label(stat_id), stat_color()},
                       {" for ", body_color},
                       {value("duration"), value_color("duration")},
                       {" secs", body_color}});
    } else if (kind == "StatBoostAura") {
      activation_line({{"-", body_color},
                       {"On Allies: ", body_color},
                       {sign("amount") + value("amount"), value_color("amount")},
                       {" ", body_color},
                       {stat_label(stat_id), stat_color()},
                       {" in ", body_color},
                       {value("range"), value_color("range")},
                       {" sqrs for ", body_color},
                       {value("duration"), value_color("duration")},
                       {" secs", body_color}});
    } else if (kind == "BulletNova") {
      activation_line({{"-", body_color},
                       {"Spell: ", special_color},
                       {"20", effect_color},
                       {" shots", body_color}});
    } else if (kind == "ConditionEffectSelf") {
      activation_line({{"-", body_color},
                       {"On Self: ", body_color},
                       {condition, condition_color()},
                       {" for ", body_color},
                       {value("duration"), value_color("duration")},
                       {" secs", body_color}});
    } else if (kind == "ConditionEffectAura") {
      activation_line({{"-", body_color},
                       {"On Allies: ", body_color},
                       {condition, condition_color()},
                       {" in ", body_color},
                       {value("range"), value_color("range")},
                       {" sqrs for ", body_color},
                       {value("duration"), value_color("duration")},
                       {" secs", body_color}});
    } else if (kind == "Teleport") {
      activation_line({{"-", body_color}, {"Teleports to cursor", body_color}});
    } else if (kind == "PoisonGrenade") {
      activation_line({{"-", body_color},
                       {"Poison: Deals ", body_color},
                       {value("totalDamage"), value_color("totalDamage")},
                       {" damage in ", body_color},
                       {value("radius"), value_color("radius")},
                       {" sqrs over ", body_color},
                       {value("duration"), value_color("duration")},
                       {" secs", body_color}});
    } else if (kind == "VampireBlast") {
      activation_line({{"-", body_color},
                       {"Skull: Deals ", body_color},
                       {value("totalDamage"), value_color("totalDamage")},
                       {" damage in ", body_color},
                       {value("radius"), value_color("radius")},
                       {" sqrs and heals players there for the damage dealt", body_color}});
    } else if (kind == "Trap") {
      activation_line({{"-", body_color},
                       {"Trap: Deals ", body_color},
                       {value("totalDamage"), value_color("totalDamage")},
                       {" damage in ", body_color},
                       {value("radius"), value_color("radius")},
                       {" sqrs", body_color}});
      activation_line({{"    Applies ", body_color},
                       {condition.empty() ? "Slowed" : condition, condition_color()},
                       {" for ", body_color},
                       {value("duration"), value_color("duration")},
                       {" secs", body_color}});
    } else if (kind == "StasisBlast") {
      activation_line({{"-", body_color},
                       {"Stasis enemies within 3 sqrs for ", body_color},
                       {value("duration"), value_color("duration")},
                       {" secs", body_color}});
    } else if (kind == "Decoy") {
      activation_line({{"-", body_color},
                       {"Decoy: Lasts for ", body_color},
                       {value("duration"), value_color("duration")},
                       {" secs", body_color}});
    } else if (kind == "Lightning") {
      activation_line({{"-", body_color},
                       {"Lightning: Targets ", body_color},
                       {value("maxTargets"), value_color("maxTargets")},
                       {" enemies dealing ", body_color},
                       {value("totalDamage"), value_color("totalDamage")},
                       {" damage", body_color}});
      if (!condition.empty()) {
        activation_line({{"    Applies ", body_color},
                         {condition, condition_color()},
                         {" for ", body_color},
                         {value("duration"), value_color("duration")},
                         {" secs", body_color}});
      }
    } else if (kind == "ClearConditionEffectAura") {
      activation_line({{"-", body_color},
                       {"Removes all condition effects from allies in ", body_color},
                       {value("range"), value_color("range")},
                       {" sqrs", body_color}});
    } else if (kind == "RemoveNegativeConditions") {
      activation_line({{"-", body_color},
                       {"Removes all negative condition effects from allies in ", body_color},
                       {value("range"), value_color("range")},
                       {" sqrs", body_color}});
    } else if (kind == "ClearConditionEffectSelf" || kind == "ClearConditionsEffectSelf") {
      activation_line({{"-", body_color}, {"Removes all condition effects", body_color}});
    } else if (kind == "RemoveNegativeConditionsSelf") {
      activation_line({{"-", body_color}, {"Removes all negative condition effects", body_color}});
    } else if (kind == "Dye") {
      activation_line({{"-", body_color}, {"Changes texture of your character", body_color}});
    } else if (kind == "Backpack") {
      activation_line({{"-", body_color}, {"Adds 8 inventory spaces, once per character", body_color}});
    } else {
      activation_line({{"-", body_color}});
    }
  }

  std::array<int, 8> boosts{};
  std::array<int, 8> datas{};
  std::array<bool, 8> present{};
  std::vector<std::size_t> order;
  for (const auto activate : xml.children("ActivateOnEquip")) {
    const int index = stat_index(std::stoi(activate.attribute("stat", "-1")));
    if (index >= 0) {
      const auto i = static_cast<std::size_t>(index);
      boosts[i] += std::stoi(activate.attribute("amount", "0"));
      if (!present[i]) {
        order.push_back(i);
      }
      present[i] = true;
    }
  }
  for (std::size_t i = 0; i < 8; ++i) {
    datas[i] = static_cast<int>(
        item_data::stat(target.item.data, 1U << (8U + static_cast<unsigned>(i)), i < 2 ? 5 : 1));
    boosts[i] += datas[i];
    if (!present[i] && datas[i]) {
      order.push_back(i);
    }
    present[i] = present[i] || datas[i] != 0;
  }
  if (std::any_of(present.begin(), present.end(), [](bool value) { return value; })) {
    content.effects.push_back(paragraph("On Equip:", body_color));
    // AVM2's initial Dictionary has four key buckets; it grows after the third key.
    // Preserve insertion order for collisions until growth gives each boost its own bucket.
    const auto first_key = order.front();
    const auto last_key = order.back();
    std::stable_sort(order.begin(), order.end(), [&](auto left, auto right) {
      if (order.size() != 2) {
        return left < right;
      }
      const auto first = first_key;
      const auto second = last_key;
      const auto bucket = [&](auto key) {
        return key == second && first % 4 == second % 4 ? (key + 1) % 4 : key % 4;
      };
      return bucket(left) < bucket(right);
    });
    for (const auto i : order) {
      int other_boost{};
      if (equipped) {
        for (const auto old : equipped->xml.children("ActivateOnEquip")) {
          if (stat_index(std::stoi(old.attribute("stat", "-1"))) == static_cast<int>(i)) {
            other_boost += std::stoi(old.attribute("amount", "0"));
          }
        }
        other_boost += static_cast<int>(
            item_data::stat(equipped_data, 1U << (8U + static_cast<unsigned>(i)), i < 2 ? 5 : 1));
      }
      const auto color =
          equipped        ? target.usable ? comparison_color(boosts[i] - other_boost) : effect_color
          : target.usable ? (boosts[i] < 0 ? worse_color : better_color)
                          : effect_color;
      effect("",
             std::string("    ") + (boosts[i] > 0 ? "+" : "") + std::to_string(boosts[i]) +
                 (datas[i] > 0 ? " (+" + std::to_string(datas[i]) + ")" : "") + " " +
                 std::string(stat_names[i]),
             color);
    }
  }

  if (xml.integer("Doses") > 0) {
    const int doses = xml.integer("Doses");
    const int old = equipped ? equipped->xml.integer("Doses") : 0;
    effect("Max Doses", std::to_string(xml.integer("MaxDoses")));
    effect("Doses", std::to_string(doses),
           equipped ? target.usable ? comparison_color(doses - old) : effect_color
                    : no_comparison_color(target.usable));
  }
  const int fame_mod = static_cast<int>(item_data::stat(target.item.data, 1U << 19U, 1));
  if (xml.has("FameBonus") || fame_mod) {
    const int fame = xml.integer("FameBonus") + fame_mod;
    const int old = equipped ? equipped->xml.integer("FameBonus") +
                                   static_cast<int>(item_data::stat(equipped_data, 1U << 19U, 1))
                             : 0;
    effect("Fame Bonus",
           std::to_string(fame) + "%" + (fame_mod ? " (+" + std::to_string(fame_mod) + "%)" : ""),
           equipped ? target.usable ? comparison_color(fame - old) : effect_color
                    : no_comparison_color(target.usable));
  }
  if (item->usable && xml.has("MpCost") && !xml.has("MultiPhase")) {
    const int cost = xml.integer("MpCost");
    const int old = equipped ? equipped->xml.integer("MpCost") : 0;
    effect("MP Cost", std::to_string(cost),
           equipped ? target.usable ? comparison_color(old - cost) : effect_color
                    : no_comparison_color(target.usable));
  }
  if (item->usable && xml.has("MultiPhase")) {
    const int cost = xml.integer("MpEndCost");
    const int old = equipped ? equipped->xml.integer("MpEndCost") : 0;
    effect("MP Cost", std::to_string(cost),
           equipped ? target.usable ? comparison_color(old - cost) : effect_color
                    : no_comparison_color(target.usable));
  }
  if (item->usable) {
    const double cooldown_mod = item_data::stat(target.item.data, item_data::cooldown);
    const double cooldown = effective_cooldown(*item, target.item.data);
    const double old = equipped ? effective_cooldown(*equipped, equipped_data) : 0;
    effect("Cooldown",
           number(cooldown) + " secs" +
               (cooldown_mod ? " (-" + std::to_string(static_cast<int>(cooldown_mod * 100)) + "%)"
                             : ""),
           equipped ? target.usable ? comparison_color(old - cooldown) : effect_color
                    : no_comparison_color(target.usable));
  }
  if (xml.has("Resurrects")) {
    content.effects.push_back(paragraph("This item resurrects you from death", body_color));
  }

  auto restriction = [&](std::string text, std::uint32_t color = body_color, bool bold = false) {
    content.restrictions.push_back(paragraph(std::move(text), color, bold));
  };
  if (!target.usable) {
    if (const auto* player = item_definition(catalog, target.player_type)) {
      restriction("Not usable by " + player->display_id, warning_color, true);
    }
  }
  if (item->slot_type != 10 && item->slot_type != 9) {
    std::string classes;
    for (const auto& player : catalog.objects()) {
      if (player.player && std::find(player.slot_types.begin(), player.slot_types.end(),
                                     item->slot_type) != player.slot_types.end()) {
        if (!classes.empty()) {
          classes += ", ";
        }
        classes += player.display_id;
      }
    }
    restriction("Usable by: " + classes);
  }
  if (item->usable) {
    restriction("Press [" + std::string(key_name(target.use_special)) + "] in world to use",
                0xdedaeb);
  }
  if (equippable) {
    const bool equip = target.inventory_full || target.owner == TooltipOwner::player;
    restriction("Must be equipped to use");
    restriction(equip ? "Double-Click to equip" : "Double-Click to take");
  }
  if (item->consumable) {
    restriction("Double-Click to consume");
  }
  for (const auto requirement : xml.children("EquipRequirement")) {
    if (requirement.text() == "Stat") {
      const int id = std::stoi(requirement.attribute("stat", "-1"));
      const int value = std::stoi(requirement.attribute("value", "0"));
      const int index = stat_index(id);
      const bool met = index >= 0 && target.stats[static_cast<std::size_t>(index)] >= value;
      restriction("Requires " +
                      (id >= 0 && id < 8 ? std::string(stat_names[static_cast<std::size_t>(id)])
                                         : "Unknown Stat") +
                      " of " + std::to_string(value),
                  met ? body_color : warning_color, !met);
    }
  }
  return content;
}
}
