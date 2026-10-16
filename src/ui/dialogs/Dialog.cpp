#include "realm/ui/dialogs/Dialog.hpp"
#include "realm/ui/GraphicsUtil.hpp"
#include <algorithm>
#include <cmath>
#include <utility>

namespace realm {
namespace {
// SimpleText's title has fixed width, explicit newlines, and no wordWrap.
// updateMetrics sums integer TextLineMetrics.height+4 for each title line.
TextBitmap title_bitmap(const FontBook& fonts, std::string_view text) {
  const auto first = fonts.rasterize("Ag", 18, true);
  const double line = first.height;
  const double scale = fonts.pixel_scale();
  std::vector<std::string_view> lines;
  std::size_t at = 0;

  do {
    const auto end = text.find('\n', at);
    lines.push_back(text.substr(at, end == text.npos ? text.size() - at : end - at));
    if (end == text.npos) {
      break;
    }
    at = end + 1;
  } while (true);
  const double h = line * static_cast<double>(lines.size());
  std::vector<std::pair<TextBitmap, Vec2>> parts;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    auto part = fonts.rasterize(lines[i], 18, true, false, 0x57ad62);
    const Vec2 origin{8 + (296 - fonts.measure(lines[i], 18, true)) / 2 + part.offset.x,
                      8 + static_cast<double>(i) * fonts.line_advance(18, true) + part.offset.y};
    parts.emplace_back(std::move(part), origin);
  }

  return redrawable_text([parts = std::move(parts), scale, h](Vec2 phase) {
    TextBitmap result{
        ui_image(316 + phase.x / scale, h + 16 + phase.y / scale, scale), {-8, -8}, 300, h};
    for (const auto& [part, origin] : parts) {
      double x = origin.x + phase.x / scale;
      double y = origin.y + phase.y / scale;
      const auto placed = place_text(part, scale, x, y);
      ui_blit(result.image, placed.image, x, y, false, false, 1, 8, 8, 300, h);
    }
    premultiply_flash_bitmap(result.image);
    ui_shadow(result.image, 4, 316 + phase.x / scale, h + 16 + phase.y / scale);
    return result;
  });
}

TextBitmap body_bitmap(const FontBook& fonts, std::string_view text) {
  const auto base = fonts.rasterize_centered(text, 14, false, false, 0xb3b3b3, 260);
  const double scale = fonts.pixel_scale();

  return redrawable_text([base, scale](Vec2 phase) {
    const double width = 276 + phase.x / scale;
    const double height = base.height + 16 + phase.y / scale;
    auto halo = ui_image(width, height, scale);
    double x = 8 + phase.x / scale;
    double y = 8 + phase.y / scale;
    const auto placed = place_text(base, scale, x, y);
    ui_blit(halo, placed.image, x, y, false, true);
    ui_shadow(halo, 3, width, height);
    return TextBitmap{std::move(halo), {-8, -8}, base.width, base.height};
  });
}
}

void Dialog::show(Content content) {
  content_ = std::move(content);
  visible_ = true;
  message_close_ = false;
  enabled_ = {true, true};
  alpha_ = 1;
  dirty_ = true;
  buttons_ = {};

  focus_lost();
}

void Dialog::message_close(std::string title, std::string body, std::string button) {
  show({std::move(body),
        std::move(title),
        {button.empty() ? "Close" : std::move(button), std::nullopt}});
  message_close_ = true;
}

bool Dialog::purchase_result(const net::incoming::BuyResult& result) {
  if (result.result != 1) {
    return false;
  }
  message_close("Purchase Error", result.text);
  return true;
}

void Dialog::close() {
  visible_ = false;
  focus_lost();
}

void Dialog::set_enabled(int button, bool enabled) {
  auto& value = enabled_.at(static_cast<std::size_t>(button));
  if (value != enabled) {
    value = enabled;
    dirty_ = true;
    if (pressed_ == button) {
      pressed_ = -1;
    }
    if (pointer_valid_) {
      pointer_move(pointer_);
    }
  }
}

void Dialog::set_base_alpha(double alpha) {
  const auto value = std::isfinite(alpha) ? std::clamp(alpha, 0.0, 1.0) : 1;
  if (alpha_ != value) {
    alpha_ = value;
    dirty_ = true;
  }
}

void Dialog::focus_lost() {
  pressed_ = -1;
  pointer_valid_ = false;
  if (hovered_ != -1) {
    hovered_ = -1;
    dirty_ = true;
  }
}

int Dialog::hit(Vec2 point) const {
  if (!visible_) {
    return -1;
  }
  const Vec2 local{(point.x - position_.x) / scale_, (point.y - position_.y) / scale_};
  for (int i = 0; i < 2; ++i) {
    const auto& b = buttons_[static_cast<std::size_t>(i)];
    if (enabled_[static_cast<std::size_t>(i)] && b.width > 0 && local.x >= b.x &&
        local.x < b.x + b.width && local.y >= b.y && local.y < b.y + b.height) {
      return i;
    }
  }
  return -1;
}

void Dialog::pointer_move(Vec2 point) {
  pointer_ = point;
  pointer_valid_ = true;
  const int over = hit(point);
  if (over != hovered_) {
    hovered_ = over;
    dirty_ = true;
  }
}

void Dialog::pointer_down(Vec2 point) {
  pointer_move(point);
  pressed_ = hovered_;
}

std::optional<int> Dialog::pointer_up(Vec2 point) {
  pointer_move(point);
  const int pressed = std::exchange(pressed_, -1);
  if (pressed < 0 || pressed != hovered_) {
    return std::nullopt;
  }
  if (message_close_ && pressed == 0) {
    close();
  }
  return pressed;
}

bool Dialog::update(const FontBook& fonts, double stage_width, double stage_height) {
  if (!visible_ || stage_width <= 0 || stage_height <= 0) {
    return false;
  }

  const double density = stage_height / 600;
  if (scale_ != density || stage_width_ != stage_width || stage_height_ != stage_height) {
    // Cancel a press during resize so stale hit areas cannot activate.
    pressed_ = -1;
    dirty_ = true;
    scale_ = density;
    stage_width_ = stage_width;
    stage_height_ = stage_height;
  }
  if (!dirty_) {
    return false;
  }

  const auto body = body_bitmap(fonts, content_.body);
  std::optional<TextBitmap> title;
  if (content_.title) {
    title = title_bitmap(fonts, *content_.title);
  }
  const double top = title ? 2 : 4;
  const double body_y = title ? title->height + 8 : 4;
  double bottom = body_y + body.height;
  buttons_ = {};

  if (content_.buttons[0]) {
    const double y = std::floor(bottom - top + 16);
    for (int i = 0; i < (content_.buttons[1] ? 2 : 1); ++i) {
      const auto text = fonts.rasterize(*content_.buttons[static_cast<std::size_t>(i)], 16, true,
                                        false, 0x363636);
      const double h = text.height - 4 + 8;
      buttons_[static_cast<std::size_t>(i)] = {content_.buttons[1] ? 15.0 + 150 * i : 90.0, y, 120,
                                               h};
      bottom = std::max(bottom, y + h);
    }
  }
  height_ = std::floor(bottom - top + 10);
  position_ = {offset_.x + (stage_width - (width + 1) * scale_) / 2,
               offset_.y + (stage_height - (height_ + 1) * scale_) / 2};

  if (pointer_valid_) {
    hovered_ = hit(pointer_);
  }
  image_ = ui_image(width + 2 * padding, height_ + 2 * padding, density);
  auto frame = ui_image(width + 2 * padding, height_ + 2 * padding, density);
  ui_path(frame, cut_edge_rect(padding, padding, width, height_, 4), 0x363636, 0xffffff);
  ui_blit(image_, frame, 0, 0, true, true, alpha_);
  const auto blit = [&](const TextBitmap& bitmap, double x, double y) {
    ui_blit_text(image_, bitmap, padding + x + bitmap.offset.x, padding + y + bitmap.offset.y,
                 false, true);
  };

  if (title) {
    blit(*title, 0, 2);
  }
  blit(body, 20, body_y);

  for (int i = 0; i < 2; ++i) {
    const auto index = static_cast<std::size_t>(i);
    const auto& b = buttons_[index];
    if (b.width == 0) {
      continue;
    }
    ui_path(image_, cut_edge_rect(padding + b.x, padding + b.y, b.width, b.height, 4),
            !enabled_[index] ? 0x7f7f7f
            : hovered_ == i  ? 0xffdc85
                             : 0xffffff);
    const auto text = fonts.rasterize(*content_.buttons[index], 16, true, false, 0x363636);
    blit(text, b.x + b.width / 2 - fonts.measure(*content_.buttons[index], 16, true) / 2 - 2,
         b.y + 1);
  }

  ui_shadow(image_, 8, width + 2 * padding, height_ + 2 * padding);
  dirty_ = false;
  return true;
}
}
