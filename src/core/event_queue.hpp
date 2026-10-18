#pragma once
#include <cstddef>
#include <cstdint>
#include <new>
#include <queue>
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

class EventQueue {
 public:
  EventQueue() {
    std::vector<Event> storage;
    storage.reserve(kInitialCapacity);  // PHASE1-NAIVE(E2): std::priority_queue over the reserved vector
    q_ = Heap(Later(), std::move(storage));
  }

  template <class F>
  void push(Time t, F fn) {
    static_assert(std::is_trivially_copyable<F>::value, "event callables must be trivially copyable");
    static_assert(sizeof(F) <= Event::kInlineBytes, "event callable too large for the inline buffer");
    static_assert(alignof(F) <= 8, "event callable over-aligned");
    Event e;
    e.time = t;
    e.seq = next_seq_++;
    e.invoke = [](void* p) { (*static_cast<F*>(p))(); };
    ::new (static_cast<void*>(e.buf)) F(fn);
    q_.push(e);
  }
  bool empty() const { return q_.empty(); }
  std::size_t size() const { return q_.size(); }
  Time nextTime() const { return q_.top().time; }
  Event pop() {
    Event e = q_.top();
    q_.pop();
    ++popped_;
    return e;
  }
  std::uint64_t eventsProcessed() const { return popped_; }

 private:
  static constexpr std::size_t kInitialCapacity = 4096;
  struct Later {
    bool operator()(const Event& a, const Event& b) const {
      return a.time != b.time ? a.time > b.time : a.seq > b.seq;
    }
  };
  using Heap = std::priority_queue<Event, std::vector<Event>, Later>;
  Heap q_;
  std::uint64_t next_seq_ = 0;
  std::uint64_t popped_ = 0;
};

}  // namespace bcc
