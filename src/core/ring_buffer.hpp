#pragma once
// Bounded lock-free FIFO between the simulation thread (single producer) and the pusher thread (single consumer).
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "core/cache_line.hpp"

namespace bcc {

// Wait-free SPSC ring (E4). Positions grow monotonically; the slot is position % capacity, so any capacity works.
// push() never blocks: a full ring drops the item and counts it. T must be trivially copyable.
template <class T>
class SpscRing {
  static_assert(std::is_trivially_copyable<T>::value, "ring items cross threads by memcpy");

 public:
  explicit SpscRing(std::size_t capacity) : buf_(capacity) {}

  // Producer thread only.
  bool push(const T& v) {
    const std::uint64_t tail = c_.tail.load(std::memory_order_relaxed);
    if (tail - c_.head.load(std::memory_order_acquire) == buf_.size()) {
      c_.dropped.store(c_.dropped.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
      return false;
    }
    buf_[static_cast<std::size_t>(tail % buf_.size())] = v;
    c_.tail.store(tail + 1, std::memory_order_release);
    return true;
  }
  // Consumer thread only.
  bool pop(T& out) {
    const std::uint64_t head = c_.head.load(std::memory_order_relaxed);
    if (head == c_.tail.load(std::memory_order_acquire)) return false;
    out = buf_[static_cast<std::size_t>(head % buf_.size())];
    c_.head.store(head + 1, std::memory_order_release);
    return true;
  }
  // Any thread; exact only when the ring is quiescent.
  std::size_t size() const {
    const std::uint64_t head = c_.head.load(std::memory_order_acquire);
    return static_cast<std::size_t>(c_.tail.load(std::memory_order_acquire) - head);
  }
  std::size_t capacity() const { return buf_.size(); }
  std::uint64_t dropped() const { return c_.dropped.load(std::memory_order_relaxed); }

 private:
  // E7: each cursor owns a cache line, so the producer and the consumer never invalidate each other's line.
  struct Cursors {
    alignas(kCacheLine) std::atomic<std::uint64_t> head{0};  // next position to pop (written by the consumer)
    alignas(kCacheLine) std::atomic<std::uint64_t> tail{0};  // next position to push (written by the producer)
    alignas(kCacheLine) std::atomic<std::uint64_t> dropped{0};
  };
 public:
  static constexpr std::size_t kHeadOffset = offsetof(Cursors, head);
  static constexpr std::size_t kTailOffset = offsetof(Cursors, tail);
  static constexpr std::size_t kDroppedOffset = offsetof(Cursors, dropped);
  static_assert(offsetof(Cursors, tail) - offsetof(Cursors, head) >= kCacheLine, "head and tail share a cache line");
  static_assert(offsetof(Cursors, dropped) - offsetof(Cursors, tail) >= kCacheLine, "tail and dropped share a cache line");

 private:
  std::vector<T> buf_;
  Cursors c_;
};

}  // namespace bcc
