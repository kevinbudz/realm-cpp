#pragma once
#include "realm/appengine/SavedCharacter.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace realm {
struct FameLine {
  std::string label;
  int value{};
  bool fame{};
  bool bonus{};
  std::string unit;
};

struct CharacterFame {
  SavedCharacter character;
  std::string name;
  std::string killed_by;
  std::string death_date;
  int total_fame{};
  std::vector<FameLine> lines;  // ScoringBox order, followed by server-ordered bonuses.
};

[[nodiscard]] CharacterFame parse_character_fame(std::string_view xml, int character_id);
}
