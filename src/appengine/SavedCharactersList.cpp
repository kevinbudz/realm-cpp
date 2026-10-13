#include "realm/appengine/SavedCharactersList.hpp"
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <cmath>
#include <ctime>
#include <limits>
#include <set>
#include <cerrno>
namespace realm {
namespace {
struct Document {
  xmlDoc* doc;
  ~Document() { xmlFreeDoc(doc); }
};
std::string_view name(const xmlNode* node) { return reinterpret_cast<const char*>(node->name); }
const xmlNode* child(const xmlNode* node, std::string_view tag) {
  for (auto* c = node ? node->children : nullptr; c; c = c->next)
    if (c->type == XML_ELEMENT_NODE && name(c) == tag) return c;
  return nullptr;
}
std::string text(const xmlNode* node) {
  if (!node) return {};
  auto* content = xmlNodeGetContent(node);
  std::string result = content ? reinterpret_cast<const char*>(content) : "";
  xmlFree(content);
  return result;
}
std::string attribute(const xmlNode* node, const char* key) {
  auto* value = xmlGetProp(node, reinterpret_cast<const xmlChar*>(key));
  std::string result = value ? reinterpret_cast<const char*>(value) : "";
  xmlFree(value);
  return result;
}
// AS3 int(String): unparsable values become zero.
int integer(const std::string& value) {
  char* end{};
  const auto result = std::strtol(value.c_str(), &end, 0);
  return end == value.c_str() ? 0 : static_cast<int>(result);
}
int integer(const xmlNode* node) { return integer(text(node)); }
std::unique_ptr<Document> parse(std::string_view source) {
  if (source.size() > 8 * 1024 * 1024) throw std::runtime_error("AppEngine XML exceeds limit");
  auto* doc = xmlReadMemory(source.data(), static_cast<int>(source.size()), nullptr, nullptr,
                            XML_PARSE_NONET | XML_PARSE_NOBLANKS | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
  if (!doc) throw std::runtime_error("Malformed AppEngine XML");
  return std::unique_ptr<Document>(new Document{doc});
}
}
int star_count(int fame) {
  int n{};
  while (n < 5 && fame >= fame_stars[n]) ++n;
  return n;
}
int next_star_fame(int best, int current) {
  const int value = std::max(best, current);
  for (const int star : fame_stars)
    if (star > value) return star;
  return -1;
}
int CharList::stars() const {
  int total{};
  for (const auto& entry : best_fame) total += star_count(entry.second);
  return total;
}
std::optional<std::string> app_error(std::string_view response) {
  const auto start = response.find_first_not_of(" \t\r\n\xEF\xBB\xBF");
  if (start == std::string_view::npos || response.compare(start, 6, "<Error") != 0) return std::nullopt;
  try {
    const auto doc = parse(response);
    return text(xmlDocGetRootElement(doc->doc));
  } catch (const std::exception&) {
    return std::string(response.substr(start));
  }
}
void parse_delete_result(std::string_view response) {
  const auto doc = parse(response);
  const auto* root = xmlDocGetRootElement(doc->doc);
  if (root && name(root) == "Error") throw std::runtime_error(text(root));
  if (!root || name(root) != "Success" || root->children) throw std::runtime_error("Invalid character deletion reply");
}
GuildMemberList parse_guild_members(std::string_view source) {
  if (auto error = app_error(source)) throw std::runtime_error(*error);
  const auto doc = parse(source);
  const auto* root = xmlDocGetRootElement(doc->doc);
  if (!root || name(root) != "Members") throw std::runtime_error("Guild list reply has no <Members> root");
  GuildMemberList list;
  list.name = attribute(root, "name");
  list.fame = integer(child(root, "CurrentFame"));
  list.level = integer(child(root, "GuildLevel"));
  for (auto* c = root->children; c; c = c->next)
    if (c->type == XML_ELEMENT_NODE && name(c) == "Member") {
      if (list.members.size() >= 1000) throw std::runtime_error("Guild member list exceeds limit");
      list.members.push_back({text(child(c, "Name")), integer(child(c, "Rank")), integer(child(c, "Fame"))});
    }
  return list;
}
CharList parse_char_list(std::string_view source) {
  if (auto error = app_error(source)) throw std::runtime_error(*error);
  const auto doc = parse(source);
  const auto* root = xmlDocGetRootElement(doc->doc);
  if (!root || name(root) != "Chars") throw std::runtime_error("Character list reply has no <Chars> root");
  CharList list;
  list.next_char_id = integer(attribute(root, "nextCharId"));
  list.max_num_chars = integer(attribute(root, "maxNumChars"));
  const auto* account = child(root, "Account");
  list.account_id = integer(child(account, "AccountId"));
  list.name = text(child(account, "Name"));
  const auto* stats = child(account, "Stats");
  list.total_fame = integer(child(stats, "TotalFame"));
  list.fame = integer(child(stats, "Fame"));
  list.credits = integer(child(stats, "Credits"));
  for (auto* c = stats ? stats->children : nullptr; c; c = c->next)
    if (c->type == XML_ELEMENT_NODE && name(c) == "ClassStats") {
      const auto type = integer(attribute(c, "objectType"));
      list.best_fame[type] = integer(child(c, "BestFame"));
      list.best_level[type] = integer(child(c, "BestLevel"));
    }
  auto ownership = text(child(root, "OwnedSkins"));
  std::replace(ownership.begin(), ownership.end(), ',', ' ');
  std::istringstream owned(ownership);
  int skin{};
  while (owned >> skin)
    if (skin > 0 && skin <= 65535 &&
        std::find(list.owned_skins.begin(), list.owned_skins.end(), skin) == list.owned_skins.end())
      list.owned_skins.push_back(skin);
  for (auto* c = root->children; c; c = c->next) {
    if (c->type != XML_ELEMENT_NODE || name(c) != "Char") continue;
    SavedCharacter character;
    character.id = integer(attribute(c, "id"));
    character.object_type = integer(child(c, "ObjectType"));
    character.level = integer(child(c, "Level"));
    character.experience = integer(child(c, "Exp"));
    character.fame = integer(child(c, "CurrentFame"));
    character.skin = integer(child(c, "Texture"));
    character.tex1 = integer(child(c, "Tex1"));
    character.tex2 = integer(child(c, "Tex2"));
    character.hp = integer(child(c, "HitPoints"));
    character.max_hp = integer(child(c, "MaxHitPoints"));
    character.mp = integer(child(c, "MagicPoints"));
    character.max_mp = integer(child(c, "MaxMagicPoints"));
    const auto items = [&](const char* key, auto& values) {
      auto raw = text(child(c, key));
      std::replace(raw.begin(), raw.end(), ',', ' ');
      std::istringstream input(raw);
      for (auto& value : values)
        if (!(input >> value)) break;
    };
    items("Equipment", character.equipment);
    items("ItemDatas", character.item_data);
    list.characters.push_back(character);
  }
  for (auto* c = child(root, "News") ? child(root, "News")->children : nullptr; c; c = c->next) {
    if (c->type != XML_ELEMENT_NODE || name(c) != "Item") continue;
    list.news.push_back({text(child(c, "Icon")), text(child(c, "Title")), text(child(c, "TagLine")),
                         text(child(c, "Link")), std::strtoll(text(child(c, "Date")).c_str(), nullptr, 10)});
  }
  return list;
}
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
std::vector<Legend> parse_legends(std::string_view source, std::string_view timespan) {
  if (auto error = app_error(source)) throw std::runtime_error(*error);
  const auto doc = parse(source);
  const auto* root = xmlDocGetRootElement(doc->doc);
  if (!root || name(root) != "FameList") throw std::runtime_error("Legends reply has no <FameList> root");
  if (attribute(root, "timespan") != timespan) throw std::runtime_error("Legends reply has the wrong timespan");
  const auto required_int = [](const std::string& value) {
    char* end{};
    errno = 0;
    const auto result = std::strtoll(value.c_str(), &end, 0);
    if (value.empty() || end == value.c_str() || *end || errno == ERANGE || result < std::numeric_limits<int>::min() ||
        result > std::numeric_limits<int>::max())
      throw std::runtime_error("Invalid Legends numeric field");
    return static_cast<int>(result);
  };
  const auto slots = [&](const xmlNode* node, std::array<int, 4>& out) {
    if (!node) return;
    const auto value = text(node);
    std::size_t at{};
    // Only four equipped slots are displayed; the server also sends inventory.
    for (std::size_t i = 0; i < 4 && at < value.size(); ++i) {
      const auto end = value.find(',', at);
      out[i] = required_int(value.substr(at, end == std::string::npos ? end : end - at));
      if (end == std::string::npos) break;
      at = end + 1;
    }
  };
  std::vector<Legend> result;
  std::set<std::pair<int, int>> seen;
  std::size_t count{};
  for (auto* c = root->children; c; c = c->next)
    if (c->type == XML_ELEMENT_NODE && name(c) == "FameListElem") {
      if (++count > 256) throw std::runtime_error("Legends list exceeds limit");
      Legend legend;
      legend.account_id = required_int(attribute(c, "accountId"));
      legend.character_id = required_int(attribute(c, "charId"));
      if (!seen.emplace(legend.account_id, legend.character_id).second) continue;
      legend.name = text(child(c, "Name"));
      if (legend.name.size() > 128) throw std::runtime_error("Legends name exceeds limit");
      legend.total_fame = required_int(text(child(c, "TotalFame")));
      legend.character.id = legend.character_id;
      legend.character.object_type = required_int(text(child(c, "ObjectType")));
      if (legend.character.object_type < 1 || legend.character.object_type > 65535 || legend.total_fame < 0)
        throw std::runtime_error("Invalid Legends character or fame");
      legend.character.skin = integer(child(c, "Texture"));
      legend.character.tex1 = integer(child(c, "Tex1"));
      legend.character.tex2 = integer(child(c, "Tex2"));
      slots(child(c, "Equipment"), legend.equipment);
      slots(child(c, "ItemDatas"), legend.item_data);
      result.push_back(std::move(legend));
    }
  return result;
}
void sort_characters(CharList& list, const std::map<int, std::int64_t>& use) {
  const auto used = [&](int id) {
    const auto found = use.find(id);
    return found == use.end() ? std::int64_t{} : found->second;
  };
  std::stable_sort(list.characters.begin(), list.characters.end(), [&](const auto& a, const auto& b) {
    if (used(a.id) != used(b.id)) return used(a.id) > used(b.id);
    return a.experience > b.experience;
  });
}
std::filesystem::path account_path() {
#ifdef _WIN32
  if (const char* appdata = std::getenv("APPDATA"))
    return std::filesystem::path(appdata) / "realm-cpp" / "account.json";
#else
  if (const char* config = std::getenv("XDG_CONFIG_HOME"); config && *config)
    return std::filesystem::path(config) / "realm-cpp" / "account.json";
  if (const char* home = std::getenv("HOME"))
    return std::filesystem::path(home) / ".config" / "realm-cpp" / "account.json";
#endif
  return "realm-account.json";
}
SavedAccount load_account(const std::filesystem::path& path) {
  SavedAccount account;
  std::ifstream file(path);
  if (!file) return account;
  try {
    const auto json = nlohmann::json::parse(file);
    account.username = json.value("username", std::string{});
    account.password = json.value("password", std::string{});
    if (const auto use = json.find("charIdUseMap"); use != json.end() && use->is_object())
      for (const auto& [id, time] : use->items())
        if (time.is_number_integer()) account.char_use[std::stoi(id)] = time.get<std::int64_t>();
  } catch (const std::exception&) {
    return {};
  }
  return account;
}
void save_account(const std::filesystem::path& path, const SavedAccount& account) {
  nlohmann::json json{
      {"username", account.username}, {"password", account.password}, {"charIdUseMap", nlohmann::json::object()}};
  for (const auto& [id, time] : account.char_use) json["charIdUseMap"][std::to_string(id)] = time;
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  const auto temporary = path.string() + ".tmp";
  {
    std::ofstream file(temporary, std::ios::trunc);
    if (!file) throw std::runtime_error("Cannot write account file " + temporary);
    file << json.dump(2) << '\n';
  }
  std::filesystem::permissions(temporary, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                               error);
  std::filesystem::rename(temporary, path);
}
}
