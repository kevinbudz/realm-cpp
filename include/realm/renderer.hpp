#pragma once
#include "realm/atlas.hpp"
#include "realm/camera.hpp"
#include "realm/post_effects.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <memory>
#include <vector>

namespace realm {
// Affine quads support billboards, rotated terrain, shadows, and HUD rectangles.
// Stream order is painter order. Atlas pages, tints, and shadows do not reorder it.
struct Quad {
    std::array<float, 2> origin{}, axis_x{}, axis_y{};
    std::array<float, 4> uv{};
    float page{};
    std::array<float, 4> multiply{1, 1, 1, 1}, offset{};
    float kind{}; // 0 sprite, 1 shadow, 2 solid, 3 repeat, 4 crop, 5 UI bitmap, 6 world bitmap, 7 particle square, 8 thrown fill, 9 projected mesh (owner is a MeshDraw)
    std::array<float, 2> uv_offset{};
    std::shared_ptr<const void> owner; // Keeps lazy GPU storage alive through submission.
    GLuint texture{}; // zero: shared sprite atlas; otherwise a retained UI surface
};
// A model already projected to screen pixels, with back faces removed. Triangles
// are grouped by material; vertices are expanded three per triangle.
struct MeshVertex { float x, y, u, v, shade; };
struct MeshGroup { bool solid{}; std::uint32_t first{}, count{}; };
struct MeshDraw {
    std::vector<MeshVertex> vertices;
    std::vector<MeshGroup> groups;
    std::array<float, 4> bounds{}; // sprite UV rectangle in its atlas page; zero size without a sprite
    float page{};
    std::array<float, 3> color{1, 1, 1}; // flat color for Solid* groups and sprite-less models
};
// Takes its place in the painter-ordered stream at the model's depth.
[[nodiscard]] Quad mesh_quad(std::shared_ptr<const MeshDraw> mesh);
[[nodiscard]] Quad sprite_quad(SpriteRegion sprite, Vec2 origin, Vec2 axis_x, Vec2 axis_y);
[[nodiscard]] Quad projectile_quad(SpriteRegion sprite, Vec2 center, double angle,
                                   int size, double zoom, bool outline);
[[nodiscard]] Quad radial_shadow(Vec2 origin, Vec2 axis_x, Vec2 axis_y,
                                 std::array<float, 3> color, float alpha_center, float alpha_edge = 0);

// Resizing a surface replaces only its own storage, never the world atlas.
class SurfaceTexture {
public:
    ~SurfaceTexture();
    explicit SurfaceTexture(bool retain_capacity=false):retain_capacity_(retain_capacity) {}
    SurfaceTexture(const SurfaceTexture&)=delete;
    SurfaceTexture& operator=(const SurfaceTexture&)=delete;
    void update(const Image& image);
    // Animated content can reserve its full size range before the first upload.
    void reserve(int width,int height) { reserved_width_=width;reserved_height_=height; }
    void update_region(const Image& image,int x,int y,int width,int height);
    [[nodiscard]] Quad quad(Vec2 origin,Vec2 axis_x,Vec2 axis_y,int x=0,int y=0,int width=0,int height=0) const;
private:
    GLuint texture_{};
    int width_{},height_{},storage_width_{},storage_height_{};
    int reserved_width_{},reserved_height_{};
    bool retain_capacity_{};
};

class Renderer {
public:
    explicit Renderer(std::size_t capacity);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    void clear_color(std::array<float,4> color) { clear_color_=color; }
    void draw(const Atlas& atlas, std::span<const Quad> quads, int width, int height, int clip_width,std::span<const Quad> overlay={},const ScreenEffects& effects={});
    // Resolves to the default framebuffer; the world is rendered offscreen only while a filter is active.
    [[nodiscard]] std::size_t draw_calls() const { return draw_calls_; }
    [[nodiscard]] double gpu_ms() const { return gpu_ms_; }
    [[nodiscard]] Image capture(int width, int height) const;
private:
    std::array<float,4> clear_color_{0,0,0,1};
    GLuint program_{}, vao_{}, mesh_program_{}, mesh_vao_{}, mesh_buffer_{};
    std::array<GLuint, 3> buffers_{};
    std::array<GLuint, 8> timers_{};
    std::array<bool, 8> timer_pending_{};
    std::size_t frame_{}, capacity_{}, draw_calls_{};
    double gpu_ms_{-1};
    GLuint post_program_{}, post_vao_{};
    std::array<GLuint, 2> post_textures_{}, post_buffers_{};
    int post_width_{}, post_height_{};
    GLint post_rect_{}, post_viewport_{}, post_mode_{}, post_axis_{}, post_blur_{}, post_blind_{}, post_region_{}, post_alpha_{}, post_sidebar_{};
    void ensure_post_targets(int width, int height);
    void run_post(int width, int height, int clip_width, const ScreenEffects& effects);
    void run_screen_overlays(int width, int height, int clip_width, const ScreenEffects& effects);
    GLint viewport_location_{}, mesh_viewport_{}, mesh_bounds_{}, mesh_color_{}, mesh_page_{}, mesh_textured_{};
};
}
