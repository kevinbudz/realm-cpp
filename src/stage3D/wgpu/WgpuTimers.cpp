#include "WgpuTimers.hpp"
#include "WgpuContext.hpp"
#include <cstring>

namespace realm::gfx::wgpu {
namespace {
// Four queries per slot: each marker pass writes both of its stamps (an undefined index reaches the browser
// as 0xFFFFFFFF, which Firefox rejects). The frame is the first stamp to the last.
constexpr std::uint32_t slot_queries = 4;
constexpr std::size_t stamp_bytes = slot_queries * sizeof(std::uint64_t);
constexpr std::uint64_t resolve_stride = 256;  // resolves into the resolve buffer are 256-aligned

// The map callback: the holder owns a reference to the result, so the write is safe whatever the timers'
// state. The callback runs once per map, even when the buffer is released first.
void on_timers_mapped(WGPUMapAsyncStatus status, WGPUStringView /*message*/, void* userdata1,
                      void* /*userdata2*/) {
  auto* holder = static_cast<std::shared_ptr<TimerMap>*>(userdata1);
  (*holder)->status = status;
  (*holder)->done = true;
  delete holder;
}

// An empty compute pass whose timestamps mark the start or the end of the frame's work.
void stamp(WGPUCommandEncoder encoder, WGPUQuerySet set, std::uint32_t begin, std::uint32_t end) {
  WGPUPassTimestampWrites writes{};
  writes.querySet = set;
  writes.beginningOfPassWriteIndex = begin;
  writes.endOfPassWriteIndex = end;
  WGPUComputePassDescriptor descriptor{};
  descriptor.timestampWrites = &writes;
  WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &descriptor);
  wgpuComputePassEncoderEnd(pass);
  wgpuComputePassEncoderRelease(pass);
}
}  // namespace

WgpuTimers::~WgpuTimers() {
  release();
}

void WgpuTimers::release() noexcept {
  for (auto& slot : slots_) {
    if (slot.readback) {
      wgpuBufferRelease(slot.readback);
      slot.readback = nullptr;
    }
    slot.pending = false;
  }
  if (resolve_) {
    wgpuBufferRelease(resolve_);
    resolve_ = nullptr;
  }
  if (query_set_) {
    wgpuQuerySetRelease(query_set_);
    query_set_ = nullptr;
  }
}

bool WgpuTimers::ensure() {
  if (query_set_) {
    return true;
  }
  if (failed_) {
    return false;
  }
  auto* device = context().device();
  WGPUQuerySetDescriptor set{};
  set.type = WGPUQueryType_Timestamp;
  set.count = static_cast<std::uint32_t>(slot_queries * slot_count);
  query_set_ = wgpuDeviceCreateQuerySet(device, &set);

  WGPUBufferDescriptor buffer{};
  buffer.usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc;
  buffer.size = slot_count * resolve_stride;
  resolve_ = wgpuDeviceCreateBuffer(device, &buffer);

  bool created = query_set_ && resolve_;
  for (auto& slot : slots_) {
    WGPUBufferDescriptor readback{};
    readback.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
    readback.size = stamp_bytes;
    slot.readback = wgpuDeviceCreateBuffer(device, &readback);
    created = created && slot.readback;
  }
  if (!created) {
    failed_ = true;
    release();
    return false;
  }
  return true;
}

void WgpuTimers::timing(telemetry::Recorder* recorder, std::uint64_t frame_id, bool normal) {
  recorder_ = recorder;
  sample_frame_ = frame_id;
  sample_normal_ = normal;
}

void WgpuTimers::begin_frame() {
  // Finished slots are read first, as GL and Vulkan poll at the start of a frame's draw.
  poll();
  if (active_) {
    return;  // already measuring this frame
  }
  if (!recorder_ || !context().timestamps() || !ensure()) {
    return;
  }
  const auto index = frame_ % slot_count;
  auto& slot = slots_[index];
  if (slot.pending) {
    ++skipped_;
    return;
  }
  slot.frame_id = sample_frame_;
  slot.normal = sample_normal_;
  const auto first = static_cast<std::uint32_t>(slot_queries * index);
  stamp(context().encoder(), query_set_, first, first + 1);
  active_ = &slot;
}

void WgpuTimers::end_frame() {
  if (active_) {
    const auto index = static_cast<std::size_t>(active_ - slots_.data());
    const auto first = static_cast<std::uint32_t>(slot_queries * index);
    WGPUCommandEncoder encoder = context().encoder();
    stamp(encoder, query_set_, first + 2, first + 3);
    wgpuCommandEncoderResolveQuerySet(encoder, query_set_, first, slot_queries, resolve_, index * resolve_stride);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, resolve_, index * resolve_stride, active_->readback, 0,
                                         stamp_bytes);
    submit_ = active_;
    active_ = nullptr;
  }
  ++frame_;
}

void WgpuTimers::submitted() {
  if (!submit_) {
    return;
  }
  Slot& slot = *submit_;
  submit_ = nullptr;
  slot.map = std::make_shared<TimerMap>();
  WGPUBufferMapCallbackInfo info{};
  info.mode = WGPUCallbackMode_AllowSpontaneous;
  info.callback = on_timers_mapped;
  info.userdata1 = new std::shared_ptr<TimerMap>(slot.map);
  wgpuBufferMapAsync(slot.readback, WGPUMapMode_Read, 0, stamp_bytes, info);
  slot.pending = true;
}

void WgpuTimers::poll() {
  for (auto& slot : slots_) {
    if (!slot.pending || !slot.map->done) {
      continue;
    }
    // The slot is free again whatever the map result; a failed map has nothing to unmap.
    slot.pending = false;
    if (slot.map->status != WGPUMapAsyncStatus_Success) {
      continue;
    }
    std::uint64_t ns[slot_queries]{};
    const auto* mapped = wgpuBufferGetConstMappedRange(slot.readback, 0, stamp_bytes);
    if (mapped) {
      std::memcpy(ns, mapped, stamp_bytes);
    }
    wgpuBufferUnmap(slot.readback);
    // Timestamps that do not advance are dropped (the browser may quantize them).
    if (ns[3] <= ns[0]) {
      continue;
    }
    gpu_ms_ = static_cast<double>(ns[3] - ns[0]) / 1000000.0;
    if (recorder_) {
      // The whole frame goes in the total and the first phase; the other phases and models are not measured.
      recorder_->gpu({slot.frame_id, slot.normal, gpu_ms_, gpu_ms_, 0, 0, 0, 0, 0, 0});
    }
  }
}

std::size_t WgpuTimers::pending() const {
  std::size_t count = 0;
  for (const auto& slot : slots_) {
    count += slot.pending ? 1 : 0;
  }
  return count;
}
}  // namespace realm::gfx::wgpu
