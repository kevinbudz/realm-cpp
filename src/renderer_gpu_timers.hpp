#pragma once

#include "realm/telemetry.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace realm {
class RendererGpuTimers {
 public:
  RendererGpuTimers() = default;
  ~RendererGpuTimers();
  RendererGpuTimers(const RendererGpuTimers&) = delete;
  RendererGpuTimers& operator=(const RendererGpuTimers&) = delete;

  void timing(telemetry::Recorder* recorder, std::uint64_t frame_id, bool normal);
  [[nodiscard]] bool begin_frame();
  void phase(std::size_t index);
  [[nodiscard]] bool model_begin(std::size_t instances);
  void model_end(bool measured);
  void finish_sample();
  void advance_frame();
  void poll();
  [[nodiscard]] std::size_t pending() const;
  [[nodiscard]] std::size_t skipped() const { return skipped_; }
  [[nodiscard]] double gpu_ms() const { return gpu_ms_; }

 private:
  static constexpr std::size_t model_query_limit = 32;
  static constexpr std::size_t timestamps = 5 + 2 * model_query_limit;
  struct Timer {
    std::array<std::uint32_t, timestamps> ids{};
    bool pending{}, normal{};
    std::uint64_t frame_id{};
    std::size_t models{}, models_total{};
  };
  std::array<Timer, 8> timers_{};
  telemetry::Recorder* recorder_{};
  std::uint64_t sample_frame_{};
  bool sample_normal_{}, measuring_{};
  std::size_t frame_{}, skipped_{};
  double gpu_ms_{-1};
  Timer* active_{};
};
}
