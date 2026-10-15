#include "realm/ui/tooltip/SavedCharacterTooltip.hpp"
#include "realm/ui/GraphicsUtil.hpp"
#include "realm/hud/Hud.hpp"
#include <algorithm>

namespace realm {
SavedCharacterTooltipImage render_saved_character_tooltip(const FontBook& fonts, const HudArt& art,
                                                          const Catalog& catalog,
                                                          std::string_view name,
                                                          const SavedCharacter& character,
                                                          int best_fame,
                                                          int best_level) {
  // MyPlayerToolTip: header, HP/MP, equipped/main inventory, then class quests.
  const auto* definition = catalog.object(static_cast<std::uint16_t>(character.object_type));
  const std::string class_name = definition ? definition->display_id : "Unknown";
  const std::string suffix = " (" + class_name + " " + std::to_string(character.level) + ")";

  const std::array<TextSpan, 2> spans{{{name, true}, {suffix, false}}};
  const auto header = fonts.rasterize_spans(spans, 13, true, 11776947, .5);
  const std::string best =
      std::to_string(star_count(best_fame)) +
      " of 5 Class Quests Completed\nBest Level Achieved: " + std::to_string(best_level) +
      "\nBest Fame Achieved: " + std::to_string(best_fame);
  const auto best_text = fonts.rasterize(best, 14, false, true, 6206769);
  const int next = next_star_fame(best_fame, 0);
  const auto goal = next > 0 ? fonts.rasterize("Next Goal: Earn " + std::to_string(next) +
                                                   " Fame\n  with a " + class_name,
                                               13, false, true, 16549442)
                             : TextBitmap{};

  const double best_y = 228;
  const double goal_y = best_y + best_text.height - 2;
  SavedCharacterTooltipImage result;
  result.frame_width = std::max({186.0, 36 + header.width, 12 + best_text.width}) + 12;
  result.frame_height = (next > 0 ? goal_y + goal.height : best_y + best_text.height) + 2 + 12;
  result.image = ui_image(result.frame_width + 32, result.frame_height + 32, fonts.pixel_scale());
  auto& image = result.image;
  ui_path(image, cut_edge_rect(16, 16, result.frame_width, result.frame_height, 4), 3552822,
          0xffffff);

  const auto text = [&](const TextBitmap& bitmap, double x, double y) {
    ui_blit_text(image, bitmap, 22 + x + bitmap.offset.x, 22 + y + bitmap.offset.y, false, true);
  };
  ui_blit(image,
          art.portrait(character.object_type, character.skin,
                       static_cast<std::uint32_t>(character.tex1),
                       static_cast<std::uint32_t>(character.tex2)),
          18, 18, true, true);
  text(header, 32, 6);
  const auto bar = [&](std::string_view label, int value, int maximum, int cap, std::uint32_t color,
                       double y) {
    value = maximum > 0 ? std::clamp(value, 0, maximum) : value;
    ui_rectangle(image, 28, 22 + y, 176, 16, 5526612);
    ui_rectangle(image, 28, 22 + y, maximum > 0 ? 176.0 * value / maximum : 176, 16, color);
    text(fonts.rasterize(label, 14, true, true, 0xffffff), 6, y - 3);
    const auto number =
        fonts.rasterize(std::to_string(value) + "/" + std::to_string(maximum), 14, true, true,
                        cap > 0 && maximum == cap ? 16572160 : 0xffffff);
    text(number, 94 - number.width / 2, y - 3);
  };
  bar("HP", character.hp, character.max_hp, definition ? definition->max_hp_cap : 0, 14693428,
      40);
  bar("MP", character.mp, character.max_mp, definition ? definition->max_mp_cap : 0, 6325472, 64);

  for (std::size_t i = 0; i < character.equipment.size(); ++i) {
    const int x = 8 + 44 * static_cast<int>(i % 4);
    const int y = i < 4 ? 88 : 132 + 44 * static_cast<int>((i - 4) / 4);
    const bool equipped = i < 4;
    std::string background =
        equipped ? "equipment" + std::to_string(i)
                 : (character.equipment[i] < 0 ? "hotkey" : "inventory") + std::to_string(i - 4);
    // Inventory backgrounds share the same tile; restricted placeholders are unnecessary for a
    // saved player.
    if (!art.contains(background)) {
      background = "equipment" + std::to_string(i % 4);
    }
    if (character.equipment[i] < 0 && equipped && definition &&
        i < definition->slot_types.size()) {
      const auto empty =
          "empty" + std::to_string(definition->slot_types[i]) + "p" + std::to_string(i);
      if (art.contains(empty)) {
        background = empty;
      }
    }
    if (!equipped && character.equipment[i] < 0 && image.pixel_scale != 1) {
      ui_blit(image, art.image("inventory" + std::to_string(i - 4)), 22 + x, 22 + y, true, true);
      const auto label = std::to_string(i - 3);
      const auto number = fonts.rasterize(label, 26, true, false, 3552822, 0, 0, 1, 26, 30);
      text(number, x + 20 - fonts.layout(label, 26, true).width / 2, y + 2);
    } else {
      ui_blit(image, art.image(background), 22 + x, 22 + y, true, true);
    }
    const auto item = "item" + std::to_string(character.equipment[i]);
    if (character.equipment[i] >= 0 && art.contains(item)) {
      const auto& icon = art.image(item);
      ui_blit(image, icon, 22 + x + 20 - icon.width / 2.0, 22 + y + 20 - icon.height / 2.0, true,
              true);
    }
    if (character.equipment[i] > 0) {
      const auto* item_definition =
          catalog.object(static_cast<std::uint16_t>(character.equipment[i]));
      if (item_definition) {
        if (const auto tier = tier_bitmap(fonts, *item_definition)) {
          text(*tier, x + 40 - tier->width, y + 24);
        }
      }
    }
  }

  ui_rectangle(image, 28, 250, result.frame_width - 22, 2, 1842204);
  text(best_text, 8, best_y);
  if (next > 0) {
    text(goal, 8, goal_y);
  }
  ui_shadow(image, 16, result.frame_width + 32, result.frame_height + 32);
  return result;
}
}
