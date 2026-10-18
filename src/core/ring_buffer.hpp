#pragma once
// Bounded lock-free FIFO between the simulation thread (single producer) and the pusher thread (single consumer).
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

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
    const std::uint64_t tail = tail_.load(std::memory_order_relaxed);
    if (tail - head_.load(std::memory_order_acquire) == buf_.size()) {
      dropped_.store(dropped_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
      return false;
    }
    buf_[static_cast<std::size_t>(tail % buf_.size())] = v;
    tail_.store(tail + 1, std::memory_order_release);
    return true;
  }
  // Consumer thread only.
  bool pop(T& out) {
    const std::uint64_t head = head_.load(std::memory_order_relaxed);
    if (head == tail_.load(std::memory_order_acquire)) return false;
    out = buf_[static_cast<std::size_t>(head % buf_.size())];
    head_.store(head + 1, std::memory_order_release);
    return true;
  }
  // Any thread; exact only when the ring is quiescent.
  std::size_t size() const {
    const std::uint64_t head = head_.load(std::memory_order_acquire);
    return static_cast<std::size_t>(tail_.load(std::memory_order_acquire) - head);
  }
  std::size_t capacity() const { return buf_.size(); }
  std::uint64_t dropped() const { return dropped_.load(std::memory_order_relaxed); }

 private:
  std::vector<T> buf_;
  std::atomic<std::uint64_t> head_{0};  // next position to pop (written by the consumer)
  std::atomic<std::uint64_t> tail_{0};  // next position to push (written by the producer)
  std::atomic<std::uint64_t> dropped_{0};
};

}  // namespace bcc
