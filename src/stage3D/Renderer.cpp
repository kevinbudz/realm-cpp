#include "realm/stage3D/Renderer.hpp"
#include "GraphicsBackend.hpp"

namespace realm {
GraphicsInfo graphics_info() {
  return gfx::backend().info();
}

void check_graphics_error() {
  gfx::backend().check_frame();
}

void initialise_graphics(const gfx::WindowSurface& window) {
  gfx::backend().initialise(window);
}

void shutdown_graphics() noexcept {
  gfx::backend().shutdown();
}

void set_present_interval(int interval) {
  gfx::backend().set_swap_interval(interval);
}

Renderer::Renderer(std::size_t capacity, bool persistent_streaming,
                   std::size_t persistent_minimum_bytes)
    : backend_(gfx::backend().create_renderer(capacity, persistent_streaming,
                                              persistent_minimum_bytes)) {}

Renderer::~Renderer() = default;

void Renderer::clear_color(std::array<float, 4> color) {
  backend_->set_clear_color(color);
}

void Renderer::draw(const Atlas& atlas, std::span<const Quad> quads, int width, int height,
                    int clip_width, std::span<const Quad> overlay, const ScreenEffects& effects) {
  backend_->draw(atlas, quads, width, height, clip_width, overlay, effects);
}

void Renderer::finish_frame() {
  backend_->finish_frame();
}

void Renderer::draw_overlay(const Atlas& atlas, std::span<const Quad> overlay, int width,
                            int height) {
  backend_->draw_overlay(atlas, overlay, width, height);
}

std::size_t Renderer::draw_calls() const {
  return backend_->draw_calls();
}

void Renderer::timing(telemetry::Recorder* recorder, std::uint64_t frame_id, bool normal) {
  backend_->timing(recorder, frame_id, normal);
}

void Renderer::poll_gpu() {
  backend_->poll_gpu();
}

std::size_t Renderer::pending_gpu() const {
  return backend_->pending_gpu();
}

std::size_t Renderer::skipped_gpu() const {
  return backend_->skipped_gpu();
}

double Renderer::gpu_ms() const {
  return backend_->gpu_ms();
}

bool Renderer::persistent_streaming() const {
  return backend_->persistent_streaming();
}

Image Renderer::capture(int width, int height, bool presented) const {
  return backend_->capture(width, height, presented);
}

void Renderer::set_presented_capture(bool enabled) {
  backend_->set_presented_capture(enabled);
}

std::optional<Image> Renderer::take_capture() {
  return backend_->take_capture();
}

void Renderer::set_base_instance_draws(bool enabled) {
  backend_->set_base_instance_draws(enabled);
}

bool Renderer::base_instance_draws() const {
  return backend_->base_instance_draws();
}
}  // namespace realm
