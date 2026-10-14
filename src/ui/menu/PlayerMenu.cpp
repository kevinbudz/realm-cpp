#include "realm/ui/menu/PlayerMenu.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <cmath>

namespace realm {
namespace {
struct OptionArt {
  std::string_view icon, label;
};
// PlayerMenu.as: AssetLibrary image sets, indices and labels per action.
constexpr std::array<OptionArt, 7> option_art{{{"lofiInterface2-3", "Teleport"},
                                               {"lofiInterfaceBig-10", "Invite"},
                                               {"lofiInterface2-5", "Lock"},
                                               {"lofiInterface2-6", "Unlock"},
                                               {"lofiInterfaceBig-7", "Trade"},
                                               {"lofiInterfaceBig-8", "Ignore"},
                                               {"lofiInterfaceBig-9", "Unignore"}}};
const OptionArt& art_for(PlayerMenuAction action) { return option_art[static_cast<std::size_t>(action)]; }
// MenuOption.mouseOverCT and TeleportMenuOption.inactiveCT applied to white text.
constexpr std::uint32_t text_color = 0xffffff, hover_color = 0xffdc85, inactive_color = 0x545454;
// GuildUtil.OFFICER.
constexpr int officer_rank = 20;
constexpr int option_y(std::size_t index) { return 40 + 28 * static_cast<int>(index); }
}

TextBitmap object_row_label(const Catalog& catalog, const FontBook& fonts, int type, std::string_view name_text,
                            int level, std::uint32_t color) {
  const auto* def = catalog.object(static_cast<std::uint16_t>(type));
  const std::string name = name_text.empty() ? def->display_id : std::string(name_text);
  const std::string suffix =
      name_text.empty() ? "" : " (" + def->display_id + (level > 0 ? " " + std::to_string(level) : "") + ")";
  const std::array spans{TextSpan{name, true}, TextSpan{suffix, false}};
  return fonts.rasterize_spans(spans, 13, true, color, suffix.empty() ? 0 : .5);
}

TextBitmap player_row_label(const Catalog& catalog, const FontBook& fonts, const PlayerMenuTarget& row) {
  return object_row_label(catalog, fonts, row.type, row.name, row.level,
                          row.fellow_guild ? 10944349U : 16572160U);
}

std::optional<PlayerMenuTarget> player_menu_target(const World& world, const Party& party, std::int32_t id,
                                                   bool allow_teleport) {
  const auto* member = world.entity(id);
  const auto* local = world.entity(world.player_id());
  if (!member || !member->player || !local || id == local->id) return {};
  PlayerMenuTarget result;
  result.id = id;
  result.type = member->type;
  result.skin = member->appearance.texture;
  result.tex1 = member->appearance.tex1;
  result.tex2 = member->appearance.tex2;
  result.name = member->profile.name;
  result.level = member->profile.stats.value_or(flash::StatId::LEVEL_STAT, 1);
  result.fellow_guild = !local->profile.guild.empty() && local->profile.guild == member->profile.guild;
  using A = PlayerMenuAction;
  // Player.isTeleportEligible: the target is not invisible.
  if (allow_teleport && !(member->combat.condition & invisible_condition)) result.options.push_back(A::teleport);
  if (local->profile.stats.value_or(flash::StatId::GUILD_RANK_STAT, 0) >= officer_rank && member->profile.guild.empty())
    result.options.push_back(A::invite);
  result.options.push_back(party.starred(id) ? A::unlock : A::lock);
  if (party.ignored(member->profile.stats.value_or(flash::StatId::ACCOUNT_ID_STAT, -1)))
    result.options.push_back(A::unignore);
  else {
    result.options.push_back(A::trade);
    result.options.push_back(A::ignore);
  }
  return result;
}

bool PlayerMenu::open(PlayerMenuTarget target, Vec2 pointer, int window_width, int window_height, double scale) {
  if (target.options.empty() || target.type < 0 || target.type > 65535 ||
      !catalog_.object(static_cast<std::uint16_t>(target.type)))
    return false;
  target_ = std::move(target);
  scale_ = scale;
  hover_ = -1;
  pressed_ = -1;
  entered_ = false;
  cooldown_ms_ = 0;
  layout();
  // DisplayObject width/height include the one-pixel outline stroke.
  const double w = (frame_width_ + 1) * scale, h = (frame_height_ + 1) * scale;
  double x = pointer.x < window_width / 2.0 ? pointer.x + 12 : pointer.x - w - 1;
  double y = pointer.y < window_height / 3.0 ? pointer.y + 12 : pointer.y - h - 1;
  position_ = {std::max(12.0, x), std::max(12.0, y)};
  render();
  return true;
}

void PlayerMenu::layout() {
  double content = 36 + player_row_label(catalog_, fonts_, *target_).width;
  option_widths_.clear();
  for (const auto action : target_->options) {
    // MenuOption bounds: the 40-pixel icon at -12 and the label field at 20.
    const int width = 32 + static_cast<int>(std::ceil(fonts_.layout(art_for(action).label, 18, true).width)) + 4;
    option_widths_.push_back(width);
    content = std::max(content, static_cast<double>(width));
  }
  frame_width_ = std::max(154, static_cast<int>(std::ceil(content)) + 12);
  frame_height_ = option_y(target_->options.size()) + 16;
}

void PlayerMenu::render() {
  if (image_.pixel_scale != fonts_.pixel_scale() ||
      image_.width != static_cast<int>(std::ceil(canvas_width * fonts_.pixel_scale())))
    image_ = ui_image(canvas_width, canvas_height, fonts_.pixel_scale());
  else
    std::fill(image_.pixels.begin(), image_.pixels.end(), 0);
  const auto blit = [&](const Image& source, double x, double y, bool premultiplied, double clip_width = 1e9) {
    ui_blit(image_, source, origin + x, origin + y, premultiplied, true, 1, 0, 0, origin + clip_width, 1e9);
  };
  const auto blit_text = [&](const TextBitmap& text, double x, double y, bool premultiplied, double clip_width = 1e9) {
    ui_blit_text(image_, text, origin + x, origin + y, premultiplied, true, 1, 0, 0, origin + clip_width, 1e9);
  };
  // Stretch the uniform interior of the original cut-edge frame; the outline
  // and its antialiased corners keep their source pixels.
  const auto& frame = art_.image("menuframe");
  const int sw = frame_width_ + 1, sh = frame_height_ + 1;
  Image shell{sw, sh, std::vector<unsigned char>(static_cast<std::size_t>(sw) * static_cast<std::size_t>(sh) * 4)};
  for (int y = 0; y < sh; ++y)
    for (int x = 0; x < sw; ++x) {
      const int sx = x < 12         ? 15 + x
                     : x >= sw - 12 ? 65 - (sw - x)
                                    : 40,
                sy = y < 12         ? 15 + y
                     : y >= sh - 12 ? 65 - (sh - y)
                                    : 40;
      std::copy_n(frame.pixels.begin() + static_cast<std::ptrdiff_t>(sy * frame.width + sx) * 4, 4,
                  shell.pixels.begin() + static_cast<std::ptrdiff_t>(y * sw + x) * 4);
    }
  blit(shell, -7, -7, true);
  const auto portrait = "portrait" + std::to_string(catalog_.portrait_type(target_->type, target_->skin));
  if (art_.contains(portrait))
    blit(art_.portrait(target_->type, target_->skin, target_->tex1, target_->tex2), -4, -4, true);
  const auto header = player_row_label(catalog_, fonts_, *target_);
  blit_text(header, 32 + header.offset.x, 6 + header.offset.y, false);
  for (std::size_t i = 0; i < target_->options.size(); ++i) {
    const auto action = target_->options[i];
    const auto& art = art_for(action);
    const int y = option_y(i);
    const bool cooling = action == PlayerMenuAction::teleport && cooldown_ms_ > 0;
    // TeleportMenuOption tracks hover but only shows it once the cooldown ends.
    const bool hovered = static_cast<int>(i) == hover_ && !cooling;
    const auto icon = "menuicon-" + std::string(art.icon) + (cooling ? "inactive" : hovered ? "hover" : "");
    if (art_.contains(icon)) blit(art_.image(icon), 8 - 12 - 8, y - 12 - 8, true);
    const auto label = fonts_.rasterize(art.label, 18, true, true,
                                        cooling   ? inactive_color
                                        : hovered ? hover_color
                                                  : text_color);
    blit_text(label, 28 + label.offset.x, y - 6 + label.offset.y, false);
    if (cooling) {
      // The white "Teleport" overlay narrows its field as the cooldown runs out.
      const auto bar = fonts_.rasterize(art.label, 18, true, true, text_color);
      const double field = fonts_.layout(art.label, 18, true).width + 4;
      blit_text(bar, 28 + bar.offset.x, y - 6 + bar.offset.y, false,
                28 + field * (1 - cooldown_ms_ / static_cast<double>(teleport_cooldown_ms)));
    }
  }
  // Menu's DropShadowFilter(0,0,0,1,16,16).
  ui_shadow(image_, 8, canvas_width, canvas_height);
  ++rebuilds_;
}

void PlayerMenu::render_state(const PlayerMenuTarget& target, int hover, int cooldown_ms) {
  target_ = target;
  hover_ = hover;
  cooldown_ms_ = std::max(0, cooldown_ms);
  layout();
  render();
}

Vec2 PlayerMenu::local(Vec2 pointer) const {
  return {(pointer.x - position_.x) / scale_, (pointer.y - position_.y) / scale_};
}

bool PlayerMenu::contains(Vec2 pointer) const {
  if (!target_) return false;
  const auto p = local(pointer);
  return p.x >= -6 && p.x < frame_width_ - 6 && p.y >= -6 && p.y < frame_height_ - 6;
}

int PlayerMenu::option_at(Vec2 p) const {
  // Option rows overlap by 12 pixels; the later option is on top.
  for (int i = static_cast<int>(option_widths_.size()) - 1; i >= 0; --i) {
    const int y = option_y(static_cast<std::size_t>(i));
    if (p.x >= -4 && p.x < -4 + option_widths_[static_cast<std::size_t>(i)] && p.y >= y - 12 && p.y < y + 28) return i;
  }
  return -1;
}

bool PlayerMenu::update(const World& world, Vec2 pointer, bool pointer_valid, std::int32_t teleport_ms, double scale) {
  if (!target_) return false;
  if (!world.entity(target_->id)) {
    close();
    return false;
  }
  const auto* live = world.entity(target_->id);
  const bool appearance = target_->skin != live->appearance.texture || target_->tex1 != live->appearance.tex1 ||
                          target_->tex2 != live->appearance.tex2;
  target_->skin = live->appearance.texture;
  target_->tex1 = live->appearance.tex1;
  target_->tex2 = live->appearance.tex2;
  scale_ = scale;
  if (pointer_valid) {
    // Menu.onRollOut, and Menu.onEnterFrame's 40-pixel distance from getRect(stage).
    const double left = position_.x - 6 * scale_, top = position_.y - 6 * scale_;
    const double right = left + frame_width_ * scale_, bottom = top + frame_height_ * scale_;
    const double dx = std::max({left - pointer.x, 0.0, pointer.x - right}),
                 dy = std::max({top - pointer.y, 0.0, pointer.y - bottom});
    const bool inside = contains(pointer);
    if ((entered_ && !inside) || std::hypot(dx, dy) > 40) {
      close();
      return false;
    }
    entered_ = entered_ || inside;
  }
  const int hover = pointer_valid && contains(pointer) ? option_at(local(pointer)) : -1;
  const bool teleport =
      std::find(target_->options.begin(), target_->options.end(), PlayerMenuAction::teleport) != target_->options.end();
  cooldown_ms_ = teleport ? std::max(0, teleport_ms) : 0;
  // Redraw only when the visible overlay width moves by a physical pixel.
  const int cooldown_key = cooldown_ms_ > 0
                               ? static_cast<int>(std::lround(cooldown_ms_ / static_cast<double>(teleport_cooldown_ms) *
                                                              80 * fonts_.pixel_scale()))
                               : -1;
  if (!appearance && hover == hover_ && cooldown_key == rendered_cooldown_ &&
      image_.pixel_scale == fonts_.pixel_scale())
    return false;
  hover_ = hover;
  rendered_cooldown_ = cooldown_key;
  render();
  return true;
}

bool PlayerMenu::pointer_down(Vec2 pointer) {
  if (!contains(pointer)) return false;
  pressed_ = option_at(local(pointer));
  return true;
}

std::optional<PlayerMenu::Request> PlayerMenu::pointer_up(Vec2 pointer) {
  if (!target_) return {};
  const int pressed = std::exchange(pressed_, -1);
  if (!contains(pointer)) return {};
  const int option = option_at(local(pointer));
  if (option < 0 || option != pressed) return {};
  Request request{target_->options[static_cast<std::size_t>(option)], target_->id, target_->name};
  close();
  return request;
}

Vec2 PlayerMenu::canvas_position() const { return {position_.x - origin * scale_, position_.y - origin * scale_}; }
}
