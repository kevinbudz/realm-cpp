#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace realm {
struct SavedCharacter {
  int id{}, object_type{}, level{1}, experience{}, fame{}, skin{}, tex1{}, tex2{};
  int hp{}, max_hp{}, mp{}, max_mp{};
  std::array<int, 12> equipment{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
  std::array<int, 12> item_data{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
};
struct NewsItem {
  std::string icon, title, tagline, link;
  std::int64_t date{};
};
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
struct FameLine {
  std::string label;
  int value{};
  bool fame{}, bonus{};
  std::string unit;
};
struct CharacterFame {
  SavedCharacter character;
  std::string name, killed_by, death_date;
  int total_fame{};
  std::vector<FameLine> lines;  // ScoringBox order, followed by server-ordered bonuses.
};
[[nodiscard]] CharacterFame parse_character_fame(std::string_view xml, int character_id);
struct Legend {
  int account_id{}, character_id{}, total_fame{};
  std::string name;
  SavedCharacter character;
  std::array<int, 4> equipment{-1, -1, -1, -1}, item_data{-1, -1, -1, -1};
};
// Legends keep server order, removing repeated account/character pairs.
// MyFameListElem is deliberately ignored by the current source factory.
[[nodiscard]] std::vector<Legend> parse_legends(std::string_view xml, std::string_view timespan);
struct GuildMember {
  std::string name;
  int rank{}, fame{};
};
struct GuildMemberList {
  std::string name;
  int fame{}, level{};
  std::vector<GuildMember> members;
};
[[nodiscard]] GuildMemberList parse_guild_members(std::string_view xml);
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
