#pragma once
#include "realm/objects/ObjectProperties.hpp"
#include "realm/util/Xml.hpp"
#include <cstdint>
#include <string>

namespace realm {
struct GroundDef {
  std::uint16_t type{};
  std::string id;
  TextureDef texture;
  TextureDef edge;
  TextureDef corner;
  TextureDef inner_corner;
  TextureDef top;
  double speed{1};
  double offset_x{};
  double offset_y{};
  double animate_x{};
  double animate_y{};
  double top_animate_x{};
  double top_animate_y{};
  int damage{};
  int blend_priority{-1};
  int composite_priority{};
  int animate{};
  int top_animate{};
  bool no_walk{true};
  bool push{};
  bool sink{};
  bool sinking{};
  bool random_offset{};
  bool same_type_edge{};
  Xml xml;
};
}
