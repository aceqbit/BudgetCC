#pragma once
// Shared by the window-based controllers (Reno, Cubic): decision 3 of CLAUDE.md.
#include <algorithm>
#include <cstdint>
#include <limits>

#include "cc/cc_interface.hpp"
#include "core/packet.hpp"

namespace bcc {

class WindowCc : public CongestionControl {
 public:
  static constexpr double kMss = kWireBytes;

  explicit WindowCc(double iw_pkts) : cwnd_(iw_pkts * kMss) {}

  void onTimer(Time) override {}
  double pacingRateBps() const override {
    const double gain = cwnd_ < ssthresh_ ? 2.0 : 1.25;
    return gain * cwnd_ * 8.0 / toSec(srttOrDefault());
  }
  std::uint32_t inflightCapBytes() const override {
    return static_cast<std::uint32_t>(std::min(cwnd_, 4e9));
  }
  CcProbe probe() const override {
    CcProbe p;
    p.cwnd_bytes = cwnd_;
    return p;
  }

  double cwndBytes() const { return cwnd_; }
  double ssthreshBytes() const { return ssthresh_; }

 protected:
  static constexpr Duration kDefaultRtt = 100'000'000;

  Duration srttOrDefault() const { return srtt_ > 0 ? srtt_ : kDefaultRtt; }
  void updateSrtt(Duration rtt) { srtt_ = srtt_ == 0 ? rtt : srtt_ + (rtt - srtt_) / 8; }
  // Losses within one RTT of the previous reduction belong to the same congestion event.
  bool sameEvent(Time now) const { return have_cut_ && now - last_cut_ < srttOrDefault(); }
  void markCut(Time now) {
    have_cut_ = true;
    last_cut_ = now;
  }

  double cwnd_;
  double ssthresh_ = 1e18;
  Duration srtt_ = 0;

 private:
  bool have_cut_ = false;
  Time last_cut_ = 0;
};

}  // namespace bcc
