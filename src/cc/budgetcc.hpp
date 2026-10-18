#pragma once
// BudgetCC: delay-budget controller. The application passes D; the queue-delay target is
// Q* = max(10 ms, (D - owd_min) x (1 - margin)). The per-ACK path only updates filters; a 10 ms control step
// tracks capacity asymmetrically (fast down, slow up), steers the rate on the queue-delay error and drains
// proportionally when the queue exceeds Q*.
#include "cc/cc_interface.hpp"
#include "cc/params.hpp"
#include "core/filters.hpp"
#include "core/packet.hpp"

namespace bcc {

class BudgetCc final : public CongestionControl {
 public:
  BudgetCc(ParamReader& p, double budget_ms);

  void onAck(const AckInfo& a) override;
  void onLoss(const LossInfo& l) override;
  void onTimer(Time now) override;
  double pacingRateBps() const override { return rate_; }
  std::uint32_t inflightCapBytes() const override;
  const char* name() const override { return "budgetcc"; }
  CcProbe probe() const override;

  double queueDelayEstimateMs() const;
  double qstarMs() const;
  double capacityEstimateBps() const { return c_hat_; }

 private:
  static constexpr double kSaturationFraction = 0.1;  // queue above this share of Q* means the link is the limit
  static constexpr double kCollapseMinPackets = 4.0;
  double owdMinMs() const;

  double budget_ms_;
  double margin_;
  double k_;
  double w_down_;
  double w_up_;
  double probe_ceiling_;
  double floor_rate_;
  double rate_;
  double c_hat_;
  WindowedMin<Duration> owd_min_;
  WindowedMin<Duration> rtt_min_;
  RateWindow delivered_;  // bytes by receive time
  double owd_s_ms_ = 0.0;
  bool have_owd_ = false;
  Duration srtt_ = 0;
  Time last_now_ = 0;
  Time last_recv_ = 0;
  Time loss_window_start_ = 0;
  int losses_in_window_ = 0;
};

}  // namespace bcc
