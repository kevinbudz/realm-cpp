#pragma once
#include "realm/objects/ObjectProperties.hpp"
#include "realm/util/Xml.hpp"
#include <cstdint>
#include <string>

namespace realm {
struct GroundDef {
  std::uint16_t type{};
  std::string id;
  TextureDef texture, edge, corner, inner_corner, top;
  double speed{1}, offset_x{}, offset_y{}, animate_x{}, animate_y{}, top_animate_x{}, top_animate_y{};
  int damage{}, blend_priority{-1}, composite_priority{}, animate{}, top_animate{};
  bool no_walk{true}, push{}, sink{}, sinking{}, random_offset{}, same_type_edge{};
  Xml xml;
};
}
