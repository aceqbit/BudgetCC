#include "cc/budgetcc.hpp"

#include <algorithm>

namespace bcc {

BudgetCc::BudgetCc(ParamReader& p, double budget_ms)
    : budget_ms_(budget_ms),
      margin_(p.get("margin", 0.2)),
      k_(p.get("k", 0.5)),
      w_down_(p.get("w_down", 0.5)),
      w_up_(p.get("w_up", 0.1)),
      probe_ceiling_(p.get("probe_ceiling", 1.15)),
      floor_rate_(p.get("floor_rate_bps", 100e3)),
      rate_(p.get("start_rate_bps", 1e6)),
      c_hat_(rate_),
      owd_min_(fromSec(10)),
      rtt_min_(fromSec(10)),
      delivered_(fromSec(1)) {}

void BudgetCc::onAck(const AckInfo& a) {
  last_now_ = a.now;
  last_recv_ = std::max(last_recv_, a.recv_ts);
  owd_min_.update(a.now, a.owd);
  rtt_min_.update(a.now, a.rtt);
  const double owd_ms = toMs(a.owd);
  owd_s_ms_ = have_owd_ ? owd_s_ms_ + (owd_ms - owd_s_ms_) / 8.0 : owd_ms;
  have_owd_ = true;
  srtt_ = srtt_ == 0 ? a.rtt : srtt_ + (a.rtt - srtt_) / 8;
  delivered_.add(a.recv_ts, a.bytes);
}

void BudgetCc::onLoss(const LossInfo& l) {
  const Duration rtt = srtt_ > 0 ? srtt_ : fromMs(100);
  if (losses_in_window_ == 0 || l.now - loss_window_start_ > rtt) {
    loss_window_start_ = l.now;
    losses_in_window_ = 0;
  }
  // A burst of three losses within one RTT is buffer overflow, not radio loss: cut the capacity estimate once.
  if (++losses_in_window_ == 3) c_hat_ *= 0.8;
}

double BudgetCc::owdMinMs() const {
  return (have_owd_ && !owd_min_.empty(last_now_)) ? toMs(owd_min_.get(last_now_)) : 0.0;
}

double BudgetCc::queueDelayEstimateMs() const {
  if (!have_owd_ || owd_min_.empty(last_now_)) return 0.0;
  return std::max(0.0, owd_s_ms_ - owdMinMs());
}

double BudgetCc::qstarMs() const { return std::max(10.0, (budget_ms_ - owdMinMs()) * (1.0 - margin_)); }

void BudgetCc::onTimer(Time now) {
  last_now_ = std::max(last_now_, now);
  if (!have_owd_) return;  // nothing measured yet: keep the start rate
  const double q = queueDelayEstimateMs();
  const double qstar = qstarMs();
  // The capacity sample spans about one RTT, stretched at low rates so that it always covers enough whole
  // packets to be a rate and not a count of 0, 1 or 2 packets.
  const Duration min_span = static_cast<Duration>(kSampleMinPackets * kWireBytes * 8.0 / c_hat_ * 1e9);
  const Duration srtt = std::clamp<Duration>(std::max(srtt_, min_span), fromMs(20), fromMs(500));
  // Delivery is measured on the receive timeline, which reaches the sender one feedback lag late: anchor the
  // window at the newest receive time, but never earlier than "now minus the lag" so an outage shows up.
  const Duration lag =
      rtt_min_.empty(last_now_) ? fromMs(30) : rtt_min_.get(last_now_) - owd_min_.get(last_now_) + fromMs(10);
  const Time anchor = std::max(last_recv_, now - lag);
  const double sample = delivered_.rateBpsWarm(anchor, srtt);
  // With an empty queue the delivery rate only reflects what we sent, not what the link can carry, so it may
  // raise the estimate but not lower it (otherwise the estimate spirals down with the pacing rate).
  const bool link_limited = q > kSaturationFraction * qstar;
  if (sample < c_hat_) {
    if (link_limited) c_hat_ = (1.0 - w_down_) * c_hat_ + w_down_ * sample;  // fast down
  } else {
    c_hat_ += w_up_ * (sample - c_hat_);  // slow up
  }
  // Collapse fast path. A 20 ms window only holds whole packets, so it is trusted only when at least
  // kCollapseMinPackets are expected in it at the current rate.
  const double fast = delivered_.rateBpsWarm(anchor, fromMs(20));
  const bool resolvable = rate_ * 0.020 / 8.0 >= kCollapseMinPackets * kWireBytes;
  if (resolvable && fast < 0.5 * c_hat_) c_hat_ = fast;
  c_hat_ = std::max(c_hat_, floor_rate_);

  const double e = (qstar - q) / qstar;
  double rate = c_hat_ * std::clamp(1.0 + k_ * e, 0.5, probe_ceiling_);
  if (q > qstar) rate = std::min(rate, c_hat_ * qstar / q);  // proportional drain
  rate_ = std::max(rate, floor_rate_);
}

std::uint32_t BudgetCc::inflightCapBytes() const {
  const double rtt_min = rtt_min_.empty(last_now_) ? 0.1 : toSec(rtt_min_.get(last_now_));
  const double bytes = rate_ / 8.0 * (rtt_min + qstarMs() / 1000.0);
  return static_cast<std::uint32_t>(std::min(std::max(bytes, 4.0 * kWireBytes), 4e9));
}

CcProbe BudgetCc::probe() const {
  CcProbe p;
  p.chat_bps = c_hat_;
  p.qest_ms = queueDelayEstimateMs();
  p.qstar_ms = qstarMs();
  return p;
}

}  // namespace bcc
