#pragma once
// Simulated time: int64 nanoseconds. Only the Simulator advances it.
#include <cstdint>
#include <type_traits>

namespace bcc {

using Time = std::int64_t;
using Duration = std::int64_t;

constexpr Duration kNsPerUs = 1'000;
constexpr Duration kNsPerMs = 1'000'000;
constexpr Duration kNsPerSec = 1'000'000'000;

template <class T, class = std::enable_if_t<std::is_integral_v<T>>>
constexpr Duration fromNs(T v) {
  return static_cast<Duration>(v);
}
template <class T, class = std::enable_if_t<std::is_integral_v<T>>>
constexpr Duration fromUs(T v) {
  return static_cast<Duration>(v) * kNsPerUs;
}
template <class T, class = std::enable_if_t<std::is_integral_v<T>>>
constexpr Duration fromMs(T v) {
  return static_cast<Duration>(v) * kNsPerMs;
}
template <class T, class = std::enable_if_t<std::is_integral_v<T>>>
constexpr Duration fromSec(T v) {
  return static_cast<Duration>(v) * kNsPerSec;
}

constexpr double toUs(Duration d) { return static_cast<double>(d) / 1e3; }
constexpr double toMs(Duration d) { return static_cast<double>(d) / 1e6; }
constexpr double toSec(Duration d) { return static_cast<double>(d) / 1e9; }

}  // namespace bcc
