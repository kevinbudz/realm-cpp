#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>
#ifdef REALM_TRACY
#include <tracy/Tracy.hpp>
#endif
namespace realm::telemetry {
using Clock = std::chrono::steady_clock;
enum class Counter : std::size_t {
  allocations,
  allocation_bytes,
  texture_upload_bytes,
  texture_storage_bytes,
  mesh_upload_bytes,
  quad_upload_bytes,
  draw_calls,
  triangles,
  quads,
  models,
  appearance_hits,
  appearance_misses,
  appearance_evictions,
  gpu_appearance_hits,
  gpu_appearance_misses,
  gpu_appearance_evictions,
  glyph_hits,
  glyph_misses,
  glyph_evictions,
  packet_rx_bytes,
  packet_tx_bytes,
  packets_decoded,
  outgoing_queue_bytes,
  framing_queue_bytes,
  optional_drops,
  resident_entities,
  visible_entities,
  resident_projectiles,
  particles,
  appearance_cpu_bytes,
  appearance_gpu_bytes,
  glyph_bytes,
  atlas_uploads,
  atlas_evictions,
  atlas_fallbacks,
  atlas_resident_bytes,
  atlas_shelf_bytes,
  atlas_resizes,
  atlas_pages,
  ui_text_rasterizations,
  ui_painter_rasterizations,
  ui_texture_uploads,
  ui_texture_upload_bytes,
  ui_text_meshes,
  ui_native_image_meshes,
  ui_native_image_bytes,
  ui_documents,
  ui_draw_calls,
  ui_native_texture_uploads,
  ui_native_texture_upload_bytes,
  // Audio (Player::counters per frame: event counts are deltas, gauges are values).
  audio_plays_requested,
  audio_plays_gated,
  audio_plays_deduped,
  audio_plays_started,
  audio_plays_deferred,
  audio_pending_dropped,
  audio_command_dropped,
  audio_voices_active,
  audio_steals,
  audio_loads_ok,
  audio_loads_failed,
  audio_queue_dropped,
  audio_evictions,
  audio_decoded_bytes,
  audio_cue_overflow,
  count
};
constexpr std::array names{"allocations",
                           "allocation_bytes",
                           "texture_upload_bytes",
                           "texture_storage_bytes",
                           "mesh_upload_bytes",
                           "quad_upload_bytes",
                           "draw_calls",
                           "triangles",
                           "quads",
                           "models",
                           "appearance_hits",
                           "appearance_misses",
                           "appearance_evictions",
                           "gpu_appearance_hits",
                           "gpu_appearance_misses",
                           "gpu_appearance_evictions",
                           "glyph_hits",
                           "glyph_misses",
                           "glyph_evictions",
                           "packet_rx_bytes",
                           "packet_tx_bytes",
                           "packets_decoded",
                           "outgoing_queue_bytes",
                           "framing_queue_bytes",
                           "optional_drops",
                           "resident_entities",
                           "visible_entities",
                           "resident_projectiles",
                           "particles",
                           "appearance_cpu_bytes",
                           "appearance_gpu_bytes",
                           "glyph_bytes",
                           "atlas_uploads",
                           "atlas_evictions",
                           "atlas_fallbacks",
                           "atlas_resident_bytes",
                           "atlas_shelf_bytes",
                           "atlas_resizes",
                           "atlas_pages",
                           "ui_text_rasterizations",
                           "ui_painter_rasterizations",
                           "ui_texture_uploads",
                           "ui_texture_upload_bytes",
                           "ui_text_meshes",
                           "ui_native_image_meshes",
                           "ui_native_image_bytes",
                           "ui_documents",
                           "ui_draw_calls",
                           "ui_native_texture_uploads",
                           "ui_native_texture_upload_bytes",
                           "audio_plays_requested",
                           "audio_plays_gated",
                           "audio_plays_deduped",
                           "audio_plays_started",
                           "audio_plays_deferred",
                           "audio_pending_dropped",
                           "audio_command_dropped",
                           "audio_voices_active",
                           "audio_steals",
                           "audio_loads_ok",
                           "audio_loads_failed",
                           "audio_queue_dropped",
                           "audio_evictions",
                           "audio_decoded_bytes",
                           "audio_cue_overflow"};
struct Frame {
  std::uint64_t id{};
  bool warmup{}, capture{}, interval_valid{};
  double interval_ms{}, cpu_ms{}, present_ms{}, paced_ms{}, input_to_submit_ms{-1};
  int width{}, height{}, swap_interval{}, fps_cap{};
};
struct GpuSample {
  std::uint64_t frame_id{};
  bool normal{};
  double total_ms{}, upload_ms{}, world_ms{}, effects_ms{}, ui_ms{}, models_ms{};
  std::size_t models_measured{}, models_total{};
};
class Recorder;
extern thread_local Recorder* active;
void add(Counter counter, std::uint64_t value = 1) noexcept;
void set(Counter counter, std::uint64_t value) noexcept;
// Main-thread instrumentation only; no hooks in worker threads or malloc.
class Zone {
 public:
  explicit Zone(const char* name) noexcept;
  ~Zone();
  void finish() noexcept;

 private:
  Recorder* recorder_{};
  const char* name_{};
  Clock::time_point start_{};
  std::uint64_t allocations_{}, bytes_{};
};
class Recorder {
 public:
  static constexpr std::size_t maximum_zones = 8192;
  Recorder(const std::filesystem::path& samples, const std::filesystem::path& trace);
  ~Recorder();
  void begin(std::uint64_t frame_id, Clock::time_point start);
  void end(const Frame& frame);
  void gpu(const GpuSample& sample);
  void metadata(std::string_view key, std::string_view value);
  void finish();
  // Offline stress mode: cumulative inclusive time per name; bounds independent of population.
  void aggregate_repeated(bool value) { aggregate_repeated_ = value; }
  [[nodiscard]] Clock::time_point epoch() const { return epoch_; }
  // The running count of the frame in progress; counters reset at begin().
  [[nodiscard]] std::uint64_t counter(Counter value) const { return counters_[static_cast<std::size_t>(value)]; }

 private:
  friend class Zone;
  friend void add(Counter, std::uint64_t) noexcept;
  friend void set(Counter, std::uint64_t) noexcept;
  struct Sample {
    const char* name;
    double start_us, duration_us;
    std::uint64_t allocations, bytes, calls{1};
  };
  std::array<Sample, maximum_zones> zones_{};
  std::array<std::uint64_t, static_cast<std::size_t>(Counter::count)> counters_{};
  std::ofstream samples_, trace_;
  Clock::time_point epoch_{Clock::now()};
  std::uint64_t frame_id_{}, dropped_{};
  double frame_start_us_{};
  std::size_t zone_count_{};
  bool trace_first_{true}, finished_{}, aggregate_repeated_{};
};
}
#define REALM_JOIN_IMPL(a, b) a##b
#define REALM_JOIN(a, b) REALM_JOIN_IMPL(a, b)
#ifdef REALM_TRACY
#define REALM_ZONE(name) \
  ZoneScopedN(name);     \
  ::realm::telemetry::Zone REALM_JOIN(realm_zone_, __LINE__)(name)
#else
#define REALM_ZONE(name) ::realm::telemetry::Zone REALM_JOIN(realm_zone_, __LINE__)(name)
#endif
