#pragma once
#include <cstdint>
#include <functional>
#include <vector>

#include "core/packet_pool.hpp"
#include "core/simulator.hpp"
#include "transport/feedback.hpp"
#include "transport/rtp_header.hpp"

namespace bcc {

struct ReceiverCounters {
  std::uint64_t recv_pkts = 0;
  std::uint64_t recv_bytes = 0;
  std::uint64_t bad_packets = 0;
  // One-way delay violations at the three budgets used in the evaluation.
  std::uint64_t over_100ms = 0;
  std::uint64_t over_150ms = 0;
  std::uint64_t over_300ms = 0;
};

// Per-interval accumulators the sampler drains every 10 ms.
struct ReceiverInterval {
  std::uint64_t bytes = 0;
  std::uint64_t pkts = 0;
  double owd_ms_sum = 0.0;
};

class Receiver {
 public:
  using FeedbackFn = std::function<void(const FeedbackItem&)>;

  Receiver(Simulator& sim, PacketPool& pool, std::uint32_t flow, Duration return_delay, FeedbackFn to_sender);

  // Takes ownership of the packet.
  void onPacket(Packet* p);

  const ReceiverCounters& counters() const { return c_; }
  const std::vector<std::int64_t>& owdNs() const { return owd_ns_; }
  ReceiverInterval takeInterval();

 private:
  Simulator& sim_;
  PacketPool& pool_;
  std::uint32_t flow_;
  Duration return_delay_;
  FeedbackFn to_sender_;
  SeqUnwrapper unwrap_;
  std::vector<std::int64_t> owd_ns_;
  ReceiverInterval interval_;
  ReceiverCounters c_;
};

}  // namespace bcc
