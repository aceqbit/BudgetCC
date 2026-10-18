#include "transport/traffic_source.hpp"

#include <algorithm>
#include <cmath>

namespace bcc {

void VideoSource::generateFrame(Time now, double rate_bps) {
  const double mean = rate_bps / kFps / 8.0;
  // delta = mean * K / (K - 1 + ratio) so that the mean over one key interval equals `mean`.
  const double delta = mean * static_cast<double>(kKeyInterval) / (static_cast<double>(kKeyInterval) - 1.0 + kKeyRatio);
  const bool key = (c_.frames_generated % kKeyInterval) == 0;
  last_frame_bytes_ = key ? kKeyRatio * delta : delta;
  const auto pkts = static_cast<std::uint32_t>(
      std::max(1.0, std::ceil(last_frame_bytes_ / static_cast<double>(kPayloadBytes))));
  frames_.push_back(Frame{now, pkts});
  ++c_.frames_generated;
  c_.packets_generated += pkts;
}

bool VideoSource::hasPacket(Time now) {
  while (!frames_.empty() && now - frames_.front().born > kMaxWait) {
    frames_.pop_front();
    ++c_.frames_dropped;
  }
  return !frames_.empty();
}

void VideoSource::consume() {
  if (--frames_.front().packets_left == 0) frames_.pop_front();
}

}  // namespace bcc
