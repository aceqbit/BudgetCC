#pragma once
// Google Congestion Control, simplified (Carlucci et al., MMSys 2016): inter-group delay variation -> trendline
// slope -> adaptive-threshold over-use detector -> AIMD rate controller, plus a loss-based rate cap.
// Omitted: Kalman estimator variant, probing, ALR handling, REMB signalling (see docs/decisions.md).
#include <deque>
#include <utility>

#include "cc/cc_interface.hpp"
#include "cc/params.hpp"
#include "core/filters.hpp"
#include "core/packet.hpp"

namespace bcc {

class GccLite final : public CongestionControl {
 public:
  enum class Usage { Normal, Overusing, Underusing };

  explicit GccLite(ParamReader& p);

  void onAck(const AckInfo& a) override;
  void onLoss(const LossInfo& l) override;
  void onTimer(Time now) override;
  double pacingRateBps() const override { return std::min(delay_rate_, loss_rate_); }
  std::uint32_t inflightCapBytes() const override;
  const char* name() const override { return "gcc_lite"; }
  CcProbe probe() const override;

  Usage usage() const { return usage_; }
  double delayRateBps() const { return delay_rate_; }
  double lossRateBps() const { return loss_rate_; }

 private:
  struct Group {
    Time first_send = 0;
    Time last_send = 0;
    Time arrival = 0;
  };
  void onGroupComplete(const Group& prev, const Group& cur);
  void detect(double trend, double delta_ms);
  void updateThreshold(double modified, double delta_ms);
  void controlRate(Time now);

  // parameters
  double start_rate_bps_;
  double min_rate_bps_;
  double max_rate_bps_;
  double mult_per_100ms_;

  // inter-group delay / trendline
  bool have_group_ = false;
  Group cur_{}, prev_{};
  bool have_prev_ = false;
  double acc_delay_ms_ = 0.0;
  double smoothed_delay_ms_ = 0.0;
  std::deque<std::pair<double, double>> window_;  // (arrival ms, smoothed delay ms)
  int num_deltas_ = 0;
  double first_arrival_ms_ = -1.0;
  double prev_trend_ = 0.0;
  double threshold_ = 12.5;
  double time_over_using_ms_ = -1.0;
  int overuse_counter_ = 0;
  Usage usage_ = Usage::Normal;

  // rate control
  enum class State { Hold, Increase, Decrease };
  State state_ = State::Increase;
  double delay_rate_;
  double loss_rate_;
  double avg_max_rate_ = 0.0;
  Time last_rate_update_ = 0;
  RateWindow acked_;
  Duration srtt_ = 0;

  // loss window
  Time loss_window_start_ = 0;
  std::uint64_t window_acked_ = 0;
  std::uint64_t window_lost_ = 0;
};

}  // namespace bcc
