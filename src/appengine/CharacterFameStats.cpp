#include "realm/appengine/CharacterFameStats.hpp"
#include "realm/appengine/SavedCharactersList.hpp"
#include "AppEngineXml.hpp"
#include <cstdlib>
#include <stdexcept>

namespace realm {
using namespace appengine_xml;

int CharacterFameStats::stat(std::string_view tag) const {
  for (const auto& [key, value] : stats) {
    if (key == tag) {
      return value;
    }
  }
  return 0;
}

int CharacterFameStats::dungeons_completed() const {
  int total = 0;
  for (const auto& entry : dungeons) {
    total += entry.second;
  }
  return total;
}

CharacterFameStats parse_character_fame_stats(std::string_view source) {
  if (auto error = app_error(source)) {
    throw std::runtime_error(*error);
  }

  const auto doc = parse(source);
  const auto* root = xmlDocGetRootElement(doc->doc);
  if (!root || name(root) != "CharFameStats") {
    throw std::runtime_error("Fame stats reply has no <CharFameStats> root");
  }
  const auto* character = child(root, "Char");
  if (!character || !child(character, "ObjectType") || !child(root, "BaseFame") ||
      !child(root, "TotalFame")) {
    throw std::runtime_error("Fame stats reply is missing the character");
  }

  CharacterFameStats result;
  result.character.id = integer(attribute(character, "id"));
  result.character.object_type = integer(child(character, "ObjectType"));
  result.character.level = integer(child(character, "Level"));
  result.character.experience = integer(child(character, "Exp"));
  result.character.fame = integer(child(character, "CurrentFame"));
  result.character.skin = integer(child(character, "Texture"));
  result.character.tex1 = integer(child(character, "Tex1"));
  result.character.tex2 = integer(child(character, "Tex2"));
  result.name = text(child(root, "Name"));
  result.created_on = std::strtoll(text(child(root, "CreatedOn")).c_str(), nullptr, 10);
  result.base_fame = integer(child(root, "BaseFame"));
  result.total_fame = integer(child(root, "TotalFame"));

  for (auto* node = child(root, "FameStats") ? child(root, "FameStats")->children : nullptr; node;
       node = node->next) {
    if (node->type == XML_ELEMENT_NODE && name(node) != "Dungeons") {
      result.stats.emplace_back(std::string(name(node)), integer(node));
    }
  }
  for (auto* node = child(root, "Dungeons") ? child(root, "Dungeons")->children : nullptr; node;
       node = node->next) {
    if (node->type == XML_ELEMENT_NODE && name(node) == "Dungeon") {
      result.dungeons.emplace_back(attribute(node, "id"), integer(node));
    }
  }
  for (auto* node = child(root, "Bonuses") ? child(root, "Bonuses")->children : nullptr; node;
       node = node->next) {
    if (node->type == XML_ELEMENT_NODE && name(node) == "Bonus") {
      result.bonuses.push_back({attribute(node, "id"), attribute(node, "desc"),
                                integer(attribute(node, "percent")),
                                integer(attribute(node, "level")),
                                integer(attribute(node, "earned")) != 0, integer(node)});
    }
  }
  return result;
}
}
