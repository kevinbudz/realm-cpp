#pragma once
#include <array>
namespace realm {
struct SavedCharacter {
  int id{}, object_type{}, level{1}, experience{}, fame{}, skin{}, tex1{}, tex2{};
  int hp{}, max_hp{}, mp{}, max_mp{};
  std::array<int, 12> equipment{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
  std::array<int, 12> item_data{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
};
}
