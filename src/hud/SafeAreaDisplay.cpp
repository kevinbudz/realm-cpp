#include "realm/hud/SafeAreaDisplay.hpp"
#include <algorithm>
#include <cmath>

namespace realm {
namespace {
// The logical rows [top, bottom), relative to the origin of the bitmap's text, that hold alpha.
struct InkRows {
  double top{};
  double bottom{};
};

InkRows ink_rows(const TextBitmap& text) {
  const auto& image = text.image;
  int first = -1;
  int end = 0;
  for (int y = 0; y < image.height; ++y) {
    for (int x = 0; x < image.width; ++x) {
      const auto at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
                       static_cast<std::size_t>(x)) *
                      4;
      if (image.pixels[at + 3]) {
        if (first < 0) {
          first = y;
        }
        end = y + 1;
        break;
      }
    }
  }
  if (first < 0) {
    return {};
  }
  const double scale = image.pixel_scale;
  return {text.offset.y + first / scale, text.offset.y + end / scale};
}

void premultiplied_to_straight(Image& image) {
  for (std::size_t i = 0; i < image.pixels.size(); i += 4) {
    const unsigned alpha = image.pixels[i + 3];
    for (std::size_t c = 0; c < 3; ++c) {
      image.pixels[i + c] = static_cast<unsigned char>(
          alpha == 255 ? image.pixels[i + c]
          : alpha      ? std::min(255U, static_cast<unsigned>(image.pixels[i + c]) * 256 / alpha)
                       : 0);
    }
  }
}
}

SafeAreaDisplayImage render_safe_area_display(const FontBook& fonts, const HudArt& art, int stars,
                                              std::string_view guild, int guild_rank) {
  constexpr double rank_x = 8;
  constexpr double rank_y = 4;
  constexpr double guild_icon_x = 56;
  constexpr double guild_icon_y = -2;
  constexpr double guild_text_x = 88;
  constexpr double guild_text_y = 6;
  // The rank number is white and outlined, with no backing plate.
  const auto number = stars < 0 ? TextBitmap{}
                                : fonts.rasterize(std::to_string(stars), 18, true,
                                                  TextEffect::outline, 0xffffff, 0, 0, 2);
  const double density = fonts.pixel_scale();
  const auto star = stars < 0 ? Image{} : art.rank_star_icon(stars, 1.4, density);
  // FameUtil's Sprite is scaled 1.4; the 8px image margin holds the outline and is
  // outside the Sprite's layout bounds.
  const auto star_size = art.rank_star_size();
  const double star_w = star_size.x;
  const double star_h = star_size.y;
  const double rank_icon_x = rank_x + number.width + 2;
  // The star's centre sits on the middle of the number's ink, outline included, snapped to the
  // device pixel grid.
  double rank_icon_y = rank_y;
  if (stars >= 0) {
    const auto ink = ink_rows(number);
    const double middle = rank_y + (ink.top + ink.bottom) / 2;
    rank_icon_y = std::round((middle - star_h / 2) * density) / density;
  }
  const double rank_right = stars < 0 ? rank_x : rank_icon_x + star_w + 8;
  const double rank_bottom =
      stars < 0 ? rank_y : std::max(rank_y + number.height, rank_icon_y + star_h + 8);

  const bool has_guild = !guild.empty();
  const auto guild_art = has_guild && guild_rank % 10 == 0 && guild_rank >= 0 && guild_rank <= 40
                             ? &art.image("guildrankicon" + std::to_string(guild_rank))
                             : nullptr;
  const auto guild_bitmap =
      has_guild ? fonts.rasterize(guild, 16, false, TextEffect::outline, 0xffffff) : TextBitmap{};

  // With no rank display, the guild's own bitmap starts at x=56. Do not keep
  // the otherwise-unused rank anchor in the output bounds.
  double left = stars >= 0 ? std::min({rank_x - 2, rank_x + number.offset.x, rank_icon_x - 8})
                           : (has_guild ? guild_icon_x : 0);
  double top = stars >= 0 ? std::min({rank_y + number.offset.y, rank_icon_y - 8, rank_icon_y - 3})
                          : (has_guild ? guild_icon_y : 0);
  double right = stars >= 0 ? rank_right : left;
  double bottom = stars >= 0 ? rank_bottom : top;
  if (has_guild) {
    left = std::min(left, guild_icon_x);
    top = std::min(top, guild_icon_y);
    if (guild_art) {
      right = std::max(right, guild_icon_x + guild_art->width);
      bottom = std::max(bottom, guild_icon_y + guild_art->height);
    }
    right =
        std::max(right, guild_text_x + guild_bitmap.image.width / guild_bitmap.image.pixel_scale +
                            guild_bitmap.offset.x);
    bottom =
        std::max(bottom, guild_text_y + guild_bitmap.image.height / guild_bitmap.image.pixel_scale +
                             guild_bitmap.offset.y);
  }

  SafeAreaDisplayImage result;
  result.origin_offset = {left, top};
  double layout_left = stars >= 0 ? rank_x - 2 : (has_guild ? guild_icon_x : 0);
  double layout_top =
      stars >= 0 ? std::min(rank_y, rank_icon_y - 3) : (has_guild ? guild_icon_y : 0);
  double layout_right = stars >= 0 ? rank_icon_x + star_w + 4 : layout_left;
  double layout_bottom =
      stars >= 0 ? std::max(rank_y + number.height, rank_icon_y + star_h + 5) : layout_top;
  if (has_guild) {
    layout_left = std::min(layout_left, guild_icon_x);
    layout_top = std::min(layout_top, guild_icon_y);
    if (guild_art) {
      layout_right = std::max(layout_right, guild_icon_x + guild_art->width);
      layout_bottom = std::max(layout_bottom, guild_icon_y + guild_art->height);
    }
    layout_right = std::max(layout_right, guild_text_x + guild_bitmap.width);
    layout_bottom = std::max(layout_bottom, guild_text_y + guild_bitmap.height);
  }
  result.layout_bounds = {layout_left, layout_top, layout_right - layout_left,
                          layout_bottom - layout_top};
  if (right <= left || bottom <= top) {
    result.image = ui_image(1, 1, fonts.pixel_scale());
    return result;
  }
  result.image = ui_image(right - left, bottom - top, fonts.pixel_scale());

  if (stars >= 0) {
    const double text_x = rank_x - left;
    const double text_y = rank_y - top;
    const double icon_x = rank_icon_x - left;
    const double icon_y = rank_icon_y - top;
    ui_blit_text(result.image, number, text_x + number.offset.x, text_y + number.offset.y, false,
                 true);
    // The large-star filter adds an eight-pixel image margin; the Sprite's
    // x/y remain at the unfiltered StarGraphic origin.
    ui_blit(result.image, star, icon_x - 8, icon_y - 8, true, true);
  }
  if (has_guild) {
    if (guild_art) {
      ui_blit(result.image, *guild_art, guild_icon_x - left, guild_icon_y - top, true, true);
    }
    ui_blit_text(result.image, guild_bitmap, guild_text_x - left + guild_bitmap.offset.x,
                 guild_text_y - top + guild_bitmap.offset.y, false, true);
  }
  premultiplied_to_straight(result.image);
  return result;
}

}
