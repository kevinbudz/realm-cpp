#pragma once
#include <cmath>

namespace realm {
struct Vec2 { double x{}, y{}; };
struct Rect { double x{}, y{}, width{}, height{}; };

// Camera.as: the normal gameplay path is orthographic, at 50 pixels/world unit.
// Keep AS3 Number precision through simulation; convert only at the GPU boundary.
class Camera {
public:
    void configure(Vec2 position, double angle, double width, double height,
                   double zoom = 1.0, bool centered = true);
    // An explicit clip rectangle without the gameplay HUD allowance (MapBackground).
    void configure(Vec2 position, double angle, Rect clip);
    [[nodiscard]] Vec2 world_to_screen(Vec2 position, double z = 0) const;
    [[nodiscard]] Vec2 screen_to_world(Vec2 position) const;
    [[nodiscard]] Rect clip_rect() const { return clip_; }
    [[nodiscard]] double scale() const { return zoom_; }
    [[nodiscard]] double angle() const { return std::atan2(s_, c_); }
    [[nodiscard]] Vec2 origin() const { return {-clip_.x*zoom_,-clip_.y*zoom_}; }
    [[nodiscard]] double max_distance() const { return max_distance_; }
    [[nodiscard]] double playable_width() const { return width_ - height_ / 3.0; }
    [[nodiscard]] double pixels_per_unit() const { return 50 * zoom_; }
private:
    Vec2 position_{};
    Rect clip_{};
    double width_{800}, height_{600}, zoom_{1}, c_{1}, s_{0}, max_distance_{};
};
}
