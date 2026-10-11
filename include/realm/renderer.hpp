#pragma once
#include "realm/atlas.hpp"
#include "realm/camera.hpp"
#include <array>
#include <span>

namespace realm {
// Affine quads support billboards, rotated terrain, shadows, and HUD rectangles.
// Stream order is painter order. Atlas pages, tints, and shadows do not reorder it.
struct Quad {
    std::array<float, 2> origin{}, axis_x{}, axis_y{};
    std::array<float, 4> uv{};
    float page{};
    std::array<float, 4> multiply{1, 1, 1, 1}, offset{};
    float kind{}; // 0 sprite, 1 radial shadow, 2 solid, 3 repeat, 4 interior crop, 5 display-list bitmap
    std::array<float, 2> uv_offset{};
};
[[nodiscard]] Quad sprite_quad(SpriteRegion sprite, Vec2 origin, Vec2 axis_x, Vec2 axis_y);
[[nodiscard]] Quad projectile_quad(SpriteRegion sprite, Vec2 center, double angle,
                                   int size, double zoom, bool outline);
[[nodiscard]] Quad radial_shadow(Vec2 origin, Vec2 axis_x, Vec2 axis_y,
                                 std::array<float, 3> color, float alpha_center, float alpha_edge = 0);

class Renderer {
public:
    explicit Renderer(std::size_t capacity);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    void draw(const Atlas& atlas, std::span<const Quad> quads, int width, int height, int clip_width,std::span<const Quad> overlay={});
    [[nodiscard]] std::size_t draw_calls() const { return draw_calls_; }
    [[nodiscard]] double gpu_ms() const { return gpu_ms_; }
    [[nodiscard]] Image capture(int width, int height) const;
private:
    GLuint program_{}, vao_{};
    std::array<GLuint, 3> buffers_{};
    std::array<GLuint, 8> timers_{};
    std::array<bool, 8> timer_pending_{};
    std::size_t frame_{}, capacity_{}, draw_calls_{};
    double gpu_ms_{-1};
    GLint viewport_location_{};
};
}
