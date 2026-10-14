#pragma once
#include "realm/appengine/SavedCharacter.hpp"
#include <array>
#include <string>
#include <string_view>
#include <vector>
namespace realm {
struct Legend {
  int account_id{}, character_id{}, total_fame{};
  std::string name;
  SavedCharacter character;
  std::array<int, 4> equipment{-1, -1, -1, -1}, item_data{-1, -1, -1, -1};
};
// Legends keep server order, removing repeated account/character pairs.
// MyFameListElem is deliberately ignored by the current source factory.
[[nodiscard]] std::vector<Legend> parse_legends(std::string_view xml, std::string_view timespan);
}
