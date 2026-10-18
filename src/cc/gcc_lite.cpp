#include "cc/gcc_lite.hpp"

#include <algorithm>
#include <cmath>

namespace bcc {

namespace {
constexpr Duration kGroupSpan = 5'000'000;        // packets sent within 5 ms form one group
constexpr std::size_t kTrendWindow = 20;
constexpr double kSmoothing = 0.9;
constexpr double kThresholdGain = 4.0;
constexpr double kOveruseTimeMs = 10.0;
constexpr Duration kLossWindow = 500'000'000;
}  // namespace

GccLite::GccLite(ParamReader& p)
    : start_rate_bps_(p.get("start_rate_bps", 1e6)),
      min_rate_bps_(p.get("min_rate_bps", 100e3)),
      max_rate_bps_(p.get("max_rate_bps", 1e9)),
      mult_per_100ms_(p.get("increase_per_100ms", 0.08)),
      delay_rate_(start_rate_bps_),
      loss_rate_(start_rate_bps_ * 100.0),
      acked_(fromSec(2)) {}

void GccLite::onAck(const AckInfo& a) {
  srtt_ = srtt_ == 0 ? a.rtt : srtt_ + (a.rtt - srtt_) / 8;
  acked_.add(a.now, a.bytes);
  ++window_acked_;
  if (!have_group_) {
    cur_ = Group{a.send_ts, a.send_ts, a.recv_ts};
    have_group_ = true;
    return;
  }
  if (a.send_ts - cur_.first_send <= kGroupSpan) {  // same burst
    cur_.last_send = std::max(cur_.last_send, a.send_ts);
    cur_.arrival = std::max(cur_.arrival, a.recv_ts);
    return;
  }
  const Group done = cur_;
  cur_ = Group{a.send_ts, a.send_ts, a.recv_ts};
  if (have_prev_) onGroupComplete(prev_, done);
  prev_ = done;
  have_prev_ = true;
}

void GccLite::onGroupComplete(const Group& prev, const Group& cur) {
  const double d_send = toMs(cur.last_send - prev.last_send);
  const double d_arr = toMs(cur.arrival - prev.arrival);
  const double delta = d_arr - d_send;
  const double arrival_ms = toMs(cur.arrival);
  if (first_arrival_ms_ < 0.0) first_arrival_ms_ = arrival_ms;
  acc_delay_ms_ += delta;
  smoothed_delay_ms_ = kSmoothing * smoothed_delay_ms_ + (1.0 - kSmoothing) * acc_delay_ms_;
  window_.emplace_back(arrival_ms - first_arrival_ms_, smoothed_delay_ms_);
  if (window_.size() > kTrendWindow) window_.pop_front();
  num_deltas_ = std::min(num_deltas_ + 1, 1000);

  double trend = prev_trend_;
  if (window_.size() == kTrendWindow) {  // least-squares slope of smoothed delay against arrival time
    double sx = 0, sy = 0;
    for (const auto& p : window_) {
      sx += p.first;
      sy += p.second;
    }
    const double mx = sx / kTrendWindow, my = sy / kTrendWindow;
    double num = 0, den = 0;
    for (const auto& p : window_) {
      num += (p.first - mx) * (p.second - my);
      den += (p.first - mx) * (p.first - mx);
    }
    trend = den > 0.0 ? num / den : 0.0;
  }
  detect(trend, std::min(d_arr, 100.0));
}

void GccLite::detect(double trend, double delta_ms) {
  const double modified = std::min(num_deltas_, 60) * trend * kThresholdGain;
  if (modified > threshold_) {
    if (time_over_using_ms_ < 0.0) {
      time_over_using_ms_ = delta_ms / 2.0;
    } else {
      time_over_using_ms_ += delta_ms;
    }
    ++overuse_counter_;
    if (time_over_using_ms_ > kOveruseTimeMs && overuse_counter_ > 1 && trend >= prev_trend_) {
      time_over_using_ms_ = 0.0;
      overuse_counter_ = 0;
      usage_ = Usage::Overusing;
    }
  } else if (modified < -threshold_) {
    time_over_using_ms_ = -1.0;
    overuse_counter_ = 0;
    usage_ = Usage::Underusing;
  } else {
    time_over_using_ms_ = -1.0;
    overuse_counter_ = 0;
    usage_ = Usage::Normal;
  }
  prev_trend_ = trend;
  updateThreshold(modified, delta_ms);
}

void GccLite::updateThreshold(double modified, double delta_ms) {
  const double abs_m = std::fabs(modified);
  if (abs_m > threshold_ + 15.0) return;  // ignore outliers
  const double k = abs_m < threshold_ ? 0.039 : 0.0087;
  threshold_ += k * (abs_m - threshold_) * std::min(delta_ms, 100.0);
  threshold_ = std::min(std::max(threshold_, 6.0), 600.0);
}

void GccLite::onLoss(const LossInfo&) { ++window_lost_; }

void GccLite::onTimer(Time now) {
  controlRate(now);
  // Loss-based rate (RFC draft-ietf-rmcat-gcc): >10% loss cuts, <2% grows, otherwise holds.
  if (now - loss_window_start_ >= kLossWindow) {
    const std::uint64_t total = window_acked_ + window_lost_;
    if (total > 0) {
      const double ratio = static_cast<double>(window_lost_) / static_cast<double>(total);
      if (ratio > 0.10) {
        loss_rate_ = std::min(loss_rate_, std::max(delay_rate_, min_rate_bps_)) * (1.0 - 0.5 * ratio);
      } else if (ratio < 0.02) {
        loss_rate_ = std::min(loss_rate_ * 1.05, max_rate_bps_);
      }
      loss_rate_ = std::max(loss_rate_, min_rate_bps_);
    }
    window_acked_ = window_lost_ = 0;
    loss_window_start_ = now;
  }
}

void GccLite::controlRate(Time now) {
  const double dt = last_rate_update_ == 0 ? 0.01 : std::min(toSec(now - last_rate_update_), 0.1);
  last_rate_update_ = now;
  const double incoming = acked_.rateBpsWarm(now, fromMs(250));
  switch (usage_) {
    case Usage::Overusing:
      state_ = State::Decrease;
      break;
    case Usage::Underusing:
      state_ = State::Hold;
      break;
    case Usage::Normal:
      if (state_ == State::Hold || state_ == State::Decrease) state_ = State::Increase;
      break;
  }
  if (state_ == State::Decrease) {
    if (incoming > 0.0) {
      avg_max_rate_ = avg_max_rate_ == 0.0 ? incoming : 0.95 * avg_max_rate_ + 0.05 * incoming;
      delay_rate_ = std::min(delay_rate_, 0.85 * incoming);
    }
    delay_rate_ = std::max(delay_rate_, min_rate_bps_);
    state_ = State::Hold;
    usage_ = Usage::Normal;  // one decrease per detection
    return;
  }
  if (state_ != State::Increase) return;
  double next;
  if (avg_max_rate_ > 0.0 && delay_rate_ > 0.9 * avg_max_rate_) {
    // near the last known capacity: additive, about one packet per response time
    const double response = toSec(srtt_ > 0 ? srtt_ : fromMs(100)) + 0.1;
    next = delay_rate_ + std::max(4000.0, kWireBytes * 8.0 / response) * dt;
  } else {
    next = delay_rate_ * (1.0 + mult_per_100ms_ * dt / 0.1);
  }
  // never run far ahead of what the receiver confirms
  if (incoming > 0.0) next = std::min(next, 1.5 * incoming + 10e3);
  delay_rate_ = std::min(std::max(next, min_rate_bps_), max_rate_bps_);
}

std::uint32_t GccLite::inflightCapBytes() const {
  const double bytes = std::min(delay_rate_, loss_rate_) / 8.0;  // one second of data
  return static_cast<std::uint32_t>(std::min(std::max(bytes, 4.0 * kWireBytes), 4e9));
}

CcProbe GccLite::probe() const {
  CcProbe p;
  p.chat_bps = std::min(delay_rate_, loss_rate_);
  return p;
}

}  // namespace bcc
