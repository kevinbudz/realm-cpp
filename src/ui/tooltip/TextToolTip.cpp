#include "realm/ui/tooltip/TextToolTip.hpp"
#include "realm/ui/GraphicsUtil.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace realm {
namespace {
constexpr double margin = 12;
// A wordWrap SimpleText's lines, rejoined with explicit newlines.
std::string wrapped(const FontBook& fonts, std::string_view text, int size, bool bold, int width, std::size_t& lines) {
  const auto parts = fonts.wrap(text, size, bold, width);
  std::string joined;
  for (const auto& line : parts) {
    if (!joined.empty()) joined += '\n';
    joined += line;
  }
  lines = parts.size();
  return joined;
}
}
TextToolTipImage render_text_tooltip(const FontBook& fonts, std::string_view title, std::string_view text,
                                     std::uint32_t background, std::uint32_t outline, int max_width) {
  std::size_t title_lines = 0, text_lines = 0;
  const auto title_text = title.empty() ? std::string{} : wrapped(fonts, title, 20, true, max_width, title_lines);
  const auto body_text = text.empty() ? std::string{} : wrapped(fonts, text, 14, false, max_width, text_lines);
  const auto title_bitmap = title_text.empty() ? TextBitmap{} : fonts.rasterize(title_text, 20, true, true);
  const auto body_bitmap = body_text.empty() ? TextBitmap{} : fonts.rasterize(body_text, 14, false, true, 0xb3b3b3);
  // Both fields are wordWrap SimpleTexts that keep their `max_width`; the title's height is its metrics
  // (26 for one 20 pixel line) and the tip's is 16.8 per line plus four (useTextDimensions).
  const double title_height = title_text.empty() ? 0 : title_bitmap.height;
  const double body_height =
      body_text.empty() ? 0 : 16.8 * static_cast<double>(std::max<std::size_t>(1, text_lines)) + 4;
  const double body_y = title_text.empty() ? 0 : title_height + 8;
  const double w = max_width, h = body_text.empty() ? title_height : body_y + body_height;
  TextToolTipImage result;
  result.margin = margin;
  result.frame_width = w + 12;
  result.frame_height = h + 12;
  result.image = ui_image(w + 12 + margin * 2, h + 12 + margin * 2, fonts.pixel_scale());
  ui_path(result.image, cut_edge_rect(margin, margin, w + 12, h + 12, 4), background, outline, 1);
  if (!title_text.empty())
    ui_blit_text(result.image, title_bitmap, margin + 6 + title_bitmap.offset.x, margin + 6 + title_bitmap.offset.y,
                 false, true);
  if (!body_text.empty())
    ui_blit_text(result.image, body_bitmap, margin + 6 + body_bitmap.offset.x,
                 margin + 6 + body_y + body_bitmap.offset.y, false, true);
  ui_shadow(result.image, 8, result.image.width / result.image.pixel_scale,
            result.image.height / result.image.pixel_scale);
  return result;
}
Vec2 text_tooltip_position(Vec2 pointer, double frame_width, double frame_height, double stage_width,
                           double stage_height, double scale) {
  const double w = frame_width * scale, h = frame_height * scale;
  double x = pointer.x < stage_width / 2 ? pointer.x + 12 : pointer.x - w - 1;
  double y = pointer.y < stage_height / 3 ? pointer.y + 12 : pointer.y - h - 1;
  x = std::max(x, 12.0);
  y = std::max(y, 12.0);
  // The origin sits six units inside the frame; callers place the frame, so shift it back.
  return {x - 6 * scale, y - 6 * scale};
}
}
