#pragma once
#include "realm/assets/HudArt.hpp"
#include "realm/objects/Catalog.hpp"
#include "realm/ui/fonts/FontBook.hpp"

namespace realm {

struct ClassToolTipImage {
  Image image;
  double frame_width{}, frame_height{};
  static constexpr int margin = 16;
};

// ClassToolTip for one player class. best_fame and best_level are the account's CharacterStats for the class, zero
// when it has no ClassStats entry.
[[nodiscard]] ClassToolTipImage render_class_tooltip(const FontBook& fonts, const HudArt& art, const Catalog& catalog,
                                                     int object_type, int best_fame, int best_level);

}
