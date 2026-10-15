#pragma once
#include <cstdint>

namespace realm {
struct Entity;

// Map.as post effects. There is no per-map lighting: model shading is fixed
// (Model3D.cpp), so the environment effects are the status filters on the map
// sprite, the low-breath HurtOverlay, and the sidebar GradientOverlay.
inline constexpr std::uint32_t blind_condition = 1U << 6U;  // ConditionEffect.BLIND_BIT (BLIND = 7)
inline constexpr std::uint32_t drunk_condition = 1U << 8U;  // ConditionEffect.DRUNK_BIT (DRUNK = 9)
inline constexpr int breath_threshold = 20;                 // Parameters.BREATH_THRESH
inline constexpr float sidebar_gradient_width = 10;         // GradientOverlay, stage pixels

// Per-frame request to the renderer. All sizes are drawable pixels.
struct ScreenEffects {
  bool blind{};      // ColorMatrixFilter: every channel becomes 0.05 * (r + g + b)
  float blur{};      // BlurFilter box width on both axes; 0 for none
  float vignette{};  // breath overlay alpha multiplier (b * t)
  float sidebar{};   // GradientOverlay width; 0 when no player is in the map

  [[nodiscard]] bool filtered() const { return blind || blur > 0; }
};

// Map.as: d = 20 + 10 * sin(time / 1000), in stage pixels.
[[nodiscard]] float drunk_blur_diameter(std::int32_t time_ms);
// Map.as breath overlay: b * |sin(time / 300)| * 0.75 while 0 <= breath < 20, else 0.
[[nodiscard]] float breath_overlay_alpha(int breath, std::int32_t time_ms);
// HurtOverlay radial gradient: ratios 0/155/255 with alphas 0/0/0.92, spread pad.
[[nodiscard]] float vignette_edge_alpha(float radius);
// Player breath from the BREATH_STAT stat; -1 (the default) until the server sends it.
[[nodiscard]] int player_breath(const Entity& player);
// `pixel_scale` is drawable height / 600, the same scale used for the HUD.
[[nodiscard]] ScreenEffects screen_effects(const Entity* player, std::int32_t time_ms,
                                           double pixel_scale);
}
