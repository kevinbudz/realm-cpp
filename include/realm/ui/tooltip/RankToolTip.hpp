#pragma once
#include "realm/assets/HudArt.hpp"
#include "realm/objects/Catalog.hpp"
#include "realm/ui/fonts/FontBook.hpp"

namespace realm {

// RankToolTip(numStars): "You have earned N" with its star, how to earn more, and the star range of each class tier.
// The ToolTip frame sits `margin` pixels inside the image, which holds the drop shadow.
struct RankToolTipImage {
  Image image;
  double frame_width{}, frame_height{};
  static constexpr int margin = 16;
};

[[nodiscard]] RankToolTipImage render_rank_tooltip(const FontBook& fonts, const HudArt& art, const Catalog& catalog,
                                                   int stars);

}
