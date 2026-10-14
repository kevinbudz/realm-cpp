#pragma once
#include "realm/assets/FontBook.hpp"
#include "realm/assets/HudArt.hpp"
#include "realm/map/Camera.hpp"

namespace realm {

// Image bounds are relative to the GameSprite origin. The rank and guild text
// are placed in this safe area only on maps with showDisplays enabled.
struct SafeAreaDisplayImage {
  Image image;
  Vec2 origin_offset{};
  // Sprite bounds without image/filter safety padding.
  Rect layout_bounds{};
};

[[nodiscard]] SafeAreaDisplayImage render_safe_area_display(const FontBook& fonts, const HudArt& art, int stars,
                                                            std::string_view guild, int guild_rank);

}
