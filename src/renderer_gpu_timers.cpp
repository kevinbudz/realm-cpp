#include <GL/glew.h>
#include "renderer_gpu_timers.hpp"
#include <algorithm>

namespace realm {
RendererGpuTimers::~RendererGpuTimers() {
  for (auto& timer : timers_)
    if (timer.ids[0]) glDeleteQueries(static_cast<GLsizei>(timestamps), timer.ids.data());
}

void RendererGpuTimers::timing(telemetry::Recorder* recorder, std::uint64_t frame_id, bool normal) {
  recorder_ = recorder;
  sample_frame_ = frame_id;
  sample_normal_ = normal;
}

bool RendererGpuTimers::begin_frame() {
  active_ = &timers_[frame_ % timers_.size()];
  measuring_ = false;
  if (!recorder_) return false;
  if (active_->pending) {
    ++skipped_;
    return false;
  }
  if (!active_->ids[0]) glGenQueries(static_cast<GLsizei>(timestamps), active_->ids.data());
  active_->frame_id = sample_frame_;
  active_->normal = sample_normal_;
  active_->models = active_->models_total = 0;
  glQueryCounter(active_->ids[0], GL_TIMESTAMP);
  measuring_ = true;
  return true;
}

void RendererGpuTimers::phase(std::size_t index) {
  if (measuring_) glQueryCounter(active_->ids[index], GL_TIMESTAMP);
}

bool RendererGpuTimers::model_begin(std::size_t instances) {
  if (!measuring_) return false;
  active_->models_total += instances;
  const bool measured = active_->models < model_query_limit;
  if (measured) glQueryCounter(active_->ids[5 + 2 * active_->models], GL_TIMESTAMP);
  return measured;
}

void RendererGpuTimers::model_end(bool measured) {
  if (!measuring_ || !measured) return;
  glQueryCounter(active_->ids[6 + 2 * active_->models], GL_TIMESTAMP);
  ++active_->models;
}

void RendererGpuTimers::finish_sample() {
  if (!measuring_) return;
  glQueryCounter(active_->ids[4], GL_TIMESTAMP);
  active_->pending = true;
}

void RendererGpuTimers::advance_frame() {
  ++frame_;
  active_ = nullptr;
  measuring_ = false;
}

std::size_t RendererGpuTimers::pending() const {
  return static_cast<std::size_t>(
      std::count_if(timers_.begin(), timers_.end(), [](const auto& timer) { return timer.pending; }));
}

void RendererGpuTimers::poll() {
  if (!recorder_) return;
  REALM_ZONE("render.gpu_poll");
  for (auto& timer : timers_)
    if (timer.pending) {
      GLint ready{};
      glGetQueryObjectiv(timer.ids[4], GL_QUERY_RESULT_AVAILABLE, &ready);
      if (!ready) continue;
      std::array<GLuint64, timestamps> ns{};
      for (std::size_t i = 0; i < 5 + 2 * timer.models; ++i)
        glGetQueryObjectui64v(timer.ids[i], GL_QUERY_RESULT, &ns[i]);
      const auto elapsed = [&](std::size_t a, std::size_t b) { return static_cast<double>(ns[b] - ns[a]) / 1000000.0; };
      double models{};
      for (std::size_t i = 0; i < timer.models; ++i) models += elapsed(5 + 2 * i, 6 + 2 * i);
      gpu_ms_ = elapsed(0, 4);
      recorder_->gpu({timer.frame_id, timer.normal, gpu_ms_, elapsed(0, 1), elapsed(1, 2), elapsed(2, 3), elapsed(3, 4),
                      models, timer.models, timer.models_total});
      timer.pending = false;
    }
}
}
