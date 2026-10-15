#include "realm/ui/tooltip/ClassToolTip.hpp"
#include "realm/appengine/SavedCharactersList.hpp"
#include "realm/ui/GraphicsUtil.hpp"
#include <algorithm>
#include <array>
#include <string>

namespace realm {

ClassToolTipImage render_class_tooltip(const FontBook& fonts, const HudArt& art,
                                       const Catalog& catalog, int object_type, int best_fame,
                                       int best_level) {
  // ClassToolTip: the portrait at (-4,-4), the name at (32,6), the description in a 174 pixel
  // wordWrap field at (8,40), a divider, then the best stats and the next class quest. The content
  // is 190 pixels wide from the portrait's -4 edge, and ToolTip.draw's frame is 12 pixels larger
  // than that, starting at (-6,-6).
  constexpr double content_width = 190;
  const auto* definition = catalog.object(static_cast<std::uint16_t>(object_type));
  const std::string name = definition ? definition->id : "Unknown";
  const std::string description = definition && definition->xml.has("Description")
                                      ? definition->xml.child("Description").text()
                                      : "";
  const auto& portrait = art.image("tooltipportrait" + std::to_string(object_type));
  const auto name_text = fonts.rasterize(name, 13, true, true, 11776947);
  std::string body;
  for (const auto& line : fonts.wrap(description, 13, false, 174)) {
    if (!body.empty()) {
      body += '\n';
    }
    body += line;
  }
  const auto description_text =
      fonts.rasterize(body.empty() ? std::string(" ") : body, 13, false, true, 11776947);

  // The divider sits four pixels below the lowest of the portrait, name and description.
  const double content_bottom =
      std::max({-4.0 + portrait.height, 6.0 + name_text.height, 40.0 + description_text.height});
  const double divider_y = content_bottom + 4;
  const int stars = star_count(best_fame);
  const auto best_text = fonts.rasterize(
      std::to_string(stars) + " of 5 Class Quests Completed\nBest Level Achieved: " +
          std::to_string(best_level) + "\nBest Fame Achieved: " + std::to_string(best_fame),
      14, false, true, 6206769);
  const int next = next_star_fame(best_fame, 0);
  const auto goal =
      next > 0
          ? fonts.rasterize("Next Goal: Earn " + std::to_string(next) + " Fame\n  with a " + name,
                            13, false, true, 16549442)
          : TextBitmap{};
  const double best_y = content_bottom + 10;
  const double goal_y = best_y + 2 + best_text.height;
  const double bottom = next > 0 ? goal_y + goal.height : best_y + best_text.height;

  ClassToolTipImage result;
  result.frame_width = content_width + 12;
  result.frame_height = bottom + 16;
  const double width = result.frame_width;
  const double height = result.frame_height;
  result.image = ui_image(width + 32, height + 32, fonts.pixel_scale());
  auto& image = result.image;
  ui_path(image, cut_edge_rect(16, 16, width, height, 4), 3552822, 0xffffff);
  const auto text = [&](const TextBitmap& bitmap, double x, double y) {
    ui_blit_text(image, bitmap, 22 + x + bitmap.offset.x, 22 + y + bitmap.offset.y, false, true);
  };
  ui_blit(image, portrait, 18, 18, true, true);
  text(name_text, 32, 6);
  text(description_text, 8, 40);
  // LineBreakDesign(176): a 2 pixel bar from x 6 to 182, with diamonds of half-diagonal 4 at both
  // ends.
  ui_rectangle(image, 28, 22 + divider_y - 1, 176, 2, 1842204);
  for (const double cx : {28.0, 204.0}) {
    const double cy = 22 + divider_y;
    const std::array<Vec2, 4> diamond{{{cx - 4, cy}, {cx, cy - 4}, {cx + 4, cy}, {cx, cy + 4}}};
    ui_path(image, diamond, 1842204);
  }
  text(best_text, 8, best_y);
  if (next > 0) {
    text(goal, 8, goal_y);
  }
  ui_shadow(image, 16, width + 32, height + 32);
  return result;
}

}
