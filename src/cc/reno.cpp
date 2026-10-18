#include "cc/reno.hpp"

namespace bcc {

void Reno::onAck(const AckInfo& a) {
  updateSrtt(a.rtt);
  if (cwnd_ < ssthresh_) {
    cwnd_ += a.bytes;  // slow start: doubles per RTT
  } else {
    cwnd_ += a.bytes * kMss / cwnd_;  // congestion avoidance: +1 MSS per RTT
  }
}

void Reno::onLoss(const LossInfo& l) {
  if (sameEvent(l.now)) return;
  markCut(l.now);
  ssthresh_ = std::max(cwnd_ / 2.0, 2.0 * kMss);
  cwnd_ = l.timeout ? 2.0 * kMss : ssthresh_;
}

}  // namespace bcc
