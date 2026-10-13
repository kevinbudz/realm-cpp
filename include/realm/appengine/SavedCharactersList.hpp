#pragma once
#include "realm/appengine/SavedCharacter.hpp"
#include "realm/appengine/SavedNewsItem.hpp"
#include "realm/fame/CharacterFame.hpp"
#include "realm/legends/Legend.hpp"
#include "realm/ui/guild/GuildMemberList.hpp"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace realm {
// SavedCharactersList: the /char/list reply.
struct CharList {
  std::string name;
  int account_id{}, next_char_id{}, max_num_chars{1}, credits{}, fame{}, total_fame{};
  std::vector<SavedCharacter> characters;
  std::vector<NewsItem> news;
  // ClassStats objectType -> BestFame.
  std::map<int, int> best_fame, best_level;
  std::vector<int> owned_skins;
  [[nodiscard]] int available_slots() const { return std::max(0, max_num_chars - static_cast<int>(characters.size())); }
  [[nodiscard]] int stars() const;
};
// FameUtil.STARS thresholds.
inline constexpr int fame_stars[5]{20, 150, 400, 800, 2000};
[[nodiscard]] int star_count(int fame);
[[nodiscard]] int next_star_fame(int best_fame, int current_fame);
// AppEngine replies carry failures as <Error>text</Error>.
[[nodiscard]] std::optional<std::string> app_error(std::string_view response);
// Destructive endpoints require an explicit, well-formed <Success/> reply.
void parse_delete_result(std::string_view response);
[[nodiscard]] CharList parse_char_list(std::string_view xml);
// SavedCharacter.compare: most recently played first, then most experience.
void sort_characters(CharList& list, const std::map<int, std::int64_t>& use);

// WebAccount persists Username/Password in the OWRotMG SharedObject and
// Parameters.charIdUseMap; this is the native equivalent.
struct SavedAccount {
  std::string username, password;
  std::map<int, std::int64_t> char_use;
  [[nodiscard]] bool registered() const { return !password.empty(); }
};
[[nodiscard]] std::filesystem::path account_path();
[[nodiscard]] SavedAccount load_account(const std::filesystem::path& path);
void save_account(const std::filesystem::path& path, const SavedAccount& account);
}
