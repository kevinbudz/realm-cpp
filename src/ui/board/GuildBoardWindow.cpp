#include "realm/ui/board/GuildBoardWindow.hpp"
#include "realm/appengine/SavedCharactersList.hpp"
#include "realm/util/GraphicsUtil.hpp"
#include <algorithm>
#include <cmath>
namespace realm {
namespace {
std::size_t previous(std::string_view s, std::size_t at) {
  if (at) {
    --at;
    while (at && (static_cast<unsigned char>(s[at]) & 0xc0) == 0x80) --at;
  }
  return at;
}
std::size_t next(std::string_view s, std::size_t at) {
  if (at < s.size()) {
    ++at;
    while (at < s.size() && (static_cast<unsigned char>(s[at]) & 0xc0) == 0x80) ++at;
  }
  return at;
}
bool contains(HudRect r, Vec2 p) { return p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height; }
bool url_character(unsigned char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
         std::string_view(":#@%/;$()~_?+-=\\.&").find(static_cast<char>(c)) != std::string_view::npos;
}
}
void GuildBoard::open(std::string host, std::uint16_t port, net::FormFields credentials, int rank) {
  close();
  host_ = std::move(host);
  port_ = port;
  credentials_ = std::move(credentials);
  can_edit_ = rank >= 20;
  start(false);
}
void GuildBoard::show(std::string text, int rank) {
  ++generation_;
  saved_ = std::move(text);
  draft_ = saved_;
  can_edit_ = rank >= 20;
  state_ = State::view;
  dialog_.close();
  scrollbar_.set_pos(0);
  layout_dirty_ = dirty_ = true;
  focus_lost();
}
void GuildBoard::close() {
  ++generation_;
  state_ = State::closed;
  queued_ = false;
  dialog_.close();
  credentials_.clear();
  saved_.clear();
  draft_.clear();
  lines_.clear();
  links_.clear();
  urls_.clear();
  link_.reset();
  focus_lost();
  // Keep the future until poll drains it. Discarding an async future here
  // would block map teardown for the entire HTTP deadline.
}
void GuildBoard::permissions(int rank) {
  if (can_edit_ == (rank >= 20)) return;
  can_edit_ = rank >= 20;
  dirty_ = true;
  if (!can_edit_ && state_ == State::edit) {
    state_ = State::view;
    draft_ = saved_;
    layout_dirty_ = true;
    focus_lost();
  }
}
void GuildBoard::start(bool save) {
  state_ = save ? State::saving : State::loading;
  retry_save_ = save;
  request_save_ = save;
  dialog_.show({save ? "Saving..." : "Loading...", std::nullopt, {}});
  focus_lost();
  dirty_ = true;
  if (request_.valid()) {
    queued_ = true;
    return;
  }
  auto fields = credentials_;
  // AppServer parses the form and then UrlDecode(board) a second time.
  // Protect literal '+' and '%xx' through both decoding layers.
  if (save) fields.emplace_back("board", net::url_encode(draft_));
  request_generation_ = generation_;
  queued_ = false;
  request_ = std::async(std::launch::async, [host = host_, port = port_, save, fields = std::move(fields)] {
    return net::http_post(host, port, save ? "/guild/setBoard" : "/guild/getBoard", fields);
  });
}
void GuildBoard::poll() {
  if (!request_.valid() || request_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
  std::string response, error;
  try {
    response = request_.get();
    if (auto e = app_error(response)) error = *e;
  } catch (const std::exception& e) {
    error = e.what();
  }
  if (queued_) {
    start(request_save_);
    return;
  }
  if (request_generation_ != generation_ || !visible()) return;
  if (error.empty()) {
    saved_ = std::move(response);
    draft_ = saved_;
    state_ = State::view;
    dialog_.close();
    scrollbar_.set_pos(0);
    layout_dirty_ = dirty_ = true;
  } else {
    state_ = State::error;
    dialog_.show({error, retry_save_ ? "Save Error" : "Load Error", {"Retry", retry_save_ ? "Back" : "Close"}});
    dirty_ = true;
  }
}
void GuildBoard::edit() {
  if (!can_edit_) return;
  draft_ = saved_;
  state_ = State::edit;
  caret_ = anchor_ = 0;
  focused_ = false;
  layout_dirty_ = dirty_ = true;
  scrollbar_.set_pos(0);
  focus_lost();
}
void GuildBoard::layout() {
  if (!fonts_) return;
  lines_.clear();
  const auto& value = state_ == State::edit ? draft_ : saved_;
  std::size_t start = 0;
  for (auto& line : fonts_->wrap(value, 16, false, 400)) {
    // wrap retains trailing spaces and strips paragraph delimiters.
    lines_.push_back({line, start});
    start += line.size();
    if (start < value.size() && (value[start] == '\n' || value[start] == '\r')) {
      if (value[start] == '\r' && start + 1 < value.size() && value[start + 1] == '\n') ++start;
      ++start;
    }
  }
  if (lines_.empty()) lines_.push_back({"", 0});
  line_height_ = fonts_->line_advance(16);
  text_height_ = fonts_->rasterize("Ag", 16).height + (static_cast<double>(lines_.size()) - 1) * line_height_;
  width_ = state_ == State::edit || text_height_ > 400 ? 426 : 400;
  height_ = 400 + fonts_->rasterize("Close", 14, true).height - 4 + 8 + 8;
  scrollbar_.indicator(400, text_height_, false);
  layout_dirty_ = false;
  // A linear scan of the URL grammar avoids recursive regex matching
  // on arbitrarily long board links. Cache ranges until content changes.
  urls_.clear();
  if (state_ == State::view)
    for (std::size_t at = 0; at < saved_.size();) {
      const auto tail = std::string_view(saved_).substr(at);
      const std::size_t prefix = tail.starts_with("https:")  ? 6
                                 : tail.starts_with("http:") ? 5
                                 : tail.starts_with("ftp:")  ? 4
                                                             : 0;
      if (!prefix) {
        ++at;
        continue;
      }
      auto end = at + prefix;
      bool slashes = false;
      while (end + 1 < saved_.size() &&
             ((saved_[end] == '/' && saved_[end + 1] == '/') || (saved_[end] == '\\' && saved_[end + 1] == '\\'))) {
        end += 2;
        slashes = true;
      }
      if (!slashes) {
        ++at;
        continue;
      }
      while (end < saved_.size() && url_character(static_cast<unsigned char>(saved_[end]))) ++end;
      urls_.push_back({at, end});
      at = end;
    }
}
int GuildBoard::hit(Vec2 p) const {
  const auto q = local(p);
  for (int i = 0; i < 2; ++i)
    if ((i == 1 || state_ == State::edit || can_edit_) && contains(button(i), q)) return i;
  return -1;
}
std::size_t GuildBoard::caret_at(Vec2 p) const {
  if (!fonts_ || lines_.empty()) return 0;
  const double offset = text_offset();
  const auto row = static_cast<std::size_t>(
      std::clamp(std::floor((p.y + offset - 2) / line_height_), 0.0, static_cast<double>(lines_.size() - 1)));
  const auto& line = lines_[row];
  std::size_t at = 0;
  while (at < line.text.size()) {
    const auto end = next(line.text, at);
    if (p.x < 2 + (fonts_->measure(line.text.substr(0, at), 16) + fonts_->measure(line.text.substr(0, end), 16)) / 2)
      break;
    at = end;
  }
  return std::min(draft_.size(), line.start + at);
}
void GuildBoard::pointer_move(Vec2 p) {
  if (transient()) {
    dialog_.pointer_move(p);
    return;
  }
  const int h = hit(p);
  if (h != hover_) {
    hover_ = h;
    dirty_ = true;
  }
  const auto q = local(p);
  const double before = scrollbar_.pos();
  if (width_ == 426 && scrollbar_.move({q.x - 406, q.y})) dirty_ = true;
  if (selecting_) {
    caret_ = caret_at(q);
    dirty_ = true;
  }
  if (before != scrollbar_.pos()) dirty_ = true;
}
void GuildBoard::pointer_down(Vec2 p, bool shift) {
  if (transient()) {
    dialog_.pointer_down(p);
    return;
  }
  pointer_move(p);
  pressed_ = hover_;
  const auto q = local(p);
  pressed_link_.reset();
  if (state_ == State::view)
    for (const auto& link : links_)
      if (contains(link.rect, q)) pressed_link_ = {{link.start, link.end}};
  if (width_ == 426) {
    scrollbar_.down({q.x - 406, q.y});
    dirty_ = true;
  }
  if (state_ == State::edit) {
    focused_ = contains({0, 0, 400, 400}, q);
    selecting_ = focused_;
    if (focused_) {
      caret_ = caret_at(q);
      if (!shift) anchor_ = caret_;
      dirty_ = true;
    }
  }
}
void GuildBoard::pointer_up(Vec2 p) {
  if (transient()) {
    if (const auto action = dialog_.pointer_up(p)) {
      if (*action == 0)
        start(retry_save_);
      else if (retry_save_) {
        dialog_.close();
        state_ = State::edit;
        focused_ = false;
        layout_dirty_ = dirty_ = true;
      } else
        close();
    }
    return;
  }
  pointer_move(p);
  scrollbar_.up();
  selecting_ = false;
  const auto pressed = std::exchange(pressed_, -1);
  if (pressed >= 0 && pressed == hover_) {
    if (state_ == State::view) {
      if (pressed == 0)
        edit();
      else
        close();
    } else if (state_ == State::edit) {
      if (pressed == 0) {
        state_ = State::view;
        draft_ = saved_;
        scrollbar_.set_pos(0);
        layout_dirty_ = dirty_ = true;
        focus_lost();
      } else if (can_edit_)
        start(true);
    }
  } else if (state_ == State::view)
    for (const auto& link : links_)
      if (pressed_link_ == std::pair{link.start, link.end} && contains(link.rect, local(p))) {
        link_ = saved_.substr(link.start, link.end - link.start);
        break;
      }
  pressed_link_.reset();
}
void GuildBoard::wheel(double steps) {
  if (!transient() && width_ == 426) {
    const auto before = scrollbar_.pos();
    scrollbar_.wheel(steps);
    if (before != scrollbar_.pos()) dirty_ = true;
  }
}
void GuildBoard::focus_lost() {
  pressed_ = hover_ = -1;
  pressed_link_.reset();
  focused_ = selecting_ = false;
  scrollbar_.focus_lost();
  dialog_.focus_lost();
  dirty_ = true;
}
std::string GuildBoard::selection() const {
  return state_ == State::edit
             ? draft_.substr(std::min(caret_, anchor_), std::max(caret_, anchor_) - std::min(caret_, anchor_))
             : "";
}
void GuildBoard::cut() {
  if (wants_text() && caret_ != anchor_) insert("");
}
void GuildBoard::insert(std::string_view text) {
  if (!wants_text()) return;
  const auto start = std::min(caret_, anchor_), end = std::max(caret_, anchor_);
  std::string value(text);
  value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
  draft_.replace(start, end - start, value);
  caret_ = anchor_ = start + value.size();
  layout_dirty_ = dirty_ = true;
  layout();
  ensure_caret();
}
void GuildBoard::ensure_caret() {
  if (lines_.empty() || text_height_ <= 400) return;
  std::size_t row = 0;
  for (std::size_t i = 0; i < lines_.size(); ++i)
    if (caret_ >= lines_[i].start) row = i;
  const auto visible = static_cast<std::size_t>(std::floor(396 / line_height_));
  const auto rows = lines_.size() > visible ? lines_.size() - visible : 0;
  if (!rows) return;
  const auto first = static_cast<std::size_t>(std::floor(scrollbar_.pos() * static_cast<double>(rows) + .00001));
  if (row < first)
    scrollbar_.set_pos(static_cast<double>(row) / static_cast<double>(rows));
  else if (row >= first + visible)
    scrollbar_.set_pos(static_cast<double>(row - visible + 1) / static_cast<double>(rows));
}
double GuildBoard::text_offset() const {
  if (state_ != State::edit) return scrollbar_.pos() * std::max(0.0, text_height_ - 400);
  const auto visible = static_cast<std::size_t>(std::floor(396 / line_height_));
  const auto rows = lines_.size() > visible ? lines_.size() - visible : 0;
  return std::floor(scrollbar_.pos() * static_cast<double>(rows) + .00001) * line_height_;
}
void GuildBoard::key(Key key, bool shift, bool control) {
  if (!wants_text()) return;
  if (key == Key::select_all) {
    anchor_ = 0;
    caret_ = draft_.size();
    dirty_ = true;
    return;
  }
  if (key == Key::enter) {
    insert("\n");
    return;
  }
  if (key == Key::backspace || key == Key::del) {
    if (caret_ == anchor_) {
      if (key == Key::backspace)
        anchor_ = previous(draft_, caret_);
      else
        anchor_ = next(draft_, caret_);
    }
    insert("");
    return;
  }
  if (key == Key::left) caret_ = !shift && caret_ != anchor_ ? std::min(caret_, anchor_) : previous(draft_, caret_);
  if (key == Key::right) caret_ = !shift && caret_ != anchor_ ? std::max(caret_, anchor_) : next(draft_, caret_);
  if (key == Key::home)
    caret_ =
        control
            ? 0
            : (caret_ == 0 || draft_.rfind('\n', caret_ - 1) == draft_.npos ? 0 : draft_.rfind('\n', caret_ - 1) + 1);
  if (key == Key::end)
    caret_ = control || draft_.find('\n', caret_) == draft_.npos ? draft_.size() : draft_.find('\n', caret_);
  if (key == Key::up || key == Key::down) {
    std::size_t row = 0;
    for (std::size_t i = 0; i < lines_.size(); ++i)
      if (caret_ >= lines_[i].start) row = i;
    const double x = 2 + fonts_->measure(lines_[row].text.substr(0, caret_ - lines_[row].start), 16);
    const auto target = key == Key::up ? (row > 0 ? row - 1 : 0) : std::min(row + 1, lines_.size() - 1);
    caret_ = caret_at({x, static_cast<double>(target) * line_height_ + 3 - text_offset()});
  }
  if (!shift) anchor_ = caret_;
  ensure_caret();
  dirty_ = true;
}
bool GuildBoard::update(const FontBook& fonts, double stage_width, double stage_height, double dt) {
  if (!visible() || stage_height <= 0 || stage_width <= 0) return false;
  fonts_ = &fonts;
  if (stage_width_ != stage_width || stage_height_ != stage_height) {
    stage_width_ = stage_width;
    stage_height_ = stage_height;
    scale_ = stage_height / 600;
    focus_lost();
    layout_dirty_ = true;
  }
  if (transient()) return dialog_.update(fonts, stage_width, stage_height);
  if (image_.pixel_scale != fonts.pixel_scale()) dirty_ = true;
  if (layout_dirty_) layout();
  position_ = {(stage_width - width_ * scale_) / 2, (stage_height - height_ * scale_) / 2};
  const auto before = scrollbar_.pos();
  scrollbar_.tick(dt);
  if (before != scrollbar_.pos()) dirty_ = true;
  if (!dirty_) return false;
  image_ = ui_image(width_ + 16, height_ + 16, fonts.pixel_scale());
  ui_path(image_, cut_edge_rect(2, 2, width_ + 12, height_ + 12, 4), 0x333333, 0xffffff, 2);
  const double offset = text_offset();
  links_.clear();
  const auto blit = [&](const TextBitmap& part, double x, double y) {
    ui_blit_text(image_, part, 8 + x + part.offset.x, 8 + y + part.offset.y, false, true, 1, 8, 8, 400, 400);
  };
  for (std::size_t row = 0; row < lines_.size(); ++row) {
    const auto& line = lines_[row];
    const double y = static_cast<double>(row) * line_height_ - offset;
    if (y + line_height_ < 0 || y >= 400 || (state_ == State::edit && y + line_height_ + 4 > 400)) continue;
    if (state_ == State::edit) {
      const auto a = std::max(std::min(caret_, anchor_), line.start),
                 b = std::min(std::max(caret_, anchor_), line.start + line.text.size());
      if (b > a) {
        const double left = 2 + fonts.measure(line.text.substr(0, a - line.start), 16),
                     right = 2 + fonts.measure(line.text.substr(0, b - line.start), 16);
        ui_rectangle(image_, 8 + left, 8 + std::max(0.0, y + 2), right - left,
                     std::min(line_height_, 400 - std::max(0.0, y + 2)), 0x4a6583, true);
      }
    }
    blit(fonts.rasterize(line.text, 16, false, false, 0xb3b3b3, 0, 0, 1, 400, static_cast<int>(line_height_ + 4)), 0,
         y);
    if (state_ == State::view) {
      for (const auto& url : urls_) {
        const auto a = std::max(url.start, line.start), b = std::min(url.end, line.start + line.text.size());
        if (b <= a) continue;
        const auto part = line.text.substr(a - line.start, b - a);
        const double x = fonts.measure(line.text.substr(0, a - line.start), 16);
        blit(fonts.rasterize(part, 16, false, false, 0x7777ee), x, y);
        links_.push_back(
            {url.start,
             url.end,
             {x + 2, std::max(0.0, y), fonts.measure(part, 16), std::min(line_height_, 400 - std::max(0.0, y))}});
      }
    }
    if (state_ == State::edit && focused_ && caret_ >= line.start && caret_ <= line.start + line.text.size() &&
        (row + 1 == lines_.size() || caret_ < lines_[row + 1].start)) {
      const double x = 2 + fonts.measure(line.text.substr(0, caret_ - line.start), 16);
      ui_rectangle(image_, 8 + x, 8 + std::max(0.0, y + 2), 1, std::min(line_height_, 400 - std::max(0.0, y + 2)),
                   0xb3b3b3, true);
    }
  }
  if (width_ == 426) scrollbar_.draw(image_, {414, 8});
  for (int i = 0; i < 2; ++i)
    if (i == 1 || state_ == State::edit || can_edit_) {
      const auto b = button(i);
      const std::string label = state_ == State::edit ? (i == 0 ? "Cancel" : "Save") : (i == 0 ? "Edit" : "Close");
      ui_path(image_, cut_edge_rect(8 + b.x, 8 + b.y, b.width, b.height, 4), hover_ == i ? 0xffdc85 : 0xffffff);
      const auto t = fonts.rasterize(label, 14, true, false, 0x363636);
      ui_blit_text(image_, t, 8 + b.x + 60 - fonts.measure(label, 14, true) / 2 - 2 + t.offset.x,
                   8 + b.y + 1 + t.offset.y, false, true);
    }
  // Board has no shadow filter; convert premultiplied path edges for upload.
  for (std::size_t i = 0; i < image_.pixels.size(); i += 4)
    if (const unsigned a = image_.pixels[i + 3]; a && a != 255)
      for (unsigned c = 0; c < 3; ++c)
        image_.pixels[i + c] = static_cast<unsigned char>(std::min(255U, image_.pixels[i + c] * 255U / a));
  dirty_ = false;
  return true;
}
}
