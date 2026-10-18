#pragma once
// What the sender has to send: a saturating bulk flow or RTP-style video frames.
#include <cstdint>
#include <deque>

#include "core/clock.hpp"
#include "core/packet.hpp"

namespace bcc {

class TrafficSource {
 public:
  virtual ~TrafficSource() = default;
  // True if a packet is ready to leave. May discard stale data first.
  virtual bool hasPacket(Time now) = 0;
  // Removes the packet that hasPacket() announced.
  virtual void consume() = 0;
};

class BulkSource final : public TrafficSource {
 public:
  bool hasPacket(Time) override { return true; }
  void consume() override {}
};

struct VideoCounters {
  std::uint64_t frames_generated = 0;
  std::uint64_t frames_dropped = 0;
  std::uint64_t packets_generated = 0;
};

// 30 fps; every 60th frame is a key frame at 4x the size of a delta frame; mean frame size = rate / 30.
class VideoSource final : public TrafficSource {
 public:
  static constexpr int kFps = 30;
  static constexpr std::uint64_t kKeyInterval = 60;
  static constexpr double kKeyRatio = 4.0;
  static constexpr Duration kMaxWait = 1'000'000'000;  // frames whose head packet waited longer are dropped

  // Time of frame k: exact 30 fps, integer arithmetic.
  static Time frameTime(std::uint64_t k) { return static_cast<Time>(k * 1'000'000'000ULL / kFps); }

  void generateFrame(Time now, double rate_bps);
  bool hasPacket(Time now) override;
  void consume() override;

  const VideoCounters& counters() const { return c_; }
  double lastFrameBytes() const { return last_frame_bytes_; }

 private:
  struct Frame {
    Time born;
    std::uint32_t packets_left;
  };
  std::deque<Frame> frames_;
  VideoCounters c_;
  double last_frame_bytes_ = 0.0;
};

}  // namespace bcc
