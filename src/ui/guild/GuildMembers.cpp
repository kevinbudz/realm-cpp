#include "realm/ui/guild/GuildMembers.hpp"
#include "realm/ui/GraphicsUtil.hpp"
#include <algorithm>
#include <cmath>

namespace realm {
namespace {
bool contains(HudRect area, Vec2 p) {
  return p.x >= area.x && p.y >= area.y && p.x < area.x + area.width && p.y < area.y + area.height;
}
}

GuildMembers::GuildMembers(const HudArt& art) : art_(art) {}

std::string GuildMembers::rank_name(int rank) {
  switch (rank) {
    case 0:
      return "Initiate";
    case 10:
      return "Member";
    case 20:
      return "Officer";
    case 30:
      return "Leader";
    case 40:
      return "Founder";
    default:
      return "Unknown";
  }
}

int GuildMembers::promoted(int rank) {
  return rank == 0 ? 10 : rank == 10 ? 20 : rank == 20 ? 30 : 40;
}

int GuildMembers::demoted(int rank) {
  return rank == 40 ? 30 : rank == 30 ? 20 : rank == 20 ? 10 : 0;
}

bool GuildMembers::allowed(int actor, int target, Action action) {
  if (action == Action::remove) {
    return actor >= 20 && target < actor;
  }
  const int next = action == Action::promote ? promoted(target) : demoted(target);
  if (next == target) {
    return false;
  }
  return (actor == 40 && target < 40 && next < 40) || (actor == 30 && target < 30 && next <= 30) ||
         (actor == 20 && target < 20 && next < 20);
}

void GuildMembers::open(std::string host, std::uint16_t port, net::FormFields credentials,
                        std::string guild, std::string own, int rank) {
  close();
  host_ = std::move(host);
  port_ = port;
  credentials_ = std::move(credentials);
  guild_ = std::move(guild);
  own_ = std::move(own);
  rank_ = rank;
  load();
}

void GuildMembers::close() {
  ++generation_;
  state_ = State::closed;
  queued_ = false;
  credentials_.clear();
  pending_.reset();
  outgoing_.reset();
  list_ = {};
  dialog_.close();
  focus_lost();
  // Keep async future alive: close/map teardown never waits for HTTP.
}

void GuildMembers::show(GuildMemberList list, std::string own, int rank) {
  ++generation_;
  queued_ = false;
  list_ = std::move(list);
  guild_ = list_.name;
  own_ = std::move(own);
  rank_ = rank;
  state_ = State::list;
  pending_.reset();
  outgoing_.reset();
  dialog_.close();
  scrollbar_.set_pos(0);
  dirty_ = true;
  focus_lost();
}

void GuildMembers::permissions(std::string_view guild, int rank) {
  if (!visible()) {
    return;
  }
  if (guild != guild_) {
    close();
    return;
  }
  if (rank_ == rank) {
    return;
  }
  rank_ = rank;
  focus_lost();
  dirty_ = true;
  if (state_ == State::confirm || outgoing_) {
    // Confirmation is cancelled for any authoritative permission change.
    dialog_.close();
    pending_.reset();
    outgoing_.reset();
    state_ = State::list;
  }
}

void GuildMembers::load() {
  state_ = State::loading;
  dialog_.close();
  pending_.reset();
  pending_seconds_ = 0;
  dirty_ = true;
  focus_lost();
  if (worker_.valid()) {
    queued_ = true;
    return;
  }
  auto fields = credentials_;
  fields.emplace_back("num", "50");
  fields.emplace_back("offset", "0");
  worker_generation_ = generation_;
  queued_ = false;
  worker_ =
      std::async(std::launch::async, [host = host_, port = port_, fields = std::move(fields)] {
        return net::http_post(host, port, "/guild/listMembers", fields);
      });
}

void GuildMembers::fail(std::string text, bool load_error) {
  load_error_ = load_error;
  state_ = State::error;
  pending_.reset();
  outgoing_.reset();
  dialog_.show({std::move(text),
                "Error",
                {load_error ? "Retry" : "Ok",
                 load_error ? std::optional<std::string>("Close") : std::nullopt}});
  dirty_ = true;
  focus_lost();
}

void GuildMembers::poll(double dt) {
  if (state_ == State::pending) {
    pending_seconds_ += std::max(0.0, dt);
    if (pending_seconds_ >= 10) {
      fail(
          "The server has not confirmed this guild action. The list will be refreshed; check the "
          "member before trying "
          "again.",
          false);
    }
  }
  if (!worker_.valid() || worker_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
    return;
  }
  GuildMemberList result;
  std::string error;
  try {
    result = parse_guild_members(worker_.get());
  } catch (const std::exception& e) {
    error = e.what();
  }
  if (queued_) {
    load();
    return;
  }
  if (worker_generation_ != generation_ || !visible()) {
    return;
  }
  if (!error.empty()) {
    fail(std::move(error), true);
    return;
  }
  if (result.name.empty()) {
    result.name = guild_;
  }
  list_ = std::move(result);
  state_ = State::list;
  scrollbar_.set_pos(0);
  dirty_ = true;
}

void GuildMembers::receive(const net::incoming::Text& text, std::int32_t own_id) {
  if (state_ != State::pending || !pending_) {
    return;
  }
  if (text.name == "@" + guild_ && text.object_id == own_id && text.text == expected_) {
    load();
    return;
  }
  // No request IDs in these packets. Match only errors this handler emits,
  // never arbitrary combat/chat errors, and leave the actual chat intact.
  if (text.name != "*Error*") {
    return;
  }
  const bool remove = pending_->action == Action::remove;
  const bool relevant =
      remove ? text.text == "Player not found" || text.text == "Guild not found." ||
                   text.text == "Can't remove member. Insufficient privileges."
             : text.text == "A player with that name does not exist." ||
                   text.text == "No permission" || text.text == "Failed to change rank." ||
                   text.text == "Player is already a " + rank_name(pending_->rank);
  if (relevant) {
    fail(text.text, false);
  }
}

int GuildMembers::hit(Vec2 p) const {
  const auto pos = local(p);
  if (contains(continue_button_, pos)) {
    return -2;
  }
  if (state_ != State::list || pos.y < 110 || pos.y >= 540 || pos.x < 10 || pos.x >= 766) {
    return -1;
  }
  const double row_y = pos.y - 110 + scrollbar_.pos() * std::max(0.0, total_height_ - 400);
  const int row = static_cast<int>(std::floor(row_y / 32));
  if (row < 0 || static_cast<std::size_t>(row) >= list_.members.size()) {
    return -1;
  }
  const auto& member = list_.members[static_cast<std::size_t>(row)];
  const double y = row_y - row * 32;
  for (int action_index = 0; action_index < 3; ++action_index) {
    const HudRect area =
        action_index == 0 ? HudRect{678, 10, 16, 12}
        : action_index == 1
            ? HudRect{708, 10, 16, 12}
            : HudRect{740, 4, icons_[6].width > 0 ? static_cast<double>(icons_[6].width) : 24, 24};
    if (allowed(rank_, member.rank, static_cast<Action>(action_index)) &&
        contains(area, {pos.x, y})) {
      return row * 3 + action_index;
    }
  }
  return -1;
}

void GuildMembers::confirm(int index) {
  const auto& member = list_.members.at(static_cast<std::size_t>(index / 3));
  const auto action = static_cast<Action>(index % 3);
  if (!allowed(rank_, member.rank, action)) {
    return;
  }
  pending_ = Request{action, member.name,
                     action == Action::promote  ? promoted(member.rank)
                     : action == Action::demote ? demoted(member.rank)
                                                : -1};
  const std::string verb = action == Action::promote  ? "Promote"
                           : action == Action::demote ? "Demote"
                                                      : "Remove";
  const std::string lower = action == Action::promote  ? "promote"
                            : action == Action::demote ? "demote"
                                                       : "remove";
  dialog_.show({"Are you sure you want to " + lower + " " + member.name +
                    (action == Action::remove ? " from the guild?"
                                              : " to " + rank_name(pending_->rank) + "?"),
                verb + " " + member.name,
                {"Cancel", verb}});
  state_ = State::confirm;
  focus_lost();
  dirty_ = true;
}

void GuildMembers::pointer_move(Vec2 p) {
  if (dialog_.visible()) {
    dialog_.pointer_move(p);
    return;
  }
  const int index = hit(p);
  if (index != hover_) {
    hover_ = index;
    dirty_ = true;
  }
  const auto pos = local(p);
  const auto before = scrollbar_.pos();
  if (state_ == State::list && has_scroll_ && scrollbar_.move({pos.x - 780, pos.y - 104})) {
    dirty_ = true;
  }
  if (before != scrollbar_.pos()) {
    dirty_ = true;
  }
}

void GuildMembers::pointer_down(Vec2 p) {
  if (dialog_.visible()) {
    dialog_.pointer_down(p);
    return;
  }
  pointer_move(p);
  pressed_ = hover_;
  const auto pos = local(p);
  if (state_ == State::list && has_scroll_) {
    scrollbar_.down({pos.x - 780, pos.y - 104});
  }
  dirty_ = true;
}

void GuildMembers::pointer_up(Vec2 p) {
  if (dialog_.visible()) {
    if (auto button = dialog_.pointer_up(p)) {
      if (state_ == State::confirm) {
        dialog_.close();
        if (*button == 0) {
          pending_.reset();
          state_ = State::list;
        } else if (pending_) {
          outgoing_ = pending_;
          state_ = State::pending;
          pending_seconds_ = 0;
          expected_ = pending_->name +
                      (pending_->action == Action::remove
                           ? " has been kicked from the guild by " + own_
                           : (pending_->action == Action::promote ? " has been promoted to "
                                                                  : " has been demoted to ") +
                                 rank_name(pending_->rank) + ".");
        }
      } else if (state_ == State::error) {
        if (load_error_ && *button == 1) {
          close();
        } else {
          load();
        }
      }
      dirty_ = true;
    }
    return;
  }
  pointer_move(p);
  scrollbar_.up();
  const int index = std::exchange(pressed_, -1);
  if (index == hover_) {
    if (index == -2) {
      close();
    } else if (index >= 0) {
      confirm(index);
    }
  }
}

void GuildMembers::wheel(double steps) {
  if (state_ == State::list && has_scroll_) {
    scrollbar_.wheel(steps);
    dirty_ = true;
  }
}

void GuildMembers::focus_lost() {
  pressed_ = hover_ = -1;
  scrollbar_.focus_lost();
  dialog_.focus_lost();
  dirty_ = true;
}

bool GuildMembers::update(const FontBook& fonts, double width, double height, double dt) {
  if (!visible() || width <= 0 || height <= 0) {
    return false;
  }
  if (stage_width_ != width || stage_height_ != height ||
      image_.pixel_scale != fonts.pixel_scale()) {
    stage_width_ = width;
    stage_height_ = height;
    scale_ = height / 600;
    position_ = {(width - 800 * scale_) / 2, 0};
    focus_lost();
    dirty_ = true;
  }
  if (state_ == State::list && has_scroll_) {
    const auto before = scrollbar_.pos();
    scrollbar_.tick(dt);
    if (before != scrollbar_.pos()) {
      dirty_ = true;
    }
  }
  if (dialog_.visible() && dialog_.update(fonts, width, height)) {
    dirty_ = true;
  }
  if (!dirty_) {
    return false;
  }
  if (icons_[0].width == 0) {
    for (int i = 0; i < 7; ++i) {
      icons_[static_cast<std::size_t>(i)] = art_.image("guildicon" + std::to_string(i));
    }
  }
  const double logical_width = width / scale_;
  const double x = (logical_width - 800) / 2;
  image_ = ui_image(logical_width, 600, scale_);
  ui_menu_backdrop(image_, logical_width);
  const auto text = [&](std::string_view value, int size, bool bold, std::uint32_t color, double px,
                        double py, double align = 0, bool clip = false) {
    const auto bitmap = fonts.rasterize(value, size, bold, true, color);
    ui_blit_text(image_, bitmap, x + px - align * bitmap.width + bitmap.offset.x,
                 py + bitmap.offset.y, false, true, 1, clip ? x + 10 : 0, clip ? 110 : 0,
                 clip ? 756 : logical_width, clip ? 430 : 600);
  };
  // Source TitleMenuOption has a broader, half-alpha shadow; keep the same
  // font, coordinates and hover color. Glyph edges are native at resize.
  const auto continue_text = fonts.rasterize("continue", 36, true);
  continue_button_ = {400 - continue_text.width / 2, 520, continue_text.width,
                      continue_text.height};
  text("continue", 36, true, hover_ == -2 ? 0xffdc85 : 0xffffff, 400, 520, .5);
  if (state_ == State::loading || state_ == State::pending) {
    const auto bitmap = fonts.rasterize("Loading...", 22, true);
    text("Loading...", 22, true, 0xb3b3b3, 400, 300 - bitmap.height / 2, .5);
  } else if (state_ == State::list || state_ == State::confirm) {
    text(list_.name, 32, true, 0xb3b3b3, 400, 24, .5);
    const auto bitmap = fonts.rasterize(std::to_string(list_.fame), 22);
    text(std::to_string(list_.fame), 22, false, 0xffffff, 768, 16 - bitmap.height / 2, 1);
    ui_blit(image_, icons_[0], x + 760, 16 - icons_[0].height / 2.0, true, true);
    ui_rectangle(image_, x, 99, 800, 2, 0x545454, true);
    const auto slots = std::to_string(50 - static_cast<int>(list_.members.size())) + " open slots";
    // Source listSprite bounds start at -4: its first 40px fame icon
    // is centered in a 32px row. Scrollbar uses full bounds height.
    total_height_ = static_cast<double>(list_.members.size()) * 32 + fonts.line_advance(22) + 4 +
                    (list_.members.empty() ? 0 : 4);
    has_scroll_ = total_height_ > 400;
    scrollbar_.indicator(400, total_height_, false);
    const double offset = scrollbar_.pos() * std::max(0.0, total_height_ - 400);
    for (std::size_t i = 0; i < list_.members.size(); ++i) {
      const auto& member = list_.members[i];
      const double y = 110 + static_cast<double>(i) * 32 - offset;
      if (y + 40 < 110 || y - 8 > 540) {
        continue;
      }
      const std::uint32_t color = member.name == own_ ? 0xfcc219 : 0xb3b3b3;
      const double ty = y + 16 - fonts.rasterize("Ag", 22).height / 2;
      text(std::to_string(i + 1) + ".", 22, true, color, 70, ty, 1, true);
      text(member.name, 22, true, color, 110, ty, 0, true);
      text(std::to_string(member.fame), 22, false, color, 418, ty, 1, true);
      text(rank_name(member.rank), 22, false, color, 590, ty, 0, true);
      const auto icon = [&](int n, double px) {
        const auto& im = icons_[static_cast<std::size_t>(n)];
        ui_blit(image_, im, x + px, y + 16 - im.height / 2.0, true, true, 1, x + 10, 110, 756, 430);
      };
      icon(0, 410);
      if (member.rank >= 0 && member.rank <= 40 && member.rank % 10 == 0) {
        icon(1 + member.rank / 10, 558);
      }
      for (int action_index = 0; action_index < 3; ++action_index) {
        if (allowed(rank_, member.rank, static_cast<Action>(action_index))) {
          const auto tint =
              hover_ == static_cast<int>(i) * 3 + action_index ? 0xffdc85U : 0xffffffU;
          if (action_index == 2) {
            auto im = icons_[6];
            if (tint != 0xffffff) {
              for (std::size_t p = 0; p < im.pixels.size(); p += 4) {
                im.pixels[p + 1] = static_cast<unsigned char>(im.pixels[p + 1] * 220 / 255);
                im.pixels[p + 2] = static_cast<unsigned char>(im.pixels[p + 2] * 133 / 255);
              }
            }
            ui_blit(image_, im, x + 740, y + 16 - im.height / 2.0, true, true, 1, x + 10, 110, 756,
                    430);
          } else {
            auto arrow = ui_image(16, 12, scale_);
            const std::array<Vec2, 3> vertices =
                action_index == 0 ? std::array<Vec2, 3>{{{0, 12}, {16, 12}, {8, 0}}}
                                  : std::array<Vec2, 3>{{{0, 0}, {16, 0}, {8, 12}}};
            ui_path(arrow, vertices, tint);
            ui_blit(image_, arrow, x + (action_index == 0 ? 678 : 708), y + 10, true, true, 1,
                    x + 10, 110, 756, 430);
          }
        }
      }
    }
    text(slots, 22, false, 0xb3b3b3, 388,
         110 + static_cast<double>(list_.members.size()) * 32 - offset, .5, true);
    if (has_scroll_) {
      scrollbar_.draw(image_, {x + 780, 104});
    }
  }
  if (dialog_.visible()) {
    const auto at = dialog_.canvas_position();
    ui_blit(image_, dialog_.image(), at.x / scale_, at.y / scale_, false, true);
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
  dirty_ = false;
  return true;
}
}
