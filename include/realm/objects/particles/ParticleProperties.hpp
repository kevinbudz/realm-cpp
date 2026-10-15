#pragma once
#include "realm/objects/ObjectProperties.hpp"
#include <string>
#include <vector>

namespace realm {
struct ParticleFrameDef {
  TextureDef texture;
  double duration_ms{};
};

struct ParticleAnimationDef {
  double probability{1};
  std::vector<ParticleFrameDef> frames;
};

struct ParticleDef {
  std::string id;
  TextureDef texture;
  int size{100};
  double z{};
  double duration_ms{};
  std::vector<ParticleAnimationDef> animations;
};
}
