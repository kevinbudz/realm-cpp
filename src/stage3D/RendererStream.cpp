#include <GL/glew.h>
#include "RendererStream.hpp"
#include "realm/util/Telemetry.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace realm {
RendererStream::RendererStream(std::size_t capacity, bool persistent_streaming,
                               std::size_t persistent_minimum_bytes)
    : persistent_(persistent_streaming && (GLEW_VERSION_4_4 || GLEW_ARB_buffer_storage)),
      minimum_bytes_(persistent_minimum_bytes),
      capacity_(capacity) {
  try {
    glGenBuffers(static_cast<GLsizei>(buffers_.size()), buffers_.data());
    glGenBuffers(1, &overlay_buffer_);
    glGenBuffers(1, &fallback_buffer_);
    for (std::size_t i = 0; i < buffers_.size(); ++i) {
      glBindBuffer(GL_ARRAY_BUFFER, buffers_[i]);
      capacities_[i] = capacity_;
      const auto bytes = static_cast<GLsizeiptr>(capacity_ * sizeof(Quad));
      if (persistent_) {
        constexpr auto flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
        glBufferStorage(GL_ARRAY_BUFFER, bytes, nullptr, flags);
        mapped_[i] = glMapBufferRange(GL_ARRAY_BUFFER, 0, bytes, flags);
        if (!mapped_[i]) {
          throw std::runtime_error("Persistent quad buffer mapping failed");
        }
      } else {
        glBufferData(GL_ARRAY_BUFFER, bytes, nullptr, GL_STREAM_DRAW);
      }
    }
  } catch (...) {
    release();
    throw;
  }
}

RendererStream::~RendererStream() {
  release();
}

void RendererStream::release() {
  for (std::size_t i = 0; i < buffers_.size(); ++i) {
    if (fences_[i]) {
      glDeleteSync(static_cast<GLsync>(fences_[i]));
    }
    if (mapped_[i] && buffers_[i]) {
      glBindBuffer(GL_ARRAY_BUFFER, buffers_[i]);
      glUnmapBuffer(GL_ARRAY_BUFFER);
    }
  }

  if (fallback_buffer_) {
    glDeleteBuffers(1, &fallback_buffer_);
  }
  if (overlay_buffer_) {
    glDeleteBuffers(1, &overlay_buffer_);
  }

  glDeleteBuffers(static_cast<GLsizei>(buffers_.size()), buffers_.data());
  buffers_.fill(0);
  mapped_.fill(nullptr);
  fences_.fill(nullptr);
  capacities_.fill(0);
  fallback_buffer_ = overlay_buffer_ = 0;
}

void RendererStream::ensure_capacity(std::size_t total) {
  if (total > capacity_) {
    capacity_ = std::min(std::size_t{1000000}, std::max(total, capacity_ * 2));
  }
}

std::uint32_t RendererStream::upload(std::span<const Quad> quads, std::span<const Quad> overlay) {
  REALM_ZONE("render.quad_upload");
  telemetry::add(telemetry::Counter::quad_upload_bytes, quads.size_bytes() + overlay.size_bytes());
  const auto slot = frame_ % buffers_.size();
  // Small post-cache streams benchmark faster with orphaning on the Nexus workload; reserve the
  // mapped path for substantial streams. A zero threshold lets differential tests exercise it at
  // any size.
  const bool mapped = persistent_ && quads.size_bytes() + overlay.size_bytes() >= minimum_bytes_;
  auto buffer = mapped || !persistent_ ? buffers_[slot] : fallback_buffer_;
  bool available = true;
  if (mapped && fences_[slot]) {
    const auto result = glClientWaitSync(static_cast<GLsync>(fences_[slot]), 0, 0);
    available = result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED;
    if (result == GL_WAIT_FAILED) {
      throw std::runtime_error("Quad stream fence failed");
    }
    if (available) {
      glDeleteSync(static_cast<GLsync>(fences_[slot]));
      fences_[slot] = nullptr;
    }
  }
  // Busy slots never stall the CPU: orphan separate mutable storage for this frame.
  if (mapped && !available) {
    buffer = fallback_buffer_;
  }
  glBindBuffer(GL_ARRAY_BUFFER, buffer);
  if (mapped && available) {
    if (capacities_[slot] < capacity_) {
      glUnmapBuffer(GL_ARRAY_BUFFER);
      mapped_[slot] = nullptr;
      glDeleteBuffers(1, &buffers_[slot]);
      glGenBuffers(1, &buffers_[slot]);
      buffer = buffers_[slot];
      glBindBuffer(GL_ARRAY_BUFFER, buffer);
      capacities_[slot] = capacity_;
      constexpr auto flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
      const auto bytes = static_cast<GLsizeiptr>(capacity_ * sizeof(Quad));
      glBufferStorage(GL_ARRAY_BUFFER, bytes, nullptr, flags);
      mapped_[slot] = glMapBufferRange(GL_ARRAY_BUFFER, 0, bytes, flags);
      if (!mapped_[slot]) {
        throw std::runtime_error("Persistent quad buffer growth failed");
      }
    }
    auto* target = static_cast<std::byte*>(mapped_[slot]);
    if (!quads.empty()) {
      std::memcpy(target, quads.data(), quads.size_bytes());
    }
    if (!overlay.empty()) {
      std::memcpy(target + quads.size_bytes(), overlay.data(), overlay.size_bytes());
    }
  } else {
    // Orphaned at this submission's size: the driver allocates the bytes drawn, not the whole
    // capacity.
    const auto bytes = static_cast<GLsizeiptr>(quads.size_bytes() + overlay.size_bytes());
    if (overlay.empty()) {
      glBufferData(GL_ARRAY_BUFFER, bytes, quads.data(), GL_STREAM_DRAW);
    } else {
      glBufferData(GL_ARRAY_BUFFER, bytes, nullptr, GL_STREAM_DRAW);
      if (!quads.empty()) {
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(quads.size_bytes()),
                        quads.data());
      }
      glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(quads.size_bytes()),
                      static_cast<GLsizeiptr>(overlay.size_bytes()), overlay.data());
    }
  }
  return buffer;
}

std::uint32_t RendererStream::upload_overlay(std::span<const Quad> overlay) {
  glBindBuffer(GL_ARRAY_BUFFER, overlay_buffer_);
  telemetry::add(telemetry::Counter::quad_upload_bytes, overlay.size_bytes());
  // Orphaned each call: the previous frame's draw may still be reading the old storage.
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(overlay.size_bytes()), overlay.data(),
               GL_STREAM_DRAW);
  return overlay_buffer_;
}

void RendererStream::finish_frame(std::uint32_t buffer) {
  const auto slot = frame_ % buffers_.size();
  if (persistent_ && buffer == buffers_[slot]) {
    fences_[slot] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  }
  ++frame_;
}
}
