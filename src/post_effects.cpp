#include "realm/post_effects.hpp"
#include "realm/world.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <cmath>

namespace realm {
float drunk_blur_diameter(std::int32_t time_ms) { return static_cast<float>(20 + 10 * std::sin(time_ms / 1000.0)); }
float breath_overlay_alpha(int breath, std::int32_t time_ms) {
  if (breath < 0 || breath >= breath_threshold) return 0;
  const double b = static_cast<double>(breath_threshold - breath) / breath_threshold;
  return static_cast<float>(b * std::abs(std::sin(time_ms / 300.0)) * 0.75);
}
float vignette_edge_alpha(float radius) {
  constexpr float inner = 155.F / 255.F;
  return std::clamp((radius - inner) / (1 - inner), 0.F, 1.F) * 0.92F;
}
int player_breath(const Entity& player) {
  const auto it = player.profile.stats.find(static_cast<std::uint8_t>(flash::StatId::BREATH_STAT));
  return it == player.profile.stats.end() ? -1 : it->second;
}
ScreenEffects screen_effects(const Entity* player, std::int32_t time_ms, double pixel_scale) {
  ScreenEffects fx;
  if (!player) return fx;
  if (player->combat.condition & drunk_condition)
    fx.blur = static_cast<float>(drunk_blur_diameter(time_ms) * pixel_scale);
  fx.blind = (player->combat.condition & blind_condition) != 0;
  fx.vignette = breath_overlay_alpha(player_breath(*player), time_ms);
  fx.sidebar = static_cast<float>(sidebar_gradient_width * pixel_scale);
  return fx;
}
}
