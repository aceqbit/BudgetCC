#include "link/bottleneck_link.hpp"

#include <algorithm>
#include <utility>

namespace bcc {

BottleneckLink::BottleneckLink(Simulator& sim, PacketPool& pool, const Trace& trace, std::uint64_t limit_bytes,
                               Duration propagation, DeliverFn deliver)
    : sim_(sim),
      pool_(pool),
      cursor_(trace),
      limit_(limit_bytes),
      prop_(propagation),
      deliver_(std::move(deliver)),
      q_(static_cast<std::size_t>(limit_bytes / kWireBytes) + 1) {}

void BottleneckLink::start() {
  const Trace::Opp o = cursor_.next();
  sim_.schedule(o.time, [this, o] { onOpportunity(o); });
}

bool BottleneckLink::onArrival(Packet* p) {
  ++c_.arrived_pkts;
  if (queued_bytes_ + p->size > limit_) {
    ++c_.dropped_pkts;
    c_.dropped_bytes += p->size;
    pool_.free(p);
    return false;
  }
  p->enq_ts = sim_.now();
  q_.push(p);
  queued_bytes_ += p->size;
  c_.max_queue_bytes = std::max(c_.max_queue_bytes, queued_bytes_);
  return true;
}

Duration BottleneckLink::queueDelay(Time now) const {
  if (q_.empty()) return 0;
  return std::max(last_sojourn_, now - q_.front()->enq_ts);
}

void BottleneckLink::onOpportunity(Trace::Opp o) {
  if (q_.empty()) {
    c_.wasted_opps += o.count;
    credit_ = 0;
  } else {
    const std::uint64_t grant = static_cast<std::uint64_t>(o.count) * kOpportunityBytes;
    credit_ += grant;
    c_.granted_bytes += grant;
    while (!q_.empty() && credit_ >= q_.front()->size) {
      Packet* p = q_.front();
      q_.pop();
      credit_ -= p->size;
      queued_bytes_ -= p->size;
      last_sojourn_ = sim_.now() - p->enq_ts;
      ++c_.delivered_pkts;
      c_.delivered_bytes += p->size;
      sim_.scheduleIn(prop_, [this, p] { deliver_(p); });
    }
    if (q_.empty()) credit_ = 0;
  }
  const Trace::Opp next = cursor_.next();
  sim_.schedule(next.time, [this, next] { onOpportunity(next); });
}

}  // namespace bcc
