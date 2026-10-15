#include "realm/hud/CreditDisplay.hpp"
#include <algorithm>

namespace realm {
namespace {
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

CreditDisplayImage render_credit_display(const FontBook& fonts, const HudArt& art, int credits,
                                         int fame) {
  constexpr int font_size = 18;
  const auto& coin = art.image("skincreditcoin");
  const auto& fame_icon = art.image("skinfame");
  const auto credits_text =
      fonts.rasterize(std::to_string(credits), font_size, false, true, 0xffffff, 0, 0, 2);
  const auto fame_text =
      fonts.rasterize(std::to_string(fame), font_size, false, true, 0xffffff, 0, 0, 2);

  const double coin_x = -coin.width;
  // Text fields are positioned by their logical TextField width. The native
  // bitmap carries eight pixels of filter padding on each side, which belongs
  // to the image bounds rather than the layout advance.
  const double credits_x = coin_x - credits_text.width + 8;
  const double fame_icon_x = credits_x - fame_icon.width;
  const double fame_text_x = fame_icon_x - fame_text.width + 8;
  const double left = std::min(fame_text_x + fame_text.offset.x, credits_x + credits_text.offset.x);
  const double width = -left;
  const double height = coin.height;

  CreditDisplayImage result;
  result.image = ui_image(width, height, fonts.pixel_scale());
  result.origin_offset = {left, 0};
  result.layout_bounds = {fame_text_x, 0, -fame_text_x, height};

  ui_blit(result.image, fame_icon, width + fame_icon_x, 0, true, true);
  ui_blit_text(result.image, fame_text, width + fame_text_x + fame_text.offset.x,
               (height - fame_text.height) / 2 + fame_text.offset.y, false, true);
  ui_blit_text(result.image, credits_text, width + credits_x + credits_text.offset.x,
               (height - credits_text.height) / 2 + credits_text.offset.y, false, true);
  ui_blit(result.image, coin, width + coin_x, 0, true, true);
  premultiplied_to_straight(result.image);
  return result;
}

}
