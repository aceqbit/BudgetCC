#pragma once
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

#include "core/clock.hpp"

namespace bcc {

// POD event: the callable lives inline, so scheduling never allocates (E1).
// Callables must be trivially copyable and at most kInlineBytes (capture `this` plus a few words).
struct Event {
  static constexpr std::size_t kInlineBytes = 40;

  Time time = 0;
  std::uint64_t seq = 0;
  void (*invoke)(void*) = nullptr;
  alignas(8) unsigned char buf[kInlineBytes];

  void run() { invoke(buf); }
};

// Binary min-heap of 24-byte keys (time, seq, slot) over a slot array holding the 64-byte events (E2).
// Sifting moves keys only; event payloads stay put and are recycled through a free list.
class EventQueue {
 public:
  EventQueue() {
    heap_.reserve(kInitialCapacity);
    slots_.reserve(kInitialCapacity);
    free_.reserve(kInitialCapacity);
  }

  template <class F>
  void push(Time t, F fn) {
    static_assert(std::is_trivially_copyable<F>::value, "event callables must be trivially copyable");
    static_assert(sizeof(F) <= Event::kInlineBytes, "event callable too large for the inline buffer");
    static_assert(alignof(F) <= 8, "event callable over-aligned");
    std::uint32_t slot;
    if (free_.empty()) {
      slot = static_cast<std::uint32_t>(slots_.size());
      slots_.emplace_back();
    } else {
      slot = free_.back();
      free_.pop_back();
    }
    Event& e = slots_[slot];
    e.time = t;
    e.seq = next_seq_;
    e.invoke = [](void* p) { (*static_cast<F*>(p))(); };
    ::new (static_cast<void*>(e.buf)) F(fn);
    siftUp(Key{t, next_seq_++, slot});
  }
  bool empty() const { return heap_.empty(); }
  std::size_t size() const { return heap_.size(); }
  Time nextTime() const { return heap_.front().time; }
  Event pop() {
    const Key top = heap_.front();
    Event e = slots_[top.slot];
    free_.push_back(top.slot);
    const Key last = heap_.back();
    heap_.pop_back();
    if (!heap_.empty()) siftDown(last);
    ++popped_;
    return e;
  }
  std::uint64_t eventsProcessed() const { return popped_; }

 private:
  static constexpr std::size_t kInitialCapacity = 4096;
  struct Key {
    Time time;
    std::uint64_t seq;
    std::uint32_t slot;
  };
  static bool before(const Key& a, const Key& b) { return a.time != b.time ? a.time < b.time : a.seq < b.seq; }

  void siftUp(Key k) {
    std::size_t i = heap_.size();
    heap_.push_back(k);
    while (i > 0) {
      const std::size_t parent = (i - 1) / 2;
      if (!before(k, heap_[parent])) break;
      heap_[i] = heap_[parent];
      i = parent;
    }
    heap_[i] = k;
  }
  // Places k at the root hole and moves it down.
  void siftDown(Key k) {
    const std::size_t n = heap_.size();
    std::size_t i = 0;
    for (;;) {
      std::size_t child = 2 * i + 1;
      if (child >= n) break;
      if (child + 1 < n && before(heap_[child + 1], heap_[child])) ++child;
      if (!before(heap_[child], k)) break;
      heap_[i] = heap_[child];
      i = child;
    }
    heap_[i] = k;
  }

  std::vector<Key> heap_;
  std::vector<Event> slots_;
  std::vector<std::uint32_t> free_;
  std::uint64_t next_seq_ = 0;
  std::uint64_t popped_ = 0;
};

}  // namespace bcc
