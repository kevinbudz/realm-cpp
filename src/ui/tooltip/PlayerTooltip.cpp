#include "realm/ui/tooltip/PlayerTooltip.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <cmath>

namespace realm {
int PartyHover::cell(Vec2 point) {
  if (point.x < 6 || point.x >= 200 || point.y < 508 || point.y >= 600) return -1;
  return static_cast<int>((point.y - 508) / 32) * 2 + (point.x >= 106 ? 1 : 0);
}
void PartyHover::update(const World& world, const Party& party, HudState& state, const HudState& displayed,
                        Vec2 pointer, bool enabled, std::int32_t time) {
  const auto* player = world.entity(world.player_id());
  const int slot = cell(pointer);
  if (!enabled || !state.party.shown || !player || slot < 0) {
    reset();
    return;
  }
  const auto i = static_cast<std::size_t>(slot);
  if (!frozen_) {
    // Use what the user actually saw, even if the membership refreshed this frame.
    const auto& before = displayed.party.shown ? displayed.party : state.party;
    if (!before.members[i] || before.members[i]->object_id < 0) {
      reset();
      return;
    }
    for (std::size_t n = 0; n < ids_.size(); ++n) ids_[n] = before.members[n] ? before.members[n]->object_id : -1;
    frozen_ = true;
  }
  if (ids_[i] < 0) {
    reset();
    return;
  }
  state.party_members(world, *player, party, ids_, time);
  target_ = state.party.members[i] ? ids_[i] : -1;
}
std::optional<PlayerTooltipTarget> player_tooltip_target(const World& world, std::int32_t id) {
  const auto* member = world.entity(id);
  const auto* local = world.entity(world.player_id());
  if (!member || !member->player || !local || id == local->id) return {};
  const auto* definition = world.catalog().object(member->type);
  if (!definition) return {};
  PlayerTooltipTarget result;
  result.id = id;
  result.type = member->type;
  result.skin = member->appearance.texture;
  result.tex1 = member->appearance.tex1;
  result.tex2 = member->appearance.tex2;
  result.level = member->profile.stats.value_or(flash::StatId::LEVEL_STAT, 1);
  result.stars = member->profile.stats.value_or(flash::StatId::NUM_STARS_STAT, 0);
  result.guild_rank = member->profile.stats.value_or(flash::StatId::GUILD_RANK_STAT, -1);
  result.name = member->profile.name;
  result.guild = member->profile.guild;
  result.fellow_guild = !local->profile.guild.empty() && local->profile.guild == member->profile.guild;
  using S = flash::StatId;
  result.bars = {StatusBarState{"HP", 14693428, 0, member->combat.hp, member->combat.max_hp,
                                member->profile.stats.value_or(S::MAX_HP_BOOST_STAT, 0), definition->max_hp_cap},
                 StatusBarState{"MP", 6325472, 0, member->profile.stats.value_or(S::MP_STAT, 0),
                                member->profile.stats.value_or(S::MAX_MP_STAT, 0),
                                member->profile.stats.value_or(S::MAX_MP_BOOST_STAT, 0), definition->max_mp_cap}};
  for (std::size_t i = 0; i < 4 && i < definition->slot_types.size(); ++i)
    result.slot_types[i] = definition->slot_types[i];
  for (int i = 0; i < 4; ++i) result.equipment[static_cast<std::size_t>(i)] = inventory_item(*member, i);
  return result;
}
void PlayerTooltip::rectangle(double x, int y, double w, int h, std::uint32_t color) {
  x += padding;
  y += padding;
  if (image_.pixel_scale != 1) {
    ui_rectangle(image_, x, y, w, h, color, true);
    return;
  }
  for (int row = std::max(0, y); row < std::min(maximum_height, y + h); ++row)
    for (int col = std::max(0, static_cast<int>(std::floor(x)));
         col < std::min(width, static_cast<int>(std::ceil(x + w))); ++col) {
      const auto at = static_cast<std::size_t>(row * width + col) * 4;
      const double coverage = std::clamp(std::min(x + w, col + 1.0) - std::max(x, static_cast<double>(col)), 0.0, 1.0);
      const unsigned alpha = static_cast<unsigned>(std::lround(coverage * 4)) * 64;
      for (unsigned c = 0; c < 3; ++c)
        image_.pixels[at + c] = static_cast<unsigned char>(
            (((color >> (16U - 8U * c)) & 255U) * alpha + image_.pixels[at + c] * (256 - alpha)) >> 8U);
      image_.pixels[at + 3] =
          static_cast<unsigned char>(std::min(255U, alpha + (image_.pixels[at + 3] * (256 - alpha) >> 8U)));
    }
}
void PlayerTooltip::blit(const Image& source, int x, int y, bool premultiplied) {
  x += padding;
  y += padding;
  if (image_.pixel_scale != 1) {
    ui_blit(image_, source, x, y, premultiplied, true);
    return;
  }
  for (int row = std::max(0, -y); row < std::min(source.height, maximum_height - y); ++row)
    for (int col = std::max(0, -x); col < std::min(source.width, width - x); ++col) {
      const auto src = static_cast<std::size_t>(row * source.width + col) * 4;
      const auto dst = static_cast<std::size_t>((row + y) * width + col + x) * 4;
      const unsigned alpha = source.pixels[src + 3];
      if (!alpha) continue;
      for (unsigned c = 0; c < 3; ++c) {
        const unsigned value =
            premultiplied || alpha == 255 ? source.pixels[src + c] : (source.pixels[src + c] * alpha + 255U) >> 8U;
        image_.pixels[dst + c] =
            static_cast<unsigned char>(std::min(255U, value + (image_.pixels[dst + c] * (256 - alpha) >> 8U)));
      }
      image_.pixels[dst + 3] =
          static_cast<unsigned char>(std::min(255U, alpha + (image_.pixels[dst + 3] * (256 - alpha) >> 8U)));
    }
}
void PlayerTooltip::text(std::string_view value, int size, bool bold, std::uint32_t color, double x, int y,
                         double opacity, int field_width, int field_height) {
  auto bitmap = fonts_.rasterize(value, size, bold, true, color, x - std::floor(x), 0, 1, field_width, field_height);
  double bx = x + bitmap.offset.x + padding, by = y + bitmap.offset.y + padding;
  if (image_.pixel_scale != 1) bitmap = place_text(bitmap, image_.pixel_scale, bx, by);
  premultiply_flash_bitmap(bitmap.image);
  if (opacity != 1)
    for (auto& channel : bitmap.image.pixels)
      channel = static_cast<unsigned char>(static_cast<unsigned>(channel) * static_cast<unsigned>(opacity * 256) >> 8U);
  if (image_.pixel_scale != 1)
    ui_blit(image_, bitmap.image, bx, by, true, true);
  else
    blit(bitmap.image, static_cast<int>(std::round(x + bitmap.offset.x)), y + static_cast<int>(bitmap.offset.y), true);
}
void PlayerTooltip::shadow() {
  if (image_.pixel_scale != 1) {
    ui_shadow(image_, 8, draw_width_, height_);
    return;
  }
  // DropShadowFilter(0,0,0,1,16,16): one separable box pass with half
  // endpoint taps. Work happens only when visible content changes.
  const int w = draw_width_, h = height_;
  std::vector<unsigned char> horizontal(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      unsigned sum{};
      for (int k = -8; k <= 8; ++k)
        if (x + k >= 0 && x + k < w)
          sum += (std::abs(k) == 8 ? 1U : 2U) *
                 image_.pixels[(static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x + k)) * 4 + 3];
      horizontal[static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)] = static_cast<unsigned char>(sum >> 5U);
    }
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      unsigned sum{};
      for (int k = -8; k <= 8; ++k)
        if (y + k >= 0 && y + k < h)
          sum += (std::abs(k) == 8 ? 1U : 2U) *
                 horizontal[static_cast<std::size_t>(y + k) * w + static_cast<std::size_t>(x)];
      auto& alpha = image_.pixels[(static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)) * 4 + 3];
      alpha = static_cast<unsigned char>(alpha + ((sum >> 5U) * (256U - alpha) >> 8U));
    }
  for (std::size_t at = 0; at < image_.pixels.size(); at += 4) {
    const unsigned alpha = image_.pixels[at + 3];
    if (!alpha || alpha == 255) continue;
    for (unsigned c = 0; c < 3; ++c)
      image_.pixels[at + c] = static_cast<unsigned char>(std::min(255U, (image_.pixels[at + c] * 256U) / alpha));
  }
}
bool PlayerTooltip::update(const std::optional<PlayerTooltipTarget>& target) {
  if (!target) {
    target_.reset();
    return false;
  }
  if (target_ == target && image_.pixel_scale == fonts_.pixel_scale()) return false;
  if (target->type < 0 || target->type > 65535 || !catalog_.object(static_cast<std::uint16_t>(target->type))) {
    target_.reset();
    return false;
  }
  target_ = target;
  render(*target);
  ++rebuilds_;
  return true;
}
void PlayerTooltip::render(const PlayerTooltipTarget& target) {
  if (image_.pixel_scale != fonts_.pixel_scale()) image_ = ui_image(width, maximum_height, fonts_.pixel_scale());
  std::fill(image_.pixels.begin(), image_.pixels.end(), 0);
  const auto* definition = catalog_.object(static_cast<std::uint16_t>(target.type));
  std::string suffix = target.name.empty() ? ""
                                           : " (" + definition->display_id +
                                                 (target.level > 0 ? " " + std::to_string(target.level) : "") + ")";
  const std::array spans{
      TextSpan{target.name.empty() ? std::string_view(definition->display_id) : std::string_view(target.name), true},
      TextSpan{suffix, false}};
  // Mixed HTML uses the regular face's baseline; the 13px bold run lands
  // half a pixel lower than a stand-alone bold TextField.
  const auto header =
      fonts_.rasterize_spans(spans, 13, true, target.fellow_guild ? 10944349U : 16572160U, suffix.empty() ? 0 : .5);
  double minimum_x = -4, maximum_x = std::max(182.0, 32 + header.width);
  const double before_width = std::max(target.guild.empty() ? 184.0 : 186.0, 36 + header.width);
  const int hp_y = target.guild.empty() ? 64 : 94, equipment_y = hp_y + 48, hint_y = equipment_y + 52;
  for (std::size_t i = 0; i < target.equipment.size(); ++i) {
    const auto key = "item" + std::to_string(target.equipment[i].type);
    if (art_.contains(key)) {
      const auto& item = art_.image(key);
      const double center = 28 + 44 * static_cast<int>(i);
      minimum_x = std::min(minimum_x, center - item.width / 2.0);
      maximum_x = std::max(maximum_x, center + item.width / 2.0);
    }
  }
  const int content_width = std::min(width - padding * 2, static_cast<int>(maximum_x - minimum_x)),
            content_height = hint_y + 22;
  draw_width_ = content_width + padding * 2;
  height_ = content_height + padding * 2;
  // Stretch only the uniform interior of the original cut-edge background;
  // preserve its outline and antialiased corner pixels without resampling.
  const auto& frame = art_.image("playerframe");
  const int fw = content_width + 44, fh = content_height + 44;
  Image shell{fw, fh, std::vector<unsigned char>(static_cast<std::size_t>(fw) * static_cast<std::size_t>(fh) * 4)};
  for (int y = 0; y < fh; ++y)
    for (int x = 0; x < fw; ++x) {
      const int sx = x < 24         ? x
                     : x >= fw - 24 ? 80 - (fw - x)
                                    : 40,
                sy = y < 24         ? y
                     : y >= fh - 24 ? 80 - (fh - y)
                                    : 40;
      std::copy_n(frame.pixels.begin() + static_cast<std::ptrdiff_t>(sy * 80 + sx) * 4, 4,
                  shell.pixels.begin() + static_cast<std::ptrdiff_t>(y * fw + x) * 4);
    }
  blit(shell, -22, -22, true);
  const auto portrait = "portrait" + std::to_string(catalog_.portrait_type(target.type, target.skin));
  if (art_.contains(portrait)) blit(art_.portrait(target.type, target.skin, target.tex1, target.tex2), -4, -4, true);
  if (image_.pixel_scale != 1)
    ui_blit_text(image_, header, padding + 32 + header.offset.x, padding + 6 + header.offset.y, false, true);
  else
    blit(header.image, 32 + static_cast<int>(header.offset.x), 6 + static_cast<int>(header.offset.y));
  // The vector star is captured against this same translucent interior.
  // Copying avoids a second alpha quantization of its antialiased edges.
  const auto rank_number = std::to_string(std::clamp(target.stars, -1, 99));
  if (image_.pixel_scale != 1) {
    text("Rank: ", 16, false, 11776947, 6, 34);
    if (target.stars >= 0) {
      const auto number = fonts_.layout(rank_number, 16), prefix = fonts_.layout("Rank: ", 16);
      const auto icon = rank_star_bitmap(fonts_, art_, target.stars);
      const double x = 6 + prefix.width, y = std::floor(number.height / 2 - icon.height / 2) + 1;
      ui_rounded_rectangle(image_, padding + x - 2, padding + 34 + y - 3, number.width + icon.width + 8,
                           icon.height + 8, 6, 0, .4, true);
      text(rank_number, 16, false, 11776947, x, 34);
      blit(icon, static_cast<int>(x + number.width + 2), 34 + static_cast<int>(y), true);
    }
  } else {
    blit(art_.image("playerrank" + rank_number), 6, 34, true);
    const auto& rank = art_.image("playerbadge" + rank_number);
    for (int y = 0; y < rank.height; ++y)
      std::copy_n(rank.pixels.begin() + (static_cast<std::ptrdiff_t>(y) * rank.width + 41) * 4,
                  static_cast<std::size_t>(rank.width - 41) * 4,
                  image_.pixels.begin() + (static_cast<std::ptrdiff_t>(y + padding + 34) * width + padding + 47) * 4);
  }
  if (!target.guild.empty()) {
    const auto guild = "playerguild" + std::to_string(target.guild_rank);
    if (art_.contains(guild)) blit(art_.image(guild), -2, 54, true);
    text(target.guild, 16, false, 0xffffff, 30, 62, 1, 152, 23);
  }
  for (std::size_t i = 0; i < target.bars.size(); ++i) {
    const auto& bar = target.bars[i];
    const int y = hp_y + 24 * static_cast<int>(i);
    const int value = bar.maximum > 0 ? std::clamp(bar.value, 0, bar.maximum) : bar.value;
    const auto color = bar.cap > 0 && static_cast<std::int64_t>(bar.maximum) - bar.boost == bar.cap ? 16572160U
                       : bar.boost > 0                                                              ? 6206769U
                                                                                                    : 0xffffffU;
    rectangle(6, y, 176, 16, 5526612);
    rectangle(6, y, bar.maximum > 0 ? 176.0 * value / bar.maximum : 176, 16, bar.color);
    text(bar.label, 14, true, 0xffffff, 6, y - 3);
    const auto amount = std::to_string(value) + (bar.maximum > 0 ? "/" + std::to_string(bar.maximum) : "");
    const auto boost = bar.boost ? " (" + std::string(bar.boost > 0 ? "+" : "") + std::to_string(bar.boost) + ")" : "";
    const auto amount_width = fonts_.layout(amount, 14, true).width,
               boost_width = boost.empty() ? 0 : fonts_.layout(boost, 14, true).width;
    const double x = 94 - (amount_width + boost_width) / 2;
    text(amount, 14, true, color, x, y - 3);
    if (!boost.empty()) text(boost, 14, true, color, x + amount_width, y - 3, .6);
  }
  for (std::size_t i = 0; i < target.equipment.size(); ++i) {
    const auto item = target.equipment[i];
    const int x = 8 + 44 * static_cast<int>(i);
    const auto* def =
        item.type > 0 && item.type <= 65535 ? catalog_.object(static_cast<std::uint16_t>(item.type)) : nullptr;
    const std::string background = !def && target.slot_types[i] > 0
                                       ? "empty" + std::to_string(target.slot_types[i]) + "p" + std::to_string(i)
                                       : "equipment" + std::to_string(i);
    if (art_.contains(background)) blit(art_.image(background), x, equipment_y, true);
    if (!def) continue;
    const int mana = def->usable ? (def->xml.has("MultiPhase") ? def->xml.integer("MpEndCost") : def->mp_cost) : 0;
    const auto icon = (target.bars[1].value < mana ? "dim" : "item") + std::to_string(item.type);
    if (art_.contains(icon)) {
      const auto& bitmap = art_.image(icon);
      blit(bitmap, x + 20 - bitmap.width / 2, equipment_y + 20 - bitmap.height / 2, true);
    }
    if (image_.pixel_scale != 1) {
      if (const auto tier = tier_bitmap(fonts_, *def))
        ui_blit_text(image_, *tier, padding + x + 40 - tier->width + tier->offset.x,
                     padding + equipment_y + 24 + tier->offset.y, false, true);
    } else {
      const auto tier = "tier" + std::to_string(item.type);
      if (art_.contains(tier)) blit(art_.image(tier), x, equipment_y, true);
    }
  }
  const double hint_x = before_width / 2 - 107 / 2.0;
  // Two finite phases preserve the original half-pixel hint placement.
  text("(Click to open menu)", 12, false, 11776947U, hint_x, hint_y);
  shadow();
}
Vec2 PlayerTooltip::position(Vec2 pointer, int window_width, int window_height, int tooltip_width, int tooltip_height,
                             double scale) {
  const double visible_width = (tooltip_width - padding * 2 + 12) * scale,
               visible_height = (tooltip_height - padding * 2 + 12) * scale;
  const double x = pointer.x < window_width / 2.0 ? pointer.x + 12 : pointer.x - visible_width - 1;
  const double y = pointer.y < window_height / 3.0 ? pointer.y + 12 : pointer.y - visible_height - 1;
  return {std::max(12.0, x) - padding * scale, std::max(12.0, y) - padding * scale};
}
}
