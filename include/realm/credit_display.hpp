#pragma once
#include "realm/font.hpp"
#include "realm/hud_art.hpp"
#include "realm/camera.hpp"

namespace realm {

// CreditDisplay is anchored at the right edge of its containing screen. Its
// visible bitmap extends left of that anchor.
struct CreditDisplayImage {
  Image image;
  Vec2 origin_offset{};
  // DisplayObject bounds before the text filter's raster padding.
  Rect layout_bounds{};
};

[[nodiscard]] CreditDisplayImage render_credit_display(const FontBook& fonts, const HudArt& art, int credits, int fame);

}
