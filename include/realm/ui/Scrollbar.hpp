#pragma once
#include "realm/assets/Image.hpp"
#include "realm/map/Camera.hpp"
namespace realm {
// Mirrors the Scrollbar class. Local design coordinates; normalized finite position even
// when all content fits. Mouse-up/focus loss ends arrow holds and thumb drags.
class Scrollbar {
 public:
  Scrollbar(double width = 16, double height = 396, double speed = 1) : width_(width), height_(height), speed_(speed) {}
  void indicator(double window, double total, bool reset = true);
  void set_pos(double value);
  [[nodiscard]] double pos() const { return pos_; }
  [[nodiscard]] double thumb_height() const { return thumb_; }
  [[nodiscard]] double thumb_y() const { return top() + pos_ * travel(); }
  bool move(Vec2 p);
  void down(Vec2 p);
  void up() {
    hold_ = 0;
    drag_ = false;
  }
  void focus_lost() {
    up();
    hover_ = -1;
  }
  void wheel(double steps);
  void tick(double seconds);
  void draw(Image& image, Vec2 at) const;

 private:
  [[nodiscard]] double top() const { return static_cast<int>(width_ * .75) + 5; }
  [[nodiscard]] double track() const { return height_ - 2 * static_cast<int>(width_ * .75) - 10; }
  [[nodiscard]] double travel() const { return track() - thumb_; }
  [[nodiscard]] int hit(Vec2 p) const;
  double width_, height_, speed_, thumb_{}, pos_{}, page_{}, grab_{};
  int hover_{-1}, hold_{};
  bool drag_{};
};
}
