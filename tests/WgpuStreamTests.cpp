#include "realm/stage3D/wgpu/StreamStager.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool value, const char* message) {
  if (!value) {
    throw std::runtime_error(message);
  }
}

struct Upload {
  std::size_t chunk{};
  std::uint64_t offset{};
  std::vector<std::byte> bytes;
};

void verify_stream_staging() {
  using realm::gfx::wgpu::StreamStager;
  StreamStager stager(1024);

  const auto empty = stager.write(nullptr, 0, 16);
  check(empty.chunk == 0 && empty.offset == 0 && stager.chunk_count() == 0,
        "empty scene write should not allocate a chunk");
  std::size_t empty_uploads = 0;
  stager.flush([&](std::size_t, std::uint64_t, std::span<const std::byte>) { ++empty_uploads; });
  check(empty_uploads == 0, "empty scene unexpectedly queued an upload");

  const std::array<std::byte, 4> first{std::byte{0x11}, std::byte{0x12}, std::byte{0x13},
                                       std::byte{0x14}};
  const std::array<std::byte, 8> second{std::byte{0x21}, std::byte{0x22}, std::byte{0x23},
                                        std::byte{0x24}, std::byte{0x25}, std::byte{0x26},
                                        std::byte{0x27}, std::byte{0x28}};
  std::array<std::byte, 80> uniform_a{};
  std::array<std::byte, 80> uniform_b{};
  uniform_a.fill(std::byte{0x31});
  uniform_b.fill(std::byte{0x42});
  std::vector<std::byte> large(1200, std::byte{0x53});
  const std::array<std::byte, 12> tail{std::byte{0x61}, std::byte{0x62}, std::byte{0x63},
                                       std::byte{0x64}, std::byte{0x65}, std::byte{0x66},
                                       std::byte{0x67}, std::byte{0x68}, std::byte{0x69},
                                       std::byte{0x6a}, std::byte{0x6b}, std::byte{0x6c}};
  std::vector<std::byte> very_large(3500, std::byte{0x74});

  const auto a = stager.write(first.data(), first.size(), 4);
  const auto b = stager.write(second.data(), second.size(), 4);
  const auto ua = stager.write(uniform_a.data(), uniform_a.size(), 256);
  const auto ub = stager.write(uniform_b.data(), uniform_b.size(), 256);
  const auto l = stager.write(large.data(), large.size(), 4);
  const auto t = stager.write(tail.data(), tail.size(), 16);
  const auto vl = stager.write(very_large.data(), very_large.size(), 4);

  check(a.chunk == 0 && a.offset == 0 && b.chunk == 0 && b.offset == 4,
        "four-byte aligned allocations have wrong offsets");
  check(ua.chunk == 0 && ua.offset == 256 && ub.chunk == 0 && ub.offset == 512,
        "uniform blocks are not independently aligned");
  check(l.chunk == 1 && l.offset == 0 && t.chunk == 1 && t.offset == 1200 && vl.chunk == 2 &&
            vl.offset == 0,
        "stream chunk rollover or large allocation is incorrect");
  check(stager.chunk_count() == 3 && stager.chunk_capacity(1) >= 1200 &&
            stager.chunk_capacity(2) >= 3500,
        "stream chunks did not grow to fit their allocations");

  std::vector<Upload> uploads;
  std::vector<std::vector<std::byte>> gpu(stager.chunk_count());
  stager.flush([&](std::size_t chunk, std::uint64_t offset, std::span<const std::byte> bytes) {
    check(offset % 4 == 0 && bytes.size() % 4 == 0,
          "mock writeBuffer call is not four-byte aligned");
    auto& target = gpu.at(chunk);
    target.resize(std::max(target.size(), static_cast<std::size_t>(offset) + bytes.size()));
    std::memcpy(target.data() + static_cast<std::size_t>(offset), bytes.data(), bytes.size());
    uploads.push_back({chunk, offset, {bytes.begin(), bytes.end()}});
  });
  check(uploads.size() == 3, "expected exactly one queue write per dirty chunk");
  check(uploads[0].chunk == 0 && uploads[0].offset == 0 && uploads[0].bytes.size() == 592,
        "chunk zero upload did not coalesce its aligned writes");
  check(uploads[1].chunk == 1 && uploads[1].offset == 0 && uploads[1].bytes.size() == 1212,
        "chunk one upload did not coalesce its allocations");
  check(uploads[2].chunk == 2 && uploads[2].offset == 0 && uploads[2].bytes.size() == 3500,
        "large chunk upload has the wrong range");
  check(std::equal(first.begin(), first.end(), gpu[0].begin()) &&
            std::equal(second.begin(), second.end(), gpu[0].begin() + 4) &&
            std::equal(uniform_a.begin(), uniform_a.end(), gpu[0].begin() + 256) &&
            std::equal(uniform_b.begin(), uniform_b.end(), gpu[0].begin() + 512),
        "distinct instance or uniform bytes were not staged at their returned offsets");
  check(std::equal(large.begin(), large.end(), gpu[1].begin()) &&
            std::equal(tail.begin(), tail.end(), gpu[1].begin() + 1200) &&
            std::equal(very_large.begin(), very_large.end(), gpu[2].begin()),
        "rolled-over or large writes changed contents");

  std::size_t repeated_uploads = 0;
  stager.flush([&](std::size_t, std::uint64_t, std::span<const std::byte>) { ++repeated_uploads; });
  check(repeated_uploads == 0, "repeated flush re-uploaded clean chunks");

  stager.reset();
  check(stager.chunk_count() == 3, "reset discarded reusable stream chunks");
  const std::array<std::byte, 12> next_frame{std::byte{0x7a}, std::byte{0x7b}, std::byte{0x7c},
                                             std::byte{0x7d}, std::byte{0x7e}, std::byte{0x7f},
                                             std::byte{0x70}, std::byte{0x71}, std::byte{0x72},
                                             std::byte{0x73}, std::byte{0x74}, std::byte{0x75}};
  const auto next = stager.write(next_frame.data(), next_frame.size(), 4);
  check(next.chunk == 0 && next.offset == 0, "next frame did not reuse the first chunk");
  uploads.clear();
  stager.flush([&](std::size_t chunk, std::uint64_t offset, std::span<const std::byte> bytes) {
    uploads.push_back({chunk, offset, {bytes.begin(), bytes.end()}});
  });
  check(uploads.size() == 1 && uploads[0].chunk == 0 && uploads[0].offset == 0 &&
            uploads[0].bytes.size() == next_frame.size() &&
            std::equal(next_frame.begin(), next_frame.end(), uploads[0].bytes.begin()),
        "next frame did not upload only its fresh bytes");

  bool rejected_unaligned_size = false;
  const std::array<std::byte, 5> bad_size{};
  try {
    (void)stager.write(bad_size.data(), bad_size.size(), 4);
  } catch (const std::invalid_argument&) {
    rejected_unaligned_size = true;
  }
  check(rejected_unaligned_size, "non-four-byte write size was accepted");

  // Reserved space is packed in place: the pointer names the span's bytes in the staged chunk.
  stager.reset();
  const auto nothing = stager.reserve(0, 16);
  check(nothing.data == nullptr && nothing.span.chunk == 0 && nothing.span.offset == 0,
        "empty reservation should not return storage");
  const auto head = stager.write(first.data(), first.size(), 4);
  auto packed = stager.reserve(88, 16);
  check(head.chunk == packed.span.chunk && packed.span.offset == 16 && packed.data != nullptr,
        "reserved span is not aligned after the preceding write");
  for (std::size_t i = 0; i < 88; ++i) {
    packed.data[i] = static_cast<std::byte>(0x90 + i);
  }
  std::vector<std::byte> reserved_upload;
  std::uint64_t reserved_offset = ~std::uint64_t{0};
  stager.flush([&](std::size_t chunk, std::uint64_t offset, std::span<const std::byte> bytes) {
    check(chunk == packed.span.chunk, "reserved bytes flushed to the wrong chunk");
    reserved_offset = offset;
    reserved_upload.assign(bytes.begin(), bytes.end());
  });
  check(reserved_offset == 0 && reserved_upload.size() == 104,
        "reserved bytes were not included in the chunk upload");
  bool reserved_contents = std::equal(first.begin(), first.end(), reserved_upload.begin());
  for (std::size_t i = 0; i < 88; ++i) {
    reserved_contents = reserved_contents &&
                        reserved_upload[16 + i] == static_cast<std::byte>(0x90 + i);
  }
  check(reserved_contents, "reserved bytes differ from their staged contents");
}
}  // namespace

int main() {
  try {
    verify_stream_staging();
    std::cout << "WebGPU stream staging tests passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
