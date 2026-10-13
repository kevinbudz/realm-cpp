#include "realm/fame/CharacterFame.hpp"
#include "realm/appengine/SavedCharactersList.hpp"
#include "../appengine/AppEngineXml.hpp"
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <limits>
namespace realm {
using namespace appengine_xml;
CharacterFame parse_character_fame(std::string_view source, int id) {
  if (auto error = app_error(source)) throw std::runtime_error(*error);
  const auto doc = parse(source);
  const auto* root = xmlDocGetRootElement(doc->doc);
  if (!root || name(root) != "Fame") throw std::runtime_error("Fame reply has no <Fame> root");
  const xmlNode* character{};
  for (auto* c = root->children; c; c = c->next)
    if (c->type == XML_ELEMENT_NODE && name(c) == "Char" && integer(attribute(c, "id")) == id) {
      character = c;
      break;
    }
  if (!character || !child(character, "ObjectType") || !child(character, "Level") || !child(root, "TotalFame") ||
      !child(root, "KilledOn"))
    throw std::runtime_error("Fame reply is missing the requested character or death data");
  CharacterFame result;
  result.character = {id,
                      integer(child(character, "ObjectType")),
                      integer(child(character, "Level")),
                      0,
                      0,
                      integer(child(character, "Texture")),
                      integer(child(character, "Tex1")),
                      integer(child(character, "Tex2"))};
  result.name = text(child(child(character, "Account"), "Name"));
  result.killed_by = text(child(root, "KilledBy"));
  result.total_fame = integer(child(root, "TotalFame"));
  const auto stamp = static_cast<std::time_t>(std::strtoll(text(child(root, "KilledOn")).c_str(), nullptr, 10));
  std::tm date{};
#ifdef _WIN32
  const bool valid = localtime_s(&date, &stamp) == 0;
#else
  const bool valid = localtime_r(&stamp, &date) != nullptr;
#endif
  if (!valid) throw std::runtime_error("Invalid fame death date");
  constexpr const char* months[]{"January", "February", "March",     "April",   "May",      "June",
                                 "July",    "August",   "September", "October", "November", "December"};
  result.death_date = std::string(months[date.tm_mon]) + " " + std::to_string(date.tm_mday) + ", " +
                      std::to_string(date.tm_year + 1900);
  const auto add = [&](const char* label, const char* tag) {
    result.lines.push_back({label, integer(child(root, tag)), false, false, {}});
  };
  add("Minutes Active", "MinutesActive");
  add("Shots", "Shots");
  const double shots = integer(child(root, "Shots")), hits = integer(child(root, "ShotsThatDamage"));
  const auto accuracy = shots == 0
                            ? 0.0
                            : std::clamp(100 * hits / shots, static_cast<double>(std::numeric_limits<int>::min()),
                                         static_cast<double>(std::numeric_limits<int>::max()));
  result.lines.push_back({"Accuracy", static_cast<int>(accuracy), false, false, "%"});
  for (const auto& [label, tag] :
       std::initializer_list<std::pair<const char*, const char*>>{{"Abilities Used", "AbilitiesUsed"},
                                                                  {"Tiles Seen", "TilesUncovered"},
                                                                  {"Monster Kills", "MonsterKills"},
                                                                  {"God Kills", "GodKills"},
                                                                  {"Oryx Kills", "OryxKills"},
                                                                  {"Cube Kills", "CubeKills"},
                                                                  {"Cyan Bags", "CyanBags"},
                                                                  {"Blue Bags", "BlueBags"},
                                                                  {"White Bags", "WhiteBags"},
                                                                  {"Damage Taken", "DamageTaken"},
                                                                  {"Damage Dealt", "DamageDealt"},
                                                                  {"Teleports", "Teleports"},
                                                                  {"Potions Drank", "PotionsDrank"},
                                                                  {"Quests Completed", "QuestsCompleted"}})
    add(label, tag);
  std::int64_t dungeons{};
  for (const auto* tag : {"PirateCavesCompleted", "UndeadLairsCompleted", "AbyssOfDemonsCompleted",
                          "SnakePitsCompleted", "SpiderDensCompleted", "SpriteWorldsCompleted", "TombsCompleted"})
    dungeons += integer(child(root, tag));
  result.lines.push_back(
      {"Dungeons Completed",
       static_cast<int>(std::clamp(dungeons, std::int64_t{0}, std::int64_t{std::numeric_limits<int>::max()})),
       false,
       false,
       {}});
  add("Escapes", "Escapes");
  add("Near Death Escapes", "NearDeathEscapes");
  add("Party Member Level Ups", "LevelUpAssists");
  result.lines.push_back({"Base Fame Earned", integer(child(root, "BaseFame")), true, false, {}});
  for (auto* c = root->children; c; c = c->next)
    if (c->type == XML_ELEMENT_NODE && name(c) == "Bonus") {
      if (result.lines.size() >= 278) throw std::runtime_error("Fame bonus list exceeds limit");
      result.lines.push_back({"Bonus: " + attribute(c, "id"), integer(c), true, true, {}});
    }
  return result;
}
}
