#pragma once
#include <array>

namespace realm {
struct SavedCharacter {
  int id{};
  int object_type{};
  int level{1};
  int experience{};
  int fame{};
  int skin{};
  int tex1{};
  int tex2{};
  int hp{};
  int max_hp{};
  int mp{};
  int max_mp{};
  std::array<int, 12> equipment{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
  std::array<int, 12> item_data{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
};
}
