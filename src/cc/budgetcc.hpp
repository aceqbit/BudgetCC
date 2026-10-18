#pragma once
// BudgetCC v0 (Phase 1): threshold AIMD on queue delay against Q* = max(10 ms, (D - owd_min) x (1 - margin)).
// Below Q* the rate grows by one packet per RTT; above Q* it is multiplied by 0.8 (at most once per RTT).
#include "cc/cc_interface.hpp"
#include "cc/params.hpp"
#include "core/filters.hpp"
#include "core/packet.hpp"

namespace bcc {

class BudgetCc final : public CongestionControl {
 public:
  BudgetCc(ParamReader& p, double budget_ms);  // PHASE1-NAIVE(A1)

  void onAck(const AckInfo& a) override;
  void onLoss(const LossInfo&) override {}
  void onTimer(Time now) override;
  double pacingRateBps() const override { return rate_; }
  std::uint32_t inflightCapBytes() const override;
  const char* name() const override { return "budgetcc"; }
  CcProbe probe() const override;

  double queueDelayEstimateMs() const;
  double qstarMs() const;

 private:
  double budget_ms_;
  double margin_;
  double rate_;
  double floor_rate_;
  WindowedMin<Duration> owd_min_;
  WindowedMin<Duration> rtt_min_;
  double owd_s_ms_ = 0.0;
  bool have_owd_ = false;
  Duration srtt_ = 0;
  Time last_decrease_ = 0;
  bool decreased_ = false;
  Time last_now_ = 0;
};

}  // namespace bcc
