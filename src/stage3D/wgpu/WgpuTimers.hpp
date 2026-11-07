#pragma once
// WebGPU frame timing (the browser build, when the device has the timestamp-query feature). Each
// measured frame writes a start and an end timestamp around its passes; they are resolved into a
// per-slot readback that maps after the submit, and poll() reports the frame's GPU time to the
// telemetry recorder. Mirrors RendererGpuTimers (GL): a frame whose slot is still busy is skipped.
#include "realm/util/Telemetry.hpp"
#include <webgpu/webgpu.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace realm::gfx::wgpu {
// A readback's map result, written by the map callback (which may run after the timers are gone, so
// it writes through a holder that owns a reference to this).
struct TimerMap {
  bool done = false;
  WGPUMapAsyncStatus status = WGPUMapAsyncStatus_Error;
};

class WgpuTimers {
 public:
  WgpuTimers() = default;
  ~WgpuTimers();
  WgpuTimers(const WgpuTimers&) = delete;
  WgpuTimers& operator=(const WgpuTimers&) = delete;

  void timing(telemetry::Recorder* recorder, std::uint64_t frame_id, bool normal);
  void set_enabled(bool enabled) { enabled_ = enabled; }
  // Starts the frame's measurement in the frame's encoder. Does nothing when the frame is not measured.
  void begin_frame();
  // Ends the frame's measurement (before the submit): the end stamp, then the resolve and the copy into
  // the slot's readback. Every frame calls it, measured or not.
  void end_frame();
  // Maps the measured slot's readback (after the submit).
  void submitted();
  void poll();

  [[nodiscard]] std::size_t pending() const;
  [[nodiscard]] std::size_t skipped() const { return skipped_; }
  [[nodiscard]] double gpu_ms() const { return gpu_ms_; }

 private:
  static constexpr std::size_t slot_count = 4;
  // Four timestamps per slot (queries 4k to 4k+3): the start and end marker passes write two each.

  struct Slot {
    WGPUBuffer readback = nullptr;  // MapRead, two uint64 nanosecond stamps
    std::shared_ptr<TimerMap> map;  // the current map's result
    std::uint64_t frame_id{};
    bool normal{};
    bool pending{};  // mapped or mapping, until poll() reads it
  };

  // Creates the query set and buffers on first use; false when the device could not make them.
  [[nodiscard]] bool ensure();
  void release() noexcept;

  std::array<Slot, slot_count> slots_{};
  WGPUQuerySet query_set_ = nullptr;
  // Resolve target: one 256-byte stride per slot (resolveQuerySet destinations are 256-aligned).
  WGPUBuffer resolve_ = nullptr;
  bool failed_{};
  telemetry::Recorder* recorder_{};
  bool enabled_{};  // timing without a recorder
  std::uint64_t sample_frame_{};
  bool sample_normal_{};
  std::size_t frame_{};
  std::size_t skipped_{};
  double gpu_ms_{-1};
  Slot* active_{};  // the slot of the frame being recorded
  Slot* submit_{};  // the slot to map once the frame is submitted
};
}  // namespace realm::gfx::wgpu
