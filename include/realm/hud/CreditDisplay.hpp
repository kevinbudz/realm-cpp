#pragma once
#include "realm/assets/FontBook.hpp"
#include "realm/assets/HudArt.hpp"
#include "realm/map/Camera.hpp"

namespace realm {

// CreditDisplay is anchored at the right edge of its containing screen. Its
// visible bitmap extends left of that anchor.
struct CreditDisplayImage {
  Image image;
  Vec2 origin_offset{};
  // DisplayObject bounds before the text filter's raster padding.
  Rect layout_bounds{};
  // The Nexus fame button (_fameButton), relative to the right-edge anchor; empty without it.
  Rect fame_button{};
};

// Pass fame_button in the Nexus, as CreditDisplayMediator calls addResourceButtons there: the Deca
// tab_info_button left of the fame text, tinted while hovered. Clicking it opens FameContentPopup.
[[nodiscard]] CreditDisplayImage render_credit_display(const FontBook& fonts, const HudArt& art,
                                                       int credits, int fame,
                                                       bool fame_button = false,
                                                       bool fame_hover = false);

}
