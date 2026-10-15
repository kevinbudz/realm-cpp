#include "realm/ui/SkinChooser.hpp"
#include "realm/ui/GraphicsUtil.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace realm {
SkinChooser::~SkinChooser() {
  if (request_.valid()) {
    request_.wait();
  }
}

void SkinChooser::endpoint(std::string host, std::uint16_t port, net::FormFields credentials) {
  host_ = std::move(host);
  port_ = port;
  credentials_ = std::move(credentials);
}

void SkinChooser::close() {
  visible_ = ready_ = replace_modal_ = false;
  rows_.clear();
  selection_.reset();
  focus_lost();
  error_.close();
  ++generation_;
}

void SkinChooser::open(int type, int current, const CharList& data, bool startup) {
  close();
  class_type_ = type;
  startup_ = startup;
  data_ = data;
  visible_ = ready_ = true;
  scroll_ = 0;
  set_rows(current);
  dirty_ = true;
}

void SkinChooser::set_rows(int current) {
  rows_ = {{0, "Classic", true}};
  for (const auto& skin : catalog_.skins()) {
    if (skin.class_type == class_type_) {
      rows_.push_back({skin.id, skin.name,
                       std::find(data_.owned_skins.begin(), data_.owned_skins.end(), skin.id) !=
                           data_.owned_skins.end()});
    }
  }
  selected_ = owned(current) ? current : 0;
  preview_ = next_preview_ = selected_;
  view_height_ = std::min(410.0, list_height()) + 87;
}

bool SkinChooser::owned(int id) const {
  return std::any_of(rows_.begin(), rows_.end(),
                     [&](const Row& row) { return row.id == id && row.owned; });
}

void SkinChooser::fetch(int type, int current, std::string host, std::uint16_t port,
                        net::FormFields credentials) {
  close();
  class_type_ = type;
  selected_ = current;
  visible_ = true;
  startup_ = false;
  view_height_ = 147;
  endpoint(std::move(host), port, std::move(credentials));
  dirty_ = true;
  // An older request is drained before starting a new one, without blocking UI.
  if (task_ == Task::none) {
    request(Task::list, "/char/list", credentials_);
  }
}

void SkinChooser::request(Task task, std::string path, net::FormFields fields) {
  task_ = task;
  request_generation_ = generation_;
  request_username_.clear();
  for (const auto& field : fields) {
    if (field.first == "username") {
      request_username_ = field.second;
    }
  }
  request_ = std::async(std::launch::async, [host = host_, port = port_, path = std::move(path),
                                             fields = std::move(fields)] {
    return net::http_post(host, port, path, fields);
  });
}

void SkinChooser::poll() {
  if (task_ == Task::none || !request_.valid() ||
      request_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
    return;
  }
  const auto task = std::exchange(task_, Task::none);
  std::string response;
  std::optional<std::string> failure;
  try {
    response = request_.get();
    failure = app_error(response);
  } catch (const std::exception& e) {
    failure = e.what();
  }
  if (task == Task::buy) {
    purchase_ = Purchase{requested_, request_username_, failure.value_or("")};
  }
  if (request_generation_ != generation_) {
    const bool same_account =
        std::any_of(credentials_.begin(), credentials_.end(), [&](const auto& field) {
          return field.first == "username" && field.second == request_username_;
        });
    if (task == Task::buy && !failure && same_account &&
        std::find(data_.owned_skins.begin(), data_.owned_skins.end(), requested_) ==
            data_.owned_skins.end()) {
      data_.owned_skins.push_back(requested_);
      data_.credits = std::max(0, data_.credits - 1000);
      if (visible_ && ready_) {
        set_rows(selected_);
      }
      dirty_ = true;
    }
    if (visible_ && !ready_) {
      request(Task::list, "/char/list", credentials_);
    }
    return;
  }
  dirty_ = true;
  if (task == Task::list && !failure) {
    try {
      data_ = parse_char_list(response);
      set_rows(selected_);
      ready_ = true;
    } catch (const std::exception& e) {
      failure = e.what();
    }
  } else if (task == Task::buy && !failure) {
    data_.credits = std::max(0, data_.credits - 1000);
    data_.owned_skins.push_back(requested_);
    set_rows(requested_);
  }
  if (failure) {
    replace_modal_ = !startup_;
    if (task == Task::list) {
      error_.show({*failure, "Error", {"Cancel", "Retry"}});
    } else {
      purchase_error(*failure);
    }
  }
}

void SkinChooser::purchase_error(const std::string& message, bool insufficient) {
  replace_modal_ = !startup_ || !visible_;
  visible_ = true;
  dirty_ = true;
  if (insufficient) {
    error_.message_close("Not Enough Gold", "You do not have enough Gold for this item.", "Cancel");
  } else {
    error_.message_close("D'oh, this isn't good", "An error has occured:\n" + message, "Ok");
  }
}

void SkinChooser::focus_lost() {
  pressed_ = hover_ = -1;
  drag_ = false;
  error_.focus_lost();
  dirty_ = true;
}

bool SkinChooser::update(double width, double height, std::int32_t time) {
  poll();
  time_ = time;
  if (width_ != width || height_ != height) {
    width_ = width;
    height_ = height;
    focus_lost();
  }
  scale_ = height / 600;
  position_ = startup_ ? Vec2{(width - 800 * scale_) / 2 + 341 * scale_, 73 * scale_}
                       : Vec2{(width - 462 * scale_) / 2, (height - view_height_ * scale_) / 2};
  if (startup_ && time >= preview_at_ && preview_ != next_preview_) {
    preview_ = next_preview_;
    preview_dirty_ = true;
  }
  if (startup_ && animation_frame_ != time / 200 % 2) {
    animation_frame_ = time / 200 % 2;
    preview_dirty_ = true;
  }
  if (error_.visible() && error_.update(fonts_, width, height)) {
    dirty_ = true;
  }
  if (!visible_) {
    return false;
  }
  if (dirty_ || image_.pixel_scale != fonts_.pixel_scale()) {
    render();
    dirty_ = preview_dirty_ = false;
    change_ = {0, 0, image_.width, image_.height};
    return true;
  }
  if (startup_ && ready_ && preview_dirty_ && !error_.visible()) {
    const double x = (width_ / scale_ - 800) / 2 + 125;
    const auto key = "skinwalk" + std::to_string(class_type_) + "-" + std::to_string(preview_) +
                     "-" + std::to_string(animation_frame_);
    const auto& preview = art_.image(key);
    const auto pixel_scale = image_.pixel_scale;
    const int px = static_cast<int>(std::lround(x * pixel_scale));
    const int py = static_cast<int>(std::lround(110 * pixel_scale));
    const int left = std::max(0, px);
    const int top = std::max(0, py);
    const int right = std::min(image_.width, px + static_cast<int>(std::ceil(104 * pixel_scale)));
    const int bottom = std::min(image_.height, py + static_cast<int>(std::ceil(104 * pixel_scale)));
    if (right <= left || bottom <= top) {
      preview_dirty_ = false;
      return false;
    }
    for (int y = top; y < bottom; ++y) {
      std::fill_n(image_.pixels.begin() + static_cast<std::ptrdiff_t>(y * image_.width + left) * 4,
                  (right - left) * 4, static_cast<unsigned char>(0));
    }
    ui_blit(image_, preview, x, 110, true, false);
    preview_dirty_ = false;
    change_ = {left, top, std::max(0, right - left), std::max(0, bottom - top)};
    return change_.width > 0 && change_.height > 0;
  }
  return false;
}

int SkinChooser::hit(Vec2 point) const {
  const auto pos = local(point);
  if (!ready_) {
    return -1;
  }
  if (!startup_ && pos.y >= view_height_ - 40 && pos.y < view_height_ - 10) {
    if (pos.x >= 55.5 && pos.x < 176.5) {
      return -2;
    }
    if (pos.x >= 286.5 && pos.x < 407.5) {
      return -3;
    }
  }
  if (pos.y < 37 || pos.y >= 37 + std::min(400.0, list_height()) || pos.x < 10 || pos.x >= 452) {
    return -1;
  }
  if (scrolling() && pos.x >= 432) {
    return -4;
  }
  const auto row = static_cast<int>((pos.y - 37 + scroll_) / 65);
  if (row < 0 || row >= static_cast<int>(rows_.size()) ||
      std::fmod(pos.y - 37 + scroll_, 65) >= 60) {
    return -1;
  }
  return row;
}

void SkinChooser::pointer_move(Vec2 point) {
  if (error_.visible()) {
    error_.pointer_move(point);
    return;
  }
  if (drag_) {
    const auto pos = local(point);
    scroll_ = std::clamp((pos.y - 37 - drag_offset_) / (400 - bar_height()), 0.0, 1.0) *
              (list_height() - 400);
    dirty_ = true;
  }
  const auto index = hit(point);
  if (index != hover_) {
    hover_ = index;
    dirty_ = true;
    next_preview_ = index >= 0 ? rows_[static_cast<std::size_t>(index)].id : selected_;
    preview_at_ = time_ + 250;
  }
}

void SkinChooser::pointer_down(Vec2 point) {
  if (error_.visible()) {
    error_.pointer_down(point);
    return;
  }
  pressed_ = hit(point);
  if (pressed_ == -4) {
    const auto y = local(point).y - 37;
    const auto top = scroll_ / (list_height() - 400) * (400 - bar_height());
    if (y >= top && y < top + bar_height()) {
      drag_ = true;
      drag_offset_ = y - top;
    }
  }
}

void SkinChooser::pointer_up(Vec2 point) {
  if (error_.visible()) {
    if (const auto action = error_.pointer_up(point)) {
      error_.close();
      dirty_ = true;
      const bool replaced = std::exchange(replace_modal_, false);
      if (replaced && ready_) {
        selection_ = -1;
        visible_ = false;
      }
      if (!ready_) {
        if (*action == 1) {
          request(Task::list, "/char/list", credentials_);
        } else {
          selection_ = -1;
          visible_ = false;
        }
      }
    }
    return;
  }
  const auto was = std::exchange(pressed_, -1);
  const auto index = hit(point);
  const bool dragged = std::exchange(drag_, false);
  if (was != index) {
    return;
  }
  if (index == -2) {
    selection_ = -1;
    visible_ = false;
    return;
  }
  if (index == -3 && owned(selected_) && task_ == Task::none) {
    selection_ = selected_;
    visible_ = false;
    return;
  }
  if (index == -4 && !dragged) {
    scroll_ =
        std::clamp((local(point).y - 37 - bar_height() / 2) / (400 - bar_height()), 0.0, 1.0) *
        (list_height() - 400);
    dirty_ = true;
  }
  if (index < 0) {
    return;
  }
  const auto& row = rows_[static_cast<std::size_t>(index)];
  if (row.owned) {
    selected_ = row.id;
    next_preview_ = selected_;
    dirty_ = true;
  } else if (task_ == Task::none) {
    const auto p = local(point);
    const auto label = fonts_.layout("Buy for 1000", 16, true);
    const double bw =
        std::max(120, static_cast<int>(label.width + art_.image("currency0").width + 15));
    const double ry = std::fmod(p.y - 37 + scroll_, 65);
    const double bh = label.height + 4;
    if (p.x < 10 + (scrolling() ? 420 : 442) - 16 - bw ||
        p.x >= 10 + (scrolling() ? 420 : 442) - 16 || ry < (60 - bh) / 2 || ry >= (60 + bh) / 2) {
      return;
    }
    if (data_.credits < 1000) {
      purchase_error("", true);
      return;
    }
    requested_ = row.id;
    auto fields = credentials_;
    fields.emplace_back("skinType", std::to_string(row.id));
    request(Task::buy, "/account/purchaseSkin", std::move(fields));
    dirty_ = true;
  }
}

void SkinChooser::wheel(double steps) {
  if (ready_ && !error_.visible() && scrolling()) {
    scroll_ = std::clamp(scroll_ - steps * .05 * (list_height() - 400), 0.0, list_height() - 400);
    dirty_ = true;
  }
}

void SkinChooser::render() {
  const auto density = fonts_.pixel_scale();
  image_ = ui_image(width_ / scale_, 600, density);
  // Division by the UI scale can round a viewport edge above an integer.
  image_.width = static_cast<int>(std::lround(width_));
  image_.height = static_cast<int>(std::lround(height_));
  image_.pixels.resize(static_cast<std::size_t>(image_.width) *
                       static_cast<std::size_t>(image_.height) * 4);
  const double x = position_.x / scale_;
  const double y = position_.y / scale_;
  auto text = [&](std::string_view value, int size, bool bold, std::uint32_t color, double tx,
                  double ty, bool shadow = true) {
    auto im = fonts_.rasterize(value, size, bold, shadow, color);
    ui_blit_text(image_, im, tx + im.offset.x, ty + im.offset.y, false, true);
  };
  auto button = [&](std::string_view label, double bx, double by, bool hover) {
    auto im = fonts_.rasterize(label, 16, true, false, 0x363636);
    const auto height = im.height + 4;
    ui_path(image_, cut_edge_rect(bx, by, 120, height, 4), hover ? 0xffdc85 : 0xffffff);
    ui_blit_text(image_, im, bx + 60 - fonts_.measure(label, 16, true) / 2 - 2 + im.offset.x,
                 by + 1 + im.offset.y, false, true);
  };
  if (!startup_ && !replace_modal_) {
    ui_rounded_rectangle(image_, 0, 0, width_ / scale_, 600, 1, 0x151515, .6, true);
    ui_path(image_, cut_edge_rect(x, y, 462, view_height_, 4), 0x363636, 0xffffff);
    ui_rectangle(image_, x + 1, y + 27 - 1 / density, 460, 2 / density, 0x5b5b5b, true);
    text("Select a Skin", 18, true, 0xb6b6b6,
         x + (462 - fonts_.layout("Select a Skin", 18, true).width) / 2 - 2, y, false);
  }
  if (!replace_modal_) {
    if (!ready_) {
      text("Loading...", 22, true, 0xb3b3b3, x + 160, y + 54);
    } else {
      // One bounded clipped list surface; only visible rows request icons/text.
      auto list = ui_image(442, std::min(400.0, list_height()), density);
      const double rw = scrolling() ? 420 : 442;
      for (std::size_t i = 0; i < rows_.size(); ++i) {
        const auto& row = rows_[i];
        const double ry = static_cast<double>(i) * 65.0 - scroll_;
        if (ry + 60 <= 0 || ry >= 400) {
          continue;
        }
        const bool purchasing = task_ == Task::buy && row.id == requested_;
        ui_rectangle(
            list, 0, ry, rw, 60,
            row.owned ? row.id == selected_ || static_cast<int>(i) == hover_ ? 0x7b7b7b : 0x5a5a5a
                      : 0x282828,
            true);
        const auto key = "skinicon" + std::to_string(class_type_) + "-" + std::to_string(row.id);
        if (purchasing) {
          auto icon = art_.image(key);
          for (std::size_t n = 0; n < icon.pixels.size(); n += 4) {
            const auto gray = static_cast<unsigned char>(std::lround(
                .3 * icon.pixels[n] + .59 * icon.pixels[n + 1] + .11 * icon.pixels[n + 2]));
            icon.pixels[n] = icon.pixels[n + 1] = icon.pixels[n + 2] = gray;
          }
          ui_blit(list, icon, 8, ry + 4, true, true);
        } else {
          ui_blit(list, art_.image(key), 8, ry + 4, true, true);
        }
        const auto name = fonts_.rasterize(row.name, 18, true, true, 0xffffff);
        ui_blit_text(list, name, 75 + name.offset.x, ry + 15 + name.offset.y, false, true);
        if (row.owned) {
          ui_path(list, cut_edge_rect(rw - 45, ry + 15, 28, 28, 4), 0, 0xffffff, 2, .01);
          if (row.id == selected_) {
            ui_path(list, cut_edge_rect(rw - 41, ry + 19, 20, 20, 2), 0xffffff);
          }
        } else if (purchasing) {
          const auto label = fonts_.rasterize("Purchasing...", 14, false, false, 0xffffff);
          ui_blit_text(list, label, 420 - 15 - label.width + label.offset.x,
                       ry + (60 - label.height) / 2 + label.offset.y, false, true);
        } else {
          const auto label = fonts_.rasterize("Buy for 1000", 16, true, false, 0x363636);
          const auto& coin = art_.image("currency0");
          const double bw = std::max(120, static_cast<int>(label.width + coin.width + 15));
          const double bh = label.height + 4;
          const double bx = rw - 16 - bw;
          const double by = ry + (60 - bh) / 2;
          const double tx = (bw - coin.width - fonts_.measure("Buy for 1000", 16, true) - 5) / 2;
          ui_path(list, cut_edge_rect(bx, by, bw, bh, 4),
                  static_cast<int>(i) == hover_ ? 0xffdc85 : 0xffffff);
          ui_blit_text(list, label, bx + tx + label.offset.x, by + 1 + label.offset.y, false, true);
          ui_blit(list, coin, bx + tx + fonts_.measure("Buy for 1000", 16, true) + 5,
                  by + (bh - coin.height) / 2, true, true);
        }
      }
      if (scrolling()) {
        ui_path(list, cut_edge_rect(422, 0, 20, 400, 4), 0x545454);
        const auto top = scroll_ / (list_height() - 400) * (400 - bar_height());
        ui_path(list, cut_edge_rect(422, top, 20, bar_height(), 4),
                hover_ == -4 || drag_ ? 0xffdc84 : 0xcccccc);
      }
      ui_blit(image_, list, x + 10, y + 37, true, true);
      if (!startup_) {
        const double by = y + view_height_ - (fonts_.layout("Cancel", 16, true).height + 4) - 10;
        button("Cancel", x + 55.5, by, hover_ == -2);
        button("Select", x + 286.5, by, hover_ == -3);
      } else {
        const double ox = (width_ / scale_ - 800) / 2;
        const auto& coin = art_.image("skincreditcoin");
        const double coin_x = width_ / scale_ - coin.width;
        ui_blit(image_, coin, coin_x, 20, true, true);
        const auto gold =
            fonts_.rasterize(std::to_string(data_.credits), 18, false, true, 0xffffff);
        const double gold_x = coin_x - gold.width + 8;
        ui_blit_text(image_, gold, gold_x + gold.offset.x,
                     20 + (coin.height - gold.height) / 2 + gold.offset.y, false, true);
        const auto& fame_icon = art_.image("skinfame");
        const double fame_x = gold_x - fame_icon.width;
        ui_blit(image_, fame_icon, fame_x, 20, true, true);
        const auto account_fame =
            fonts_.rasterize(std::to_string(data_.fame), 18, false, true, 0xffffff);
        ui_blit_text(image_, account_fame, fame_x - account_fame.width + 8 + account_fame.offset.x,
                     20 + (fame_icon.height - account_fame.height) / 2 + account_fame.offset.y,
                     false, true);
        const auto* def = catalog_.object(static_cast<std::uint16_t>(class_type_));
        const auto preview = "skinwalk" + std::to_string(class_type_) + "-" +
                             std::to_string(preview_) + "-" + std::to_string(time_ / 200 % 2);
        ui_blit(image_, art_.image(preview), ox + 125, 110, true, true);
        text(def->display_id, 20, true, 0xffffff,
             ox + 177 - fonts_.layout(def->display_id, 20, true).width / 2 - 2, 220);
        const auto desc = fonts_.wrap(def->xml.child("Description").text(), 14, false, 188);
        double dy = 220 + fonts_.layout(def->display_id, 20, true).height + 1;
        for (const auto& line : desc) {
          text(line, 14, false, 0xffffff, ox + 177 - fonts_.layout(line, 14, false).width / 2 - 2,
               dy);
          dy += fonts_.layout(line, 14, false).height - 4;
        }
        dy += 16;
        const int fame =
            data_.best_fame.contains(class_type_) ? data_.best_fame.at(class_type_) : 0;
        const int level =
            data_.best_level.contains(class_type_) ? data_.best_level.at(class_type_) : 0;
        const double field_height = fonts_.layout("Class Quests Completed", 14, true).height;
        text("Class Quests Completed", 14, true, 0xffffff,
             ox + 210 - fonts_.layout("Class Quests Completed", 14, true).width, dy + 6);
        ui_blit(image_, art_.image("skinstars" + std::to_string(star_count(fame))), ox + 228,
                dy + 6, true, true);
        dy += field_height + 11;
        text("Highest Level Achieved", 14, true, 0xffffff,
             ox + 210 - fonts_.layout("Highest Level Achieved", 14, true).width, dy);
        text(std::to_string(level), 16, true, 0xffffff, ox + 228, dy);
        dy += field_height + 5;
        text("Most Fame Achieved", 14, true, 0xffffff,
             ox + 210 - fonts_.layout("Most Fame Achieved", 14, true).width, dy);
        text(std::to_string(fame), 16, true, 0xeacc6c, ox + 228, dy);
        ui_blit(image_, art_.image("skinfame"),
                ox + 225 + fonts_.layout(std::to_string(fame), 16, true).width, dy - 7, true, true);
        dy += field_height + 5;
        if (const auto next = next_star_fame(fame, 0); next > 0) {
          dy += 12;
          text("Next Goal:", 14, true, 0xffffff,
               ox + 177 - fonts_.layout("Next Goal:", 14, true).width / 2 - 2, dy);
          const auto goal = "Earn " + std::to_string(next) + " Fame with a " + def->display_id;
          text(goal, 14, false, 0xffffff, ox + 177 - fonts_.layout(goal, 14, false).width / 2 - 2,
               dy + fonts_.layout("Next Goal:", 14, true).height);
        }
      }
    }
  }
  if (error_.visible()) {
    ui_rounded_rectangle(image_, 0, 0, width_ / scale_, 600, 1, 0x151515, .6, true);
    const auto at = error_.canvas_position();
    ui_blit(image_, error_.image(), at.x / scale_, at.y / scale_, false, true);
  }
  for (std::size_t i = 0; i < image_.pixels.size(); i += 4) {
    const unsigned alpha = image_.pixels[i + 3];
    if (alpha && alpha < 255) {
      for (unsigned c = 0; c < 3; ++c) {
        image_.pixels[i + c] = static_cast<unsigned char>(std::min(
            255U, (static_cast<unsigned>(image_.pixels[i + c]) * 255 + alpha / 2) / alpha));
      }
    }
  }
}
}
