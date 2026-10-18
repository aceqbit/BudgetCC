#include "cc/cubic.hpp"

namespace bcc {

void Cubic::onAck(const AckInfo& a) {
  updateSrtt(a.rtt);
  if (min_rtt_ == 0 || a.rtt < min_rtt_) min_rtt_ = a.rtt;
  if (cwnd_ < ssthresh_) {
    cwnd_ += a.bytes;
    return;
  }
  const double cw = cwnd_ / kMss;
  const double acked = a.bytes / kMss;
  if (!epoch_) {
    epoch_ = true;
    epoch_start_ = a.now;
    if (cw < w_max_) {
      k_ = std::cbrt((w_max_ - cw) / kC);
      origin_ = w_max_;
    } else {
      k_ = 0.0;
      origin_ = cw;
    }
    w_est_ = cw;
  }
  const double t = toSec(a.now - epoch_start_) + toSec(min_rtt_);
  const double d = t - k_;
  double target = origin_ + kC * d * d * d;
  target = std::min(target, 1.5 * cw);
  double next = cw;
  if (target > cw) next += (target - cw) / cw * acked;
  // TCP-friendly region: the window Reno with beta = 0.7 would have.
  w_est_ += 3.0 * (1.0 - kBeta) / (1.0 + kBeta) * acked / cw;
  next = std::max(next, w_est_);
  cwnd_ = next * kMss;
}

void Cubic::onLoss(const LossInfo& l) {
  if (sameEvent(l.now)) return;
  markCut(l.now);
  epoch_ = false;
  const double cw = cwnd_ / kMss;
  if (cw < w_last_max_) {
    w_last_max_ = cw;
    w_max_ = cw * (1.0 + kBeta) / 2.0;  // fast convergence
  } else {
    w_last_max_ = cw;
    w_max_ = cw;
  }
  ssthresh_ = std::max(cwnd_ * kBeta, 2.0 * kMss);
  cwnd_ = l.timeout ? 2.0 * kMss : ssthresh_;
}

}  // namespace bcc
