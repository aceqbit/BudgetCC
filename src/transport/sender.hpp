#pragma once
#include <cstdint>
#include <functional>
#include <memory>

#include "cc/cc_interface.hpp"
#include "core/packet_pool.hpp"
#include "core/simulator.hpp"
#include "transport/feedback.hpp"
#include "transport/inflight_ring.hpp"
#include "transport/pacer.hpp"
#include "transport/traffic_source.hpp"

namespace bcc {

struct SenderCounters {
  std::uint64_t sent_pkts = 0;
  std::uint64_t sent_bytes = 0;
  std::uint64_t acked_pkts = 0;
  std::uint64_t lost_pkts = 0;
  std::uint64_t timeouts = 0;
};

// One flow (flow id = RTP SSRC): pacer + in-flight tracking + loss detection around a CongestionControl.
class Sender {
 public:
  using TxFn = std::function<void(Packet*)>;
  static constexpr Duration kControlInterval = 10'000'000;  // 10 ms
  static constexpr int kReorderThreshold = 3;

  Sender(Simulator& sim, PacketPool& pool, std::uint32_t flow, std::unique_ptr<CongestionControl> cc,
         TrafficSource& source, TxFn tx);

  void start(Time at);
  // New application data became available (video frame).
  void kick() { trySend(); }
  void onFeedback(const FeedbackItem& fb) { onFeedbackAt(sim_.now(), fb); }
  // Same, with an explicit "now" so tests and the ACK microbenchmark can script time.
  void onFeedbackAt(Time now, const FeedbackItem& fb);
  // Records a packet as sent without emitting it (scripted tests and the ACK microbenchmark).
  void trackSent(std::uint64_t seq, Time send_ts, std::uint32_t size);
  void onControlTick(Time now);

  std::uint32_t flow() const { return flow_; }
  CongestionControl& cc() { return *cc_; }
  const CongestionControl& cc() const { return *cc_; }
  std::uint64_t inflightBytes() const { return inflight_bytes_; }
  Duration srtt() const { return srtt_; }
  const SenderCounters& counters() const { return c_; }

 private:
  void trySend();
  void sendOne(Time now);
  void armSendTimer(Time at);
  void declareLost(std::uint64_t seq, Time now, bool timeout);
  Duration lossTimeout() const;

  Simulator& sim_;
  PacketPool& pool_;
  std::uint32_t flow_;
  std::unique_ptr<CongestionControl> cc_;
  TrafficSource& source_;
  TxFn tx_;
  Pacer pacer_;
  bool started_ = false;
  std::uint64_t next_seq_ = 0;
  InflightRing inflight_;
  std::uint64_t inflight_bytes_ = 0;
  Duration srtt_ = 0;
  std::uint64_t timer_gen_ = 0;
  bool timer_armed_ = false;
  Time timer_at_ = 0;
  SenderCounters c_;
};

}  // namespace bcc
