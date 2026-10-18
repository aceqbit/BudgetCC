#include "cc/budgetcc.hpp"

#include <algorithm>

namespace bcc {

BudgetCc::BudgetCc(ParamReader& p, double budget_ms)
    : budget_ms_(budget_ms),
      margin_(p.get("margin", 0.2)),
      rate_(p.get("start_rate_bps", 1e6)),
      floor_rate_(p.get("floor_rate_bps", 100e3)),
      owd_min_(fromSec(10)),
      rtt_min_(fromSec(10)) {}

void BudgetCc::onAck(const AckInfo& a) {
  last_now_ = a.now;
  owd_min_.update(a.now, a.owd);
  rtt_min_.update(a.now, a.rtt);
  const double owd_ms = toMs(a.owd);
  owd_s_ms_ = have_owd_ ? owd_s_ms_ + (owd_ms - owd_s_ms_) / 8.0 : owd_ms;
  have_owd_ = true;
  srtt_ = srtt_ == 0 ? a.rtt : srtt_ + (a.rtt - srtt_) / 8;
}

double BudgetCc::queueDelayEstimateMs() const {
  if (!have_owd_ || owd_min_.empty(last_now_)) return 0.0;
  return std::max(0.0, owd_s_ms_ - toMs(owd_min_.get(last_now_)));
}

double BudgetCc::qstarMs() const {
  const double owd_min = (have_owd_ && !owd_min_.empty(last_now_)) ? toMs(owd_min_.get(last_now_)) : 0.0;
  return std::max(10.0, (budget_ms_ - owd_min) * (1.0 - margin_));
}

void BudgetCc::onTimer(Time now) {
  last_now_ = std::max(last_now_, now);
  const Duration rtt = srtt_ > 0 ? srtt_ : fromMs(100);
  if (queueDelayEstimateMs() > qstarMs()) {
    if (!decreased_ || now - last_decrease_ >= rtt) {
      rate_ *= 0.8;
      last_decrease_ = now;
      decreased_ = true;
    }
  } else {
    // +1 packet per RTT: the rate gains one packet per RTT every RTT; this tick is 10 ms of that.
    rate_ += kWireBytes * 8.0 / toSec(rtt) * (0.01 / toSec(rtt));
  }
  rate_ = std::min(std::max(rate_, floor_rate_), 1e10);
}

std::uint32_t BudgetCc::inflightCapBytes() const {
  const double rtt_min = rtt_min_.empty(last_now_) ? 0.1 : toSec(rtt_min_.get(last_now_));
  const double bytes = rate_ / 8.0 * (rtt_min + qstarMs() / 1000.0);
  return static_cast<std::uint32_t>(std::min(std::max(bytes, 4.0 * kWireBytes), 4e9));
}

CcProbe BudgetCc::probe() const {
  CcProbe p;
  p.chat_bps = rate_;
  p.qest_ms = queueDelayEstimateMs();
  p.qstar_ms = qstarMs();
  return p;
}

}  // namespace bcc
