#pragma once
// Spaces packets at the controller's rate. Next-send time is kept as a double in ns so that
// rounding never accumulates: the error after any number of packets stays below 1 ns.
#include <cmath>
#include <cstdint>

#include "core/clock.hpp"

namespace bcc {

class Pacer {
 public:
  static constexpr double kMinRateBps = 1000.0;

  explicit Pacer(double rate_bps) : rate_(rate_bps < kMinRateBps ? kMinRateBps : rate_bps) {}

  double rateBps() const { return rate_; }
  Time nextSend() const { return static_cast<Time>(std::llround(next_ns_)); }
  bool ready(Time now) const { return now >= nextSend(); }

  // Records a packet leaving at `now`.
  void onSend(Time now, std::uint32_t bytes) {
    const double base = static_cast<double>(now) > next_ns_ + 1.0 ? static_cast<double>(now) : next_ns_;
    last_send_ns_ = base;
    last_bytes_ = bytes;
    next_ns_ = base + gapNs(bytes, rate_);
  }

  // Applies a new rate. The gap after the most recent packet is recomputed, so a faster rate pulls
  // the next send earlier and a slower one pushes it later.
  void setRate(double rate_bps) {
    rate_ = rate_bps < kMinRateBps ? kMinRateBps : rate_bps;
    if (last_bytes_ != 0) next_ns_ = last_send_ns_ + gapNs(last_bytes_, rate_);
  }

 private:
  static double gapNs(std::uint32_t bytes, double rate_bps) { return static_cast<double>(bytes) * 8e9 / rate_bps; }

  double rate_;
  double next_ns_ = 0.0;
  double last_send_ns_ = 0.0;
  std::uint32_t last_bytes_ = 0;
};

}  // namespace bcc
