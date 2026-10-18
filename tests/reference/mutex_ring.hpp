#pragma once
// Bounded FIFO between the simulation thread (producer) and the pusher thread (consumer).
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace bccref {

// Phase 1 telemetry ring: mutex-protected queue. Oracle for the SPSC ring (E4).
template <class T>
class MutexRing {
 public:
  explicit MutexRing(std::size_t capacity) : buf_(capacity) {}

  // Never blocks. Returns false and counts a dropped sample when full.
  bool push(const T& v) {
    std::lock_guard<std::mutex> lk(m_);
    if (count_ == buf_.size()) {
      ++dropped_;
      return false;
    }
    buf_[(head_ + count_) % buf_.size()] = v;
    ++count_;
    return true;
  }
  bool pop(T& out) {
    std::lock_guard<std::mutex> lk(m_);
    if (count_ == 0) return false;
    out = buf_[head_];
    head_ = (head_ + 1) % buf_.size();
    --count_;
    return true;
  }
  std::size_t size() const {
    std::lock_guard<std::mutex> lk(m_);
    return count_;
  }
  std::size_t capacity() const { return buf_.size(); }
  std::uint64_t dropped() const {
    std::lock_guard<std::mutex> lk(m_);
    return dropped_;
  }

 private:
  mutable std::mutex m_;
  std::vector<T> buf_;
  std::size_t head_ = 0;
  std::size_t count_ = 0;
  std::uint64_t dropped_ = 0;
};

}  // namespace bccref
