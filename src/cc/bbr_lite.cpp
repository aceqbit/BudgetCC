#include "cc/bbr_lite.hpp"

#include <algorithm>

namespace bcc {

constexpr double BbrLite::kProbeGains[8];

BbrLite::BbrLite(ParamReader&) : bw_filter_(10), rtt_filter_(fromSec(10)), delivered_(fromSec(1)) {}

double BbrLite::btlBwBps() const {
  double bw = round_max_bps_;
  if (!bw_filter_.empty(round_)) bw = std::max(bw, bw_filter_.get(round_));
  return bw;
}

double BbrLite::pacingGain() const {
  switch (mode_) {
    case Mode::Startup: return kStartupGain;
    case Mode::Drain: return 1.0 / kStartupGain;
    case Mode::ProbeBw: return kProbeGains[phase_];
  }
  return 1.0;
}

void BbrLite::onAck(const AckInfo& a) {
  last_now_ = a.now;
  delivered_.add(a.now, a.bytes);
  rtt_filter_.update(a.now, a.rtt);
  min_rtt_ = rtt_filter_.get(a.now);
  srtt_ = srtt_ == 0 ? a.rtt : srtt_ + (a.rtt - srtt_) / 8;
  const Duration window = std::min<Duration>(std::max<Duration>(srtt_, std::max<Duration>(min_rtt_, fromMs(40))), fromMs(250));
  round_max_bps_ = std::max(round_max_bps_, delivered_.rateBps(a.now, window));

  if (a.send_ts >= round_start_) {  // a packet sent after the round began came back: one round trip elapsed
    bw_filter_.update(round_, round_max_bps_);
    ++round_;
    round_start_ = a.now;
    round_max_bps_ = 0.0;
    onRoundEnd();
  }
  if (mode_ == Mode::Drain && (a.rtt <= min_rtt_ + min_rtt_ / 10 + fromMs(1) || drain_rounds_ > 10)) {
    mode_ = Mode::ProbeBw;
    phase_ = 2;
    phase_start_ = a.now;
  }
}

void BbrLite::onRoundEnd() {
  if (mode_ == Mode::Drain) ++drain_rounds_;
  if (mode_ != Mode::Startup) return;
  const double bw = btlBwBps();
  if (bw >= 1.25 * full_bw_) {
    full_bw_ = bw;
    full_bw_rounds_ = 0;
    return;
  }
  if (++full_bw_rounds_ >= 3) mode_ = Mode::Drain;
}

void BbrLite::onTimer(Time now) {
  if (mode_ != Mode::ProbeBw) return;
  if (now - phase_start_ >= std::max<Duration>(min_rtt_, fromMs(10))) {
    phase_ = (phase_ + 1) % 8;
    phase_start_ = now;
  }
}

double BbrLite::pacingRateBps() const {
  const double bw = btlBwBps();
  if (bw <= 0.0) return kStartupGain * 10.0 * kWireBytes * 8.0 / 0.1;  // initial window over a 100 ms guess
  return pacingGain() * bw;
}

std::uint32_t BbrLite::inflightCapBytes() const {
  const double bw = btlBwBps();
  if (bw <= 0.0 || min_rtt_ == 0) return 10U * kWireBytes;
  const double gain = mode_ == Mode::Startup ? kStartupGain : kCwndGain;
  const double bdp = bw / 8.0 * toSec(min_rtt_);
  return static_cast<std::uint32_t>(std::min(std::max(gain * bdp, 4.0 * kWireBytes), 4e9));
}

CcProbe BbrLite::probe() const {
  CcProbe p;
  p.chat_bps = btlBwBps();
  p.min_rtt_ms = toMs(min_rtt_);
  p.cwnd_bytes = inflightCapBytes();
  return p;
}

}  // namespace bcc
