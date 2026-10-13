#include "realm/objects/ObjectProperties.hpp"

namespace realm {
const TextureDef& TextureDef::select(std::uint32_t id) const {
  return random.empty() ? *this : random[id % random.size()].select(id);
}
}
