#include "alloc_counter.hpp"

#include <atomic>
#include <cstdlib>
#include <new>

// The sanitizer runtimes interpose operator new/delete themselves; replacing them here would mismatch
// allocators (ASan) or collide at link time (TSan).
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define BCC_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define BCC_SANITIZED 1
#endif
#endif

#ifndef BCC_SANITIZED
namespace {
std::atomic<std::uint64_t> g_allocs{0};

void* allocate(std::size_t n) {
  g_allocs.fetch_add(1, std::memory_order_relaxed);
  if (void* p = std::malloc(n == 0 ? 1 : n)) return p;
  throw std::bad_alloc();
}
}  // namespace

namespace bcc {
std::uint64_t allocationCount() { return g_allocs.load(std::memory_order_relaxed); }
bool allocationCounterActive() { return true; }
}  // namespace bcc

void* operator new(std::size_t n) { return allocate(n); }
void* operator new[](std::size_t n) { return allocate(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#else
namespace bcc {
std::uint64_t allocationCount() { return 0; }
bool allocationCounterActive() { return false; }
}  // namespace bcc
#endif
