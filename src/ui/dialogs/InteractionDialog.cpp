#include "realm/ui/dialogs/InteractionDialog.hpp"
#include <algorithm>
#include <cmath>

namespace realm {
bool InteractionDialog::open(const HudState::Social& panel, const Entity& player) {
  if (visible() || !panel.enabled || panel.id < 0 || panel.kind == Kind::none ||
      panel.kind == Kind::hall) {
    return false;
  }

  reset();
  kind_ = panel.kind;
  player_id_ = player.id;
  guild_ = player.profile.guild;
  if (kind_ == Kind::leave_guild) {
    value_ = player.profile.name;
    title_ = "Renounce Guild";
    note_ = "Are you sure you want to quit:\n" + guild_;
  } else if (kind_ == Kind::create_guild) {
    title_ = "Create a new Guild";
    note_ = "Maximum 20 characters. Letters and spaces only.\nCreation costs 1000 fame.";
  } else {
    // ChooseNameFrame: the cost lives on NameChangerPanel's buy button, not in the frame.
    title_ = "Choose a unique account name";
  }
  dirty_ = true;
  return true;
}

void InteractionDialog::reset() {
  kind_ = Kind::none;
  player_id_ = -1;
  guild_.clear();
  value_.clear();
  error_.clear();
  title_.clear();
  note_.clear();
  pending_ = false;
  pressed_ = -1;
  request_.reset();
  accepted_name_.reset();
  dirty_ = true;
}

void InteractionDialog::text(std::string_view input) {
  if (!wants_text()) {
    return;
  }
  const std::size_t limit = kind_ == Kind::create_guild ? 20 : 10;
  for (const unsigned char c : input) {
    if (value_.size() < limit && ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                                  (c == ' ' && kind_ == Kind::create_guild))) {
      value_ += static_cast<char>(c);
    }
  }
  error_.clear();
  dirty_ = true;
}

void InteractionDialog::backspace() {
  if (wants_text() && !value_.empty()) {
    value_.pop_back();
    error_.clear();
    dirty_ = true;
  }
}

void InteractionDialog::cancel() {
  if (!pending_) {
    reset();
  }
}

void InteractionDialog::submit() {
  if (!visible() || pending_) {
    return;
  }
  // ChooseNameFrame.onChoose sends whatever is typed, so only guild creation checks for blanks.
  if (kind_ == Kind::create_guild && value_.find_first_not_of(' ') == std::string::npos) {
    error_ = "Please enter a name.";
    dirty_ = true;
    return;
  }

  request_ = Request{kind_, value_};
  pending_ = true;
  error_.clear();
  dirty_ = true;
}

int InteractionDialog::hit(Vec2 point) const {
  if (HudInput::button_at(name_frame() ? name_cancel_ : cancel_button, point)) {
    return 0;
  }
  if (HudInput::button_at(name_frame() ? name_submit_ : submit_button, point)) {
    return 1;
  }
  return -1;
}

void InteractionDialog::pointer_down(Vec2 point) {
  pressed_ = pending_ ? -1 : hit(point);
}

void InteractionDialog::pointer_up(Vec2 point) {
  const int pressed = std::exchange(pressed_, -1);
  if (pressed < 0 || hit(point) != pressed) {
    return;
  }
  if (pressed == 0) {
    cancel();
  } else {
    submit();
  }
}

void InteractionDialog::receive(const net::Event& event) {
  if (event.id == flash::PacketId::MAPINFO || event.id == flash::PacketId::RECONNECT ||
      event.id == flash::PacketId::DEATH) {
    reset();
    return;
  }
  if (!pending_) {
    return;
  }
  bool matched = false;
  bool success = false;
  std::string message;
  if (kind_ == Kind::create_guild && event.id == flash::PacketId::GUILDRESULT) {
    const auto& result = std::get<net::incoming::GuildResult>(event.data);
    matched = true;
    success = result.success;
    message = result.text;
  } else if ((kind_ == Kind::choose_name || kind_ == Kind::change_name) &&
             event.id == flash::PacketId::NAMERESULT) {
    const auto& result = std::get<net::incoming::NameResult>(event.data);
    matched = true;
    success = result.success;
    message = result.text;
  } else if (kind_ == Kind::leave_guild && event.id == flash::PacketId::TEXT) {
    const auto& result = std::get<net::incoming::Text>(event.data);
    if (result.name == "*Error*") {
      matched = true;
      message = result.text;
    }
  }
  if (!matched) {
    return;
  }

  if (success) {
    const auto name = (kind_ == Kind::choose_name || kind_ == Kind::change_name)
                          ? std::optional<std::string>{value_}
                          : std::nullopt;
    reset();
    accepted_name_ = name;
  } else {
    pending_ = false;
    // ChooseNameFrame.setError shows the server's errorText as is.
    error_ = message.empty() && !name_frame() ? "The request was rejected." : message.substr(0, 512);
    dirty_ = true;
  }
}

void InteractionDialog::update(const World& world) {
  if (!visible()) {
    return;
  }

  const auto* player = world.entity(world.player_id());
  if (!player || player->id != player_id_) {
    reset();
    return;
  }
  if (kind_ == Kind::leave_guild && player->profile.guild != guild_) {
    reset();
  }

  if (kind_ == Kind::create_guild && !player->profile.guild.empty()) {
    reset();
  }
}

double InteractionDialog::scale(double width, double height) {
  return std::max(.01, std::min({height / 600.0, (width - 16) / InteractionDialog::width,
                                 (height - 16) / InteractionDialog::height}));
}

Vec2 InteractionDialog::position(double width, double height) {
  const double factor = scale(width, height);
  return {(width - InteractionDialog::width * factor) / 2,
          (height - InteractionDialog::height * factor) / 2};
}

Vec2 InteractionDialog::local(Vec2 point, double width, double height) {
  const auto origin = position(width, height);
  const double factor = scale(width, height);
  return {(point.x - origin.x) / factor, (point.y - origin.y) / factor};
}

void InteractionDialog::rectangle(int x, int y, int w, int h, std::uint32_t color) {
  if (image_.pixel_scale != 1) {
    ui_rectangle(image_, x, y, w, h, color);
    return;
  }
  for (int row = std::max(0, y); row < std::min(height, y + h); ++row) {
    for (int col = std::max(0, x); col < std::min(width, x + w); ++col) {
      const auto pixel_index = static_cast<std::size_t>(row * width + col) * 4;
      for (unsigned channel = 0; channel < 3; ++channel) {
        image_.pixels[pixel_index + channel] =
            static_cast<unsigned char>((color >> (16 - channel * 8)) & 255U);
      }
      image_.pixels[pixel_index + 3] = 255;
    }
  }
}

void InteractionDialog::paint(const Image& source, int x, int y) {
  if (image_.pixel_scale != 1) {
    ui_blit(image_, source, x, y);
    return;
  }
  for (int row = 0; row < source.height; ++row) {
    for (int col = 0; col < source.width; ++col) {
      const int px = x + col, py = y + row;
      if (px < 0 || py < 0 || px >= width || py >= height) {
        continue;
      }
      const auto source_index = static_cast<std::size_t>(row * source.width + col) * 4;
      const auto destination_index = static_cast<std::size_t>(py * width + px) * 4;
      const unsigned alpha = source.pixels[source_index + 3];
      if (!alpha) {
        continue;
      }
      for (unsigned channel = 0; channel < 3; ++channel) {
        image_.pixels[destination_index + channel] = static_cast<unsigned char>(
            (source.pixels[source_index + channel] * alpha +
             image_.pixels[destination_index + channel] * (255 - alpha) + 127) /
            255);
      }
    }
  }
}

void InteractionDialog::draw_text(const FontBook& fonts, std::string_view value, int size,
                                  bool bold, std::uint32_t color, int x, int y, int field) {
  const auto bitmap = fonts.rasterize(value, size, bold, true, color, 0, 0, 1, field);
  if (image_.pixel_scale != 1) {
    ui_blit_text(image_, bitmap, x + bitmap.offset.x, y + bitmap.offset.y);
  } else {
    paint(bitmap.image, x + static_cast<int>(bitmap.offset.x), y + static_cast<int>(bitmap.offset.y));
  }
}

// Flash ChooseNameFrame built on Frame and TextInputField: a 288x248 box with 4px cut corners
// and a 1px white outline, centered on the canvas. The canvas outside the box stays transparent.
void InteractionDialog::draw_name_frame(const FontBook& fonts) {
  constexpr int box_x = 76, box_y = 46, box_w = 288, box_h = 248;
  // Flash's frame origin (0,0) is 6px inside the box edge.
  constexpr int ox = box_x + 6, oy = box_y + 6;
  std::fill(image_.pixels.begin(), image_.pixels.end(), static_cast<unsigned char>(0));
  const auto cut_box = [&](int x, int y, int w, int h, std::uint32_t color, bool top_only) {
    for (int row = 0; row < h; ++row) {
      const int from_edge = top_only ? row : std::min(row, h - 1 - row);
      const int inset = std::max(0, 4 - from_edge);
      rectangle(x + inset, y + row, w - 2 * inset, 1, color);
    }
  };
  cut_box(box_x, box_y, box_w, box_h, 0xffffff, false);
  cut_box(box_x + 1, box_y + 1, box_w - 2, box_h - 2, 0x363636, false);
  // Frame title bar: 32px tall, fill #4D4D4D, top corners cut.
  cut_box(box_x + 1, box_y + 1, box_w - 2, 31, 0x4d4d4d, true);

  // Frame.titleText_: 12px, not bold, #B3B3B3, x=5.
  draw_text(fonts, title_, 12, false, 0xb3b3b3, ox + 5, oy);
  // TextInputField at frame (17,40): bold 18px #B3B3B3 label, then the 238x30 box at field (0,30)
  // with a 2px #454545 stroke and #333333 fill.
  draw_text(fonts, "Name", 18, true, 0xb3b3b3, ox + 17, oy + 40);
  rectangle(ox + 16, oy + 69, 240, 32, 0x454545);
  rectangle(ox + 18, oy + 71, 236, 28, 0x333333);
  draw_text(fonts, value_, 20, false, 0xb3b3b3, ox + 23, oy + 70, 238);
  // TextInputField.errorText_: 12px #FC8642 at field (0,62).
  draw_text(fonts, error_, 12, false, 0xfc8642, ox + 17, oy + 102);

  // Frame.addPlainText lines, 12px white at frame x=17.
  draw_text(fonts, "Maximum 10 characters", 12, false, 0xffffff, ox + 17, oy + 122);
  draw_text(fonts, "No numbers, spaces or punctuation", 12, false, 0xffffff, ox + 17, oy + 142);
  draw_text(fonts, "Racism or profanity gets you banned", 12, false, 0xffffff, ox + 17, oy + 162);

  // Frame buttons: 18px bold text at y=h-52. Cancel at x=109, Choose right-aligned 26px from the
  // right edge. Both turn #B3B3B3 while pending, like Frame.disable().
  const std::uint32_t button_color = pending_ ? 0xb3b3b3 : 0xffffff;
  const int button_y = oy + 196;
  const double submit_width = fonts.measure("Choose", 18, true);
  name_cancel_ = {static_cast<double>(ox + 109), static_cast<double>(button_y),
                  fonts.measure("Cancel", 18, true), 24};
  name_submit_ = {ox + 262 - submit_width, static_cast<double>(button_y), submit_width, 24};
  draw_text(fonts, "Cancel", 18, true, button_color, ox + 109, button_y);
  draw_text(fonts, "Choose", 18, true, button_color, static_cast<int>(name_submit_.x), button_y);
}

bool InteractionDialog::render(const FontBook& fonts) {
  if (image_.pixel_scale != fonts.pixel_scale()) {
    image_ = ui_image(width, height, fonts.pixel_scale());
    dirty_ = true;
  }
  if (!visible() || !dirty_) {
    return false;
  }

  if (name_frame()) {
    draw_name_frame(fonts);
    dirty_ = false;
    ++rebuilds_;
    return true;
  }

  rectangle(0, 0, width, height, 0xffffff);
  rectangle(1, 1, width - 2, height - 2, 0x363636);
  rectangle(1, 1, width - 2, 34, 0x4d4d4d);
  const auto text = [&](std::string_view value, int size, bool bold, std::uint32_t color, int x,
                        int y, int field = 0) {
    draw_text(fonts, value, size, bold, color, x, y, field);
  };
  text(title_, 18, true, 0xffffff, 16, 5, width - 32);

  if (kind_ != Kind::leave_guild) {
    text(kind_ == Kind::create_guild ? "Guild Name" : "Name", 18, true, 0xb3b3b3, 20, 50);
    rectangle(20, 82, 400, 34, 0x454545);
    rectangle(21, 83, 398, 32, 0x222222);
    text(value_, 20, false, 0xffffff, 26, 84, 388);
  }
  const auto paragraph = [&](std::string_view value, int y, std::uint32_t color, int max_lines) {
    const auto lines = fonts.wrap(value, 14, false, width - 40);
    int count = 0;
    for (const auto& line : lines) {
      if (count >= max_lines) {
        break;
      }
      text(line, 14, false, color, 20, y + count * 18, width - 40);
      ++count;
    }
  };
  paragraph(note_, kind_ == Kind::leave_guild ? 62 : 132, 0xb3b3b3, 4);
  paragraph(error_, 218, 0xff7777, 3);

  if (pending_) {
    text("Waiting for the server...", 14, false, 0xffffff, 20, 260);
  }
  for (const auto area : {cancel_button, submit_button}) {
    for (int y = 0; y < 28; ++y) {
      const int cut = std::max(0, 4 - std::min(y, 27 - y));
      rectangle(static_cast<int>(area.x) + cut, static_cast<int>(area.y) + y,
                static_cast<int>(area.width) - 2 * cut, 1, pending_ ? 0x777777 : 0xb3b3b3);
    }
  }

  const std::string left = kind_ == Kind::leave_guild ? "No, I'll stay" : "Cancel";
  const std::string right = kind_ == Kind::leave_guild    ? "Yes, I'll quit"
                            : kind_ == Kind::create_guild ? "Create"
                                                          : "Choose";
  text(left, 16, true, 0x222222, 29, 294);
  text(right, 16, true, 0x222222, 229, 294);
  dirty_ = false;
  ++rebuilds_;
  return true;
}
}
