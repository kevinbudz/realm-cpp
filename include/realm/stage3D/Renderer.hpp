#pragma once
#include "realm/stage3D/Atlas.hpp"
#include "realm/util/Telemetry.hpp"
#include "realm/stage3D/DrawTypes.hpp"
#include "realm/stage3D/WallGeometry.hpp"
#include "realm/map/PostEffects.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <memory>
#include <optional>
#include <vector>

#include "realm/stage3D/GraphicsApi.hpp"

namespace realm {
namespace gfx {
class RendererBackend;
}

// Queried once after the platform adapter has made the active API's context current.
struct GraphicsInfo {
  std::string vendor;
  std::string renderer;
  std::string version;
};

[[nodiscard]] GraphicsInfo graphics_info();
void check_graphics_error();
// Brings up the active graphics backend once its window exists, before any GPU object. Throws with
// the reason when the API cannot start (the caller then falls back to OpenGL).
void initialise_graphics(const gfx::WindowSurface& window);
// Releases the active API's state; GPU objects must be gone first. Idempotent.
void shutdown_graphics() noexcept;
// The vsync interval of the window, for the presentation mode (Vulkan; OpenGL swaps through the session).
void set_present_interval(int interval);

// Resizing a surface replaces only its own storage, never the world atlas.
class SurfaceTexture {
 public:
  ~SurfaceTexture();

  explicit SurfaceTexture(bool retain_capacity = false) : retain_capacity_(retain_capacity) {}

  SurfaceTexture(const SurfaceTexture&) = delete;
  SurfaceTexture& operator=(const SurfaceTexture&) = delete;
  void update(const Image& image, bool premultiplied = false);

  // Animated content can reserve its full size range before the first upload.
  void reserve(int width, int height) {
    reserved_width_ = width;
    reserved_height_ = height;
  }

  void update_region(const Image& image, int x, int y, int width, int height,
                     bool premultiplied = false);
  [[nodiscard]] Quad quad(Vec2 origin, Vec2 axis_x, Vec2 axis_y, int x = 0, int y = 0,
                          int width = 0, int height = 0) const;

  // Nearest sampling unless set. Applies to the texture at once when it exists, else at creation.
  void set_linear_filter(bool linear);

 private:
  std::uint32_t texture_{};
  int width_{};
  int height_{};
  int storage_width_{};
  int storage_height_{};
  int reserved_width_{};
  int reserved_height_{};
  bool retain_capacity_{};
  bool linear_filter_{};
  int texture_limit_{};
};

// Retained, chunk-local ground instances. Camera motion changes only the command's affine
// transform.
class TerrainBuffer {
 public:
  explicit TerrainBuffer(std::span<const Quad> quads);
  ~TerrainBuffer();
  TerrainBuffer(const TerrainBuffer&) = delete;
  TerrainBuffer& operator=(const TerrainBuffer&) = delete;

  [[nodiscard]] std::uint32_t buffer() const { return buffer_; }

  [[nodiscard]] std::size_t count() const { return count_; }

 private:
  std::uint32_t buffer_{};
  std::size_t count_{};
};

// The CPU-only part of one quad in a submission, in the quad's own order: what the draw loop
// decides runs by. owner is the quad's owner.get(); the submission's quads keep it alive while
// drawn.
struct QuadMeta {
  const void* owner{};
  std::uint32_t texture{};
  float kind{};
  float model{};  // offset[2]: the model of a kind 9 instance
  float walls{};  // multiply[1]: the corner count of a kind 17 wall
};

// A clipped overlay kept together in painter order (minimap map, markers, arrow and buttons).
struct QuadLayer {
  QuadLayer() = default;
  QuadLayer(const QuadLayer&) = delete;
  QuadLayer& operator=(const QuadLayer&) = delete;
  ~QuadLayer();
  std::vector<Quad> quads;
  std::uint32_t buffer{};
  // Packed GPU instances (quad_instance_bytes each) of quads, and the bytes last uploaded to
  // buffer; an unchanged layer keeps the buffer's contents.
  std::vector<std::byte> packed;
  std::vector<std::byte> uploaded;
  std::vector<QuadMeta> meta;
};

// The frame facade: forwards to the active graphics backend (gfx::backend().create_renderer). Its
// methods are per frame or per submission, so the indirection costs nothing per quad. Texture
// handles in the submission are the active backend's (see GraphicsBackend.hpp).
class Renderer {
 public:
  explicit Renderer(std::size_t capacity, bool persistent_streaming = true,
                    std::size_t persistent_minimum_bytes = 64 * 1024);
  ~Renderer();
  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  void clear_color(std::array<float, 4> color);

  void draw(const Atlas& atlas, std::span<const Quad> quads, int width, int height, int clip_width,
            std::span<const Quad> overlay = {}, const ScreenEffects& effects = {});
  // Extra overlay quads after another GL pass (e.g. RmlUi's) in the same frame: re-establishes
  // every GL state it uses and draws over the whole window. Quads and draw calls are counted like
  // draw()'s.
  void draw_overlay(const Atlas& atlas, std::span<const Quad> overlay, int width, int height);
  // Ends the frame before the window presents: Vulkan submits and presents here; OpenGL does nothing.
  void finish_frame();

  // Resolves to the default framebuffer; the world is rendered offscreen only while a filter is
  // active.
  [[nodiscard]] std::size_t draw_calls() const;

  // Timing is opt-in. The fixed query ring skips busy slots; never waits.
  void timing(telemetry::Recorder* recorder, std::uint64_t frame_id, bool normal);
  // Measures GPU time for gpu_ms() without a recorder (the performance stats display).
  void set_gpu_timing(bool enabled);
  void poll_gpu();
  [[nodiscard]] std::size_t pending_gpu() const;
  [[nodiscard]] std::size_t skipped_gpu() const;
  [[nodiscard]] double gpu_ms() const;
  [[nodiscard]] bool persistent_streaming() const;
  [[nodiscard]] Image capture(int width, int height, bool presented = false) const;
  // Vulkan and WebGPU: while enabled, each presented frame is copied on the GPU so capture(presented)
  // can read the last one back (the death screen). Enable it only while a session is playing. OpenGL
  // ignores it.
  void set_presented_capture(bool enabled);
  // WebGPU: the presented capture arrives a few frames later; returns it once.
  [[nodiscard]] std::optional<Image> take_capture();

  // Runs read their instances from their first quad (GL 4.2 base instance) instead of re-pointing
  // attributes per run. Only enabled where supported; false selects the per-run path, which renders
  // the same pixels.
  void set_base_instance_draws(bool enabled);
  [[nodiscard]] bool base_instance_draws() const;

 private:
  std::unique_ptr<gfx::RendererBackend> backend_;
};
}
