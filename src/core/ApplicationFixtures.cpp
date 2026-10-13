#include "ApplicationOptions.hpp"
#include <algorithm>
#include <iostream>
#include <sstream>

namespace realm::application {
void load_offline_map(World& world, const Catalog& catalog, const Options& o) {
  if (!o.map.empty()) {
    if (o.map.extension() == ".wmap")
      world.load_wmap(o.map);
    else
      world.load_jm(o.map);
    const auto spawn = world.spawn_position();
    if (const auto* player = catalog.object("Wizard")) {
      world.add(-2, player->type, spawn);
      world.set_player_id(-2);
      auto* local = world.entity(-2);
      local->profile.name = "Wizard";
      local->combat.condition = o.condition;
      if (o.breath >= 0) local->profile.stats[static_cast<std::uint8_t>(realm::flash::StatId::BREATH_STAT)] = o.breath;
      local->profile.stats[static_cast<std::uint8_t>(realm::flash::StatId::MP_STAT)] = player->max_mp;
      local->profile.stats[static_cast<std::uint8_t>(realm::flash::StatId::MAX_MP_STAT)] = player->max_mp;
      local->profile.stats[static_cast<std::uint8_t>(realm::flash::StatId::NEXT_LEVEL_EXP_STAT)] = 50;
      local->motion.speed = player->xml.integer("Speed");
      local->combat.attack = player->xml.integer("Attack");
      local->combat.dexterity = player->xml.integer("Dexterity");
      auto equipment = player->xml.child("Equipment").text();
      std::replace(equipment.begin(), equipment.end(), ',', ' ');
      std::istringstream slots(equipment);
      std::string item;
      int slot{};
      while (slot < 20 && slots >> item) realm::set_inventory_item(*local, slot++, {std::stoi(item, nullptr, 0), -1});
    }
    if (auto* local = world.entity(-2)) {
      local->combat.max_hp = 100;
      local->combat.hp = o.hp >= 0 ? o.hp : 100;
      if (o.ally) {
        auto& ally = world.add(-3, local->type, {local->motion.position.x + 2, local->motion.position.y});
        ally.profile.name = o.ally_name;
        ally.combat.max_hp = 100;
        ally.combat.hp = o.hp >= 0 ? o.hp : 100;
        ally.combat.condition = o.condition;
      }
      if (!o.notify.empty()) {
        // TEXT[:RRGGBB], two seconds above the local player.
        const auto colon = o.notify.rfind(':');
        const bool colored = colon != std::string::npos && o.notify.size() - colon == 7;
        world.notify(
            -2, colored ? o.notify.substr(0, colon) : o.notify,
            colored ? static_cast<std::uint32_t>(std::stoul(o.notify.substr(colon + 1), nullptr, 16)) : 0xffffffU,
            20000);
      }
      if (o.damage > 0 || !o.damage_effects.empty()) {
        std::vector<std::uint8_t> effects;
        std::istringstream list(o.damage_effects);
        std::string part;
        while (std::getline(list, part, ',')) effects.push_back(static_cast<std::uint8_t>(std::stoi(part)));
        world.report_damage(-2, o.damage, 0, effects);
      }
    }
    // Background reference for status fixtures: the same scene without the player's sprite and bars.
    if (o.ghost) world.remove(-2);
    std::cout << "Map " << world.name() << " " << world.width() << "x" << world.height()
              << " objects=" << world.entities().size() << " unresolved=" << world.unresolved_ids().size() << '\n';
  }
}
}
