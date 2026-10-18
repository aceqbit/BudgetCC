#pragma once
// BBR v1, simplified: Startup, Drain and ProbeBW with a 10-round bottleneck-bandwidth max filter, a 10 s min-RTT
// filter and an in-flight cap of about 2 x BDP. Omitted: ProbeRTT, packet-timed delivery-rate samples,
// inflight-based Drain exit, ProbeBW random phase start, loss/ECN response (see docs/decisions.md).
#include "cc/cc_interface.hpp"
#include "cc/params.hpp"
#include "core/filters.hpp"
#include "core/packet.hpp"

namespace bcc {

class BbrLite final : public CongestionControl {
 public:
  enum class Mode { Startup, Drain, ProbeBw };
  static constexpr double kStartupGain = 2.885;  // 2 / ln 2
  static constexpr double kCwndGain = 2.0;
  static constexpr double kProbeGains[8] = {1.25, 0.75, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0};

  explicit BbrLite(ParamReader& p);

  void onAck(const AckInfo& a) override;
  void onLoss(const LossInfo&) override {}
  void onTimer(Time now) override;
  double pacingRateBps() const override;
  std::uint32_t inflightCapBytes() const override;
  const char* name() const override { return "bbr_lite"; }
  CcProbe probe() const override;

  Mode mode() const { return mode_; }
  double btlBwBps() const;
  Duration minRtt() const { return min_rtt_; }
  double pacingGain() const;

 private:
  void onRoundEnd();

  Mode mode_ = Mode::Startup;
  WindowedMax<double> bw_filter_;  // keyed by round number, window = 10 rounds
  WindowedMin<Duration> rtt_filter_;
  RateWindow delivered_;
  Duration min_rtt_ = 0;
  Duration srtt_ = 0;
  std::int64_t round_ = 0;
  Time round_start_ = 0;
  double round_max_bps_ = 0.0;
  double full_bw_ = 0.0;
  int full_bw_rounds_ = 0;
  int drain_rounds_ = 0;
  int phase_ = 2;
  Time phase_start_ = 0;
  Time last_now_ = 0;
};

}  // namespace bcc
