#include "realm/ui/tooltip/QuestToolTip.hpp"
#include "realm/ui/menu/PlayerMenu.hpp"
#include "realm/ui/GraphicsUtil.hpp"
#include "flash_assets.hpp"
#include <algorithm>

namespace realm {
namespace {
// ToolTip(6036765, 1, 16549442, 1): background 0x5C1D1D, outline 0xFC8642.
constexpr std::uint32_t frame_background = 0x5c1d1d;
constexpr std::uint32_t frame_outline = 0xfc8642;
// The title's color is the outline color. GameObjectListItem names players in NAME_COLOUR or
// FELLOW_GUILD_COLOR and every other object in 0xB3B3B3.
constexpr std::uint32_t object_color = 0xb3b3b3;
constexpr std::uint32_t name_color = 0xfcdf00;
constexpr std::uint32_t fellow_color = 0xa6ff5d;
}

std::optional<QuestTarget> quest_target(const World& world, std::int32_t id) {
  const auto* target = world.entity(id);
  const auto* local = world.entity(world.player_id());
  if (!target || !local || !world.catalog().object(target->type)) {
    return {};
  }
  QuestTarget result;
  result.id = id;
  result.type = target->type;
  result.skin = target->appearance.texture;
  result.tex1 = target->appearance.tex1;
  result.tex2 = target->appearance.tex2;
  result.name = target->profile.name;
  // GameObject.level_ is -1 until a level is known, and the label shows a level only from 1 up.
  result.level = target->profile.stats.value_or(flash::StatId::LEVEL_STAT, -1);
  result.player = target->player;
  result.fellow_guild = target->player && !local->profile.guild.empty() &&
                        local->profile.guild == target->profile.guild;
  return result;
}

bool QuestToolTip::update(const QuestTarget& target) {
  if (target_ && *target_ == target && image_.pixel_scale == fonts_.pixel_scale()) {
    return false;
  }
  target_ = target;
  render();
  return true;
}

void QuestToolTip::render() {
  const auto& target = *target_;
  const auto title = fonts_.rasterize("Quest!", 22, true, true, frame_outline);
  const auto label = object_row_label(
      catalog_, fonts_, target.type, target.name, target.level,
      target.player ? (target.fellow_guild ? fellow_color : name_color) : object_color);

  const auto portrait =
      art_.game_object_portrait(target.type, target.skin, target.tex1, target.tex2);
  const double portrait_width = portrait.width / portrait.pixel_scale;
  const double portrait_height = portrait.height / portrait.pixel_scale;
  // GameObjectListItem sits at (0, 32): its portrait at (-4, -4) and its name at (32, 6). The title
  // sits at (0, 0). Sprite.width spans the children's extent, so the content runs from the
  // portrait's left edge to the furthest child.
  constexpr double row_y = 32;
  constexpr double portrait_x = -4;
  constexpr double portrait_y = -4;
  constexpr double name_x = 32;
  constexpr double name_y = 6;
  const double left = std::min(0.0, portrait_x);
  const double right = std::max({title.width, name_x + label.width, portrait_x + portrait_width});
  const double bottom =
      std::max(title.height, row_y + std::max(portrait_y + portrait_height, name_y + label.height));
  content_width_ = static_cast<int>(right - left);
  content_height_ = static_cast<int>(bottom);

  image_ = ui_image(canvas_width(), canvas_height(), fonts_.pixel_scale());
  ui_path(image_,
          cut_edge_rect(origin - 6, origin - 6, content_width_ + 12, content_height_ + 12, 4),
          frame_background, frame_outline, 1);
  ui_blit_text(image_, title, origin + title.offset.x, origin + title.offset.y, false, true);
  ui_blit(image_, portrait, origin + portrait_x, origin + row_y + portrait_y, true, true);
  ui_blit_text(image_, label, origin + name_x + label.offset.x,
               origin + row_y + name_y + label.offset.y, false, true);
  ++rebuilds_;
}
}
