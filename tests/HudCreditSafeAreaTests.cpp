#include "realm/hud/CreditDisplay.hpp"
#include "realm/hud/SafeAreaDisplay.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <fstream>

namespace {
int checks{};
void check(bool value, const char* reason) {
  ++checks;
  if (!value) throw std::runtime_error(reason);
}
bool same_pixels(const realm::Image& a, const realm::Image& b) {
  return a.width == b.width && a.height == b.height && a.pixels == b.pixels;
}
}

int main(int argc, char** argv) {
  try {
    using namespace realm;
    FontBook fonts;
    HudArt art;
    const std::filesystem::path output = argc > 1 ? argv[1] : std::filesystem::path{};
    if (!output.empty()) std::filesystem::create_directories(output);
    const auto capture = [&](const char* name, const Image& image) {
      if (!output.empty()) save_png(output / name, image);
    };

    const auto credit = render_credit_display(fonts, art, 12345, 678);
    check(credit.image.width > 0 && credit.image.height == 40, "credit display uses the 40px source icons");
    check(credit.origin_offset.x < 0 && std::abs(credit.origin_offset.x + credit.image.width) < 1,
          "credit image ends at its right-edge display anchor");
    check(std::abs(credit.layout_bounds.x + 145) < 1 && std::abs(credit.layout_bounds.width - 145) < 1,
          "credit layout bounds match the unfiltered Flash display object");
    check(std::any_of(credit.image.pixels.begin() + static_cast<std::ptrdiff_t>((credit.image.width - 2) * 4),
                      credit.image.pixels.end(), [](unsigned char channel) { return channel != 0; }),
          "coin icon reaches the right edge");
    capture("credit-1x.png", credit.image);
    const auto changed_credit = render_credit_display(fonts, art, 987654, 3210);
    check(!same_pixels(credit.image, changed_credit.image), "credit and fame changes update the display pixels");

    const auto& class_quest_star = art.image("classqueststar");
    check(class_quest_star.width > 16 && class_quest_star.height > 16,
          "class quest star includes the scaled StarGraphic and filter padding");
    check(class_quest_star.pixels[3] == 0 &&
              class_quest_star.pixels[(static_cast<std::size_t>(class_quest_star.height - 1) * class_quest_star.width +
                                       class_quest_star.width - 1) *
                                          4 +
                                      3] == 0,
          "class quest star padding keeps the drop shadow away from its bitmap edge");
    capture("classqueststar.png", class_quest_star);

    const auto empty = render_safe_area_display(fonts, art, -1, "", -1);
    check(empty.image.width == 1 && empty.image.height == 1,
          "negative rank and empty guild produce an empty safe-area image");
    const auto rank = render_safe_area_display(fonts, art, 12, "", -1);
    check(rank.image.width > empty.image.width && rank.image.height > empty.image.height,
          "positive rank renders without requiring guild data");
    check(std::abs(rank.layout_bounds.x - 6) < 1 && std::abs(rank.layout_bounds.y - 4) < 1 &&
              std::abs(rank.layout_bounds.width - 47) <= 1 && std::abs(rank.layout_bounds.height - 26) <= 1,
          "rank logical bounds match Flash RankText");
    capture("safe-rank.png", rank.image);
    const auto zero_rank = render_safe_area_display(fonts, art, 0, "", -1);
    check(zero_rank.image.width > empty.image.width && zero_rank.image.height > empty.image.height,
          "zero stars render the Flash RankText star and count");
    const auto guild = render_safe_area_display(fonts, art, -1, "Native Guild", 20);
    check(guild.image.width > 0 && guild.image.height > 0, "guild name and rank icon render when rank text is absent");
    check(std::abs(guild.layout_bounds.x - 56) < 1 && std::abs(guild.layout_bounds.y + 2) < 1 &&
              std::abs(guild.layout_bounds.width - 118) <= 1 && std::abs(guild.layout_bounds.height - 40) < 1,
          "guild logical bounds match Flash GuildText");
    capture("safe-guild.png", guild.image);
    const auto changed_guild = render_safe_area_display(fonts, art, 12, "Native Guild", 40);
    check(!same_pixels(render_safe_area_display(fonts, art, 12, "Native Guild", 20).image, changed_guild.image),
          "guild rank and stars update the safe-area image");
    check(std::abs(changed_guild.layout_bounds.x - 6) < 1 && std::abs(changed_guild.layout_bounds.y + 2) < 1 &&
              std::abs(changed_guild.layout_bounds.width - 168) <= 1 &&
              std::abs(changed_guild.layout_bounds.height - 40) < 1,
          "combined rank and guild bounds match Flash safe-area displays");
    capture("safe-rank-guild.png", changed_guild.image);

    if (!output.empty()) {
      const auto write_metric = [&](std::ofstream& file, const char* id, Rect bounds, bool comma) {
        file << (comma ? "," : "") << "{\"id\":\"" << id << "\",\"x\":" << bounds.x << ",\"y\":" << bounds.y
             << ",\"width\":" << bounds.width << ",\"height\":" << bounds.height << "}";
      };
      std::ofstream metrics(output / "metrics.json");
      metrics << "[";
      write_metric(metrics, "credit-1x", credit.layout_bounds, false);
      write_metric(metrics, "safe-rank", rank.layout_bounds, true);
      write_metric(metrics, "safe-guild", guild.layout_bounds, true);
      write_metric(metrics, "safe-rank-guild", changed_guild.layout_bounds, true);
      metrics << "]\n";
    }

    fonts.set_pixel_scale(2);
    const auto high_density_credit = render_credit_display(fonts, art, 12345, 678);
    check(high_density_credit.image.width > credit.image.width && high_density_credit.image.height == 80,
          "credit display scales text and icons at high density");
    check(high_density_credit.origin_offset.x < 0, "high-density image keeps its right-edge anchor");
    capture("credit-2x.png", high_density_credit.image);
    const auto high_density_safe = render_safe_area_display(fonts, art, 12, "Native Guild", 20);
    check(high_density_safe.image.width > 0 && high_density_safe.image.height > 0,
          "safe-area display renders at high density");
    capture("safe-rank-guild-2x.png", high_density_safe.image);
    std::cout << checks << " live HUD credit and safe-area display checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
