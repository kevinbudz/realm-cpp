#include "realm/ui/options/OptionsScreen.hpp"
#include "realm/ui/tooltip/TextToolTip.hpp"
#include "realm/ui/GraphicsUtil.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace realm {
namespace {
constexpr double box_width = 80, box_height = 32, label_x = box_width + 24;
constexpr std::uint32_t label_color = 0xb3b3b3, box_fill = 0x333333, box_edge = 0x444444,
                        hover_edge = 0xb3b3b3, tab_selected = 0xffc800, tab_hover = 0xffffff,
                        button_hover = 0xffdc85;

// MoreColorUtil.darkCT multiplies every channel of a disabled KeyMapper by 0.6.
constexpr std::uint32_t dark(std::uint32_t color) {
  const auto channel = [&](int shift) {
    return static_cast<std::uint32_t>(((color >> shift) & 255U) * 6 / 10);
  };
  return channel(16) << 16 | channel(8) << 8 | channel(0);
}

bool inside(const HudRect& r, Vec2 p) {
  return p.x >= r.x && p.x < r.x + r.width && p.y >= r.y && p.y < r.y + r.height;
}

// "Option.x/y": two columns, 44 design units apart, spacer sprites count as cells.
Vec2 cell(std::size_t index) {
  return {index % 2 == 0 ? 20.0 : 415.0, static_cast<double>(index / 2) * 44 + 122};
}

std::size_t choice_index(const OptionDef& option, int value) {
  for (std::size_t i = 0; i < option.choices.size(); ++i) {
    if (option.choices[i].second == value) {
      return i;
    }
  }
  return 0;
}
}

void OptionsScreen::open(Settings& settings) {
  settings_ = &settings;
  tab_ = OptionsTab::controls;
  hover_ = pressed_ = capture_ = -1;
  changed_ = home_ = false;
  laid_out_ = false;
  dirty_ = true;
  pointer_valid_ = false;
  tooltip_ = false;
}

void OptionsScreen::close() {
  settings_ = nullptr;
  hover_ = pressed_ = capture_ = -1;
  rows_.clear();
  laid_out_ = false;
  tooltip_ = false;
}

void OptionsScreen::set_tab(OptionsTab tab) {
  if (!visible()) {
    return;
  }
  cancel_capture();
  if (tab == tab_ && laid_out_) {
    return;
  }
  tab_ = tab;
  laid_out_ = false;
  dirty_ = true;
  pressed_ = -1;
}

void OptionsScreen::cancel_capture() {
  if (capture_ >= 0) {
    capture_ = -1;
    dirty_ = true;
  }
}

void OptionsScreen::focus_lost() {
  pressed_ = -1;
  pointer_valid_ = false;
  tooltip_ = false;
  if (hover_ != -1) {
    hover_ = -1;
    dirty_ = true;
  }
}

int OptionsScreen::hit(Vec2 point) const {
  if (!visible() || !laid_out_) {
    return -1;
  }
  const auto p = local(point);
  for (int i = 0; i < static_cast<int>(buttons_.size()); ++i) {
    if (inside(buttons_[static_cast<std::size_t>(i)], p)) {
      return continue_hit + i;
    }
  }
  for (int i = 0; i < static_cast<int>(tab_rects_.size()); ++i) {
    if (inside(tab_rects_[static_cast<std::size_t>(i)], p)) {
      return tab_hit + i;
    }
  }
  for (std::size_t i = 0; i < rows_.size(); ++i) {
    const auto& row = rows_[i];
    if (row.def->kind == OptionDef::Kind::spacer || !settings_->enabled(*row.def)) {
      continue;
    }
    if (inside(row.box, p)) {
      return box_hit + static_cast<int>(i);
    }
    if (inside(row.label, p)) {
      return label_hit + static_cast<int>(i);
    }
  }
  return -1;
}

void OptionsScreen::refresh_hover() {
  const int over = pointer_valid_ ? hit(pointer_) : -1;
  if (over != hover_) {
    hover_ = over;
    dirty_ = true;
  }
}

void OptionsScreen::pointer_move(Vec2 point) {
  pointer_ = point;
  pointer_valid_ = true;
  refresh_hover();
  if (hover_ >= label_hit && hover_ < continue_hit) {
    dirty_ = true;
  }
}

void OptionsScreen::pointer_down(Vec2 point) {
  pointer_move(point);
  // The key box listens for any stage MOUSE_DOWN (capture phase) to leave input mode.
  cancel_capture();
  pressed_ = hover_;
}

void OptionsScreen::pointer_up(Vec2 point) {
  if (!visible()) {
    return;
  }
  pointer_move(point);
  const int pressed = std::exchange(pressed_, -1);
  if (pressed >= 0 && pressed == hover_) {
    activate(pressed);
  }
}

void OptionsScreen::activate(int h) {
  if (h >= tab_hit && h < tab_hit + static_cast<int>(options_tab_count)) {
    set_tab(static_cast<OptionsTab>(h - tab_hit));
    refresh_hover();
    return;
  }
  if (h == continue_hit) {
    close();
    return;
  }
  if (h == home_hit) {
    close();
    home_ = true;
    return;
  }
  if (h == reset_hit) {
    settings_->reset_tab(tab_);
    changed_ = true;
    dirty_ = true;
    return;
  }
  if (h >= box_hit && h < label_hit) {
    const auto& option = *rows_.at(static_cast<std::size_t>(h - box_hit)).def;
    if (option.kind == OptionDef::Kind::key) {
      capture_ = h - box_hit;
      dirty_ = true;
    } else if (option.kind == OptionDef::Kind::choice && !option.choices.empty()) {
      // ChoiceBox.onClick: the next value, wrapping; ChoiceOption saves and runs its callback.
      const auto next = option
                            .choices[(choice_index(option, settings_->get(option.param)) + 1) %
                                     option.choices.size()]
                            .second;
      settings_->set_choice(option.param, next);
      changed_ = true;
      dirty_ = true;
    }
  }
}

bool OptionsScreen::key_down(int code) {
  if (!visible()) {
    return false;
  }
  if (capture_ >= 0) {
    // KeyCodeBox.onInputKeyDown takes any key. Escape cancels here instead, because the
    // game binds it to leaving the game.
    if (code == key_escape) {
      cancel_capture();
    } else if (code != 0) {
      settings_->set_key(rows_.at(static_cast<std::size_t>(capture_)).def->param, code);
      capture_ = -1;
      changed_ = true;
      dirty_ = true;
    }
    return true;
  }
  if (code == key_escape || (code != 0 && code == settings_->get("options"))) {
    close();
  }
  return true;
}

void OptionsScreen::layout(const FontBook& fonts) {
  rows_.clear();
  std::size_t index = 0;
  for (const auto* option : tab_options(tab_)) {
    Row row;
    row.def = option;
    const auto at = cell(index++);
    row.box = {at.x, at.y, box_width, box_height};
    if (option->kind != OptionDef::Kind::spacer) {
      const auto text = fonts.rasterize(option->label, 18, false, true, label_color, 0, 0, 2);
      row.label = {at.x + label_x, at.y + box_height / 2 - text.height / 2 - 2, text.width,
                   text.height};
    }
    rows_.push_back(row);
  }
  for (std::size_t i = 0; i < options_tab_count; ++i) {
    const auto text = fonts.rasterize(options_tab_name(static_cast<OptionsTab>(i)), 16, true, true);
    tab_rects_[i] = {14 + 108.0 * static_cast<double>(i), 70, text.width, text.height};
  }
  const auto size = [&](std::string_view text, int px) {
    return fonts.rasterize(text, px, true, true);
  };
  const auto cont = size("continue", 36), reset = size("reset to defaults", 22),
             home = size("back to home", 22);
  buttons_ = {{{400 - cont.width / 2, 520, cont.width, cont.height},
               {20, 532, reset.width, reset.height},
               {620, 532, home.width, home.height}}};
  laid_out_ = true;
}

bool OptionsScreen::update(const FontBook& fonts, double width, double height,
                           std::int32_t time_ms) {
  if (!visible() || width <= 0 || height <= 0) {
    return false;
  }
  const double scale = height / 600;
  if (stage_width_ != width || stage_height_ != height ||
      base_.pixel_scale != fonts.pixel_scale() || base_.width == 0) {
    stage_width_ = width;
    stage_height_ = height;
    scale_ = scale;
    position_ = {(width - 800 * scale) / 2, 0};
    base_ = ui_image(width / scale, 600, scale);
    ui_menu_backdrop(base_, width / scale);
    pressed_ = -1;
    laid_out_ = false;
    dirty_ = true;
  }
  if (!laid_out_) {
    layout(fonts);
  }
  refresh_hover();
  const int blink = capture_ >= 0 ? (time_ms / 400) % 2 : 0;
  if (blink != blink_) {
    blink_ = blink;
    dirty_ = true;
  }
  if (!dirty_) {
    return false;
  }
  const double logical_width = width / scale, x = (logical_width - 800) / 2;
  image_ = base_;
  const auto bitmap = [&](const TextBitmap& b, double px, double py) {
    ui_blit_text(image_, b, x + px + b.offset.x, py + b.offset.y, false, true);
  };
  const auto text = [&](std::string_view value, int size, bool bold, std::uint32_t color, double px,
                        double py, unsigned strength = 1) {
    bitmap(fonts.rasterize(value, size, bold, true, color, 0, 0, strength), px, py);
  };
  // Options.layoutChrome: a one-pixel rule under the tab strip.
  ui_rectangle(image_, 0, 100 - .5 / scale, logical_width, 1 / scale, 0x5e5e5e, true);
  {
    const auto title = fonts.rasterize("Options", 36, true, true);
    bitmap(title, 400 - title.width / 2, 8);
  }
  const auto button = [&](int h, std::string_view value, int size) {
    const auto& r = button_rect(h);
    text(value, size, true, hover_ == h ? button_hover : 0xffffff, r.x, r.y);
  };
  button(continue_hit, "continue", 36);
  button(reset_hit, "reset to defaults", 22);
  button(home_hit, "back to home", 22);
  for (int i = 0; i < static_cast<int>(options_tab_count); ++i) {
    const auto& r = tab_rects_[static_cast<std::size_t>(i)];
    const bool selected = static_cast<OptionsTab>(i) == tab_;
    text(options_tab_name(static_cast<OptionsTab>(i)), 16, true,
         selected                ? tab_selected
         : hover_ == tab_hit + i ? tab_hover
                                 : label_color,
         r.x, r.y);
  }
  for (std::size_t i = 0; i < rows_.size(); ++i) {
    const auto& row = rows_[i];
    const auto& option = *row.def;
    if (option.kind == OptionDef::Kind::spacer) {
      continue;
    }
    const bool enabled = settings_->enabled(option), input = capture_ == static_cast<int>(i);
    const bool over = enabled && (hover_ == box_hit + static_cast<int>(i));
    const auto tint = [&](std::uint32_t color) { return enabled ? color : dark(color); };
    const auto edge = tint(over || input ? hover_edge : box_edge);
    const auto& b = row.box;
    if (option.kind == OptionDef::Kind::key) {
      // KeyCodeBox: a plain rectangle with a two-pixel border.
      const std::array<Vec2, 4> path{{{x + b.x, b.y},
                                      {x + b.x + b.width, b.y},
                                      {x + b.x + b.width, b.y + b.height},
                                      {x + b.x, b.y + b.height}}};
      ui_path(image_, path, tint(box_fill), edge, 2);
    } else {
      ui_path(image_, cut_edge_rect(x + b.x, b.y, b.width, b.height, 4), tint(box_fill), edge, 2);
    }
    std::string value;
    if (option.kind == OptionDef::Kind::key) {
      if (input) {
        value = blink_ == 0 ? "" : "[Hit Key]";
      } else {
        value = std::string(key_name(settings_->get(option.param)));
      }
    } else if (!option.choices.empty()) {
      value = std::string(option.choices[choice_index(option, settings_->get(option.param))].first);
    }
    if (!value.empty()) {
      const auto face = fonts.rasterize(value, 16, true, true, tint(0xffffff), 0, 0, 2);
      bitmap(face, b.x + box_width / 2 - face.width / 2,
             b.y + box_height / 2 - face.height / 2 - 2);
    }
    text(option.label, 18, false, tint(label_color), row.label.x, row.label.y, 2);
  }
  // Option.onMouseOver: a TextToolTip(0x272727, 0x828282, 150) follows the pointer.
  tooltip_ = false;
  if (hover_ >= label_hit && hover_ < continue_hit && pointer_valid_) {
    const auto& option = *rows_.at(static_cast<std::size_t>(hover_ - label_hit)).def;
    if (!option.tooltip.empty()) {
      const auto tip = render_text_tooltip(fonts, {}, option.tooltip, 0x272727, 0x828282, 150);
      const auto at =
          text_tooltip_position(pointer_, tip.frame_width, tip.frame_height, width, height, scale);
      ui_blit(image_, tip.image, at.x / scale - tip.margin + 0, at.y / scale - tip.margin, false,
              true);
      tooltip_ = true;
    }
  }
  // The members screen's texture is straight alpha; so is this one.
  for (std::size_t i = 0; i < image_.pixels.size(); i += 4) {
    const unsigned alpha = image_.pixels[i + 3];
    if (alpha && alpha < 255) {
      for (unsigned c = 0; c < 3; ++c) {
        image_.pixels[i + c] = static_cast<unsigned char>(std::min(
            255U, (static_cast<unsigned>(image_.pixels[i + c]) * 255 + alpha / 2) / alpha));
      }
    }
  }
  dirty_ = false;
  return true;
}
}
