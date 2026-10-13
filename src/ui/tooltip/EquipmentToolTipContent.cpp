#include "realm/ui/tooltip/EquipmentToolTip.hpp"
#include "realm/util/ItemData.hpp"
#include "realm/parameters/Parameters.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace realm {
namespace {
constexpr std::array<int, 8> stat_ids{0, 3, 20, 21, 22, 28, 26, 27};
constexpr std::array<int, 8> wire_stat_ids{0, 3, 20, 21, 22, 25, 23, 24};
constexpr std::array<std::string_view, 8> stat_names{"Maximum HP", "Maximum MP", "Attack",   "Defense",
                                                     "Speed",      "Dexterity",  "Vitality", "Wisdom"};
constexpr std::uint32_t body_color = 0xb3b3b3, effect_color = 0xffff8f, warning_color = 16549442;
constexpr std::uint32_t better_color = 0x00ff00, worse_color = 0xff0000, special_color = 0x8a2be2,
                        untiered_color = 9055202;
std::string number(double value) {
  // TooltipHelper truncates to three decimals before Number.toString().
  std::ostringstream out;
  out << std::fixed << std::setprecision(3) << std::trunc(value * 1000) / 1000;
  auto result = out.str();
  while (result.size() > 1 && result.back() == '0') result.pop_back();
  if (result.back() == '.') result.pop_back();
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
  if (std::abs(difference) < 1e-9) return effect_color;
  return difference > 0 ? better_color : worse_color;
}
std::uint32_t no_comparison_color(bool usable) { return usable ? better_color : effect_color; }
double attribute_number(const Xml& xml, std::string_view name) {
  const auto value = xml.attribute(name);
  return value.empty() ? 0 : std::stod(value);
}
double round_to_two(double value) { return std::round(value * 100) / 100; }
bool same_activation(const Xml& left, const Xml& right) {
  if (left.text() != right.text()) return false;
  const auto kind = left.text();
  if (!left.attribute("stat").empty() || !right.attribute("stat").empty())
    return left.attribute("stat") == right.attribute("stat");
  if (!left.attribute("effect").empty() || !right.attribute("effect").empty())
    return left.attribute("effect") == right.attribute("effect");
  return true;
}
Xml matching_activation(const std::vector<Xml>& current, std::size_t index, const std::vector<Xml>& equipped) {
  const auto& selected = current[index];
  std::size_t occurrence{};
  for (std::size_t i = 0; i < index; ++i)
    if (same_activation(current[i], selected)) ++occurrence;
  for (const auto& candidate : equipped)
    if (same_activation(candidate, selected)) {
      if (occurrence == 0) return candidate;
      --occurrence;
    }
  return {};
}
int effective_damage(int base, std::int32_t data) {
  return base + static_cast<int>(base * item_data::stat(data, item_data::damage));
}
double effective_rate(const ObjectDef& item, std::int32_t data) {
  const double base = item.xml.number("RateOfFire", 1);
  return base + item_data::stat(data, item_data::rate_of_fire, .05 * base);
}
double effective_cooldown(const ObjectDef& item, std::int32_t data) {
  return item.xml.number("Cooldown", .2) * (1 - item_data::stat(data, item_data::cooldown));
}
}
std::optional<ItemTooltipTarget> item_tooltip_target(const World& world, const HudState& hud, Vec2 pointer) {
  const auto* player = world.entity(world.player_id());
  if (!player || !hud.inventory.enabled || hud.inventory.dragging >= 0) return {};
  ItemTooltipTarget result;
  // PotionInventory has no EquipmentToolTip, even where it overlaps the grid.
  if (HudInput::potion_at(pointer, hud.inventory.selected) >= 0) return {};
  const int slot = HudInput::slot_at(pointer, hud.inventory.selected, hud.inventory.container >= 0);
  if (slot >= 0) {
    result.slot = slot;
    result.owner = slot >= container_slot ? TooltipOwner::container : TooltipOwner::player;
    const auto* owner = slot >= container_slot ? world.entity(hud.inventory.container) : player;
    if (!owner) return {};
    result.item = inventory_item(*owner, slot >= container_slot ? slot - container_slot : slot);
  } else if (hud.sellable.id >= 0 && pointer.x >= 0 && pointer.x < 50 && pointer.y >= 500 && pointer.y < 550) {
    const auto* merchant = world.entity(hud.sellable.id);
    if (!merchant) return {};
    const auto* definition = world.catalog().object(merchant->type);
    if (!definition || definition->class_name != "Merchant") return {};
    result.item = {merchant->profile.stats.value_or(flash::StatId::MERCHANDISE_TYPE_STAT, -1), -1};
    result.owner = TooltipOwner::merchant;
  } else
    return {};
  return complete_tooltip_target(world, result);
}
std::optional<ItemTooltipTarget> item_tooltip_target(const World& world, InventoryItem item, TooltipOwner owner) {
  ItemTooltipTarget result;
  result.item = item;
  result.owner = owner;
  return complete_tooltip_target(world, result);
}
std::optional<ItemTooltipTarget> complete_tooltip_target(const World& world, ItemTooltipTarget result) {
  const auto* player = world.entity(world.player_id());
  if (!player) return {};
  result.player_type = player->type;
  const auto* class_def = world.catalog().object(player->type);
  const auto* item = item_definition(world.catalog(), result.item.type);
  if (!item || item->slot_type < 0) return {};
  result.usable = item->slot_type == 10;
  result.usable = result.usable || item->slot_type == 0 || item->slot_type > 25;
  if (class_def)
    result.usable = result.usable || std::find(class_def->slot_types.begin(), class_def->slot_types.end(),
                                               item->slot_type) != class_def->slot_types.end();
  result.inventory_full = true;
  for (int i = 4; i < (player->profile.stats.value_or(69, 0) ? 20 : 12); ++i)
    if (inventory_item(*player, i).type <= 0) result.inventory_full = false;
  for (std::size_t i = 0; i < wire_stat_ids.size(); ++i)
    result.stats[i] = player->profile.stats.value_or(static_cast<std::uint8_t>(wire_stat_ids[i]), 0);
  result.equipped = {};
  if (result.usable && item->slot_type >= 1 && item->slot_type <= 25)
    for (int i = 0; i < 4; ++i) {
      const auto worn = inventory_item(*player, i);
      const auto* worn_definition = item_definition(world.catalog(), worn.type);
      if (worn_definition && worn_definition->slot_type == item->slot_type) {
        result.equipped = worn;
        break;
      }
    }
  return result;
}
ItemTooltipContent item_tooltip_content(const Catalog& catalog, const ItemTooltipTarget& target) {
  ItemTooltipContent content;
  const auto* item = item_definition(catalog, target.item.type);
  if (!item) return content;
  const auto& xml = item->xml;
  const bool potion = xml.has("Potion"),
             equippable = item->slot_type >= 1 && item->slot_type <= 25 &&
                          !(item->slot_type == 10 && (item->consumable || item->inventory_use));
  content.speciality = item->tier >= 0 ? "Tiered" : potion ? "Potion" : equippable ? "Untiered" : "Miscellaneous";
  content.speciality_color = item->tier >= 0 || (!equippable && !potion) ? 0xffffffU : untiered_color;
  content.title = item->display_id;
  content.title_color = content.speciality == "Untiered" ? body_color : content.speciality_color;
  content.bag_type = std::clamp(xml.integer("BagType", 0), 0, 8);
  const auto* equipped_definition = item_definition(catalog, target.equipped.type);
  const auto* equipped = target.usable && equipped_definition && equipped_definition->slot_type == item->slot_type
                             ? equipped_definition
                             : nullptr;
  const auto equipped_data = target.equipped.data;
  if (xml.has("Soulbound")) content.restrictions.push_back(paragraph("Soulbound", special_color));
  for (const auto extra : xml.child("ExtraTooltipData").children("EffectInfo")) {
    auto name = extra.attribute("name"), description = extra.attribute("description");
    RichLine line;
    if (!name.empty()) line.spans.push_back({name + ": ", body_color});
    line.spans.push_back({std::move(description), effect_color});
    content.custom.push_back(std::move(line));
  }
  bool pet = false;
  for (const auto activate : xml.children("Activate")) pet = pet || activate.text() == "PermaPet";
  if (!item->consumable && !pet && !xml.has("Treasure") && !xml.has("PetFood") && !xml.has("NoTierTag") &&
      (item->tier >= 0 || equippable || !xml.attribute("setType").empty())) {
    content.tier = item->tier >= 0 ? "T" + std::to_string(item->tier) : xml.attribute("setType").empty() ? "UT" : "ST";
    content.tier_color = item->tier >= 0 ? 0xffffff : xml.attribute("setType").empty() ? untiered_color : 0xff9900;
  }
  content.description = xml.child("Description").text();
  auto effect = [&](std::string name, std::string value, std::uint32_t color = effect_color) {
    RichLine line;
    if (!name.empty()) line.spans.push_back({name + ": ", body_color});
    line.spans.push_back({std::move(value), color});
    content.effects.push_back(std::move(line));
  };
  auto activation_line = [&](std::initializer_list<std::pair<std::string, std::uint32_t>> spans) {
    RichLine line;
    line.x = 14;
    for (const auto& span : spans)
      if (!span.first.empty()) line.spans.push_back({span.first, span.second});
    content.effects.push_back(std::move(line));
  };
  if (xml.has("NumProjectiles")) {
    const int shots = xml.integer("NumProjectiles"), other = equipped ? equipped->xml.integer("NumProjectiles", 1) : 0;
    effect("Shots", std::to_string(shots),
           equipped ? comparison_color(shots - other) : no_comparison_color(target.usable));
  }
  if (xml.has("Projectile")) {
    const auto projectile = xml.child("Projectile");
    const double mod = item_data::stat(target.item.data, item_data::damage);
    const auto other_projectile = equipped ? equipped->xml.child("Projectile") : Xml{};
    if (!xml.has("NumProjectiles")) {
      const int shots = item->num_projectiles, other = equipped ? equipped->num_projectiles : 0;
      effect("Shots", std::to_string(shots),
             equipped ? target.usable ? comparison_color(shots - other) : effect_color
                      : no_comparison_color(target.usable));
    }
    const int base_minimum = projectile.integer("Damage", projectile.integer("MinDamage")),
              base_maximum = projectile.integer("Damage", projectile.integer("MaxDamage"));
    const int minimum = base_minimum + static_cast<int>(base_minimum * mod),
              maximum = base_maximum + static_cast<int>(base_maximum * mod);
    const double average = (minimum + maximum) / 2.0;
    const int old_minimum = other_projectile.integer("Damage", other_projectile.integer("MinDamage")),
              old_maximum = other_projectile.integer("Damage", other_projectile.integer("MaxDamage"));
    const double other_average =
        equipped && other_projectile
            ? (effective_damage(old_minimum, equipped_data) + effective_damage(old_maximum, equipped_data)) / 2.0
            : 0;
    const auto damage_color = equipped ? target.usable ? comparison_color(average - other_average) : effect_color
                                       : no_comparison_color(target.usable);
    effect("Damage",
           std::to_string(minimum) + (minimum == maximum ? "" : " - " + std::to_string(maximum)) +
               (mod ? " (+" + std::to_string(static_cast<int>(mod * 100)) + "%)" : ""),
           damage_color);
    const double range = round_to_two(projectile.number("Speed") * projectile.number("LifetimeMS") / 10000);
    const double other_range =
        equipped && other_projectile
            ? round_to_two(other_projectile.number("Speed") * other_projectile.number("LifetimeMS") / 10000)
            : 0;
    effect("Range", number(range),
           equipped ? target.usable ? comparison_color(range - other_range) : effect_color
                    : no_comparison_color(target.usable));
    if (projectile.has("MultiHit")) effect("", "Shots hit multiple targets");
    if (projectile.has("PassesCover")) effect("", "Shots pass through obstacles");
    const double rate = effective_rate(*item, target.item.data),
                 rate_mod =
                     item_data::stat(target.item.data, item_data::rate_of_fire, .05 * xml.number("RateOfFire", 1));
    const double other_rate = equipped ? effective_rate(*equipped, equipped_data) : 0;
    effect("Rate of Fire",
           std::to_string(static_cast<int>(xml.number("RateOfFire", 1) * 100) + static_cast<int>(rate_mod * 100)) +
               "%" + (rate_mod ? " (+" + std::to_string(static_cast<int>(rate_mod * 100)) + "%)" : ""),
           equipped ? target.usable ? comparison_color(rate - other_rate) : effect_color
                    : no_comparison_color(target.usable));
    for (const auto condition : projectile.children("ConditionEffect")) {
      const auto effect_name = condition.text();
      const auto duration_text = condition.attribute("duration");
      const double duration = duration_text.empty() ? 0 : std::stod(duration_text);
      double old_duration{};
      bool found{};
      if (equipped && other_projectile)
        for (const auto old : other_projectile.children("ConditionEffect"))
          if (old.text() == effect_name) {
            const auto old_text = old.attribute("duration");
            old_duration = old_text.empty() ? 0 : std::stod(old_text);
            found = true;
            break;
          }
      const auto color =
          equipped ? target.usable ? (found ? comparison_color(duration - old_duration) : better_color) : effect_color
                   : no_comparison_color(target.usable);
      effect("Shot Effect", effect_name + " for " + number(duration) + " secs", color);
    }
    const double arc = xml.number("ArcGap", 11.25), old_arc = equipped ? equipped->xml.number("ArcGap", 11.25) : 0;
    if (arc > 0)
      effect("Arc Gap", number(arc),
             equipped ? target.usable ? comparison_color(old_arc - arc) : effect_color
                      : no_comparison_color(target.usable));
    if (projectile.has("ArmorPiercing")) effect("", "Shots ignore defense of target", special_color);
  }
  const auto activates = xml.children("Activate");
  const bool has_use = std::any_of(activates.begin(), activates.end(), [](const Xml& activation) {
    const auto kind = activation.text();
    return kind != "Shoot" && kind != "PermaPet" && kind != "Pet" && kind != "Create" && kind != "UnlockPortal" &&
           kind != "Shuriken" && kind != "DazeBlast";
  });
  if (has_use) effect("On Use", "");
  const auto equipped_activates = equipped ? equipped->xml.children("Activate") : std::vector<Xml>{};
  for (std::size_t activation_index = 0; activation_index < activates.size(); ++activation_index) {
    const auto activate = activates[activation_index];
    const auto kind = activate.text(), amount = activate.attribute("amount"), duration = activate.attribute("duration"),
               range = activate.attribute("range"), radius = activate.attribute("radius"),
               damage = activate.attribute("totalDamage"), condition = activate.attribute("effect");
    const auto matched = matching_activation(activates, activation_index, equipped_activates);
    auto value_color = [&](std::string_view attribute, bool lower_is_better = false) {
      if (!target.usable) return effect_color;
      if (!matched) return better_color;
      const auto difference = attribute_number(activate, attribute) - attribute_number(matched, attribute);
      return comparison_color(lower_is_better ? -difference : difference);
    };
    const auto condition_color = target.usable ? (matched ? effect_color : better_color) : effect_color;
    if (kind == "ConditionEffectAura") {
      activation_line({{"Party Effect: Within ", body_color}, {range + " sqrs", value_color("range")}});
      activation_line(
          {{condition, condition_color}, {" for ", body_color}, {duration + " secs", value_color("duration")}});
    } else if (kind == "ConditionEffectSelf") {
      activation_line({{"Effect on Self: ", body_color},
                       {condition, condition_color},
                       {" for ", body_color},
                       {duration + " secs", value_color("duration")}});
    } else if (kind == "Heal" || kind == "Magic")
      activation_line(
          {{"+", body_color}, {amount, value_color("amount")}, {kind == "Heal" ? " HP" : " MP", body_color}});
    else if (kind == "HealNova")
      activation_line({{"Party Heal: ", body_color},
                       {amount, value_color("amount")},
                       {" HP at ", body_color},
                       {range, value_color("range")},
                       {" sqrs", body_color}});
    else if (kind == "MagicNova")
      activation_line({{"Fill Party Magic: ", body_color},
                       {amount, value_color("amount")},
                       {" MP at ", body_color},
                       {range, value_color("range")},
                       {" sqrs", body_color}});
    else if (kind == "Teleport")
      effect("", "Teleport to Target");
    else if (kind == "VampireBlast")
      activation_line({{"Steal ", body_color},
                       {damage, value_color("totalDamage")},
                       {" HP within ", body_color},
                       {radius, value_color("radius")},
                       {" sqrs", body_color}});
    else if (kind == "Trap") {
      activation_line({{"Trap: ", body_color},
                       {damage, value_color("totalDamage")},
                       {" HP within ", body_color},
                       {radius, value_color("radius")},
                       {" sqrs", body_color}});
      activation_line({{"Applies ", body_color},
                       {condition, condition_color},
                       {" for ", body_color},
                       {duration, value_color("duration")},
                       {" secs", body_color}});
    } else if (kind == "StasisBlast")
      activation_line({{"Stasis on Group: ", body_color}, {duration, value_color("duration")}, {" secs", body_color}});
    else if (kind == "Decoy")
      activation_line({{"Decoy: Lasts for ", body_color}, {duration, value_color("duration")}, {" secs", body_color}});
    else if (kind == "Lightning")
      activation_line({{"Lightning: ", body_color},
                       {damage, value_color("totalDamage")},
                       {" damage to ", body_color},
                       {activate.attribute("maxTargets"), value_color("maxTargets")},
                       {" targets", body_color}});
    else if (kind == "PoisonGrenade")
      activation_line({{"Poison Grenade: ", body_color},
                       {damage, value_color("totalDamage")},
                       {" HP over ", body_color},
                       {duration, value_color("duration")},
                       {" secs within ", body_color},
                       {radius, value_color("radius")},
                       {" sqrs", body_color}});
    else if (kind == "RemoveNegativeConditions" || kind == "RemoveNegativeConditionsSelf")
      effect("", "Removes negative conditions");
    else if (kind == "BulletNova")
      effect("Shots", "20");
    else if (kind == "Shuriken") {
      effect("Shots", amount);
      effect("", "Stars seek nearby enemies");
      effect("", "Dazes nearby enemies");
    } else if (kind == "IncrementStat") {
      const int index = stat_index(std::stoi(activate.attribute("stat", "-1")));
      if (index >= 0)
        effect("", index < 2 ? "+" + amount + " " + std::string(stat_names[static_cast<std::size_t>(index)])
                             : "Permanently increases " + std::string(stat_names[static_cast<std::size_t>(index)]));
    }
  }
  std::array<int, 8> boosts{}, datas{};
  std::array<bool, 8> present{};
  std::vector<std::size_t> order;
  for (const auto activate : xml.children("ActivateOnEquip")) {
    const int index = stat_index(std::stoi(activate.attribute("stat", "-1")));
    if (index >= 0) {
      const auto i = static_cast<std::size_t>(index);
      boosts[i] += std::stoi(activate.attribute("amount", "0"));
      if (!present[i]) order.push_back(i);
      present[i] = true;
    }
  }
  for (std::size_t i = 0; i < 8; ++i) {
    datas[i] =
        static_cast<int>(item_data::stat(target.item.data, 1U << (8U + static_cast<unsigned>(i)), i < 2 ? 5 : 1));
    boosts[i] += datas[i];
    if (!present[i] && datas[i]) order.push_back(i);
    present[i] = present[i] || datas[i] != 0;
  }
  if (std::any_of(present.begin(), present.end(), [](bool value) { return value; })) {
    effect("On Equip", "");
    // AVM2's initial Dictionary has four key buckets; it grows after the third key.
    // Preserve insertion order for collisions until growth gives each boost its own bucket.
    const auto first_key = order.front(), last_key = order.back();
    std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) {
      if (order.size() != 2) return a < b;
      const auto first = first_key, second = last_key;
      const auto bucket = [&](auto key) { return key == second && first % 4 == second % 4 ? (key + 1) % 4 : key % 4; };
      return bucket(a) < bucket(b);
    });
    for (const auto i : order) {
      int other_boost{};
      if (equipped) {
        for (const auto old : equipped->xml.children("ActivateOnEquip"))
          if (stat_index(std::stoi(old.attribute("stat", "-1"))) == static_cast<int>(i))
            other_boost += std::stoi(old.attribute("amount", "0"));
        other_boost +=
            static_cast<int>(item_data::stat(equipped_data, 1U << (8U + static_cast<unsigned>(i)), i < 2 ? 5 : 1));
      }
      const auto color = equipped        ? target.usable ? comparison_color(boosts[i] - other_boost) : effect_color
                         : target.usable ? (boosts[i] < 0 ? worse_color : better_color)
                                         : effect_color;
      effect("",
             (boosts[i] >= 0 ? "+" : "") + std::to_string(boosts[i]) +
                 (datas[i] > 0 ? " (+" + std::to_string(datas[i]) + ")" : "") + " " + std::string(stat_names[i]),
             color);
    }
  }
  const double cooldown_mod = item_data::stat(target.item.data, item_data::cooldown);
  if (xml.has("Cooldown") || cooldown_mod) {
    const double cooldown = effective_cooldown(*item, target.item.data),
                 old = equipped ? effective_cooldown(*equipped, equipped_data) : 0;
    effect("Cooldown",
           number(cooldown) + "s" +
               (cooldown_mod ? " (-" + std::to_string(static_cast<int>(cooldown_mod * 100)) + "%)" : ""),
           equipped ? target.usable ? comparison_color(old - cooldown) : effect_color
                    : no_comparison_color(target.usable));
  }
  if (xml.has("Doses")) {
    const int doses = xml.integer("Doses"), old = equipped ? equipped->xml.integer("Doses") : 0;
    effect(
        "Doses", std::to_string(doses),
        equipped ? target.usable ? comparison_color(doses - old) : effect_color : no_comparison_color(target.usable));
  }
  if (xml.has("MpCost")) {
    const int cost = xml.integer("MpCost"), old = equipped ? equipped->xml.integer("MpCost") : 0;
    effect("MP Cost", std::to_string(cost),
           equipped ? target.usable ? comparison_color(old - cost) : effect_color : no_comparison_color(target.usable));
  }
  const int fame_mod = static_cast<int>(item_data::stat(target.item.data, 1U << 19U, 1));
  if (xml.has("FameBonus") || fame_mod) {
    const int fame = xml.integer("FameBonus") + fame_mod,
              old = equipped ? equipped->xml.integer("FameBonus") +
                                   static_cast<int>(item_data::stat(equipped_data, 1U << 19U, 1))
                             : 0;
    effect("Fame Bonus", std::to_string(fame) + "%" + (fame_mod ? " (+" + std::to_string(fame_mod) + "%)" : ""),
           equipped ? target.usable ? comparison_color(fame - old) : effect_color : no_comparison_color(target.usable));
  }
  auto restriction = [&](std::string text, std::uint32_t color = body_color, bool bold = false) {
    content.restrictions.push_back(paragraph(std::move(text), color, bold));
  };
  const bool equip = target.inventory_full || target.owner == TooltipOwner::player;
  if (target.usable) {
    if (item->usable)
      restriction("Press [" + std::string(key_name(target.use_special)) + "] in world to use", 0xffffff);
    if (item->usable || (!item->consumable && !item->inventory_use)) {
      restriction("Must be equipped to use");
      restriction(equip ? "Double-Click to equip" : "Double-Click to take");
    } else if (item->consumable) {
      restriction("Consumed with use");
      restriction(equip ? "Double-Click or Shift-Click on item to use" : "Double-Click to take & Shift-Click to use",
                  0xffffff);
    } else {
      restriction("Can be used multiple times");
      restriction("Double-Click or Shift-Click on item to use", 0xffffff);
    }
  } else if (const auto* player = item_definition(catalog, target.player_type))
    restriction("Not usable by " + player->display_id, warning_color, true);
  if (item->slot_type != 10 && item->slot_type != 9) {
    std::string classes;
    for (const auto& player : catalog.objects())
      if (player.player &&
          std::find(player.slot_types.begin(), player.slot_types.end(), item->slot_type) != player.slot_types.end()) {
        if (!classes.empty()) classes += ", ";
        classes += player.display_id;
      }
    restriction("Usable by: " + classes);
  }
  for (const auto requirement : xml.children("EquipRequirement"))
    if (requirement.text() == "Stat") {
      const int id = std::stoi(requirement.attribute("stat", "-1")),
                value = std::stoi(requirement.attribute("value", "0")), index = stat_index(id);
      const bool met = index >= 0 && target.stats[static_cast<std::size_t>(index)] >= value;
      restriction("Requires " +
                      (id >= 0 && id < 8 ? std::string(stat_names[static_cast<std::size_t>(id)]) : "Unknown Stat") +
                      " of " + std::to_string(value),
                  met ? body_color : warning_color, !met);
    }
  return content;
}
}
