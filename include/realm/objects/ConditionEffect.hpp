#pragma once
#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace realm {
// ConditionEffect.as. Each condition has an ID (the value carried by
// DAMAGE effects and ShowEffect) and the bit 1 << (ID - 1) in the CONDITION stat.
namespace condition {
enum Id : unsigned {
  nothing,
  quiet,
  weak,
  slowed,
  sick,
  dazed,
  stunned,
  blind,
  hallucinating,
  drunk,
  confused,
  stun_immune,
  invisible,
  paralyzed,
  speedy,
  bleeding,
  healing,
  damaging,
  berserk,
  stasis,
  stasis_immune,
  invincible,
  invulnerable,
  armored,
  armor_broken,
  hexed,
  count
};

[[nodiscard]] constexpr std::uint32_t bit(unsigned id) {
  return id ? 1U << (id - 1U) : 0U;
}
}

struct ConditionEffect {
  std::string_view name;
  // Frames of lofiInterface2 (8x8); several frames animate at 500 ms each. None: no icon.
  std::array<std::uint8_t, 4> icons{};
  std::uint8_t icon_count{};
};

[[nodiscard]] inline const ConditionEffect& condition_effect(unsigned id) {
  static const std::array<ConditionEffect, condition::count> table{
      {{"Nothing", {}, 0},        {"Quiet", {32}, 1},         {"Weak", {34, 35, 36, 37}, 4},
       {"Slowed", {1}, 1},        {"Sick", {39}, 1},          {"Dazed", {44}, 1},
       {"Stunned", {45}, 1},      {"Blind", {41}, 1},         {"Hallucinating", {42}, 1},
       {"Drunk", {43}, 1},        {"Confused", {2}, 1},       {"Stun Immune", {}, 0},
       {"Invisible", {}, 0},      {"Paralyzed", {53, 54}, 2}, {"Speedy", {0}, 1},
       {"Bleeding", {46}, 1},     {"Healing", {47}, 1},       {"Damaging", {49}, 1},
       {"Berserk", {50}, 1},      {"Stasis", {}, 0},          {"Stasis Immune", {}, 0},
       {"Invincible", {}, 0},     {"Invulnerable", {17}, 1},  {"Armored", {16}, 1},
       {"Armor Broken", {55}, 1}, {"Hexed", {42}, 1}}};
  return table[id < condition::count ? id : 0];
}

// ConditionEffect.getConditionEffectIcons: one frame per active condition with
// an icon, ascending by bit; multi-frame conditions pick frame (time / 500) % n.
inline void condition_icon_frames(std::uint32_t mask, std::int32_t time_ms,
                                  std::vector<std::uint8_t>& out) {
  out.clear();
  const auto index = static_cast<unsigned>(time_ms / 500);
  for (unsigned id = 1; id < condition::count; ++id) {
    if (!(mask & condition::bit(id))) {
      continue;
    }
    const auto& effect = condition_effect(id);
    if (effect.icon_count) {
      out.push_back(effect.icons[index % effect.icon_count]);
    }
  }
}
}
