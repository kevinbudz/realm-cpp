#include "ApplicationOptions.hpp"
#include <algorithm>
#include <iostream>
#include <sstream>

namespace realm::application {
void load_offline_map(World& world, const Catalog& catalog, const Options& options) {
  if (!options.map.empty()) {
    if (options.map.extension() == ".wmap") {
      world.load_wmap(options.map);
    } else {
      world.load_jm(options.map);
    }

    const auto spawn = world.spawn_position();
    if (const auto* player = catalog.object("Wizard")) {
      world.add(-2, player->type, spawn);
      world.set_player_id(-2);
      auto* local = world.entity(-2);
      local->profile.name = "Wizard";
      local->combat.condition = options.condition;
      if (options.breath >= 0) {
        local->profile.stats[static_cast<std::uint8_t>(realm::flash::StatId::BREATH_STAT)] =
            options.breath;
      }
      local->profile.stats[static_cast<std::uint8_t>(realm::flash::StatId::MP_STAT)] =
          player->max_mp;
      local->profile.stats[static_cast<std::uint8_t>(realm::flash::StatId::MAX_MP_STAT)] =
          player->max_mp;
      local->profile.stats[static_cast<std::uint8_t>(realm::flash::StatId::NEXT_LEVEL_EXP_STAT)] =
          50;
      local->motion.speed = player->xml.integer("Speed");
      local->combat.attack = player->xml.integer("Attack");
      local->combat.dexterity = player->xml.integer("Dexterity");

      auto equipment = player->xml.child("Equipment").text();
      std::replace(equipment.begin(), equipment.end(), ',', ' ');
      std::istringstream slots(equipment);
      std::string item;
      int slot{};
      while (slot < 20 && slots >> item) {
        realm::set_inventory_item(*local, slot++, {std::stoi(item, nullptr, 0), -1});
      }
    }

    if (auto* local = world.entity(-2)) {
      local->combat.max_hp = 100;
      local->combat.hp = options.hp >= 0 ? options.hp : 100;
      if (options.ally) {
        auto& ally =
            world.add(-3, local->type, {local->motion.position.x + 2, local->motion.position.y});
        ally.profile.name = options.ally_name;
        ally.combat.max_hp = 100;
        ally.combat.hp = options.hp >= 0 ? options.hp : 100;
        ally.combat.condition = options.condition;
      }
      if (!options.notify.empty()) {
        // TEXT[:RRGGBB], two seconds above the local player.
        const auto colon = options.notify.rfind(':');
        const bool colored = colon != std::string::npos && options.notify.size() - colon == 7;
        world.notify(-2, colored ? options.notify.substr(0, colon) : options.notify,
                     colored ? static_cast<std::uint32_t>(
                                   std::stoul(options.notify.substr(colon + 1), nullptr, 16))
                             : 0xffffffU,
                     20000);
      }
      if (options.damage > 0 || !options.damage_effects.empty()) {
        std::vector<std::uint8_t> effects;
        std::istringstream list(options.damage_effects);
        std::string part;
        while (std::getline(list, part, ',')) {
          effects.push_back(static_cast<std::uint8_t>(std::stoi(part)));
        }
        world.report_damage(-2, options.damage, 0, effects);
      }
    }

    // Background reference for status fixtures: the same scene without the player's sprite and
    // bars.
    if (options.ghost) {
      world.remove(-2);
    }
    std::cout << "Map " << world.name() << " " << world.width() << "x" << world.height()
              << " objects=" << world.entities().size()
              << " unresolved=" << world.unresolved_ids().size() << '\n';
  }
}
}
