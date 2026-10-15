#include "realm/util/Telemetry.hpp"
#include <iomanip>
#include <stdexcept>

namespace realm::telemetry {
thread_local Recorder* active{};

void add(Counter counter, std::uint64_t value) noexcept {
  if (active) {
    active->counters_[static_cast<std::size_t>(counter)] += value;
  }
}

void set(Counter counter, std::uint64_t value) noexcept {
  if (active) {
    active->counters_[static_cast<std::size_t>(counter)] = value;
  }
}

Zone::Zone(const char* name) noexcept : recorder_(active), name_(name) {
  if (recorder_) {
    start_ = Clock::now();
    allocations_ = recorder_->counters_[0];
    bytes_ = recorder_->counters_[1];
  }
}

Zone::~Zone() {
  finish();
}

void Zone::finish() noexcept {
  if (!recorder_) {
    return;
  }
  const auto end = Clock::now();
  if (recorder_->aggregate_repeated_) {
    for (std::size_t i = 0; i < recorder_->zone_count_; ++i) {
      auto& z = recorder_->zones_[i];
      if (std::string_view(z.name) != name_) {
        continue;
      }
      z.duration_us += std::chrono::duration<double, std::micro>(end - start_).count();
      z.allocations += recorder_->counters_[0] - allocations_;
      z.bytes += recorder_->counters_[1] - bytes_;
      ++z.calls;
      recorder_ = nullptr;
      return;
    }
  }
  if (recorder_->zone_count_ < Recorder::maximum_zones) {
    recorder_->zones_[recorder_->zone_count_++] = {
        name_, std::chrono::duration<double, std::micro>(start_ - recorder_->epoch_).count(),
        std::chrono::duration<double, std::micro>(end - start_).count(),
        recorder_->counters_[0] - allocations_, recorder_->counters_[1] - bytes_};
  } else {
    ++recorder_->dropped_;
  }
  recorder_ = nullptr;
}

Recorder::Recorder(const std::filesystem::path& samples, const std::filesystem::path& trace)
    : samples_(samples) {
  if (!samples_) {
    throw std::runtime_error("Cannot open telemetry samples");
  }
  samples_ << std::setprecision(12);
  if (!trace.empty()) {
    trace_.open(trace);
    if (!trace_) {
      throw std::runtime_error("Cannot open telemetry trace");
    }
    trace_ << std::setprecision(12) << "{\"traceEvents\":[";
  }
  metadata("schema",
           "realm-frame-v1; durations ms; trace/zone durations us; bytes bytes; counters per-frame "
           "unless gauge");
#ifdef REALM_ALLOCATION_TRACKING
  metadata("allocations",
           "main-thread C++ new/new[] including aligned; excludes malloc, worker threads and "
           "export; includes foreign "
           "C++ new if interposed");
#else
  metadata("allocations",
           "unmeasured: configure REALM_ALLOCATION_TRACKING=ON; allocation fields zero");
#endif
#ifdef REALM_TRACY
  metadata("profiler", "Tracy 0.11.1 enabled; CPU zones; localhost on-demand");
#else
  metadata("profiler", "Tracy disabled; bounded native trace/export when requested");
#endif
}

Recorder::~Recorder() {
  if (active == this) {
    active = nullptr;
  }
  try {
    finish();
  } catch (...) {
  }
}

namespace {
void json_string(std::ostream& stream, std::string_view value) {
  constexpr char hex[] = "0123456789abcdef";
  stream << '"';
  for (const auto c : value) {
    const auto byte = static_cast<unsigned char>(c);
    if (c == '"' || c == '\\') {
      stream << '\\' << c;
    } else if (byte < 32) {
      stream << "\\u00" << hex[byte >> 4] << hex[byte & 15];
    } else {
      stream << c;
    }
  }
  stream << '"';
}
}

void Recorder::metadata(std::string_view key, std::string_view value) {
  samples_ << "{\"type\":\"metadata\",\"key\":";
  json_string(samples_, key);
  samples_ << ",\"value\":";
  json_string(samples_, value);
  samples_ << "}\n";
}

void Recorder::begin(std::uint64_t frame_id, Clock::time_point start) {
  if (active) {
    throw std::logic_error("Nested telemetry frame");
  }
  if (frame_id == 0) {
    epoch_ = start;
  }
  frame_start_us_ = std::chrono::duration<double, std::micro>(start - epoch_).count();
  frame_id_ = frame_id;
  zone_count_ = 0;
  dropped_ = 0;
  counters_.fill(0);
  active = this;
}

void Recorder::end(const Frame& f) {
  active = nullptr;
  samples_ << "{\"type\":\"frame\",\"frame_id\":" << f.id << ",\"start_us\":" << frame_start_us_
           << ",\"warmup\":" << f.warmup << ",\"capture\":" << f.capture
           << ",\"interval_valid\":" << f.interval_valid << ",\"interval_ms\":" << f.interval_ms
           << ",\"cpu_ms\":" << f.cpu_ms << ",\"present_ms\":" << f.present_ms
           << ",\"paced_ms\":" << f.paced_ms << ",\"input_to_submit_ms\":";
  if (f.input_to_submit_ms < 0) {
    samples_ << "null";
  } else {
    samples_ << f.input_to_submit_ms;
  }
  samples_ << ",\"drawable_width\":" << f.width << ",\"drawable_height\":" << f.height
           << ",\"swap_interval\":" << f.swap_interval << ",\"fps_cap\":" << f.fps_cap
           << ",\"dropped_zones\":" << dropped_ << ",\"counters\":{";
  for (std::size_t i = 0; i < counters_.size(); ++i) {
    if (i) {
      samples_ << ',';
    }
    samples_ << '"' << names[i] << "\":" << counters_[i];
  }
  samples_ << "}}\n";
  for (std::size_t i = 0; i < zone_count_; ++i) {
    const auto& z = zones_[i];
    samples_ << "{\"type\":\"zone\",\"frame_id\":" << frame_id_ << ",\"name\":\"" << z.name
             << "\",\"start_us\":" << z.start_us << ",\"duration_us\":" << z.duration_us
             << ",\"calls\":" << z.calls << ",\"aggregate\":" << aggregate_repeated_
             << ",\"allocations\":" << z.allocations << ",\"allocation_bytes\":" << z.bytes
             << "}\n";
    if (trace_.is_open()) {
      if (!trace_first_) {
        trace_ << ',';
      }
      trace_first_ = false;
      trace_ << "{\"ph\":\"X\",\"pid\":1,\"tid\":1,\"name\":\"" << z.name
             << "\",\"ts\":" << z.start_us << ",\"dur\":" << z.duration_us
             << ",\"args\":{\"frame_id\":" << frame_id_ << ",\"allocations\":" << z.allocations
             << ",\"bytes\":" << z.bytes << ",\"calls\":" << z.calls
             << ",\"aggregate\":" << aggregate_repeated_ << "}}";
    }
  }
  if (!samples_ || (trace_.is_open() && !trace_)) {
    throw std::runtime_error("Telemetry export failed");
  }
}

void Recorder::gpu(const GpuSample& g) {
  auto* saved = active;
  active = nullptr;
  samples_ << "{\"type\":\"gpu\",\"frame_id\":" << g.frame_id << ",\"normal\":" << g.normal
           << ",\"total_ms\":" << g.total_ms << ",\"upload_ms\":" << g.upload_ms
           << ",\"world_ms\":" << g.world_ms << ",\"effects_ms\":" << g.effects_ms
           << ",\"ui_ms\":" << g.ui_ms << ",\"models_ms\":" << g.models_ms
           << ",\"models_measured\":" << g.models_measured << ",\"models_total\":" << g.models_total
           << "}\n";
  active = saved;
}

void Recorder::finish() {
  if (finished_) {
    return;
  }
  finished_ = true;
  if (trace_.is_open()) {
    trace_ << "]}\n";
    trace_.flush();
    if (!trace_) {
      throw std::runtime_error("Telemetry trace export failed");
    }
  }
  samples_.flush();
  if (!samples_) {
    throw std::runtime_error("Telemetry sample export failed");
  }
}
}
