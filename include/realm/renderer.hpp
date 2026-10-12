#pragma once
#include "realm/atlas.hpp"
#include "realm/telemetry.hpp"
#include "realm/draw_types.hpp"
#include "realm/post_effects.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <memory>
#include <vector>

namespace realm {
// Queried once after the platform adapter has made a GL context current.
struct GraphicsInfo { std::string vendor, renderer, version; };
[[nodiscard]] GraphicsInfo graphics_info();
void check_graphics_error();

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
    std::uint32_t texture_{};
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
    // Timing is opt-in. The fixed query ring skips busy slots; never waits.
    void timing(telemetry::Recorder* recorder,std::uint64_t frame_id,bool normal) { recorder_=recorder;sample_frame_=frame_id;sample_normal_=normal; }
    void poll_gpu();
    [[nodiscard]] std::size_t pending_gpu() const;
    [[nodiscard]] std::size_t skipped_gpu() const { return skipped_gpu_; }
    [[nodiscard]] double gpu_ms() const { return gpu_ms_; }
    [[nodiscard]] Image capture(int width, int height) const;
private:
    std::array<float,4> clear_color_{0,0,0,1};
    std::uint32_t program_{}, vao_{}, mesh_program_{}, mesh_vao_{}, mesh_buffer_{}, model_program_{}, model_vao_{}, model_geometry_{};
    std::array<std::uint32_t, 3> buffers_{};
    // Five phase boundaries and 32 model draw start/end pairs per slot.
    static constexpr std::size_t model_query_limit=32, timestamps=5+2*model_query_limit;
    struct Timer { std::array<std::uint32_t,timestamps> ids{};bool pending{},normal{};std::uint64_t frame_id{};std::size_t models{},models_total{}; };
    std::array<Timer,8> timers_{};
    telemetry::Recorder* recorder_{};
    std::uint64_t sample_frame_{};
    bool sample_normal_{};
    std::size_t skipped_gpu_{};
    std::size_t frame_{}, capacity_{}, draw_calls_{};
    double gpu_ms_{-1};
    std::uint32_t post_program_{}, post_vao_{};
    std::array<std::uint32_t, 2> post_textures_{}, post_buffers_{};
    int post_width_{}, post_height_{};
    int post_rect_{}, post_viewport_{}, post_mode_{}, post_axis_{}, post_blur_{}, post_blind_{}, post_region_{}, post_alpha_{}, post_sidebar_{};
    void ensure_post_targets(int width, int height);
    void run_post(int width, int height, int clip_width, const ScreenEffects& effects);
    void run_screen_overlays(int width, int height, int clip_width, const ScreenEffects& effects);
    int viewport_location_{}, model_viewport_{}, mesh_viewport_{}, mesh_bounds_{}, mesh_color_{}, mesh_page_{}, mesh_textured_{};
};
}
