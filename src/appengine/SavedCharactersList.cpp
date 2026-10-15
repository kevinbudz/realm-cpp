#include "realm/appengine/SavedCharactersList.hpp"
#include "AppEngineXml.hpp"
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
using namespace appengine_xml;

int star_count(int fame) {
  int stars{};
  while (stars < 5 && fame >= fame_stars[stars]) {
    ++stars;
  }
  return stars;
}

int next_star_fame(int best, int current) {
  const int value = std::max(best, current);
  for (const int star : fame_stars) {
    if (star > value) {
      return star;
    }
  }
  return -1;
}

int CharList::stars() const {
  int total{};
  for (const auto& entry : best_fame) {
    total += star_count(entry.second);
  }
  return total;
}

std::optional<std::string> app_error(std::string_view response) {
  const auto start = response.find_first_not_of(" \t\r\n\xEF\xBB\xBF");
  if (start == std::string_view::npos || response.compare(start, 6, "<Error") != 0) {
    return std::nullopt;
  }

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
  if (root && name(root) == "Error") {
    throw std::runtime_error(text(root));
  }

  if (!root || name(root) != "Success" || root->children) {
    throw std::runtime_error("Invalid character deletion reply");
  }
}

CharList parse_char_list(std::string_view source) {
  if (auto error = app_error(source)) {
    throw std::runtime_error(*error);
  }

  const auto doc = parse(source);
  const auto* root = xmlDocGetRootElement(doc->doc);
  if (!root || name(root) != "Chars") {
    throw std::runtime_error("Character list reply has no <Chars> root");
  }

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
  // SavedCharactersList.parseGuildData: the Guild element is optional.
  if (const auto* guild = child(account, "Guild")) {
    list.guild_name = text(child(guild, "Name"));
    list.guild_rank = integer(child(guild, "Rank"));
  }

  for (auto* node = stats ? stats->children : nullptr; node; node = node->next) {
    if (node->type == XML_ELEMENT_NODE && name(node) == "ClassStats") {
      const auto type = integer(attribute(node, "objectType"));
      list.best_fame[type] = integer(child(node, "BestFame"));
      list.best_level[type] = integer(child(node, "BestLevel"));
    }
  }

  auto ownership = text(child(root, "OwnedSkins"));
  std::replace(ownership.begin(), ownership.end(), ',', ' ');
  std::istringstream owned(ownership);
  int skin{};
  while (owned >> skin) {
    if (skin > 0 && skin <= 65535 &&
        std::find(list.owned_skins.begin(), list.owned_skins.end(), skin) ==
            list.owned_skins.end()) {
      list.owned_skins.push_back(skin);
    }
  }

  for (auto* node = root->children; node; node = node->next) {
    if (node->type != XML_ELEMENT_NODE || name(node) != "Char") {
      continue;
    }
    SavedCharacter character;
    character.id = integer(attribute(node, "id"));
    character.object_type = integer(child(node, "ObjectType"));
    character.level = integer(child(node, "Level"));
    character.experience = integer(child(node, "Exp"));
    character.fame = integer(child(node, "CurrentFame"));
    character.skin = integer(child(node, "Texture"));
    character.tex1 = integer(child(node, "Tex1"));
    character.tex2 = integer(child(node, "Tex2"));
    character.hp = integer(child(node, "HitPoints"));
    character.max_hp = integer(child(node, "MaxHitPoints"));
    character.mp = integer(child(node, "MagicPoints"));
    character.max_mp = integer(child(node, "MaxMagicPoints"));
    const auto items = [&](const char* key, auto& values) {
      auto raw = text(child(node, key));
      std::replace(raw.begin(), raw.end(), ',', ' ');
      std::istringstream input(raw);
      for (auto& value : values) {
        if (!(input >> value)) {
          break;
        }
      }
    };
    items("Equipment", character.equipment);
    items("ItemDatas", character.item_data);
    list.characters.push_back(character);
  }

  for (auto* node = child(root, "News") ? child(root, "News")->children : nullptr; node;
       node = node->next) {
    if (node->type != XML_ELEMENT_NODE || name(node) != "Item") {
      continue;
    }
    list.news.push_back({text(child(node, "Icon")), text(child(node, "Title")),
                         text(child(node, "TagLine")), text(child(node, "Link")),
                         std::strtoll(text(child(node, "Date")).c_str(), nullptr, 10)});
  }
  return list;
}

void sort_characters(CharList& list, const std::map<int, std::int64_t>& use) {
  const auto used = [&](int id) {
    const auto found = use.find(id);
    return found == use.end() ? std::int64_t{} : found->second;
  };
  std::stable_sort(list.characters.begin(), list.characters.end(),
                   [&](const auto& left, const auto& right) {
                     if (used(left.id) != used(right.id)) {
                       return used(left.id) > used(right.id);
                     }
                     return left.experience > right.experience;
                   });
}

std::filesystem::path account_path() {
#ifdef _WIN32
  if (const char* appdata = std::getenv("APPDATA")) {
    return std::filesystem::path(appdata) / "realm-cpp" / "account.json";
  }
#else
  if (const char* config = std::getenv("XDG_CONFIG_HOME"); config && *config) {
    return std::filesystem::path(config) / "realm-cpp" / "account.json";
  }
  if (const char* home = std::getenv("HOME")) {
    return std::filesystem::path(home) / ".config" / "realm-cpp" / "account.json";
  }
#endif
  return "realm-account.json";
}

SavedAccount load_account(const std::filesystem::path& path) {
  SavedAccount account;
  std::ifstream file(path);
  if (!file) {
    return account;
  }

  try {
    const auto json = nlohmann::json::parse(file);

    account.username = json.value("username", std::string{});
    account.password = json.value("password", std::string{});
    if (const auto use = json.find("charIdUseMap"); use != json.end() && use->is_object()) {
      for (const auto& [id, time] : use->items()) {
        if (time.is_number_integer()) {
          account.char_use[std::stoi(id)] = time.get<std::int64_t>();
        }
      }
    }
  } catch (const std::exception&) {
    return {};
  }
  return account;
}

void save_account(const std::filesystem::path& path, const SavedAccount& account) {
  nlohmann::json json{{"username", account.username},
                      {"password", account.password},
                      {"charIdUseMap", nlohmann::json::object()}};
  for (const auto& [id, time] : account.char_use) {
    json["charIdUseMap"][std::to_string(id)] = time;
  }

  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  const auto temporary = path.string() + ".tmp";
  {
    std::ofstream file(temporary, std::ios::trunc);
    if (!file) {
      throw std::runtime_error("Cannot write account file " + temporary);
    }
    file << json.dump(2) << '\n';
  }
  std::filesystem::permissions(
      temporary, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, error);
  std::filesystem::rename(temporary, path);
}
}
