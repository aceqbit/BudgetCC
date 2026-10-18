#pragma once
// Sliding-window filters used by the controllers.
#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <utility>

#include "core/clock.hpp"

namespace bcc {

// PHASE1-NAIVE(E3): all samples inside the window are kept and get() scans them linearly.
// Cmp = std::less -> windowed minimum, std::greater -> windowed maximum.
template <class T, class Cmp>
class WindowedExtremum {
 public:
  explicit WindowedExtremum(Duration window) : window_(window) {}
  void update(Time t, T v) {
    s_.emplace_back(t, v);
    while (!s_.empty() && s_.front().first <= t - window_) s_.pop_front();
  }
  bool empty(Time now) const {
    for (const auto& e : s_)
      if (e.first > now - window_) return false;
    return true;
  }
  // Extremum over samples with time in (now - window, now]. Requires !empty(now).
  T get(Time now) const {
    bool have = false;
    T best{};
    for (const auto& e : s_) {
      if (e.first <= now - window_) continue;
      if (!have || Cmp()(e.second, best)) {
        best = e.second;
        have = true;
      }
    }
    return best;
  }

 private:
  Duration window_;
  std::deque<std::pair<Time, T>> s_;
};

template <class T>
using WindowedMin = WindowedExtremum<T, std::less<T>>;
template <class T>
using WindowedMax = WindowedExtremum<T, std::greater<T>>;

// Delivered-bytes window. rateBps(now, w) = 8 * bytes in (now - w, now] / w.
// PHASE1-NAIVE(E3): linear scan of a deque on every query.
class RateWindow {
 public:
  explicit RateWindow(Duration retention) : retention_(retention) {}
  void add(Time t, std::uint32_t bytes) {
    if (!have_first_) {
      have_first_ = true;
      first_ = t;
    }
    s_.emplace_back(t, bytes);
    while (!s_.empty() && s_.front().first <= t - retention_) s_.pop_front();
  }
  double rateBps(Time now, Duration window) const {
    std::uint64_t sum = 0;
    for (auto it = s_.rbegin(); it != s_.rend() && it->first > now - window; ++it) sum += it->second;
    return static_cast<double>(sum) * 8.0 / toSec(window);
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
  Duration retention_;
  bool have_first_ = false;
  Time first_ = 0;
  std::deque<std::pair<Time, std::uint32_t>> s_;
};

}  // namespace bcc
