#include "realm/camera.hpp"
#include <stdexcept>

namespace realm {
void Camera::configure(Vec2 position, double angle, double width, double height,
                       double zoom, bool centered) {
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(angle) ||
        !std::isfinite(width) || !std::isfinite(height) || !std::isfinite(zoom) ||
        height <= 0 || width <= height / 3.0 || zoom <= 0)
        throw std::invalid_argument("Invalid camera parameters");
    position_ = position; width_ = width; height_ = height; zoom_ = zoom;
    c_ = std::cos(angle); s_ = std::sin(angle);
    // WebMain.hudWidth() = 200 * stageHeight / 600; correctViewingArea(NO_SCALE).
    clip_ = {-(width - height / 3.0) / (2 * zoom),
             -height / zoom * (centered ? 13.0 / 24.0 : 3.0 / 4.0), width / zoom, height / zoom};
    const double w = clip_.width / 100, h = clip_.height / 100;
    max_distance_ = std::sqrt(w * w + h * h) + 1;
}
void Camera::configure(Vec2 position, double angle, Rect clip) {
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(angle) ||
        !std::isfinite(clip.x) || !std::isfinite(clip.y) || !(clip.width > 0) || !(clip.height > 0))
        throw std::invalid_argument("Invalid camera parameters");
    position_ = position; width_ = clip.width; height_ = clip.height; zoom_ = 1; clip_ = clip;
    c_ = std::cos(angle); s_ = std::sin(angle);
    const double w = clip_.width / 100, h = clip_.height / 100;
    max_distance_ = std::sqrt(w * w + h * h) + 1;
}
Vec2 Camera::world_to_screen(Vec2 p, double z) const {
    const double dx = p.x - position_.x, dy = p.y - position_.y;
    return {(50 * (dx * c_ + dy * s_) - clip_.x) * zoom_,
            (50 * (-dx * s_ + dy * c_ - z) - clip_.y) * zoom_};
}
Vec2 Camera::screen_to_world(Vec2 p) const {
    const double x = (p.x / zoom_ + clip_.x) / 50;
    const double y = (p.y / zoom_ + clip_.y) / 50;
    return {position_.x + x * c_ - y * s_, position_.y + x * s_ + y * c_};
}
}
