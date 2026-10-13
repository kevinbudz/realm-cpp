#include "realm/ui/menu/PlayerGroup.hpp"
#include "realm/util/GraphicsUtil.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <cmath>

namespace realm {
std::vector<PlayerMenuTarget> player_group_targets(const World& world, std::span<const std::int32_t> ids) {
  std::vector<PlayerMenuTarget> result;
  const auto* local = world.entity(world.player_id());
  if (!local) return result;
  for (const auto id : ids)
    if (const auto* e = world.entity(id); e && e->player && id != local->id) {
      PlayerMenuTarget row;
      row.id = id;
      row.type = e->type;
      row.skin = e->appearance.texture;
      row.tex1 = e->appearance.tex1;
      row.tex2 = e->appearance.tex2;
      row.name = e->profile.name;
      const auto found = e->profile.stats.find(static_cast<std::uint8_t>(flash::StatId::LEVEL_STAT));
      row.level = found == e->profile.stats.end() ? 1 : found->second;
      row.fellow_guild = !local->profile.guild.empty() && local->profile.guild == e->profile.guild;
      result.push_back(std::move(row));
    }
  return result;
}
std::optional<std::int32_t> first_teleport_player(const World& world, std::span<const PlayerMenuTarget> players) {
  for (const auto& row : players)
    if (const auto* e = world.entity(row.id);
        e && e->player && e->id != world.player_id() && !(e->combat.condition & invisible_condition))
      return e->id;
  return {};
}
bool PlayerGroup::refresh_appearance(const World& world) {
  bool changed = false;
  for (auto& row : players_)
    if (const auto* e = world.entity(row.id);
        e && (row.skin != e->appearance.texture || row.tex1 != e->appearance.tex1 || row.tex2 != e->appearance.tex2)) {
      row.skin = e->appearance.texture;
      row.tex1 = e->appearance.tex1;
      row.tex2 = e->appearance.tex2;
      changed = true;
    }
  if (changed) render();
  return changed;
}
void PlayerGroup::layout() {
  double content_width = 0;
  for (const auto& row : players_)
    content_width = std::max(content_width, 36 + player_row_label(catalog_, fonts_, row).width);
  option_width_ = 32 + static_cast<int>(std::ceil(fonts_.layout("Teleport", 18, true).width)) + 4;
  if (menu_) {
    if (teleport_) content_width = std::max(content_width, static_cast<double>(option_width_));
    frame_width_ = std::max(154, static_cast<int>(std::ceil(content_width)) + 12);
    // Portrait bounds are 44px tall; the teleport icon starts at y=-4.
    frame_height_ = (teleport_ ? 52 : 4) + 32 * static_cast<int>(players_.size()) + (teleport_ ? 24 : 20);
  } else {
    const auto hint = fonts_.rasterize("(Click to open menu)", 12, false, true, 0xb3b3b3);
    // Footer centering happens before draw; width includes the -4 portrait edge.
    content_width = std::max(content_width, (std::max(content_width, hint.width + 4) + hint.width) / 2 + 4);
    frame_width_ = static_cast<int>(content_width) + 12;
    frame_height_ = 32 * static_cast<int>(players_.size()) + static_cast<int>(hint.height + 4) + 12;
  }
}
void PlayerGroup::position(Vec2 pointer, int width, int height) {
  const double w = (frame_width_ + 1) * scale_, h = (frame_height_ + 1) * scale_;
  position_ = {std::max(12.0, pointer.x < width / 2.0 ? pointer.x + 12 : pointer.x - w - 1),
               std::max(12.0, pointer.y < height / 3.0 ? pointer.y + 12 : pointer.y - h - 1)};
}
bool PlayerGroup::tooltip(std::vector<PlayerMenuTarget> players, Vec2 pointer, int width, int height, double scale) {
  if (menu()) return false;
  if (players.empty()) {
    close();
    return false;
  }
  const bool changed = players_ != players || menu_ || image_.pixel_scale != fonts_.pixel_scale();
  players_ = std::move(players);
  menu_ = teleport_ = hover_ = false;
  scale_ = scale;
  if (changed) {
    layout();
    render();
  }
  position(pointer, width, height);
  return changed;
}
bool PlayerGroup::arrow_tooltip(std::vector<PlayerMenuTarget> players, Vec2 anchor, double rotation, double arrow_scale,
                                double scale) {
  const bool changed = tooltip(std::move(players), anchor, 1, 1, scale);
  if (!visible() || menu()) return changed;
  const double w = (frame_width_ - 12) * scale, h = (frame_height_ - 12) * scale;
  const double x0 = 26 * arrow_scale * std::cos(rotation), y0 = 26 * arrow_scale * std::sin(rotation);
  const double degrees = rotation * 180 / std::acos(-1.0);
  Vec2 offset;
  if (degrees >= 45 && degrees <= 135)
    offset = {x0 + w / (2 * std::tan(rotation)) - w / 2, y0};
  else if (degrees <= -45 && degrees >= -135)
    offset = {x0 - w / (2 * std::tan(rotation)) - w / 2, y0 - h};
  else if (degrees < 45 && degrees > -45)
    offset = {x0, y0 + h * std::tan(rotation) / 2 - h / 2};
  else
    offset = {x0 - w, y0 - h * std::tan(rotation) / 2 - h / 2};
  position_ = {anchor.x + offset.x, anchor.y + offset.y};
  return changed;
}
bool PlayerGroup::open(std::vector<PlayerMenuTarget> players, bool allow, Vec2 pointer, int width, int height,
                       double scale) {
  if (players.empty()) return false;
  players_ = std::move(players);
  menu_ = true;
  teleport_ = allow;
  hover_ = pressed_ = entered_ = false;
  cooldown_ = 0;
  cooldown_key_ = -2;
  scale_ = scale;
  layout();
  position(pointer, width, height);
  render();
  return true;
}
void PlayerGroup::render_state(std::vector<PlayerMenuTarget> players, bool menu, bool teleport, bool hover,
                               int cooldown) {
  players_ = std::move(players);
  menu_ = menu;
  teleport_ = teleport;
  hover_ = hover;
  cooldown_ = std::clamp(cooldown, 0, teleport_cooldown_ms);
  layout();
  render();
}
void PlayerGroup::render() {
  image_ = ui_image(canvas_width(), canvas_height(), fonts_.pixel_scale());
  const auto blit = [&](const Image& im, double x, double y, bool premult = false, double clip = 1e9) {
    ui_blit(image_, im, origin + x, origin + y, premult, true, 1, 0, 0, origin + clip, 1e9);
  };
  const auto blit_text = [&](const TextBitmap& text, double x, double y, bool premult = false, double clip = 1e9) {
    ui_blit_text(image_, text, origin + x, origin + y, premult, true, 1, 0, 0, origin + clip, 1e9);
  };
  ui_path(image_, cut_edge_rect(origin - 6, origin - 6, frame_width_, frame_height_, 4), 0x363636, 0xffffff, 1,
          menu_ ? 1 : .5);
  int y = menu_ ? (teleport_ ? 52 : 4) : 0;
  for (const auto& row : players_) {
    const auto portrait = "portrait" + std::to_string(catalog_.portrait_type(row.type, row.skin));
    if (art_.contains(portrait)) blit(art_.portrait(row.type, row.skin, row.tex1, row.tex2), -4, y - 4, true);
    const auto label = player_row_label(catalog_, fonts_, row);
    blit_text(label, 32 + label.offset.x, y + 6 + label.offset.y);
    y += 32;
  }
  if (!menu_) {
    const auto hint = fonts_.rasterize("(Click to open menu)", 12, false, true, 0xb3b3b3);
    double rows_width = 0;
    for (const auto& row : players_)
      rows_width = std::max(rows_width, 36 + player_row_label(catalog_, fonts_, row).width);
    blit_text(hint, std::max(rows_width, hint.width + 4) / 2 - hint.width / 2 + hint.offset.x, y + hint.offset.y);
  } else if (teleport_) {
    const bool cooling = cooldown_ > 0;
    const std::string key = "menuicon-lofiInterface2-3" + std::string(cooling ? "inactive" : hover_ ? "hover" : "");
    if (art_.contains(key)) blit(art_.image(key), -12, -12, true);
    const auto label = fonts_.rasterize("Teleport", 18, true, true, cooling ? 0x545454 : hover_ ? 0xffdc85 : 0xffffff);
    blit_text(label, 28 + label.offset.x, 2 + label.offset.y);
    if (cooling) {
      // barText has no own shadow in TeleportMenuOption.
      const auto bar = fonts_.rasterize("Teleport", 18, true, false, 0xffffff);
      blit_text(bar, 28 + bar.offset.x, 2 + bar.offset.y, false,
                28 + (fonts_.layout("Teleport", 18, true).width + 4) * (1 - cooldown_ / 10000.0));
    }
    // Source divider width is measured before player rows have been added.
    const int width = option_width_ - 24;
    ui_rectangle(image_, origin + 6, origin + 39, width, 2, 0x1c1c1c, true);
    for (const int x : {6, 6 + width}) {
      const std::array diamond{Vec2{static_cast<double>(origin + x - 4), origin + 40.0},
                               Vec2{origin + x + 0.0, origin + 36.0}, Vec2{origin + x + 4.0, origin + 40.0},
                               Vec2{origin + x + 0.0, origin + 44.0}};
      ui_path(image_, diamond, 0x1c1c1c);
    }
  }
  ui_shadow(image_, 8, canvas_width(), canvas_height());
  ++rebuilds_;
}
bool PlayerGroup::contains(Vec2 pointer) const {
  if (!menu()) return false;
  const auto p = local(pointer);
  return p.x >= -6.5 && p.x <= frame_width_ - 5.5 && p.y >= -6.5 && p.y <= frame_height_ - 5.5;
}
bool PlayerGroup::teleport_at(Vec2 p) const {
  return teleport_ && p.x >= -4 && p.x < -4 + option_width_ && p.y >= -4 && p.y < 36;
}
bool PlayerGroup::pointer_down(Vec2 p) {
  if (!contains(p)) return false;
  pressed_ = teleport_at(local(p));
  return true;
}
bool PlayerGroup::pointer_up(Vec2 p) {
  const bool pressed = std::exchange(pressed_, false);
  return menu() && pressed && contains(p) && teleport_at(local(p));
}
bool PlayerGroup::update(Vec2 pointer, bool valid, std::int32_t cooldown, double scale) {
  if (!menu()) return false;
  if (scale_ != scale) pressed_ = false;
  scale_ = scale;
  if (valid) {
    const double left = position_.x - 6.5 * scale_, top = position_.y - 6.5 * scale_;
    const double dx = std::max({left - pointer.x, 0.0, pointer.x - (left + (frame_width_ + 1) * scale_)});
    const double dy = std::max({top - pointer.y, 0.0, pointer.y - (top + (frame_height_ + 1) * scale_)});
    const bool inside = contains(pointer);
    if ((entered_ && !inside) || std::hypot(dx, dy) > 40) {
      close();
      return false;
    }
    entered_ = entered_ || inside;
  }
  const bool hover = valid && contains(pointer) && teleport_at(local(pointer));
  cooldown_ = teleport_ ? std::clamp(cooldown, 0, teleport_cooldown_ms) : 0;
  const int key = cooldown_ > 0 ? static_cast<int>(std::lround(cooldown_ / 10000.0 * 80 * fonts_.pixel_scale())) : -1;
  if (hover_ == hover && key == cooldown_key_ && image_.pixel_scale == fonts_.pixel_scale()) return false;
  hover_ = hover;
  cooldown_key_ = key;
  render();
  return true;
}
}
