#include "realm/background/BackdropData.hpp"
#include "realm/map/PostEffects.hpp"
#include "realm/map/Map.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
// Rules from Map.as and the background classes that need no GL context.
namespace {
int checks{};
void check(bool ok, const std::string& why) {
  ++checks;
  if (!ok) throw std::runtime_error(why);
}
}
using namespace realm;
namespace {
void near(double actual, double expected, double tolerance, const std::string& why) {
  check(std::abs(actual - expected) <= tolerance,
        why + ": got " + std::to_string(actual) + " expected " + std::to_string(expected));
}
constexpr auto stat_breath = static_cast<std::uint8_t>(flash::StatId::BREATH_STAT);
void pure_rules() {
  // Conditions: BLIND = 7 and DRUNK = 9 in ConditionEffect, bit (id - 1).
  check(blind_condition == 64 && drunk_condition == 256, "condition bits");
  for (int t = 0; t < 20000; t += 37) {
    const auto d = drunk_blur_diameter(t);
    check(d >= 10 - 1e-4F && d <= 30 + 1e-4F, "drunk blur stays within 10..30");
  }
  near(drunk_blur_diameter(0), 20, 1e-5, "blur at t=0");
  near(drunk_blur_diameter(static_cast<int>(1000 * std::numbers::pi / 2)), 30, 1e-3, "blur peak");
  // Breath overlay only while 0 <= breath < 20; strongest at 0 when |sin| is 1.
  check(breath_overlay_alpha(-1, 500) == 0 && breath_overlay_alpha(20, 500) == 0 && breath_overlay_alpha(100, 500) == 0,
        "no overlay outside 0..19");
  near(breath_overlay_alpha(0, static_cast<int>(300 * std::numbers::pi / 2)), .75, 1e-3, "full-empty peak");
  near(breath_overlay_alpha(10, static_cast<int>(300 * std::numbers::pi / 2)), .375, 1e-3, "half breath");
  check(breath_overlay_alpha(5, 0) == 0, "sin(0)");
  // Radial gradient ratios 0/155/255 with alphas 0/0/.92, padded past the corners.
  near(vignette_edge_alpha(0), 0, 1e-6, "center clear");
  near(vignette_edge_alpha(155.F / 255), 0, 1e-6, "inner stop");
  near(vignette_edge_alpha(1), .92, 1e-6, "corner");
  near(vignette_edge_alpha(2), .92, 1e-6, "pad");
  near(vignette_edge_alpha((155.F / 255 + 1) / 2), .46, 1e-5, "midway");
  // screen_effects from the player entity.
  check(!screen_effects(nullptr, 0, 1).filtered() && screen_effects(nullptr, 0, 1).sidebar == 0,
        "no player, no effects");
  Entity player;
  player.player = true;
  auto fx = screen_effects(&player, 1000, 1);
  check(!fx.blind && fx.blur == 0 && fx.vignette == 0 && fx.sidebar == 10, "idle player only has the sidebar");
  player.combat.condition = blind_condition;
  fx = screen_effects(&player, 0, 1);
  check(fx.blind && fx.blur == 0 && fx.filtered(), "blind");
  player.combat.condition = drunk_condition;
  fx = screen_effects(&player, 0, 2);
  near(fx.blur, 40, 1e-4, "drunk blur scales with the UI");
  near(fx.sidebar, 20, 1e-6, "sidebar scales");
  check(!fx.blind, "drunk is not blind");
  player.combat.condition = drunk_condition | blind_condition;
  fx = screen_effects(&player, 0, 1);
  check(fx.blind && fx.blur > 0, "both filters apply, as in the software path");
  player.combat.condition = ~(blind_condition | drunk_condition);
  fx = screen_effects(&player, 0, 1);
  check(!fx.filtered(), "other conditions do not filter");
  player.combat.condition = 0;
  player.profile.stats[stat_breath] = 4;
  fx = screen_effects(&player, static_cast<int>(300 * std::numbers::pi / 2), 1);
  near(fx.vignette, .8 * .75, 1e-3, "breath 4");
  player.profile.stats[stat_breath] = 0;
  check(player_breath(player) == 0 &&
            screen_effects(&player, static_cast<int>(300 * std::numbers::pi / 2), 1).vignette > .7F,
        "breath 0 shows the overlay");
  player.profile.stats.erase(stat_breath);
  check(player_breath(player) == -1 && screen_effects(&player, 500, 1).vignette == 0, "breath defaults to -1");
}
void backgrounds() {
  for (std::uint32_t seed = 0; seed < 32; ++seed) {
    const auto stars = star_field(seed);
    check(!stars.empty() && stars.size() <= 100, "at most 100 stars");
    for (std::size_t i = 0; i < stars.size(); ++i) {
      const auto& s = stars[i];
      check(s.x >= -500 && s.x <= 500 && s.y >= -500 && s.y <= 500, "star inside the 1000x1000 square");
      check(s.scale >= 2 && s.scale <= 4 && s.image >= 0 && s.image < 10, "star scale 2..4 and one of ten images");
      for (std::size_t j = 0; j < i; ++j)
        check(std::hypot(s.x - stars[j].x, s.y - stars[j].y) >= 3, "stars keep a distance of 3");
    }
    check(stars.size() == star_field(seed).size(), "star field is deterministic");
  }
  check(star_field(1).front().x != star_field(2).front().x, "seeds differ");
  const auto water = nexus_water(7);
  constexpr int size = nexus_water_size;
  check(water.width == size && water.height == size && water.pixels.size() == static_cast<std::size_t>(size) * size * 4,
        "water is 1024x1024 RGBA");
  check(water.pixels == nexus_water(7).pixels && water.pixels != nexus_water(8).pixels, "water is seeded");
  double sum{}, squares{};
  int minimum = 255, maximum = 0;
  for (int i = 0; i < size * size; ++i) {
    const auto* p = &water.pixels[static_cast<std::size_t>(i) * 4];
    check(p[0] == 0 && p[1] == 0 && p[3] == 255, "blue-only opaque water");
    sum += p[2];
    squares += static_cast<double>(p[2]) * p[2];
    minimum = std::min<int>(minimum, p[2]);
    maximum = std::max<int>(maximum, p[2]);
  }
  const double mean = sum / (size * size), deviation = std::sqrt(squares / (size * size) - mean * mean);
  near(mean, 127, 12, "water mean");
  check(deviation > 12 && deviation < 40, "water contrast " + std::to_string(deviation));
  check(minimum < 110 && maximum > 140, "water has relief");
  // Stitched: the seam between the last and first column is as smooth as any neighbouring pair.
  double seam{}, inner{};
  for (int y = 0; y < size; ++y) {
    seam += std::abs(water.pixels[(static_cast<std::size_t>(y) * size) * 4 + 2] -
                     water.pixels[(static_cast<std::size_t>(y) * size + size - 1) * 4 + 2]);
    inner += std::abs(water.pixels[(static_cast<std::size_t>(y) * size + 512) * 4 + 2] -
                      water.pixels[(static_cast<std::size_t>(y) * size + 511) * 4 + 2]);
  }
  check(seam <= inner * 2 + size, "water tiles without a seam");
}

}
int main() {
  try {
    pure_rules();
    backgrounds();
    std::cout << "Post effect rule checks: " << checks << " passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Post effect rule test failed: " << e.what() << "\n";
    return 1;
  }
}
