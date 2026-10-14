#include "realm/appengine/SavedCharactersList.hpp"
#include "realm/assets/HudArt.hpp"
#include "realm/objects/Catalog.hpp"
#include "realm/assets/FontBook.hpp"
#include "realm/ui/tooltip/RankToolTip.hpp"
#include "flash_assets.hpp"
#include <cmath>
#include <iostream>

namespace {
int checks{};
void check(bool value, const char* reason) {
  ++checks;
  if (!value) throw std::runtime_error(reason);
}
}

int main() {
  try {
    using namespace realm;
    const auto guilded = parse_char_list(
        "<Chars nextCharId='2' maxNumChars='2'><Account><AccountId>42</AccountId><Name>Runes</Name>"
        "<Guild><Name>Realm Guild</Name><Rank>30</Rank></Guild>"
        "<Stats><ClassStats objectType='782'><BestFame>2000</BestFame></ClassStats>"
        "<ClassStats objectType='797'><BestFame>150</BestFame></ClassStats></Stats></Account></Chars>");
    check(guilded.guild_name == "Realm Guild" && guilded.guild_rank == 30, "guild name and rank parse");
    check(guilded.stars() == 7, "five stars from 2000 fame and two from 150 fame");

    const auto unguilded = parse_char_list(
        "<Chars nextCharId='2' maxNumChars='2'><Account><AccountId>42</AccountId>"
        "<Name>Runes</Name><Stats/></Account></Chars>");
    check(unguilded.guild_name.empty() && unguilded.guild_rank == 0, "a missing Guild element leaves no guild");
    check(unguilded.stars() == 0, "no class stats means no stars");

    Catalog catalog(flash::source_root);
    FontBook fonts;
    HudArt art;
    const auto tooltip = render_rank_tooltip(fonts, art, catalog, 7);
    check(tooltip.frame_width >= 186 && tooltip.frame_height > 100, "rank tooltip frame holds the how-to paragraph");
    check(std::abs(tooltip.image.width - (tooltip.frame_width + 2 * RankToolTipImage::margin)) <= 1 &&
              std::abs(tooltip.image.height - (tooltip.frame_height + 2 * RankToolTipImage::margin)) <= 1,
          "rank tooltip image is its frame plus the shadow margin");
    const auto other = render_rank_tooltip(fonts, art, catalog, 0);
    check(other.image.pixels != tooltip.image.pixels, "rank tooltip shows the earned star count");
    std::cout << checks << " account header checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
