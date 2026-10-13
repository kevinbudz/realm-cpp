#pragma once
#include "realm/appengine/SavedCharactersList.hpp"
#include "realm/objects/ObjectLibrary.hpp"
#include "realm/ui/fonts/FontBook.hpp"
#include "realm/assets/HudArt.hpp"
namespace realm {
struct SavedCharacterTooltipImage {
  Image image;
  double frame_width{}, frame_height{};
  static constexpr int margin = 16;
};
[[nodiscard]] SavedCharacterTooltipImage render_saved_character_tooltip(const FontBook& fonts, const HudArt& art,
                                                                        const Catalog& catalog, std::string_view name,
                                                                        const SavedCharacter& character, int best_fame,
                                                                        int best_level);
}
