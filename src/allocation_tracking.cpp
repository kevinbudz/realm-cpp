// Optional viewer-only C++ allocation traffic. No allocation headers, locks,
// live-byte guesses or tracking containers; the exported numbers are traffic.
#include "realm/telemetry.hpp"
#include <cstdlib>
#include <algorithm>
#include <new>
#ifdef _WIN32
#include <malloc.h>
#endif
namespace {
void* allocate(std::size_t size, std::size_t alignment = 0) {
  size = size ? size : 1;
  void* result{};
  for (;;) {
    if (!alignment)
      result = std::malloc(size);
    else {
      alignment = std::max(alignment, sizeof(void*));
#ifdef _WIN32
      result = _aligned_malloc(size, alignment);
#else
      if (posix_memalign(&result, alignment, size) != 0) result = nullptr;
#endif
    }
    if (result) break;
    const auto handler = std::get_new_handler();
    if (!handler) throw std::bad_alloc();
    handler();
  }
  realm::telemetry::add(realm::telemetry::Counter::allocations);
  realm::telemetry::add(realm::telemetry::Counter::allocation_bytes, size);
  return result;
}
void aligned_free(void* p) noexcept {
#ifdef _WIN32
  _aligned_free(p);
#else
  std::free(p);
#endif
}
}
void* operator new(std::size_t n) { return allocate(n); }
void* operator new[](std::size_t n) { return allocate(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void* operator new(std::size_t n, std::align_val_t a) { return allocate(n, static_cast<std::size_t>(a)); }
void* operator new[](std::size_t n, std::align_val_t a) { return allocate(n, static_cast<std::size_t>(a)); }
void operator delete(void* p, std::align_val_t) noexcept { aligned_free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { aligned_free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { aligned_free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { aligned_free(p); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
  try {
    return allocate(n);
  } catch (...) {
    return nullptr;
  }
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
  try {
    return allocate(n);
  } catch (...) {
    return nullptr;
  }
}
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
  try {
    return allocate(n, static_cast<std::size_t>(a));
  } catch (...) {
    return nullptr;
  }
}
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
  try {
    return allocate(n, static_cast<std::size_t>(a));
  } catch (...) {
    return nullptr;
  }
}
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { aligned_free(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { aligned_free(p); }
