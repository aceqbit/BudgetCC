#pragma once
// Growable ring FIFO of trivially copyable values. Capacity is a power of two; push never allocates once
// the high-water mark has been reached (E1: replaces std::queue/std::deque in the link).
#include <cstddef>
#include <utility>
#include <vector>

namespace bcc {

template <class T>
class Fifo {
 public:
  explicit Fifo(std::size_t initial_capacity = 1024) {
    std::size_t c = 2;
    while (c < initial_capacity) c <<= 1;
    buf_.resize(c);
  }

  bool empty() const { return count_ == 0; }
  std::size_t size() const { return count_; }
  std::size_t capacity() const { return buf_.size(); }
  const T& front() const { return buf_[head_]; }

  void push(const T& v) {
    if (count_ == buf_.size()) grow();
    buf_[(head_ + count_) & (buf_.size() - 1)] = v;
    ++count_;
  }
  void pop() {
    head_ = (head_ + 1) & (buf_.size() - 1);
    --count_;
  }

 private:
  void grow() {
    std::vector<T> bigger(buf_.size() * 2);
    for (std::size_t i = 0; i < count_; ++i) bigger[i] = buf_[(head_ + i) & (buf_.size() - 1)];
    buf_ = std::move(bigger);
    head_ = 0;
  }

  std::vector<T> buf_;
  std::size_t head_ = 0;
  std::size_t count_ = 0;
};

}  // namespace bcc
