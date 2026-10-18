#pragma once
// Sliding-window filters used by the controllers (E3: O(1) amortised).
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "core/clock.hpp"

namespace bcc {

// Monotonic deque: the retained samples are exactly the "suffix extrema" of the stream, so the front is the
// extremum of the window and each sample is pushed and popped at most once.
// Cmp = std::less -> windowed minimum, std::greater -> windowed maximum.
template <class T, class Cmp>
class WindowedExtremum {
 public:
  explicit WindowedExtremum(Duration window) : window_(window), buf_(kInitialCapacity) {}

  void update(Time t, T v) {
    while (count_ != 0 && !Cmp()(back().second, v)) --count_;  // dominated by the newer sample
    while (count_ != 0 && front().first <= t - window_) popFront();
    if (count_ == buf_.size()) grow();
    buf_[(head_ + count_) & mask()] = {t, v};
    ++count_;
  }
  bool empty(Time now) const { return count_ == 0 || back().first <= now - window_; }
  // Extremum over samples with time in (now - window, now]. Requires !empty(now).
  T get(Time now) const {
    std::size_t i = 0;
    while (buf_[(head_ + i) & mask()].first <= now - window_) ++i;  // expired fronts not yet popped by update()
    return buf_[(head_ + i) & mask()].second;
  }

 private:
  static constexpr std::size_t kInitialCapacity = 64;
  std::size_t mask() const { return buf_.size() - 1; }
  const std::pair<Time, T>& front() const { return buf_[head_]; }
  const std::pair<Time, T>& back() const { return buf_[(head_ + count_ - 1) & mask()]; }
  void popFront() {
    head_ = (head_ + 1) & mask();
    --count_;
  }
  void grow() {
    std::vector<std::pair<Time, T>> bigger(buf_.size() * 2);
    for (std::size_t i = 0; i < count_; ++i) bigger[i] = buf_[(head_ + i) & mask()];
    buf_ = std::move(bigger);
    head_ = 0;
  }

  Duration window_;
  std::vector<std::pair<Time, T>> buf_;
  std::size_t head_ = 0;
  std::size_t count_ = 0;
};

template <class T>
using WindowedMin = WindowedExtremum<T, std::less<T>>;
template <class T>
using WindowedMax = WindowedExtremum<T, std::greater<T>>;

// Delivered-bytes window. rateBps(now, w) = 8 * bytes in (now - w, now] / w.
// Samples sit in a circular buffer next to a running byte total; a query moves a cursor to the newest sample
// at or before now - w and subtracts two totals. The cursor only walks as far as `now - w` moved since the last
// query, so repeated queries with a steady window cost O(1).
class RateWindow {
 public:
  explicit RateWindow(Duration retention) : retention_(retention), buf_(kInitialCapacity) {}

  void add(Time t, std::uint32_t bytes) {
    if (!have_first_) {
      have_first_ = true;
      first_ = t;
    }
    if (count_ == buf_.size()) grow();
    total_ += bytes;
    buf_[(first_id_ + count_) & mask()] = Sample{t, total_};
    ++count_;
    while (count_ != 0 && buf_[first_id_ & mask()].t <= t - retention_) {
      evicted_total_ = buf_[first_id_ & mask()].cum;
      ++first_id_;
      --count_;
    }
  }
  double rateBps(Time now, Duration window) const {
    if (count_ == 0) return 0.0;
    const Time threshold = now - window;
    const std::int64_t first = static_cast<std::int64_t>(first_id_);
    const std::int64_t last = first + static_cast<std::int64_t>(count_) - 1;
    cursor_ = std::min(std::max(cursor_, first - 1), last);
    while (cursor_ < last && at(cursor_ + 1).t <= threshold) ++cursor_;
    while (cursor_ >= first && at(cursor_).t > threshold) --cursor_;
    const std::uint64_t base = cursor_ >= first ? at(cursor_).cum : evicted_total_;
    return static_cast<double>(total_ - base) * 8.0 / toSec(window);
  }
  // Like rateBps, but while the window is still filling (the first `window` after the first sample) the
  // divisor is the age of the data, never less than min(window, 20 ms). Avoids a start-up underestimate.
  double rateBpsWarm(Time now, Duration window) const {
    if (!have_first_) return 0.0;
    const Duration floor = std::min<Duration>(window, 20'000'000);
    const Duration eff = std::min<Duration>(window, std::max<Duration>(now - first_, floor));
    return rateBps(now, eff);
  }

 private:
  struct Sample {
    Time t = 0;
    std::uint64_t cum = 0;  // bytes added up to and including this sample
  };
  static constexpr std::size_t kInitialCapacity = 256;
  std::size_t mask() const { return buf_.size() - 1; }
  const Sample& at(std::int64_t id) const { return buf_[static_cast<std::size_t>(id) & mask()]; }
  void grow() {
    std::vector<Sample> bigger(buf_.size() * 2);
    for (std::size_t i = 0; i < count_; ++i) bigger[(first_id_ + i) & (bigger.size() - 1)] = buf_[(first_id_ + i) & mask()];
    buf_ = std::move(bigger);
  }

  Duration retention_;
  std::vector<Sample> buf_;
  std::uint64_t first_id_ = 0;  // id of the oldest retained sample; slot = id & mask
  std::size_t count_ = 0;
  std::uint64_t total_ = 0;
  std::uint64_t evicted_total_ = 0;  // total before the oldest retained sample
  bool have_first_ = false;
  Time first_ = 0;
  mutable std::int64_t cursor_ = -1;  // id of the newest sample at or before the last query threshold
};

}  // namespace bcc
