#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace realm::gfx::wgpu {

// CPU-side staging for a frame's WebGPU stream. Multiple small writes are copied into their final
// offsets here, then submitted as one contiguous write per used GPU chunk.
class StreamStager {
 public:
  struct Span {
    std::size_t chunk = 0;
    std::uint64_t offset = 0;
  };

  explicit StreamStager(std::uint64_t chunk_bytes = std::uint64_t{4} << 20)
      : chunk_bytes_(chunk_bytes) {
    if (chunk_bytes_ == 0 || chunk_bytes_ % 256 != 0) {
      throw std::invalid_argument("Invalid stream chunk capacity");
    }
  }

  struct Reservation {
    Span span;
    // Where the caller writes the reserved bytes. Valid until the next reserve() or write() call, which
    // may grow the chunk.
    std::byte* data = nullptr;
  };

  // Reserves bytes at an aligned offset of the current frame and returns a pointer to write them through,
  // so the caller can pack directly into staging. Only the reserved bytes are meaningful: alignment
  // gaps hold unspecified data (possibly stale from earlier frames), which the GPU never reads.
  [[nodiscard]] Reservation reserve(std::size_t bytes, std::uint64_t alignment) {
    if (bytes == 0) {
      return {};
    }
    if (bytes % 4 != 0 || alignment < 4 || alignment % 4 != 0) {
      throw std::invalid_argument("Invalid WebGPU stream write");
    }
    const auto need = static_cast<std::uint64_t>(bytes);

    std::size_t index = current_;
    for (; index < chunks_.size(); ++index) {
      const auto start = align_up(chunks_[index].used, alignment);
      if (start <= chunks_[index].capacity && need <= chunks_[index].capacity - start) {
        break;
      }
    }
    if (index == chunks_.size()) {
      chunks_.push_back(Chunk{new_capacity(need, alignment)});
    }

    current_ = index;
    auto& chunk = chunks_[index];
    const auto offset = align_up(chunk.used, alignment);
    const auto end = offset + need;
    if (end > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
      throw std::length_error("WebGPU stream staging allocation is too large");
    }
    // Chunk storage is sized to its full capacity once, so this only bumps the used count and never
    // zero-fills. The loop above guarantees end <= capacity.
    chunk.used = end;
    return Reservation{Span{index, offset}, chunk.bytes.data() + static_cast<std::size_t>(offset)};
  }

  [[nodiscard]] Span write(const void* data, std::size_t bytes, std::uint64_t alignment) {
    if (bytes == 0) {
      return {};
    }
    if (data == nullptr) {
      throw std::invalid_argument("Invalid WebGPU stream write");
    }
    const auto reserved = reserve(bytes, alignment);
    std::memcpy(reserved.data, data, bytes);
    return reserved.span;
  }

  template <typename Upload>
  void flush(Upload&& upload) {
    for (std::size_t i = 0; i < chunks_.size(); ++i) {
      auto& chunk = chunks_[i];
      if (chunk.uploaded == chunk.used) {
        continue;
      }
      const auto bytes = chunk.used - chunk.uploaded;
      // Every write and alignment is four-byte aligned, which keeps WebGPU writeBuffer's offset
      // and size requirements valid even when a caller flushes more than once in a frame.
      upload(
          i, chunk.uploaded,
          std::span<const std::byte>(chunk.bytes.data() + static_cast<std::size_t>(chunk.uploaded),
                                     static_cast<std::size_t>(bytes)));
      chunk.uploaded = chunk.used;
    }
  }

  // Starts a fresh frame while keeping the chunk storage for reuse. Storage is never cleared or shrunk.
  void reset() noexcept {
    for (auto& chunk : chunks_) {
      chunk.used = 0;
      chunk.uploaded = 0;
    }
    current_ = 0;
  }

  void clear() noexcept {
    chunks_.clear();
    current_ = 0;
  }

  [[nodiscard]] std::size_t chunk_count() const noexcept { return chunks_.size(); }

  [[nodiscard]] std::uint64_t chunk_capacity(std::size_t index) const {
    return chunks_.at(index).capacity;
  }

 private:
  struct Chunk {
    explicit Chunk(std::uint64_t chunk_capacity)
        : capacity(chunk_capacity), bytes(static_cast<std::size_t>(chunk_capacity)) {}

    std::uint64_t capacity{};
    std::uint64_t used{};
    std::uint64_t uploaded{};
    // Fixed-size storage of `capacity` bytes, zeroed once at creation and reused across frames.
    std::vector<std::byte> bytes;
  };

  std::vector<Chunk> chunks_;
  std::uint64_t chunk_bytes_;
  std::size_t current_{};

  static std::uint64_t align_up(std::uint64_t value, std::uint64_t alignment) {
    const auto remainder = value % alignment;
    if (remainder == 0) {
      return value;
    }
    const auto padding = alignment - remainder;
    if (value > std::numeric_limits<std::uint64_t>::max() - padding) {
      throw std::length_error("WebGPU stream alignment overflow");
    }
    return value + padding;
  }

  std::uint64_t new_capacity(std::uint64_t need, std::uint64_t alignment) const {
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    if (need > maximum - alignment || need + alignment > maximum / 2) {
      throw std::length_error("WebGPU stream chunk capacity overflow");
    }
    auto capacity = std::max(chunk_bytes_, (need + alignment) * 2);
    if (capacity > maximum - 255) {
      throw std::length_error("WebGPU stream chunk capacity overflow");
    }
    capacity = (capacity + 255) & ~std::uint64_t{255};
    if (capacity > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
      throw std::length_error("WebGPU stream chunk capacity is too large");
    }
    return capacity;
  }
};

}  // namespace realm::gfx::wgpu
