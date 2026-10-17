#pragma once
// Flash: io/decagames/rotmg/fame/data/FameTracker.as
#include "realm/appengine/SavedCharacter.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace realm {
// One of the server's death bonuses (Database.EvaluateBonuses), earned or not.
struct FameBonusLine {
  std::string name;
  std::string description;
  int percent{};    // of base fame; Well Equipped reports its equipment sum
  int min_level{};  // the character level the bonus needs before it can be earned
  bool earned{};
  int fame{};  // 0 when not earned
};

// The /char/fameStats reply: a live or saved character's fame counters and bonus breakdown, which
// FameContentPopup shows. Flash computed these from PCStats on the client; this server sends them.
struct CharacterFameStats {
  SavedCharacter character;
  std::string name;
  std::int64_t created_on{};  // unix seconds, 0 when unknown
  // <FameStats> counters by tag (Shots, ShotsThatDamage, MonsterKills ...).
  std::vector<std::pair<std::string, int>> stats;
  // Every dungeon this server tracks, in server order, with this character's completions.
  std::vector<std::pair<std::string, int>> dungeons;
  int base_fame{};
  int total_fame{};
  std::vector<FameBonusLine> bonuses;  // server order

  // The counter named `tag`, or 0.
  [[nodiscard]] int stat(std::string_view tag) const;
  [[nodiscard]] int dungeons_completed() const;
};

// Throws std::runtime_error on an <Error> reply or a malformed document.
[[nodiscard]] CharacterFameStats parse_character_fame_stats(std::string_view xml);
}
