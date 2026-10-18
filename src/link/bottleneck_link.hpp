#pragma once
#include <cstdint>
#include <functional>

#include "core/fifo.hpp"
#include "core/packet.hpp"
#include "core/packet_pool.hpp"
#include "core/simulator.hpp"
#include "link/trace_loader.hpp"

namespace bcc {

struct LinkCounters {
  std::uint64_t arrived_pkts = 0;
  std::uint64_t delivered_pkts = 0;  // left the queue
  std::uint64_t delivered_bytes = 0;
  std::uint64_t dropped_pkts = 0;
  std::uint64_t dropped_bytes = 0;
  std::uint64_t wasted_opps = 0;
  std::uint64_t granted_bytes = 0;  // credit actually added while backlogged
  std::uint64_t max_queue_bytes = 0;
};

// Trace-driven drop-tail bottleneck with a fixed propagation delay after the queue.
class BottleneckLink {
 public:
  using DeliverFn = std::function<void(Packet*)>;  // called at departure + propagation

  BottleneckLink(Simulator& sim, PacketPool& pool, const Trace& trace, std::uint64_t limit_bytes,
                 Duration propagation, DeliverFn deliver);

  void start();                // schedule the first delivery opportunity
  bool onArrival(Packet* p);   // false: dropped (packet is freed)

  std::uint64_t queuedBytes() const { return queued_bytes_; }
  std::size_t queuedPackets() const { return q_.size(); }
  // Ground-truth queue delay: 0 when empty, else max(last sojourn, age of head packet).
  Duration queueDelay(Time now) const;
  const LinkCounters& counters() const { return c_; }

 private:
  void onOpportunity(Trace::Opp o);

  Simulator& sim_;
  PacketPool& pool_;
  Trace::Cursor cursor_;
  std::uint64_t limit_;
  Duration prop_;
  DeliverFn deliver_;
  Fifo<Packet*> q_;  // index-based ring over pooled packets
  std::uint64_t queued_bytes_ = 0;
  std::uint64_t credit_ = 0;
  Duration last_sojourn_ = 0;
  LinkCounters c_;
};

}  // namespace bcc
