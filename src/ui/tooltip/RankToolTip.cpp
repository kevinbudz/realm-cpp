#include "realm/ui/tooltip/RankToolTip.hpp"
#include "realm/ui/GraphicsUtil.hpp"
#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace realm {
namespace {
// ToolTip(3552822, 1, 16777215, 1): a #363636 frame with a white outline.
constexpr std::uint32_t background = 3552822;
constexpr std::uint32_t outline = 16777215;
// SimpleText(13, 11776947) labels; LineBreakDesign(width, 1842204).
constexpr std::uint32_t gray = 11776947;
constexpr std::uint32_t line_color = 1842204;
constexpr std::uint32_t white = 16777215;
constexpr int label_size = 13;
// Content sits 6 pixels inside the frame on every side.
constexpr double inset = 6;
constexpr double how_to_width = 174;
// FameUtil.COLORS: the star tint of each class tier, lightest first.
constexpr std::array<std::uint32_t, 5> tier_tints{0x8A98DE, 0x314DDB, 0xC1272D, 0xF7931E, 0xFFFF00};

std::string joined(const std::vector<std::string>& lines) {
  std::string result;
  for (const auto& line : lines) {
    if (!result.empty()) {
      result += '\n';
    }
    result += line;
  }
  return result;
}

// LegendLine: a tier-tinted star, ": min - max" in bold and a gray star.
struct Legend {
  std::string text;
  std::uint32_t tint{};
  TextRun label;
  Image colored;
  Image gray_star;
};
}  // namespace

RankToolTipImage render_rank_tooltip(const FontBook& fonts, const HudArt& art,
                                     const Catalog& catalog, int stars) {
  // ObjectLibrary.playerChars_: each class contributes five star tiers.
  int classes = 0;
  for (const auto& object : catalog.objects()) {
    classes += object.player ? 1 : 0;
  }

  const std::string earned_text = "You have earned " + std::to_string(stars);
  const auto earned = fonts.layout(earned_text, label_size, true);
  const auto earned_bitmap = fonts.rasterize(earned_text, label_size, true, true, gray);
  const auto earned_star = art.star_graphic(gray);
  // RankToolTip: the star follows the label 7 pixels on and sits 4 pixels below the label's top.
  const double earned_y = inset;
  const double star_x = earned.width + 7;
  const double star_y = earned_y + 4;

  const auto how_to =
      fonts.wrap("You can earn more by completing Class Quests.", label_size, false, how_to_width);
  const auto how_to_bitmap = fonts.rasterize(joined(how_to), label_size, false, true, gray);
  const double how_to_y = earned_y + 28;
  const double how_to_height = fonts.line_advance(label_size) *
                                   static_cast<double>(std::max<std::size_t>(1, how_to.size())) +
                               4;

  // FameUtil.maxStars: the last row is the total, shown in the star's own color.
  std::vector<Legend> legends;
  for (int tier = 0; tier <= 5; ++tier) {
    const int low = tier < 5 ? tier * classes : 5 * classes;
    const int high = tier < 5 ? (tier + 1) * classes - 1 : 5 * classes;
    Legend legend;
    legend.text = ": " + (low == high ? std::to_string(low)
                                      : std::to_string(low) + " - " + std::to_string(high));
    legend.tint = tier < 5 ? tier_tints[static_cast<std::size_t>(tier)] : white;
    legend.label = fonts.layout(legend.text, label_size, true);
    legend.colored = art.star_graphic(legend.tint);
    legend.gray_star = art.star_graphic(gray);
    legends.push_back(std::move(legend));
  }

  // The labels' bottom and the line break, which sits 8 pixels below them.
  const double labels_bottom =
      std::max({earned_y + earned.height, star_y + earned_star.height, how_to_y + how_to_height});
  const double line_y = labels_bottom + 8;
  double right = std::max({inset + earned.width, star_x + earned_star.width, inset + how_to_width});
  std::vector<double> row_y;
  double y = line_y + 4;
  for (const auto& legend : legends) {
    row_y.push_back(y);
    const double row_height = std::max(4.0 + legend.colored.height, legend.label.height);
    const double row_right =
        inset + legend.colored.width + legend.label.width + 2 + legend.gray_star.width;
    right = std::max(right, row_right);
    y += row_height;
  }
  const double content_bottom = y;
  const double frame_width = right + inset;
  const double frame_height = content_bottom + inset;

  RankToolTipImage result;
  constexpr double margin = RankToolTipImage::margin;
  result.frame_width = frame_width;
  result.frame_height = frame_height;
  result.image = ui_image(frame_width + margin * 2, frame_height + margin * 2, fonts.pixel_scale());
  ui_path(result.image, cut_edge_rect(margin, margin, frame_width, frame_height, 4), background,
          outline, 1);

  const auto blit_text = [&](const TextBitmap& bitmap, double x, double text_y) {
    ui_blit_text(result.image, bitmap, margin + x + bitmap.offset.x,
                 margin + text_y + bitmap.offset.y, false, true);
  };
  blit_text(earned_bitmap, inset, earned_y);
  ui_blit(result.image, earned_star, margin + star_x, margin + star_y, true, true);
  blit_text(how_to_bitmap, inset, how_to_y);

  // LineBreakDesign: diamonds of radius 4 at both ends of a 2 pixel bar, 10 pixels shorter than the
  // content.
  const double break_width = right - inset - 10;
  for (const double x : {inset, inset + break_width}) {
    const std::vector<Vec2> diamond{{margin + x - 4, margin + line_y},
                                    {margin + x, margin + line_y - 4},
                                    {margin + x + 4, margin + line_y},
                                    {margin + x, margin + line_y + 4}};
    ui_path(result.image, diamond, line_color);
  }
  ui_rectangle(result.image, margin + inset, margin + line_y - 1, break_width, 2, line_color);

  for (std::size_t i = 0; i < legends.size(); ++i) {
    const auto& legend = legends[i];
    const double top = row_y[i];
    ui_blit(result.image, legend.colored, margin + inset, margin + top + 4, true, true);
    blit_text(fonts.rasterize(legend.text, label_size, true, true, gray),
              inset + legend.colored.width, top);
    ui_blit(result.image, legend.gray_star,
            margin + inset + legend.colored.width + legend.label.width + 2, margin + top + 4, true,
            true);
  }
  ui_shadow(result.image, 16, result.image.width / result.image.pixel_scale,
            result.image.height / result.image.pixel_scale);
  return result;
}

}
