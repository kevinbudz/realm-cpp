#include "realm/scrollbar.hpp"
#include "realm/ui_draw.hpp"
#include <algorithm>
#include <cmath>
namespace realm {
void Scrollbar::indicator(double window, double total, bool reset) {
  thumb_ = std::clamp(std::floor(total > 0 ? window / total * track() : track()), width_, track());
  page_ = total > window ? window / (total - window) : 0;
  if (reset || page_ == 0) set_pos(0);
}
void Scrollbar::set_pos(double v) { pos_ = std::isfinite(v) && travel() > 0 ? std::clamp(v, 0.0, 1.0) : 0; }
int Scrollbar::hit(Vec2 p) const {
  if (p.x < 0 || p.x >= width_ || p.y < 0 || p.y >= height_) return -1;
  if (p.y < 12) return 0;
  if (p.y >= height_ - 12) return 1;
  if (p.y >= thumb_y() && p.y < thumb_y() + thumb_) return 2;
  return p.y >= top() && p.y < top() + track() ? 3 : -1;
}
bool Scrollbar::move(Vec2 p) {
  const int before = hover_;
  hover_ = hit(p);
  if (drag_ && travel() > 0) set_pos((p.y - grab_ - top()) / travel());
  return before != hover_ || drag_;
}
void Scrollbar::down(Vec2 p) {
  move(p);
  if (hover_ == 0)
    hold_ = -1;
  else if (hover_ == 1)
    hold_ = 1;
  else if (hover_ == 2 && travel() > 0) {
    drag_ = true;
    grab_ = p.y - thumb_y();
  } else if (hover_ == 3)
    set_pos(pos_ + (p.y < thumb_y() ? -page_ : page_));
}
void Scrollbar::wheel(double steps) {
  if (steps != 0) set_pos(pos_ + (steps > 0 ? -1 : 1) * page_ * .2);
}
void Scrollbar::tick(double seconds) {
  if (hold_ && travel() > 0) set_pos(pos_ + (height_ - 3 * width_) * seconds * hold_ * speed_ / travel());
}
void Scrollbar::draw(Image& im, Vec2 at) const {
  ui_rectangle(im, at.x, at.y + top(), width_, track(), 0x545454, true);
  const auto color = [&](int part) { return hover_ == part ? 0xffdc85U : 0xffffffU; };
  const std::array<Vec2, 3> up{{{at.x, at.y + 12}, {at.x + 8, at.y}, {at.x + 16, at.y + 12}}};
  const std::array<Vec2, 3> down{
      {{at.x, at.y + height_ - 12}, {at.x + 16, at.y + height_ - 12}, {at.x + 8, at.y + height_}}};
  ui_path(im, up, color(0));
  ui_path(im, down, color(1));
  ui_path(im, cut_edge_rect(at.x, at.y + thumb_y(), width_, thumb_, 4), color(2));
}
}
