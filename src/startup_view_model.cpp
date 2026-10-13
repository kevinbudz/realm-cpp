#include "startup_view_model.hpp"
#include "realm/credit_display.hpp"
#include "realm/flash_draw.hpp"
#include "realm/text_tooltip.hpp"
#include "realm/ui_draw.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace realm::startup_detail {
namespace {
constexpr std::uint32_t white = 0xffffff, gray = 11776947, dark_line = 5526612;
constexpr std::array<float, 4> over_text{1, 220.0F / 255, 133.0F / 255, 1};
constexpr int frame_width = 288, register_width = 326;
constexpr double portrait_inset = 5, portrait_content = 34;
std::array<float, 4> rgba(std::uint32_t color, double alpha = 1) {
  return {static_cast<float>((color >> 16U) & 255U) / 255, static_cast<float>((color >> 8U) & 255U) / 255,
          static_cast<float>(color & 255U) / 255, static_cast<float>(alpha)};
}
Image straight(const Image& source) {
  auto image = source;
  for (std::size_t i = 0; i < image.pixels.size(); i += 4) {
    const unsigned alpha = image.pixels[i + 3];
    for (std::size_t c = 0; c < 3; ++c)
      image.pixels[i + c] =
          static_cast<unsigned char>(alpha == 255 ? image.pixels[i + c]
                                     : alpha ? std::min(255U, static_cast<unsigned>(image.pixels[i + c]) * 256 / alpha)
                                             : 0);
  }
  return image;
}
Image glow_gradient() {
  Image image{1, 256, std::vector<unsigned char>(256 * 4)};
  constexpr double ratios[4]{20, 103, 173, 237}, alphas[4]{0, .73, .73, 0};
  for (int i = 0; i < 256; ++i) {
    double alpha{};
    for (int k = 0; k < 3; ++k)
      if (i >= ratios[k] && i <= ratios[k + 1])
        alpha = alphas[k] + (alphas[k + 1] - alphas[k]) * (i - ratios[k]) / (ratios[k + 1] - ratios[k]);
    image.pixels[static_cast<std::size_t>(i) * 4 + 3] = static_cast<unsigned char>(std::lround(alpha * 255));
  }
  return image;
}
std::string ordinal(int number) {
  const int ones = number % 10, tens = number / 10 % 10;
  return std::to_string(number) + (tens == 1 ? "th" : ones == 1 ? "st" : ones == 2 ? "nd" : ones == 3 ? "rd" : "th");
}
std::string time_difference(std::int64_t time) {
  const auto now =
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  const auto dt = now - time;
  if (dt <= 0) return "now";
  if (dt < 60) return std::to_string(dt) + " secs";
  if (dt < 3600) return std::to_string(dt / 60) + " mins";
  if (dt < 86400) return std::to_string(dt / 3600) + " hours";
  return std::to_string(dt / 86400) + " days";
}
HudRegion united(HudRegion a, HudRegion b) {
  const int left = std::min(a.x, b.x), top = std::min(a.y, b.y);
  const int right = std::max(a.x + a.width, b.x + b.width), bottom = std::max(a.y + a.height, b.y + b.height);
  return {left, top, right - left, bottom - top};
}
}  // namespace

StartupViewModel::StartupViewModel(const FontBook& fonts, const HudArt* art, const Catalog& catalog,
                                   std::string build_version)
    : fonts_(fonts), art_(art), catalog_(catalog), build_version_(std::move(build_version)) {}

bool StartupViewModel::set_window(double width, double height) {
  const bool changed = width_ != width || height_ != height;
  width_ = width;
  height_ = height;
  return changed;
}

bool StartupViewModel::set_pixel_scale(double pixel_scale) {
  if (sheet_.pixel_scale == pixel_scale) return false;
  sheet_ = ui_image(Startup::sheet_size, Startup::sheet_size, pixel_scale);
  sheet_used_ = 0;
  sheet_replaced_ = true;
  return true;
}

void StartupViewModel::sync_field(std::size_t index, const StartupScreenState& state) {
  if (index >= field_views_.size() || index >= state.fields_.size()) return;
  field_views_[index].value = state.fields_[index].value;
  field_views_[index].error = state.fields_[index].error;
}

void StartupViewModel::reset_character_tooltip() { character_tooltip_id_ = -1; }

double StartupViewModel::death_fade(const StartupScreenState& state, std::int32_t time) const {
  return state.screen_ == Screen::fame && state.fresh_fame_
             ? std::clamp(1.0 - (static_cast<double>(time) - state.death_at_) / 2000, 0.0, 1.0)
             : 0;
}

void StartupViewModel::acknowledge_sheet_upload(std::uint64_t revision) {
  if (revision == sheet_revision_) view_uploads_.clear();
}

void StartupViewModel::acknowledge_skin_upload(std::uint64_t revision) {
  if (revision == skin_revision_) pending_skin_change_ = {};
}

void StartupViewModel::skin_changed(HudRegion region) {
  pending_skin_change_ = pending_skin_change_.width == 0 || pending_skin_change_.height == 0
                             ? region
                             : united(pending_skin_change_, region);
  ++skin_revision_;
  view_pending_ = true;
}

std::optional<StartupAction> StartupViewModel::typed_action(const StartupScreenState& state, int action) const {
  using Kind = StartupAction::Kind;
  if (state.list_ && action >= delete_character_action &&
      action < delete_character_action + static_cast<int>(state.list_->characters.size()))
    return StartupAction{Kind::delete_character, static_cast<std::size_t>(action - delete_character_action)};
  if (action >= legend_tab_action && action < legend_tab_action + 3)
    return StartupAction{Kind::legend_tab, static_cast<std::size_t>(action - legend_tab_action)};
  if (action >= legend_row_action && action < legend_row_action + 256)
    return StartupAction{Kind::legend_row, static_cast<std::size_t>(action - legend_row_action)};
  if (action >= field_action && action < field_action + static_cast<int>(state.fields_.size()))
    return StartupAction{Kind::field, static_cast<std::size_t>(action - field_action)};
  if (action >= class_action && action < class_action + static_cast<int>(state.player_types_.size()))
    return StartupAction{Kind::class_type, static_cast<std::size_t>(action - class_action)};
  if (state.list_ && action >= character_action &&
      action < character_action + static_cast<int>(state.list_->characters.size()))
    return StartupAction{Kind::character, static_cast<std::size_t>(action - character_action)};
  switch (action) {
    case play_action:
      return StartupAction{Kind::play};
    case account_action:
      return StartupAction{Kind::account};
    case account_logout_action:
      return StartupAction{Kind::account_logout};
    case legends_action:
      return StartupAction{Kind::legends};
    case editor_action:
      return StartupAction{Kind::editor};
    case login_action:
      return StartupAction{Kind::login};
    case register_action:
      return StartupAction{Kind::register_account};
    case back_action:
      return StartupAction{Kind::back};
    case classes_action:
      return StartupAction{Kind::classes};
    case new_character_action:
      return StartupAction{Kind::new_character};
    case buy_slot_action:
      return StartupAction{Kind::buy_slot};
    case cancel_action:
      return StartupAction{Kind::cancel};
    case submit_action:
      return StartupAction{Kind::submit};
    case link_action:
      return StartupAction{Kind::link};
    default:
      return std::nullopt;
  }
}
void StartupViewModel::reset_sheet() {
  changed_regions_.clear();
  sheet_x_ = sheet_y_ = 1;
  sheet_row_ = 0;
}
HudRegion StartupViewModel::store(const Image& image) {
  if (image.width + 2 > sheet_.width || image.height + 2 > sheet_.height)
    throw std::runtime_error("Menu element exceeds sheet size");
  if (sheet_x_ + image.width + 1 > sheet_.width) {
    sheet_x_ = 1;
    sheet_y_ += sheet_row_ + 1;
    sheet_row_ = 0;
  }
  if (sheet_y_ + image.height + 1 > sheet_.height) throw std::runtime_error("Menu sheet is full");
  const HudRegion region{sheet_x_, sheet_y_, image.width, image.height};
  const auto row_bytes = static_cast<std::size_t>(image.width) * 4;
  int first_changed = -1, last_changed = -1;
  for (int y = 0; y < image.height; ++y) {
    const auto* source = image.pixels.data() + static_cast<std::size_t>(y) * row_bytes;
    auto* destination =
        sheet_.pixels.data() + ((static_cast<std::size_t>(region.y + y) * static_cast<std::size_t>(sheet_.width) +
                                 static_cast<std::size_t>(region.x)) *
                                4);
    if (std::memcmp(source, destination, row_bytes) != 0) {
      std::copy_n(source, row_bytes, destination);
      if (first_changed < 0) first_changed = y;
      last_changed = y;
    }
  }
  if (first_changed >= 0)
    changed_regions_.push_back({region.x, region.y + first_changed, region.width, last_changed - first_changed + 1});
  sheet_x_ += image.width + 1;
  sheet_row_ = std::max(sheet_row_, image.height);
  return region;
}
StartupViewModel::Element& StartupViewModel::add_solid(double x, double y, double w, double h, std::uint32_t color,
                                                       double alpha, int group) {
  Element e;
  e.solid = true;
  e.position = {x, y};
  e.size = {w, h};
  e.color = rgba(color, alpha);
  e.group = group;
  return elements_.emplace_back(e);
}
Vec2 StartupViewModel::add_text(std::string_view value, int size, bool bold, std::uint32_t color, double x, double y,
                                int group, bool shadow) {
  Element e;
  e.group = group;
  const auto field = fonts_.layout(value, size, bold);
  e.position = {x, y};
  e.size = {field.width, field.height};
  e.text = value;
  e.text_origin = {x, y};
  e.font_color = rgba(color);
  e.font_size = size;
  e.font_bold = bold;
  e.font_shadow = shadow;
  if (group >= 0) e.over = over_text;
  elements_.push_back(e);
  return {field.width, field.height};
}
Vec2 StartupViewModel::add_button(std::string_view label, int size, double x, double y, int action, bool pulse,
                                  bool center) {
  // TitleMenuOption: lower-case bold white text that tints on hover.
  std::string text(label);
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  const auto field = fonts_.layout(text, size, true);
  if (center) x = (800 - field.width) / 2 + x;
  const auto first = elements_.size();
  add_text(text, size, true, white, x, y, action);
  auto& hit_area = add_solid(x, y, field.width, field.height, 0, 0, action);
  hit_area.hit = true;
  if (pulse)
    for (auto i = first; i < elements_.size(); ++i) {
      elements_[i].pulse = true;
      elements_[i].pulse_center = {x + field.width / 2, y + field.height / 2};
    }
  return {field.width, field.height};
}
void StartupViewModel::add_image(const Image& image, double x, double y, double scale, int group) {
  Element e;
  if (sheet_.pixel_scale != 1) {
    auto pixels = ui_image(image.width * scale, image.height * scale, sheet_.pixel_scale);
    auto source = image;
    source.pixel_scale = 1 / scale;
    ui_blit(pixels, source, 0, 0);
    e.source = store(pixels);
  } else
    e.source = store(image);
  e.position = {x, y};
  e.size = {image.width * scale, image.height * scale};
  e.group = group;
  elements_.push_back(e);
}
void StartupViewModel::add_window_text(std::string_view value, int size, bool bold, std::uint32_t color, Vec2 anchor,
                                       Vec2 offset, Vec2 pixels, bool align_right, bool align_bottom, int action) {
  const auto field = fonts_.layout(value, size, bold);
  if (align_right) offset.x -= field.width;
  if (align_bottom) offset.y -= field.height;
  const auto first = elements_.size();
  add_text(value, size, bold, color, offset.x, offset.y, action);
  if (action >= 0) {
    auto& h = add_solid(offset.x, offset.y, field.width, field.height, 0, 0, action);
    h.hit = true;
  }
  for (auto i = first; i < elements_.size(); ++i) {
    elements_[i].anchor = Anchor::window;
    elements_[i].anchor_fraction = anchor;
    elements_[i].pixels = pixels;
  }
}
void StartupViewModel::menu_frame(bool background) {
  const auto window = [&](Element& e) {
    e.anchor = Anchor::window;
    e.full_width = true;
  };
  // MenuBackground dims the scrolling map; MenuFrame adds the glow band and the bar.
  if (background) window(add_solid(0, 0, 0, 600, 0x2B2B2B, .8));
  Element glow;
  glow.source = store(glow_gradient());
  glow.position = {0, 508};
  glow.size = {0, 80};
  window(glow);
  elements_.push_back(glow);
  window(add_solid(0, 524, 0, 52, 0x545454));
}
void StartupViewModel::loading_screen(std::string_view text) {
  const auto width = fonts_.layout(text, 30, true).width;
  add_text(text, 30, true, white, (800 - width) / 2, 526);
}
void StartupViewModel::fame_screen(StartupScreenState& state, bool busy) {
  menu_frame(true);
  const auto centered = [&](std::string_view text, int size, double y) {
    // Long names/killers remain on one familiar centered line, reduced only
    // when needed to fit the viewport's design content.
    while (size > 12 && fonts_.layout(text, size, true).width > 780) --size;
    add_text(text, size, true, 0xcccccc, (800 - fonts_.layout(text, size, true).width) / 2, y);
  };
  if (!state.fame_) {
    centered(state.fame_error_.empty() ? "Loading fame..." : "Unable to load fame", 30, 225);
    if (!state.fame_error_.empty()) centered(state.fame_error_, 18, 272);
    if (busy || state.retry_at_ >= 0)
      centered("Loading...", 20, 316);
    else
      add_button("retry", 28, 0, 360, play_action, false, true);
    if (death_fade(state, state.time_) == 0) add_button("continue", 36, 0, 520, back_action, false, true);
    return;
  }
  const auto& data = *state.fame_;
  const auto& character = data.character;
  const auto* definition = catalog_.object(static_cast<std::uint16_t>(character.object_type));
  centered(data.name + ", Level " + std::to_string(character.level) + " " +
               (definition ? definition->display_id : "Unknown"),
           38, 225);
  centered(
      data.killed_by.empty() ? "died " + data.death_date : "killed on " + data.death_date + " by " + data.killed_by, 24,
      272);
  if (art_ && definition) {
    const auto background = straight(art_->image("famebackground"));
    if (state.fame_portrait_.pixels.empty())
      state.fame_portrait_ = straight(art_->fame_portrait(character.object_type, character.skin,
                                                          static_cast<std::uint32_t>(character.tex1),
                                                          static_cast<std::uint32_t>(character.tex2)));
    const auto& icon = state.fame_portrait_;
    add_image(background, (800 - background.width) / 2.0, 20);
    add_image(icon, (800 - icon.width) / 2.0, 20 + (background.height - icon.height) / 2.0);
  }
  auto panel = ui_image(784, 150, fonts_.pixel_scale());
  const auto paint_text = [&](Image& image, std::string_view text, int size, std::uint32_t color, double x, double y) {
    const auto bitmap = fonts_.rasterize(text, size, true, true, color);
    ui_blit(image, bitmap.image, x + bitmap.offset.x, y + bitmap.offset.y);
  };
  Image fame_icon;
  if (art_) {
    const auto icon = straight(art_->image("skinfame"));
    fame_icon = {icon.width - 12, icon.height - 12,
                 std::vector<unsigned char>(static_cast<std::size_t>(icon.width - 12) *
                                            static_cast<std::size_t>(icon.height - 12) * 4)};
    ui_blit(fame_icon, icon, -6, -6);
  }
  const auto paint_line = [&](Image& image, const FameLine& line, int size, double y, double progress) {
    paint_text(image, line.label, size, size == 24 ? 0xcccccc : gray, 410 - fonts_.measure(line.label, size, true), y);
    if (line.value == -1) return;
    const auto number =
        (line.bonus ? "+" : "") + std::to_string(static_cast<int>(line.value * progress)) + " " + line.unit;
    paint_text(image, number, size, line.fame ? 0xffc800 : 5746018, 450, y);
    if (line.fame && !fame_icon.pixels.empty())
      ui_blit(image, fame_icon, 450 + fonts_.layout(number, size, true).width - 4,
              y + fonts_.layout(number, size, true).height / 2 - fame_icon.height / 2.0 + 2);
  };
  const double elapsed = state.score_at_ < 0 ? -1 : static_cast<double>(state.time_) - state.score_at_;
  const auto count = state.fame_skip_ ? data.lines.size()
                     : elapsed < 0    ? std::size_t{0}
                                      : std::min(data.lines.size(), static_cast<std::size_t>(elapsed / 500) + 1);
  const bool done = state.fame_skip_ || elapsed > static_cast<double>(data.lines.size() - 1) * 500 + 600;
  const double content_height = static_cast<double>(data.lines.size()) * 28;
  const double top =
      done ? 5 + state.fame_scroll_.pos() * (150 - content_height - 15) : 150 - static_cast<double>(count) * 28 - 10;
  for (std::size_t i = 0; i < count; ++i) {
    const double y = top + static_cast<double>(i) * 28;
    if (y < -35 || y > 150) continue;
    paint_line(panel, data.lines[i], 20, y,
               done ? 1 : std::clamp((elapsed - static_cast<double>(i) * 500) / 500, 0.0, 1.0));
  }
  // The original mask clips score content; the scrollbar is drawn after it.
  ui_rectangle(panel, 1, 1, done ? 758 : 782, 1, 0x494949);
  ui_rectangle(panel, 1, 148, done ? 758 : 782, 1, 0x494949);
  ui_rectangle(panel, 1, 1, 1, 148, 0x494949);
  ui_rectangle(panel, done ? 758 : 782, 1, 1, 148, 0x494949);
  if (done) state.fame_scroll_.draw(panel, {768, 0});
  add_image(panel, 8, 316, 1 / panel.pixel_scale);
  auto total = ui_image(784, 42, fonts_.pixel_scale());
  paint_line(total, {"Total Fame Earned", data.total_fame, true, false, {}}, 24, 0, 1);
  add_image(total, 10, 470, 1 / total.pixel_scale);
  if (death_fade(state, state.time_) == 0) add_button("continue", 36, 0, 520, back_action, false, true);
}
void StartupViewModel::legends_screen(StartupScreenState& state, bool busy) {
  menu_frame(true);
  add_text("Legends", 32, true, gray, (800 - fonts_.layout("Legends", 32, true).width) / 2, 24);
  auto& divider = add_solid(0, 99, 0, 2, dark_line);
  divider.anchor = Anchor::window;
  divider.full_width = true;
  constexpr const char* labels[]{"Week", "Month", "All Time"};
  for (std::size_t i = 0; i < 3; ++i) {
    const int action = legend_tab_action + static_cast<int>(i);
    const auto width = fonts_.layout(labels[i], 20, true).width;
    add_text(labels[i], 20, true, i == state.legends_span_ || state.pressed_ == action ? white : 11711154,
             22 + static_cast<double>(i) * 90, 70, action);
    elements_.back().over = rgba(16567065);
    auto& area = add_solid(20 + static_cast<double>(i) * 90, 70, width + 4, fonts_.layout(labels[i], 20, true).height,
                           0, 0, action);
    area.hit = true;
  }
  add_button("done", 36, 0, 524, back_action, false, true);
  const auto message = [&](std::string_view value, int size, double y) {
    while (size > 12 && fonts_.layout(value, size, true).width > 744) --size;
    add_text(value, size, true, gray, (800 - fonts_.layout(value, size, true).width) / 2, y);
  };
  const auto& rows = state.legends_lists_[state.legends_span_];
  if (!rows) {
    message(state.legends_error_.empty() ? "Loading..." : "Unable to load legends", 22, 286);
    if (!state.legends_error_.empty()) {
      message(state.legends_error_.substr(0, 256), 16, 326);
      if (busy || state.retry_at_ >= 0)
        message("Loading...", 18, 365);
      else
        add_button("retry", 28, 0, 365, play_action, false, true);
    }
    return;
  }
  if (rows->empty()) {
    message("No legends yet", 22, 286);
    return;
  }
  // Compose only the clipped viewport, never an unbounded bitmap of all rows.
  auto panel = ui_image(756, 430, fonts_.pixel_scale());
  const auto paint = [&](std::string_view text, int size, std::uint32_t color, double x, double y) {
    const auto bitmap = fonts_.rasterize(text, size, true, true, color, x - std::floor(x));
    ui_blit(panel, bitmap.image, std::floor(x) + bitmap.offset.x, y + bitmap.offset.y);
  };
  const double offset = state.legends_scroll_.pos() * std::max(0.0, static_cast<double>(rows->size()) * 56 - 400);
  for (std::size_t i = 0; i < rows->size(); ++i) {
    const double y = static_cast<double>(i) * 56 - offset;
    if (y + 56 <= 0 || y >= 430) continue;
    const auto& row = (*rows)[i];
    const int action = legend_row_action + static_cast<int>(i);
    const auto color = state.list_ && row.account_id == state.list_->account_id ? 16564761U
                       : i == 0                                                 ? 16646031U
                                                                                : white;
    if (state.hovered_ == action) ui_path(panel, cut_edge_rect(0, y, 756, 56, 0), 0, {}, 1, .4);
    const auto place = std::to_string(i + 1) + ".";
    const auto fame = std::to_string(row.total_fame);
    const auto line_y = y + 28 - fonts_.layout(place, 22, true).height / 2;
    paint(place, 22, color, 82 - fonts_.layout(place, 22, true).width, line_y);
    int name_size = 22;
    while (name_size > 12 && fonts_.layout(row.name, name_size, true).width > 224) --name_size;
    // Clip long hostile names within their column without covering equipment.
    auto name_image = ui_image(224, 56, fonts_.pixel_scale());
    const auto name = fonts_.rasterize(row.name, name_size, true, true, color);
    ui_blit(name_image, name.image, name.offset.x,
            28 - fonts_.layout(row.name, name_size, true).height / 2 + name.offset.y);
    ui_blit(panel, name_image, 170, y);
    paint(fame, 22, color, 660 - fonts_.layout(fame, 22, true).width, line_y);
    if (art_) {
      const auto* definition = catalog_.object(static_cast<std::uint16_t>(row.character.object_type));
      if (definition && definition->player) {
        auto& icon = state.legends_portraits_[state.legends_span_][i];
        if (icon.pixels.empty())
          icon = straight(art_->fame_portrait(row.character.object_type, row.character.skin,
                                              static_cast<std::uint32_t>(row.character.tex1),
                                              static_cast<std::uint32_t>(row.character.tex2), 100));
        ui_blit(panel, icon, 104, y + 28 - icon.height / 2.0 - 2);
      }
      ui_blit(panel, straight(art_->image("skinfame")), 652, y + 28 - art_->image("skinfame").height / 2.0);
      for (std::size_t slot = 0; slot < 4; ++slot) {
        const double x = 400 + static_cast<double>(slot) * 44;
        const int item = row.equipment[slot];
        const int type = definition && slot < definition->slot_types.size() ? definition->slot_types[slot] : 0;
        const auto key = item <= 0 && type > 0 ? "empty" + std::to_string(type) + "p" + std::to_string(slot)
                                               : "equipment" + std::to_string(slot);
        ui_blit(panel, straight(art_->image(art_->contains(key) ? key : "equipment" + std::to_string(slot))), x, y + 8);
        if (item > 0 && art_->contains("item" + std::to_string(item))) {
          const auto icon = straight(art_->image("item" + std::to_string(item)));
          ui_blit(panel, icon, x + 20 - icon.width / 2.0, y + 28 - icon.height / 2.0);
          const auto* item_def = catalog_.object(static_cast<std::uint16_t>(item));
          if (item_def && !item_def->xml.has("NoTierTag") && !item_def->xml.has("Consumable") &&
              !item_def->xml.has("Treasure") && !item_def->xml.has("PetFood")) {
            const bool set = !item_def->xml.attribute("setType").empty();
            const auto tag = item_def->tier >= 0 ? "T" + std::to_string(item_def->tier) : set ? "ST" : "UT";
            paint(tag, 12,
                  item_def->tier >= 0 ? white
                  : set               ? 0xff9900
                                      : 0x8a2be2,
                  x + 40 - fonts_.layout(tag, 12, true).width, y + 32);
          }
        }
      }
    }
    auto& area = add_solid(10, 110 + std::max(y, 0.0), 756, std::min(y + 56, 430.0) - std::max(y, 0.0), 0, 0, action);
    area.hit = true;
  }
  add_image(panel, 10, 110, 1 / panel.pixel_scale);
  if (rows->size() * 56 > 400) {
    auto scroll = ui_image(16, 400, fonts_.pixel_scale());
    state.legends_scroll_.draw(scroll, {0, 0});
    add_image(scroll, 780, 104, 1 / scroll.pixel_scale);
  }
}
void StartupViewModel::account_header(const StartupScreenState& state) {
  // AccountScreen anchors WebAccountInfoView 10 pixels from the right edge,
  // laid out right to left by addAndAlignHorizontally.
  struct Part {
    std::string text;
    int action;
  };
  std::vector<Part> parts;
  if (state.account_.registered())
    parts = {{"logged in as " + state.account_.username + " - ", -1}, {"log out", login_action}};
  else
    parts = {{"guest account - ", -1}, {"register", register_action}, {" - ", -1}, {"log in", login_action}};
  double x{};
  for (auto part = parts.rbegin(); part != parts.rend(); ++part) {
    x -= fonts_.layout(part->text, 18, part->action >= 0).width;
    const auto first = elements_.size();
    add_text(part->text, 18, part->action >= 0, part->action >= 0 ? white : gray, x, 2, part->action);
    if (part->action >= 0) {
      auto& h = add_solid(x, 2, fonts_.layout(part->text, 18, true).width, fonts_.layout(part->text, 18, true).height,
                          0, 0, part->action);
      h.hit = true;
    }
    for (auto i = first; i < elements_.size(); ++i) {
      elements_[i].anchor = Anchor::window;
      elements_[i].anchor_fraction = {1, 0};
      elements_[i].pixels = {-10, 0};
    }
  }
}
void StartupViewModel::title_screen(const StartupScreenState& state) {
  menu_frame(true);
  add_image(logo_, (800 - logo_.width) / 2.0, 83);
  add_button("PLAY", 36, 0, 520, play_action, true, true);
  add_button("ACCOUNT", 22, -94, 532, account_action, false, true);
  add_button("LEGENDS", 22, 96, 532, legends_action, false, true);
  add_button("EDITOR", 22, 8, 532, editor_action);
  add_window_text("RotMG " + build_version_, 12, false, 8355711, {0, 1}, {0, 0}, {0, 0}, false, true);
  add_window_text("\xC2\xA9 2010, 2011 by Wild Shadow Studios, Inc.", 12, false, 8355711, {1, 1}, {0, 0}, {0, 0}, true,
                  true);
  account_header(state);
}
void StartupViewModel::character_screen(StartupScreenState& state) {
  menu_frame(true);
  if (state.list_ && art_) {
    const auto display = render_credit_display(fonts_, *art_, state.list_->credits, state.list_->fame);
    add_image(display.image, display.origin_offset.x, 20, 1 / display.image.pixel_scale);
    elements_.back().anchor = Anchor::window;
    elements_.back().anchor_fraction = {1, 0};
  }
  auto& divider = add_solid(0, 104, 0, 2, dark_line);
  divider.anchor = Anchor::window;
  divider.full_width = true;
  const std::string name = state.list_ && !state.list_->name.empty() ? state.list_->name : "Undefined";
  add_text(name, 22, true, gray, (800 - fonts_.layout(name, 22, true).width) / 2, 24);
  add_text("Characters", 18, true, gray, 10, 79);
  add_text("News", 18, true, gray, 410, 79);
  add_solid(399, 107, 2, 419, dark_line);
  if (state.list_) {
    // NewsList: NewsLine rows of 52 pixels, 4 apart.
    for (std::size_t i = 0; i < state.list_->news.size(); ++i) {
      const auto& item = state.list_->news[i];
      const double y = 112 + 4 + static_cast<double>(i) * 56;
      add_text(item.title, 18, false, gray, 400 + 61, y);
      add_text(item.tagline, 14, false, gray, 400 + 61, y + 24);
      const auto when = time_difference(item.date);
      add_text(when, 16, false, gray, 400 + 388 - fonts_.layout(when, 16, false).width, y);
    }
    // CharacterList masks the full 430-pixel viewport, including partial
    // first/last rows while its 399-pixel scrollbar moves the 400-pixel range.
    auto panel = ui_image(356, 430, fonts_.pixel_scale());
    const auto text = [&](std::string_view value, int size, bool bold, std::uint32_t color, double x, double y) {
      const auto bitmap = fonts_.rasterize(value, size, bold, true, color);
      ui_blit_text(panel, bitmap, x + bitmap.offset.x, y + bitmap.offset.y, false, true);
    };
    double y = 4;
    const auto rect = [&](std::uint32_t color, std::uint32_t over, int action, int remove = -1) {
      const double top = y - state.scroll_;
      y += 63;
      if (top + 59 <= 0 || top >= 430) return -1000.0;
      ui_rectangle(panel, 0, top, 356, 59,
                   state.hovered_ == action || (remove >= 0 && state.hovered_ == remove) ? over : color);
      const double first = std::max(0.0, top), last = std::min(430.0, top + 59);
      auto& hit_area = add_solid(10, 112 + first, 356, last - first, 0, 0, action);
      hit_area.hit = true;
      return top;
    };
    const auto portrait = [&](int type, double top, int skin = 0, int tex1 = 0, int tex2 = 0) {
      if (art_) {
        auto icon = art_->portrait(type, skin, static_cast<std::uint32_t>(tex1), static_cast<std::uint32_t>(tex2));
        icon.pixel_scale /= 1.25;
        ui_blit(panel, icon, 4 - portrait_inset * 1.25,
                top + (59 - portrait_content * 1.25) / 2 - portrait_inset * 1.25, true, true);
      }
    };
    const auto quest = [&](std::string_view label, double top) {
      double icon_width = 12;
      if (art_) {
        const auto& star = art_->image("classqueststar");
        icon_width = star.width - 16;
        ui_blit(panel, star, 50, top + 18, true, true);
      }
      text(label, 14, false, gray, 58 + icon_width + 2, top + 24);
    };
    for (std::size_t i = 0; i < state.list_->characters.size(); ++i) {
      const auto& c = state.list_->characters[i];
      const int action = character_action + static_cast<int>(i), remove = delete_character_action + static_cast<int>(i);
      const double top = rect(6052956, 8355711, action, remove);
      if (top < -900) continue;
      portrait(c.object_type, top, c.skin, c.tex1, c.tex2);
      const auto* def = c.object_type > 0 && c.object_type <= 65535
                            ? catalog_.object(static_cast<std::uint16_t>(c.object_type))
                            : nullptr;
      text((def ? def->id : "Unknown") + " " + std::to_string(c.level), 18, true, white, 58, top);
      if (art_) {
        const auto& cross = art_->image("guildicon6");
        const double cy = top + (59 - cross.height) / 2;
        ui_blit(panel, cross, 356 - 40, cy, true, true);
        const double first = std::max(0.0, cy), last = std::min(430.0, cy + cross.height);
        if (last > first) {
          auto& area = add_solid(10 + 356 - 40, 112 + first, cross.width, last - first, 0, 0, remove);
          area.hit = true;
        }
      }
      const auto best = state.list_->best_fame.find(c.object_type);
      if (const int next = next_star_fame(best == state.list_->best_fame.end() ? 0 : best->second, c.fame); next > 0)
        quest("Class Quest: " + std::to_string(c.fame) + " of " + std::to_string(next) + " Fame", top);
    }
    for (int i = 0; i < state.list_->available_slots(); ++i) {
      const double top = rect(5526612, 7829367, new_character_action);
      if (top < -900) continue;
      portrait(state.new_character_type_, top);
      text("New Character", 18, true, white, 58, top);
      const int remaining = static_cast<int>(state.player_types_.size()) * 5 - state.list_->stars();
      if (remaining > 0) quest(std::to_string(remaining) + " Class quests not yet completed", top);
    }
    if (const double top = rect(2039583, 4342338, buy_slot_action); top > -900) {
      auto plus = ui_image(40, 40, fonts_.pixel_scale());
      flash_draw::Path circle;
      circle.circle(20 * plus.pixel_scale, 20 * plus.pixel_scale, 19 * plus.pixel_scale);
      flash_draw::fill(plus, circle, 3880246);
      flash_draw::stroke(plus, circle, plus.pixel_scale, 4603457);
      ui_rectangle(plus, 12, 18, 16, 4, 2039583);
      ui_rectangle(plus, 18, 12, 4, 16, 2039583);
      ui_blit(panel, plus, 6, top + 6, true, true);
      if (art_) ui_blit(panel, art_->image("skinfame"), 356 - 43, top + 18, true, true);
      text("Buy " + ordinal(state.list_->max_num_chars + 1) + " Character Slot", 18, true, white, 58, top + 2);
      text("2000", 18, false, white, 356 - 43 - fonts_.layout("2000", 18, false).width, top + 15);
    }
    list_height_ = y - 4;
    add_image(straight(panel), 10, 112, 1 / panel.pixel_scale);
    if (list_height_ > 400) {
      state.character_scroll_.indicator(399, list_height_, false);
      auto scrollbar = ui_image(16, 399, fonts_.pixel_scale());
      state.character_scroll_.draw(scrollbar, {});
      add_image(straight(scrollbar), 375, 113, 1 / scrollbar.pixel_scale);
    }
  }
  add_button("play", 36, 0, 520, play_action, true, true);
  add_button("back", 22, -94, 532, back_action, false, true);
  add_button("classes", 22, 96, 532, classes_action, false, true);
  account_header(state);
  if (state.dialog_ == Dialog::none && state.list_ && art_) {
    const int index = state.hovered_ >= delete_character_action ? state.hovered_ - delete_character_action
                                                                : state.hovered_ - character_action;
    if (index >= 0 && static_cast<std::size_t>(index) < state.list_->characters.size()) {
      const auto& c = state.list_->characters[static_cast<std::size_t>(index)];
      if (character_tooltip_id_ != c.id || character_tooltip_density_ != fonts_.pixel_scale()) {
        const auto best = state.list_->best_fame.find(c.object_type),
                   level = state.list_->best_level.find(c.object_type);
        character_tooltip_ = render_saved_character_tooltip(fonts_, *art_, catalog_, state.list_->name, c,
                                                            best == state.list_->best_fame.end() ? 0 : best->second,
                                                            level == state.list_->best_level.end() ? 0 : level->second);
        character_tooltip_id_ = c.id;
        character_tooltip_density_ = fonts_.pixel_scale();
      }
      character_tooltip_element_ = elements_.size();
      add_image(character_tooltip_.image, 0, 0, 1 / character_tooltip_.image.pixel_scale);
      elements_.back().anchor = Anchor::window;
      position_character_tooltip(state);
    }
  }
}
void StartupViewModel::position_character_tooltip(const StartupScreenState& state) {
  if (!character_tooltip_element_ || *character_tooltip_element_ >= elements_.size()) return;
  const double scale = height_ / 600;
  const auto at = text_tooltip_position(state.character_pointer_, character_tooltip_.frame_width,
                                        character_tooltip_.frame_height, width_, height_, scale);
  elements_[*character_tooltip_element_].position = {at.x / scale - 16, at.y / scale - 16};
}
void StartupViewModel::class_screen(const StartupScreenState& state) {
  menu_frame(true);
  add_button("back", 36, 0, 524, back_action, false, true);
  // NewCharacterScreen: five CharacterBoxes per row, 140 pixels apart.
  constexpr double box = 100;
  for (std::size_t i = 0; i < state.player_types_.size(); ++i) {
    const int action = class_action + static_cast<int>(i);
    const double x = 50 + 140 * static_cast<double>(i % 5) + 70 - box / 2, y = 88 + 140 * static_cast<double>(i / 5);
    auto& border = add_solid(x, y, box, box, 0x7F7F7F, 1, action);
    border.over = rgba(0x999999);
    auto& fill = add_solid(x + 2, y + 2, box - 4, box - 4, 0x363636, 1, action);
    fill.over = rgba(0x414141);
    if (art_) {
      // SavedCharacter.getImage: a 6x redrawn sprite centered at the top of the box.
      add_image(straight(art_->image("portrait" + std::to_string(state.player_types_[i]))),
                x + (box - portrait_content * 1.5) / 2 - portrait_inset * 1.5, y + 10 - portrait_inset * 1.5, 1.5,
                action);
      elements_.back().over = {1.2F, 1.2F, 1.2F, 1};
    }
    const auto* def = catalog_.object(static_cast<std::uint16_t>(state.player_types_[i]));
    const std::string name = def ? def->id : "Unknown";
    add_text(name, 14, true, white, x + (box - fonts_.layout(name, 14, true).width) / 2, y + 74, action);
    elements_.back().over = {1.2F, 1.2F, 1.2F, 1};
    auto& h = add_solid(x, y, box, box, 0, 0, action);
    h.hit = true;
  }
  account_header(state);
}
void StartupViewModel::skin_screen(const StartupScreenState& state) {
  menu_frame(true);
  add_solid(0, 104, 800, 2, dark_line).full_width = true;
  add_solid(345, 105, 2, 421, dark_line);
  const auto first = elements_.size();
  const bool available = state.list_ && state.list_->available_slots() > 0;
  add_button("play", 36, 0, 520, available ? play_action : -1, false, true);
  if (!available)
    for (auto i = first; i < elements_.size(); ++i) {
      elements_[i].color = rgba(0x363636);
      elements_[i].over = {};
    }
  add_button("back", 22, 30, 534, back_action);
  account_header(state);
}
void StartupViewModel::dialog_frame(StartupScreenState& state, bool busy) {
  if (state.delete_dialog_.visible()) {
    auto& dim = add_solid(0, 0, 0, 600, 0x151515, .6);
    dim.anchor = Anchor::window;
    dim.full_width = true;
    state.delete_dialog_.update(fonts_, width_, height_);
    const double scale = height_ / 600;
    const auto pos = state.delete_dialog_.canvas_position();
    const double ox = (width_ - 800 * scale) / 2;
    add_image(state.delete_dialog_.image(), (pos.x - ox) / scale, pos.y / scale, 1 / scale);
    for (int i = 0; i < 2; ++i) {
      const auto b = state.delete_dialog_.button(i);
      if (b.width <= 0) continue;
      const auto base = state.delete_dialog_.position();
      auto& hit_area = add_solid((base.x - ox) / scale + b.x, base.y / scale + b.y, b.width, b.height, 0, 0,
                                 state.dialog_ == Dialog::delete_confirm && i == 0 ? cancel_action : submit_action);
      hit_area.hit = true;
    }
    return;
  }
  auto& dim = add_solid(0, 0, 0, 600, 0x151515, .6);
  dim.anchor = Anchor::window;
  dim.full_width = true;
  const bool message = state.dialog_ == Dialog::message;
  const bool details = state.dialog_ == Dialog::account_detail;
  const bool navigation = state.dialog_ == Dialog::login || state.dialog_ == Dialog::reg;
  const int w = state.dialog_ == Dialog::reg ? register_width : frame_width;
  std::vector<std::string> lines;
  if (message) lines = fonts_.wrap(state.dialog_message_, 12, false, w - 34);
  // Frame: each TextInputField is 88 tall from h-60, links and plain text 20 from h-66.
  double h = 100;
  std::vector<double> field_y, line_y;
  for (std::size_t i = 0; i < state.fields_.size(); ++i) {
    field_y.push_back(h - 60);
    h += 88;
  }
  if (details) h += 88;
  const double link_y = h - 66;
  if (navigation || details) h += 20;
  const double logout_y = h - 66;
  if (details) h += 20;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    line_y.push_back(h - 66);
    h += 20;
  }
  const double ox = (800 - (w - 6)) / 2.0, oy = (600 - h) / 2;
  // Frame.draw: cut corners, one-pixel centered outline and the outer drop shadow.
  auto frame = ui_image(w + 40, h + 40, fonts_.pixel_scale());
  ui_path(frame, cut_edge_rect(20, 20, w, h, 4), 3552822);
  ui_path(frame, cut_edge_rect(20, 20, w, 32, 4, {true, true, false, false}), 5066061);
  ui_path(frame, cut_edge_rect(20, 20, w, h, 4), {}, white);
  flash_draw::drop_shadow(frame, 0, 0, 0, .5, 12 * frame.pixel_scale, 12 * frame.pixel_scale);
  add_image(straight(frame), ox - 26, oy - 26, 1 / frame.pixel_scale);
  add_text(state.dialog_title_, 12, false, gray, ox + 5, oy);
  if (details) {
    add_text("Currently logged in as:", 18, true, gray, ox + 17, oy + 40);
    add_text(state.account_.username, 16, false, gray, ox + 17, oy + 70);
  }
  for (std::size_t i = 0; i < state.fields_.size(); ++i) {
    const auto& f = state.fields_[i];
    const double y = oy + field_y[i];
    const int action = field_action + static_cast<int>(i);
    add_text(f.label, 18, true, gray, ox + 17, y);
    add_solid(ox + 16, y + 29, 240, 32, 4539717);
    add_solid(ox + 17, y + 30, 238, 30, 3355443);
    const auto insert_before = elements_.size();
    std::string shown = f.password ? std::string(f.value.size(), '*') : f.value;
    if (i == state.focus_ && state.caret_) shown += '|';
    if (!shown.empty()) {
      add_text(shown, 20, false, gray, ox + 17 + 6, y + 30, -1, false);
      elements_.back().field_value = true;
      elements_.back().field_index = i;
    }
    field_views_.push_back(
        {i, f.label, f.value, f.error, f.password, {ox + 17, y + 30}, {238, 30}, insert_before, i == state.focus_});
    if (!f.error.empty()) add_text(f.error, 12, false, 16549442, ox + 17, y + 62);
    auto& hit_area = add_solid(ox + 17, y + 30, 238, 30, 0, 0, action);
    hit_area.hit = true;
  }
  if (navigation || details) {
    const std::string link = details                          ? "Click here to change password"
                             : state.dialog_ == Dialog::login ? "New user?  Click here to Register"
                                                              : "Already registered?  Click here to sign in";
    const auto field = fonts_.layout(link, 12, false);
    add_text(link, 12, false, white, ox + 17, oy + link_y, link_action);
    auto& hit_area = add_solid(ox + 17, oy + link_y, field.width, field.height, 0, 0, link_action);
    hit_area.hit = true;
  }
  if (details) {
    const std::string link = "Not you?  Click here";
    const auto field = add_text(link, 12, false, white, ox + 17, oy + logout_y, account_logout_action);
    auto& area = add_solid(ox + 17, oy + logout_y, field.x, field.y, 0, 0, account_logout_action);
    area.hit = true;
  }
  for (std::size_t i = 0; i < lines.size(); ++i) add_text(lines[i], 12, false, white, ox + 17, oy + line_y[i]);
  const auto button = [&](const std::string& label, double x, int action) {
    const auto field = fonts_.layout(label, 18, true);
    if (x < 0) x = w - field.width - 26;
    add_text(label, 18, true, busy ? gray : white, ox + x, oy + h - 52, action);
    auto& hit_area = add_solid(ox + x, oy + h - 52, field.width, field.height, 0, 0, action);
    hit_area.hit = true;
  };
  if (!message && !details) button("Cancel", 109, cancel_action);
  button(details                                    ? "Continue"
         : message                                  ? "Ok"
         : state.dialog_ == Dialog::change_password ? "Submit"
         : state.dialog_ == Dialog::login           ? "Sign in"
                                                    : "Register",
         -1, submit_action);
}
void StartupViewModel::rebuild(StartupScreenState& state, bool busy) {
  dirty_ = false;
  reset_sheet();
  elements_.clear();
  field_views_.clear();
  character_tooltip_element_.reset();
  switch (state.screen_) {
    case Screen::account_loading:
      menu_frame(false);
      loading_screen(state.loading_text_);
      break;
    case Screen::loading:
    case Screen::entering:
      menu_frame(true);
      loading_screen(state.loading_text_);
      break;
    case Screen::title:
      title_screen(state);
      break;
    case Screen::characters:
      character_screen(state);
      break;
    case Screen::classes:
      class_screen(state);
      break;
    case Screen::skins:
      skin_screen(state);
      break;
    case Screen::fame:
      fame_screen(state, busy);
      break;
    case Screen::legends:
      legends_screen(state, busy);
      break;
    case Screen::game:
      break;
  }
  dialog_begin_ = elements_.size();
  if (state.dialog_ != Dialog::none && state.screen_ != Screen::game) dialog_frame(state, busy);
  const int used = std::min(sheet_.height, sheet_y_ + sheet_row_ + 1);
  const HudRegion upload{0, 0, sheet_.width, std::max(used, sheet_used_)};
  sheet_used_ = used;
  const bool pixels_changed = sheet_replaced_ || !changed_regions_.empty();
  if (pixels_changed) {
    if (sheet_replaced_) {
      if (view_uploads_.empty())
        view_uploads_.push_back(upload);
      else {
        auto dirty = view_uploads_.front();
        for (std::size_t i = 1; i < view_uploads_.size(); ++i) dirty = united(dirty, view_uploads_[i]);
        view_uploads_.assign(1, united(dirty, upload));
      }
    } else {
      for (const auto region : changed_regions_) {
        if (view_uploads_.empty())
          view_uploads_.push_back(region);
        else {
          auto dirty = view_uploads_.front();
          for (std::size_t i = 1; i < view_uploads_.size(); ++i) dirty = united(dirty, view_uploads_[i]);
          view_uploads_.assign(1, united(dirty, region));
        }
      }
    }
    ++sheet_revision_;
  }
  sheet_replaced_ = false;
  ++structure_revision_;
  view_pending_ = true;
  refresh(state, busy);
}
Vec2 StartupViewModel::to_window(const Element& e, Vec2 p) const {
  const double scale = height_ / 600;
  if (e.anchor == Anchor::content)
    return {(width_ - 800 * scale) / 2 + p.x * scale, (height_ - 600 * scale) / 2 + p.y * scale};
  return {e.anchor_fraction.x * width_ + e.pixels.x + p.x * scale,
          e.anchor_fraction.y * height_ + e.pixels.y + p.y * scale};
}
void StartupViewModel::refresh(const StartupScreenState& state, bool busy) {
  auto& view = view_;
  view = {};
  view.revision = ++view_revision_;
  view.structure_revision = structure_revision_;
  view.sheet_revision = sheet_revision_;
  view.width = static_cast<int>(std::lround(width_));
  view.height = static_cast<int>(std::lround(height_));
  view.scale = height_ / 600;
  view.characters = state.screen_ == Screen::characters;
  view.busy = busy;
  view.dialog_insertion = dialog_begin_;
  view.menus = state.menus();
  view.dialog = state.dialog_ != Dialog::none;
  view.sheet_dirty = view_uploads_;
  view.fame = state.screen_ == Screen::fame;
  view.legends = state.screen_ == Screen::legends;
  view.deletion = state.delete_dialog_.visible();
  view.elements.reserve(elements_.size());
  const double scale = height_ / 600;
  for (const auto& e : elements_) {
    StartupElementSnapshot item;
    item.source = e.source;
    item.position = to_window(e, e.position);
    item.size = {e.size.x * scale, e.size.y * scale};
    item.text_position = to_window(e, e.text_origin);
    item.pulse_center = to_window(e, e.pulse_center);
    if (e.full_width) {
      item.position.x = 0;
      item.size.x = width_;
    }
    item.color = e.color;
    item.font_color = e.font_color;
    item.hover_color = e.over;
    item.action = e.group >= 0 ? typed_action(state, e.group) : std::nullopt;
    item.text = e.text;
    item.font_size = e.font_size;
    item.solid = e.solid;
    item.full_width = e.full_width;
    item.hit = e.hit;
    item.hovered = e.group >= 0 && e.group == state.hovered_;
    item.pulse = e.pulse;
    item.font_bold = e.font_bold;
    item.font_shadow = e.font_shadow;
    item.field_value = e.field_value;
    item.field_index = e.field_index;
    view.elements.push_back(std::move(item));
  }
  view.fields.reserve(field_views_.size());
  for (auto field : field_views_) {
    Element bounds;
    bounds.position = field.position;
    bounds.size = field.size;
    field.position = to_window(bounds, bounds.position);
    field.size = {bounds.size.x * scale, bounds.size.y * scale};
    field.focused = field.index == state.focus_;
    view.fields.push_back(std::move(field));
  }
  view.skin_visible =
      state.skins_ && state.skins_->visible() && (state.screen_ == Screen::skins || state.skins_->modal_input());
  view.skin_modal_input = state.skins_ && state.skins_->visible() && state.skins_->modal_input();
  if (view.skin_visible && state.skins_) view.skin_image = &state.skins_->image();
  view.skin_revision = skin_revision_;
  if (pending_skin_change_.width > 0 && pending_skin_change_.height > 0)
    view.skin_dirty.push_back(pending_skin_change_);
  view_pending_ = false;
}
int StartupViewModel::hit(const StartupScreenState& state, Vec2 p) const {
  if (!state.menus()) return -1;
  const double scale = height_ / 600;
  for (auto e = elements_.rbegin(); e != elements_.rend(); ++e) {
    if (!e->hit || (state.dialog_ != Dialog::none && e->group < dialog_actions)) continue;
    auto origin = to_window(*e, e->position);
    Vec2 size{e->size.x * scale, e->size.y * scale};
    if (e->full_width) {
      origin.x = 0;
      size.x = width_;
    }
    if (p.x >= origin.x && p.y >= origin.y && p.x < origin.x + size.x && p.y < origin.y + size.y) return e->group;
  }
  return -1;
}
}  // namespace realm::startup_detail
