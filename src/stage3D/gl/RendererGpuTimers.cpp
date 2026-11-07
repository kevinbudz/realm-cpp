#include "GlApi.hpp"
#include "RendererGpuTimers.hpp"
#include <algorithm>
#if REALM_GLES
#include <emscripten/html5.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GLES2/gl2ext.h>
#endif

namespace realm {
namespace {
#if REALM_GLES
// Core GLES 3.0 has no timestamp queries; WebGL 2 offers them through EXT_disjoint_timer_query_webgl2.
void stamp(GLuint id) { glQueryCounterEXT(id, GL_TIMESTAMP_EXT); }

bool ready(GLuint id) {
  GLuint value{};
  glGetQueryObjectuiv(id, GL_QUERY_RESULT_AVAILABLE, &value);
  return value != 0;
}

GLuint64 result(GLuint id) {
  GLuint64 ns{};
  glGetQueryObjectui64vEXT(id, GL_QUERY_RESULT, &ns);
  return ns;
}
#else
void stamp(GLuint id) { glQueryCounter(id, GL_TIMESTAMP); }

bool ready(GLuint id) {
  GLint value{};
  glGetQueryObjectiv(id, GL_QUERY_RESULT_AVAILABLE, &value);
  return value != 0;
}

GLuint64 result(GLuint id) {
  GLuint64 ns{};
  glGetQueryObjectui64v(id, GL_QUERY_RESULT, &ns);
  return ns;
}
#endif
}

RendererGpuTimers::~RendererGpuTimers() {
  for (auto& timer : timers_) {
    if (timer.ids[0]) {
      glDeleteQueries(static_cast<GLsizei>(timestamps), timer.ids.data());
    }
  }
}

void RendererGpuTimers::timing(telemetry::Recorder* recorder, std::uint64_t frame_id, bool normal) {
  recorder_ = recorder;
  sample_frame_ = frame_id;
  sample_normal_ = normal;
}

bool RendererGpuTimers::begin_frame() {
  active_ = &timers_[frame_ % timers_.size()];
  measuring_ = false;
  if (!recorder_) {
    return false;
  }
#if REALM_GLES
  if (!timer_ext_checked_) {
    timer_ext_checked_ = true;
    timer_ext_ = emscripten_webgl_enable_extension(emscripten_webgl_get_current_context(),
                                                   "EXT_disjoint_timer_query_webgl2");
  }
  if (!timer_ext_) {
    return false;
  }
#endif
  if (active_->pending) {
    ++skipped_;
    return false;
  }
  if (!active_->ids[0]) {
    glGenQueries(static_cast<GLsizei>(timestamps), active_->ids.data());
  }
  active_->frame_id = sample_frame_;
  active_->normal = sample_normal_;
  active_->models = active_->models_total = 0;
  stamp(active_->ids[0]);
  measuring_ = true;
  return true;
}

void RendererGpuTimers::phase(std::size_t index) {
  if (measuring_) {
    stamp(active_->ids[index]);
  }
}

bool RendererGpuTimers::model_begin(std::size_t instances) {
  if (!measuring_) {
    return false;
  }
  active_->models_total += instances;
  const bool measured = active_->models < model_query_limit;
  if (measured) {
    stamp(active_->ids[5 + 2 * active_->models]);
  }
  return measured;
}

void RendererGpuTimers::model_end(bool measured) {
  if (!measuring_ || !measured) {
    return;
  }
  stamp(active_->ids[6 + 2 * active_->models]);
  ++active_->models;
}

void RendererGpuTimers::finish_sample() {
  if (!measuring_) {
    return;
  }
  stamp(active_->ids[4]);
  active_->pending = true;
}

void RendererGpuTimers::advance_frame() {
  ++frame_;
  active_ = nullptr;
  measuring_ = false;
}

std::size_t RendererGpuTimers::pending() const {
  return static_cast<std::size_t>(std::count_if(timers_.begin(), timers_.end(),
                                                [](const auto& timer) { return timer.pending; }));
}

void RendererGpuTimers::poll() {
  if (!recorder_) {
    return;
  }
  REALM_ZONE("render.gpu_poll");
#if REALM_GLES
  if (!timer_ext_) {
    return;
  }
  // Reading GPU_DISJOINT_EXT clears it, so keep the event until no timer is pending.
  GLint disjoint{};
  glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);
  disjoint_ = disjoint_ || disjoint;
#endif
  for (auto& timer : timers_) {
    if (timer.pending) {
      if (!ready(timer.ids[4])) {
        continue;
      }
#if REALM_GLES
      // Timestamps from a disjoint interval are garbage: drop them.
      if (disjoint_) {
        timer.pending = false;
        continue;
      }
#endif
      std::array<GLuint64, timestamps> ns{};
      for (std::size_t i = 0; i < 5 + 2 * timer.models; ++i) {
        ns[i] = result(timer.ids[i]);
      }
      const auto elapsed = [&](std::size_t a, std::size_t b) {
        return static_cast<double>(ns[b] - ns[a]) / 1000000.0;
      };
      double models{};
      for (std::size_t i = 0; i < timer.models; ++i) {
        models += elapsed(5 + 2 * i, 6 + 2 * i);
      }
      gpu_ms_ = elapsed(0, 4);
      recorder_->gpu({timer.frame_id, timer.normal, gpu_ms_, elapsed(0, 1), elapsed(1, 2),
                      elapsed(2, 3), elapsed(3, 4), models, timer.models, timer.models_total});
      timer.pending = false;
    }
  }
#if REALM_GLES
  if (!pending()) {
    disjoint_ = false;
  }
#endif
}
}
